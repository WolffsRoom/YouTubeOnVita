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
extern "C" void ytv_vita_lifecycle_send(SbEventType type, void* data);
// Time of the latest press sent to Cobalt; the next presented frame reports
// the input-to-photon latency (YTV_INPUT_LATENCY, verbose log only).
extern "C" volatile uint64_t g_ytv_last_input_us;
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
// Front panel: 1920x1088 reporting grid -> 960x544 window pixels.
void pump_touch(SbWindow window, uint64_t now) {
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
    g_max_fingers = fingers;
  } else if (finger && g_touch_down) {
    if (fingers > g_max_fingers) g_max_fingers = fingers;
    float dx = x - g_anchor_x, dy = y - g_anchor_y;
    float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (g_max_fingers == 1 && ax >= kSwipeStep && ax >= ay) {
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
    float dx = g_touch_x - g_press_x, dy = g_touch_y - g_press_y;
    bool still = dx * dx + dy * dy <= (2 * kTapSlop) * (2 * kTapSlop);
    bool tap = !g_swiped && still && g_max_fingers < 2;
    if (!g_swiped && g_max_fingers >= 2) {
      gesture_push(SCE_CTRL_CIRCLE);  // Back
    } else if (tap) {
      // A tap: pointer click where the finger went down.
      send_touch(window, kSbInputEventTypePress, g_press_x, g_press_y);
      send_touch(window, kSbInputEventTypeUnpress, g_press_x, g_press_y);
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

extern "C" int ytv_vita_input_pump() {
  SbWindow window = ytv_vita_current_window();
  if (!SbWindowIsValid(window)) return 0;  // Cobalt has no window yet
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
  int count = YtvTranslateVitaButtons(g_previous, current, window, events, 16);
  g_previous = current;
  for (int i = 0; i < count; i++) {
    if (events[i].type == kSbInputEventTypePress) g_ytv_last_input_us = now;
    ytv_vita_lifecycle_send(kSbEventTypeInput, &events[i]);
  }
  pump_touch(window, now);
  return count > 0 ? count : 0;
}
