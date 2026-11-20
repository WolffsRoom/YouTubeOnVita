/*
 * PS Vita controller -> Starboard input events for Cobalt.
 * Polls SceCtrl on the main (event) thread and dispatches kSbEventTypeInput
 * through the registered Cobalt event handler. Button mapping lives in
 * pad_adapter.cpp (Starboard gamepad keys, as on other console ports).
 *
 * DIAGNOSTIC AUTOMATION: ux0:data/youtubevita/input_script.txt, if present,
 * lists "<seconds> <button>" lines; each is a press+release injected through
 * the same path as physical buttons, so Vita3K runs can navigate unattended.
 * Without that file only the physical controller is used.
 *
 * FRONT TOUCH (YouTube TV is a D-pad UI, so gestures map onto it):
 *   tap             -> pointer click at the finger (sent on release)
 *   swipe / drag    -> D-pad steps, one per 72 px, content follows the finger
 *                      (finger left = focus right, finger up = focus down)
 *   two-finger tap  -> Back (Circle)
 */
#include "pad_adapter.h"
#include "starboard/event.h"
#include "starboard/log.h"
#include "starboard/window.h"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" SbWindow ytv_vita_current_window();
extern "C" bool ytv_addons_input(uint32_t previous, uint32_t current);
extern "C" bool ytv_addons_menu_open();
extern "C" void ytv_addons_toggle();
extern "C" void ytv_addons_tap(float x, float y);
extern "C" uint32_t ytv_addons_hidden_buttons();
extern "C" void ytv_ui_sound(int kind);  // port_ui_sound.cpp
extern "C" void ytv_vita_lifecycle_send(SbEventType type, void* data);
// Time of the latest press sent to Cobalt; the next presented frame reports
// the input-to-photon latency (YTV_INPUT_LATENCY, verbose log only).
extern "C" volatile uint64_t g_ytv_last_input_us;
extern volatile uint64_t g_ytv_audio_active_us;  // port_addons.cpp
volatile uint64_t g_ytv_last_input_us = 0;

namespace {
struct ScriptStep { uint32_t at_ms; uint32_t button; int16_t tap_x, tap_y; bool jitter;
                    int16_t to_x, to_y; bool swipe; };
ScriptStep g_script[64];
int g_script_count = 0, g_script_next = 0;
bool g_script_loaded = false;
uint64_t g_start_us = 0;
uint32_t g_previous = 0;
uint32_t g_script_held = 0;
uint64_t g_script_release_us = 0;
// Front touch panel state (one tracked finger plus finger count).
bool g_touch_down = false;
int g_touch_id = -1;
float g_touch_x = 0, g_touch_y = 0;      // latest finger position
float g_press_x = 0, g_press_y = 0;      // where the finger went down
float g_anchor_x = 0, g_anchor_y = 0;    // position of the last emitted swipe step
bool g_swiped = false;                   // this touch produced D-pad steps
int g_max_fingers = 0;
constexpr float kTapSlop = 12.f;         // window pixels (~ finger jitter)
constexpr float kSwipeStep = 72.f;       // window pixels per D-pad step
// Gesture-generated button presses, delivered one at a time.
uint32_t g_gesture_queue[16];
int g_gq_head = 0, g_gq_count = 0;
uint32_t g_gesture_held = 0;
uint64_t g_gesture_release_us = 0;
void gesture_push(uint32_t button) {
  if (g_gq_count < 16) g_gesture_queue[(g_gq_head + g_gq_count++) % 16] = button;
}
// Scripted swipe (diagnostic): finger moves from tap_x/y to to_x/y.
bool g_swipe_active = false;
uint64_t g_swipe_start_us = 0;
float g_swipe_x0 = 0, g_swipe_y0 = 0, g_swipe_x1 = 0, g_swipe_y1 = 0;
// Scripted tap (diagnostic): position and release time.
bool g_tap_active = false;
bool g_tap_jitter = false;
float g_tap_x = 0, g_tap_y = 0;
uint64_t g_tap_release_us = 0;

// Double tap on the left/right third of the screen while a video plays:
// seek -/+ (YouTube's MediaRewind/MediaFastForward = 10 s steps). The first
// tap on a side waits kDoubleTapUs before becoming a click.
constexpr uint64_t kDoubleTapUs = 350000;
bool g_tap_pending = false;
uint64_t g_tap_pending_us = 0;
float g_tap_pending_x = 0, g_tap_pending_y = 0;
// YouTube's MediaRewind/FastForward open its scrubber (10 s per press, video
// held); Enter commits. Repeated double taps add up before the commit.
constexpr uint64_t kSeekCommitUs = 900000;
uint64_t g_seek_commit_us = 0;
uint32_t g_seek_dir = 0;      // SCE_CTRL_LEFT / RIGHT
// Dragging on the lower part of the screen while a video plays scrubs: a
// D-pad step every kScrubStep pixels (the swipe step is 72 px) and the seek is
// committed with X after the finger lifts, as for the double tap.
constexpr float kScrubStep = 26.f;
constexpr float kScrubZoneY = 300.f;   // progress bar and controls area
bool g_scrub_touch = false;
int g_seek_steps = 0;         // D-pad presses still to send
uint64_t g_seek_next_us = 0;  // when the next one goes out
constexpr uint64_t kSeekStepUs = 600000;

uint32_t button_by_name(const char* n) {
  struct { const char* name; uint32_t bit; } t[] = {
    {"up", SCE_CTRL_UP}, {"down", SCE_CTRL_DOWN}, {"left", SCE_CTRL_LEFT},
    {"right", SCE_CTRL_RIGHT}, {"cross", SCE_CTRL_CROSS},
    {"circle", SCE_CTRL_CIRCLE}, {"square", SCE_CTRL_SQUARE},
    {"triangle", SCE_CTRL_TRIANGLE}, {"l", SCE_CTRL_LTRIGGER},
    {"r", SCE_CTRL_RTRIGGER}, {"start", SCE_CTRL_START},
    {"select", SCE_CTRL_SELECT}};
  for (auto& e : t) if (strcmp(n, e.name) == 0) return e.bit;
  return 0;
}
void load_script() {
  g_script_loaded = true;
  int fd = sceIoOpen("ux0:data/youtubevita/input_script.txt", SCE_O_RDONLY, 0);
  if (fd < 0) return;
  static char buf[2048];
  int n = sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (n <= 0) return;
  buf[n] = 0;
  char* line = buf;
  while (line && *line && g_script_count < 64) {
    char* next = strchr(line, '\n');
    if (next) *next++ = 0;
    float secs = 0; char name[16] = {0}; int tx = 0, ty = 0, ex = 0, ey = 0;
    if (line[0] != '#' && sscanf(line, "%f %15s %d %d %d %d", &secs, name, &tx, &ty, &ex, &ey) >= 2) {
      if (strcmp(name, "tap") == 0 || strcmp(name, "tapj") == 0)
        g_script[g_script_count++] = {(uint32_t)(secs * 1000.f), 0, (int16_t)tx, (int16_t)ty,
                                      strcmp(name, "tapj") == 0, 0, 0, false};
      else if (strcmp(name, "swipe") == 0)
        g_script[g_script_count++] = {(uint32_t)(secs * 1000.f), 0, (int16_t)tx, (int16_t)ty,
                                      false, (int16_t)ex, (int16_t)ey, true};
      else if (uint32_t b = button_by_name(name))
        g_script[g_script_count++] = {(uint32_t)(secs * 1000.f), b, 0, 0, false, 0, 0, false};
    }
    line = next;
  }
  char msg[96];
  snprintf(msg, sizeof(msg), "YTV_INPUT_SCRIPT steps=%d (diagnostic automation)\n",
           g_script_count);
  SbLogRaw(msg);
}

void send_touch(SbWindow window, SbInputEventType type, float x, float y) {
  SbInputData e = {};
  e.window = window;
  e.type = type;
  e.device_type = kSbInputDeviceTypeTouchScreen;
  e.device_id = 1;
  e.key = kSbKeyMouse1;
  e.position.x = x;
  e.position.y = y;
  e.pressure = type == kSbInputEventTypeUnpress ? 0.f : 0.5f;
  if (type == kSbInputEventTypePress) g_ytv_last_input_us = sceKernelGetSystemTimeWide();
  e.size.x = e.size.y = 20.f;
  ytv_vita_lifecycle_send(kSbEventTypeInput, &e);
}
void send_media_key(SbWindow window, SbKey key,
                    SbInputDeviceType device = kSbInputDeviceTypeKeyboard) {
  for (int i = 0; i < 2; i++) {
    SbInputData e = {};
    e.window = window;
    e.type = i == 0 ? kSbInputEventTypePress : kSbInputEventTypeUnpress;
    e.device_type = device;
    e.device_id = 0;
    e.key = key;
    ytv_vita_lifecycle_send(kSbEventTypeInput, &e);
  }
}

bool video_playing(uint64_t now) {
  uint64_t a = g_ytv_audio_active_us;
  return a && (int64_t)(now - a) < 1500000;  // signed: the audio thread may be ahead of now
}

// -1 left third, +1 right third, 0 middle.
int tap_side(float x) { return x < 960.f / 3 ? -1 : (x > 960.f * 2 / 3 ? 1 : 0); }

void click_at(SbWindow window, float x, float y) {
  send_touch(window, kSbInputEventTypePress, x, y);
  send_touch(window, kSbInputEventTypeUnpress, x, y);
}

// A finished one-finger tap: click, wait for a second tap, or seek.
void handle_tap(SbWindow window, uint64_t now, float x, float y) {
  int side = tap_side(x);
  if (g_tap_pending) {
    bool same = tap_side(g_tap_pending_x) == side && now - g_tap_pending_us < kDoubleTapUs;
    g_tap_pending = false;
    if (same && side != 0) {
      // What a user does with the pad (verified in Vita3K: SbPlayerSeek
      // followed): D-pad shows the controls / steps the scrubber, X commits.
      // The media FastForward/Rewind keys open a scrubber that X does not
      // commit. A double tap while the scrubber is open adds one step.
      const uint32_t dir = side < 0 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT;
      if (!g_seek_commit_us || dir != g_seek_dir) {
        g_seek_dir = dir;
        g_seek_steps = 2;  // the first press shows the controls
        g_seek_next_us = now;
      } else {
        g_seek_steps++;
      }
      g_seek_commit_us = now + (uint64_t)g_seek_steps * kSeekStepUs + kSeekCommitUs;
      char msg[64];
      snprintf(msg, sizeof(msg), "YTV_TOUCH_DOUBLE_TAP seek=%s\n", side < 0 ? "back" : "forward");
      SbLogRaw(msg);
      return;
    }
    click_at(window, g_tap_pending_x, g_tap_pending_y);  // the earlier tap was a click
  }
  if (side != 0 && (video_playing(now) || g_seek_commit_us)) {
    g_tap_pending = true;
    g_tap_pending_us = now;
    g_tap_pending_x = x;
    g_tap_pending_y = y;
    return;
  }
  click_at(window, x, y);
}

// Front panel: 1920x1088 reporting grid -> 960x544 window pixels.
void pump_touch(SbWindow window, uint64_t now) {
  if (g_seek_steps > 0 && (int64_t)(now - g_seek_next_us) >= 0) {
    gesture_push(g_seek_dir);
    g_seek_steps--;
    g_seek_next_us = now + kSeekStepUs;
  }
  if (g_seek_commit_us && g_seek_steps == 0 && (int64_t)(now - g_seek_commit_us) >= 0) {
    g_seek_commit_us = 0;
    // Commit the scrub with the controller's select button (X), as a user
    // would; the keyboard Enter only toggled the player controls.
    gesture_push(SCE_CTRL_CROSS);
  }
  if (g_tap_pending && now - g_tap_pending_us >= kDoubleTapUs) {
    g_tap_pending = false;  // no second tap: it was a click
    click_at(window, g_tap_pending_x, g_tap_pending_y);
  }
  SceTouchData t;
  memset(&t, 0, sizeof(t));
  bool have = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0;
  bool finger = false;
  int fingers = 0;
  float x = g_touch_x, y = g_touch_y;
  int id = -1;
  if (have && t.reportNum > 0) {
    int pick = 0;
    for (int i = 0; i < (int)t.reportNum; i++)
      if (t.report[i].id == g_touch_id) pick = i;
    finger = true;
    fingers = (int)t.reportNum;
    id = t.report[pick].id;
    x = t.report[pick].x * 0.5f;
    y = t.report[pick].y * 0.5f;
  }
  // Diagnostic scripted tap / swipe behave like one finger.
  if (g_tap_active) {
    if (now >= g_tap_release_us) g_tap_active = false;
    else {
      finger = true; fingers = 1; id = 0x7fff; x = g_tap_x; y = g_tap_y;
      if (g_tap_jitter) {  // real fingers wobble a few pixels while down
        static int k = 0; k++;
        x += (float)((k % 5) - 2) * 2.f; y += (float)(((k / 2) % 5) - 2) * 2.f;
      }
    }
  }
  if (g_swipe_active) {
    const float dur = 350000.f;
    float f = (float)(now - g_swipe_start_us) / dur;
    if (f > 1.f) {
      g_swipe_active = false;
    } else {
      finger = true; fingers = 1; id = 0x7ffe;
      x = g_swipe_x0 + (g_swipe_x1 - g_swipe_x0) * f;
      y = g_swipe_y0 + (g_swipe_y1 - g_swipe_y0) * f;
    }
  }
  if (finger && !g_touch_down) {
    g_press_x = g_anchor_x = x; g_press_y = g_anchor_y = y;
    g_swiped = false;
    g_scrub_touch = y > kScrubZoneY && (video_playing(now) || g_seek_commit_us);
    g_max_fingers = fingers;
  } else if (finger && g_touch_down) {
    if (fingers > g_max_fingers) g_max_fingers = fingers;
    float dx = x - g_anchor_x, dy = y - g_anchor_y;
    float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (g_max_fingers == 1 && g_scrub_touch && ax >= kScrubStep && ax >= ay * 0.7f) {
      // Scrub: finger right = forward (like dragging the bar's handle).
      gesture_push(dx > 0 ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
      g_anchor_x += dx > 0 ? kScrubStep : -kScrubStep;
      g_anchor_y = y;
      g_swiped = true;
    } else if (g_max_fingers == 1 && !g_scrub_touch && ax >= kSwipeStep && ax >= ay) {
      gesture_push(dx < 0 ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT);
      g_anchor_x += dx < 0 ? -kSwipeStep : kSwipeStep;
      g_anchor_y = y;
      g_swiped = true;
    } else if (g_max_fingers == 1 && ay >= kSwipeStep) {
      gesture_push(dy < 0 ? SCE_CTRL_DOWN : SCE_CTRL_UP);
      g_anchor_y += dy < 0 ? -kSwipeStep : kSwipeStep;
      g_anchor_x = x;
      g_swiped = true;
    }
  } else if (!finger && g_touch_down) {
    if (g_scrub_touch && g_swiped) g_seek_commit_us = now + 700000;  // commit the scrub with X
    float dx = g_touch_x - g_press_x, dy = g_touch_y - g_press_y;
    bool still = dx * dx + dy * dy <= (2 * kTapSlop) * (2 * kTapSlop);
    bool tap = !g_swiped && still && g_max_fingers < 2;
    if (!g_swiped && g_max_fingers >= 3) {
      ytv_addons_toggle();  // three-finger tap: add-on menu
    } else if (!g_swiped && g_max_fingers >= 2) {
      gesture_push(SCE_CTRL_CIRCLE);  // Back
    } else if (tap && ytv_addons_menu_open()) {
      ytv_addons_tap(g_press_x, g_press_y);
    } else if (tap) {
      // A tap: pointer click where the finger went down (or a double tap).
      handle_tap(window, now, g_press_x, g_press_y);
    }
    char msg[112];
    snprintf(msg, sizeof(msg), "YTV_TOUCH_GESTURE fingers=%d swiped=%d tap=%d at=%d,%d\n",
             g_max_fingers, (int)g_swiped, (int)tap, (int)g_press_x, (int)g_press_y);
    SbLogRaw(msg);
  }
  if (finger) { g_touch_x = x; g_touch_y = y; }
  g_touch_down = finger;
  g_touch_id = id;
}
}  // namespace

extern "C" bool ytv_osk_active();
extern "C" int ytv_vita_input_pump() {
  SbWindow window = ytv_vita_current_window();
  if (!SbWindowIsValid(window)) return 0;  // Cobalt has no window yet
  // Give the system IME exclusive access to the controller and touchscreen.
  // Otherwise X/O used for typing also activates the YouTube TV player.
  if (ytv_osk_active()) return 0;
  uint64_t now = sceKernelGetSystemTimeWide();
  if (!g_start_us) {
    g_start_us = now;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  }
  if (!g_script_loaded) load_script();
  SceCtrlData pad;
  memset(&pad, 0, sizeof(pad));
  uint32_t current = 0;
  if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) current = pad.buttons;
  // Scripted presses (diagnostic): hold for 120 ms, then release.
  if (g_script_held && now >= g_script_release_us) g_script_held = 0;
  if (!g_script_held && !g_tap_active && !g_swipe_active && g_script_next < g_script_count &&
      g_script[g_script_next].button == 0 &&
      (now - g_start_us) / 1000 >= g_script[g_script_next].at_ms) {
    const ScriptStep& st = g_script[g_script_next++];
    if (st.swipe) {
      g_swipe_active = true;
      g_swipe_start_us = now;
      g_swipe_x0 = st.tap_x; g_swipe_y0 = st.tap_y; g_swipe_x1 = st.to_x; g_swipe_y1 = st.to_y;
    } else {
      g_tap_active = true;
      g_tap_jitter = st.jitter;
      g_tap_x = st.tap_x; g_tap_y = st.tap_y;
      g_tap_release_us = now + 150000;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "YTV_INPUT_SCRIPT_TAP t=%llums x=%d y=%d\n",
             (unsigned long long)((now - g_start_us) / 1000), st.tap_x, st.tap_y);
    SbLogRaw(msg);
  }
  if (!g_script_held && g_script_next < g_script_count &&
      g_script[g_script_next].button != 0 &&
      (now - g_start_us) / 1000 >= g_script[g_script_next].at_ms) {
    g_script_held = g_script[g_script_next++].button;
    g_script_release_us = now + 120000;
    char msg[80];
    snprintf(msg, sizeof(msg), "YTV_INPUT_SCRIPT_PRESS t=%llums button=0x%x\n",
             (unsigned long long)((now - g_start_us) / 1000), (unsigned)g_script_held);
    SbLogRaw(msg);
  }
  current |= g_script_held;
  // Gesture steps: 90 ms press, then at least one released poll before the next.
  if (g_gesture_held && now >= g_gesture_release_us) {
    g_gesture_held = 0;
  } else if (!g_gesture_held && g_gq_count > 0 && !(g_previous & g_gesture_queue[g_gq_head])) {
    g_gesture_held = g_gesture_queue[g_gq_head];
    g_gq_head = (g_gq_head + 1) % 16; g_gq_count--;
    g_gesture_release_us = now + 90000;
  }
  current |= g_gesture_held;
  SbInputData events[16];
  // The add-on menu (SELECT) takes the buttons while it is open; Cobalt then
  // sees no change. Buttons held when it closes are released only after
  // they are let go (g_previous keeps tracking the real state).
  static uint32_t s_blocked = 0;
  if (ytv_addons_input(g_previous, current)) s_blocked |= current;
  s_blocked &= current;
  uint32_t visible_prev = g_previous & ~s_blocked, visible_cur = current & ~s_blocked;
  if (ytv_addons_menu_open()) visible_cur = visible_prev;
  // L/R switched off in the add-on menu.
  const uint32_t hidden = ytv_addons_hidden_buttons();
  visible_prev &= ~hidden;
  visible_cur &= ~hidden;
  int count = YtvTranslateVitaButtons(visible_prev, visible_cur, window, events, 16);
  g_previous = current;
  for (int i = 0; i < count; i++) {
    if (events[i].type == kSbInputEventTypePress) {
      g_ytv_last_input_us = now;
      // Original short UI cues: focus, confirm and back, separate from video audio.
      const SbKey k = events[i].key;
      if (k == kSbKeyGamepadDPadUp || k == kSbKeyGamepadDPadDown || k == kSbKeyGamepadDPadLeft ||
          k == kSbKeyGamepadDPadRight)
        ytv_ui_sound(1);
      else if (k == kSbKeyGamepad1)
        ytv_ui_sound(2);
      else if (k == kSbKeyGamepad2)
        ytv_ui_sound(3);
    }
    ytv_vita_lifecycle_send(kSbEventTypeInput, &events[i]);
  }
  pump_touch(window, now);
  return count > 0 ? count : 0;
}
