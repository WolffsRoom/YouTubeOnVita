/*
 * Native add-on menu drawn on top of Cobalt (nothing is injected into the
 * YouTube page). Opened with SELECT or a three-finger tap.
 *
 *   - Sleep timer: pauses playback (media Pause key) when it expires and
 *     lets the Vita go to sleep normally.
 *   - Screen off: turns the OLED/LCD off while audio keeps playing; any
 *     button turns it back on.
 *   - Screenshot: saves the current screen as PNG in ux0:picture/YouTubeVita.
 *
 * Also keeps the Vita from dimming/sleeping while a video plays.
 *
 * Drawing: the menu is composed on the CPU into an opaque RGBA panel. When
 * Cobalt is presenting frames, the panel is drawn as a textured quad just
 * before each eglSwapBuffers (GL state is saved and restored). When Cobalt
 * is idle (static page, no swaps), the panel is written straight into the
 * displayed framebuffer, and the pixels underneath are restored on close.
 */
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/rtc.h>
#include <vitaGL.h>
#include <zlib.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include "starboard/event.h"
#include "starboard/input.h"
#include "starboard/key.h"
#include "starboard/log.h"
#include "starboard/window.h"
#include "addon_font.h"
#include "addon_font_bold.h"

extern "C" void ytv_vita_lifecycle_send(SbEventType type, void* data);
extern "C" SbWindow ytv_vita_current_window();
extern "C" int ytv_download_start_or_cancel();
extern "C" void ytv_download_get_status(char*, size_t);
extern "C" bool ytv_download_config_read(int field, char*, size_t);
extern "C" bool ytv_osk_open_download_config(int field);
extern "C" bool ytv_osk_open_native_video_url();
extern "C" bool ytv_native_download_set_video(const char*);
extern "C" bool ytv_native_download_running();
extern "C" bool ytv_native_download_has_video();
extern "C" int ytv_native_download_start(bool mp3,int quality);
extern "C" bool ytv_native_download_cancel_current();
extern "C" bool ytv_native_download_running();
extern "C" void ytv_native_download_get_status(char*,size_t);
extern "C" bool ytv_native_player_present();
extern "C" void ytv_ui_sound_enable(bool on);  // port_ui_sound.cpp
extern "C" volatile int g_ytv_graphics_ext_active; // port_system.cpp
extern "C" void ytv_ui_sound(int kind);
extern volatile int g_ytv_hud_video_h;             // port_player.cpp
extern "C" volatile size_t g_vgl_free_vram, g_vgl_free_ram, g_vgl_free_phy;  // port_graphics.cpp
extern volatile unsigned g_ytv_hud_dropped_total;  // port_player.cpp
// Read atomically in the Starboard DNS worker threads.
extern "C" { int g_ytv_adblock_enabled = 0; }

namespace {

constexpr int kScreenW = 960, kScreenH = 544;
constexpr int kPanelW = 520, kPanelH = 520;
constexpr int kToastH = 52;

// Native pages instead of modifying the proprietary YouTube TV DOM.
enum Page { kPageNone, kPageHome, kPagePlayer, kPageSettings, kPageDisplay, kPageDownload };
Page g_page = kPageNone;
Page g_player_return = kPageHome;
int g_quality_idx = 1;            // 360, 480, 720
bool g_audio_only = false;         // MP4 by default
int g_language_pref = 0;           // 0=system, 1=PT, 2=English
bool g_system_pt = false;
// Screen and info page.
const int kDimLevels[] = {0, 20, 35, 50, 65, 80, 90};
constexpr int kDimChoices = sizeof(kDimLevels) / sizeof(kDimLevels[0]);
int g_dim_idx = 0;          // internal darkening, independent of the backlight
bool g_perf_hud = false;    // FPS / memory / video info in a corner
bool g_clock_hud = false;   // clock and battery in a corner
bool g_ui_sound = true;     // original, gentle navigation cues ON by default
bool g_fhd_cfg = false;     // Full HD choice (takes effect when the app starts)
bool g_fhd_boot = false;    // what this run started with
const int kTimerMinutes[] = {0, 15, 30, 45, 60, 90, 120};
constexpr int kTimerChoices = sizeof(kTimerMinutes) / sizeof(kTimerMinutes[0]);

// Shared state. Written by the main (input) thread, read by the renderer
// thread in ytv_addons_before_swap(); a generation counter publishes a new
// panel image.
volatile bool g_menu_open = false;
volatile int g_toast_until_ms = 0;  // toast visible while now_ms < this
int g_sel = 0;
int g_timer_idx = 0;
uint64_t g_timer_deadline_us = 0;
bool g_screen_off = false;
bool g_shot_pending = false;
uint64_t g_shot_at_us = 0;
uint64_t g_screen_off_at_us = 0;
bool g_input_locked = false;  // screen and buttons ignored; SELECT unlocks
bool g_lr_enabled = true;     // L/R reach YouTube (saved in addons.cfg)
constexpr const char* kAddonCfg = "ux0:data/youtubevita/addons.cfg";
bool g_pt = false;  // Portuguese UI (system or user preference)

// Current overlay image (panel or toast).
uint8_t* g_img = nullptr;  // kPanelW*kPanelH*4, top row first
volatile int g_img_w = 0, g_img_h = 0;
volatile uint32_t g_img_gen = 0;

// Renderer-thread side.
volatile uint64_t g_last_swap_us = 0;
GLuint g_tex = 0, g_prog = 0;
GLint g_loc_pos = -1, g_loc_uv = -1, g_loc_tex = -1, g_loc_alpha = -1;
GLuint g_tex_hud = 0, g_tex_black = 0;
uint32_t g_hud_tex_gen = 0;
uint32_t g_tex_gen = 0;
bool g_gl_failed = false;

// Idle (CPU) path: what was painted into which display buffer.
void* g_painted_base = nullptr;
uint32_t g_painted_gen = 0;
int g_painted_x = 0, g_painted_y = 0, g_painted_w = 0, g_painted_h = 0;
uint64_t g_painted_after_swap = 0;
uint8_t* g_backup = nullptr;  // pixels under the painted panel

uint64_t now_us() { return sceKernelGetSystemTimeWide(); }

void log_line(const char* s) { SbLogRaw(s); }

// ---- Drawing ---------------------------------------------------------------
// Everything is composed on the CPU into an RGBA image. Shapes are
// anti-aliased inside the panel; the panel's outer edge keeps binary alpha so
// the idle path can copy pixels without blending.
struct Font { const AddonGlyph* g; const uint8_t* px; int w, h; };
const Font kRegular = {kAddonGlyphs, kAddonFont, kAddonFontW, kAddonFontH};
const Font kBold = {kAddonBoldGlyphs, kAddonBoldFont, kAddonBoldFontW, kAddonBoldFontH};

// Palette (YouTube dark theme).
constexpr uint32_t kBg = 0x181818, kSurface = 0x272727, kSelected = 0x3f3f3f, kDivider = 0x333333;
constexpr uint32_t kText = 0xf1f1f1, kTextDim = 0xaaaaaa, kRed = 0xff0033;
constexpr uint32_t kCrossBlue = 0x8fb4ff, kCircleRed = 0xff7a7a;

// UTF-8 (only Latin-1 code points are drawn) -> glyph indices.
int next_cp(const char*& s) {
  unsigned char c = (unsigned char)*s;
  if (!c) return 0;
  if (c < 0x80) { s++; return c; }
  if ((c & 0xe0) == 0xc0 && s[1]) {
    int cp = ((c & 0x1f) << 6) | ((unsigned char)s[1] & 0x3f);
    s += 2;
    return cp < 256 ? cp : '?';
  }
  s++;
  while ((*s & 0xc0) == 0x80) s++;
  return '?';
}

int text_width(const char* s, const Font& f = kRegular) {
  int w = 0;
  for (int cp; (cp = next_cp(s));) w += f.g[cp].adv;
  return w;
}

struct Canvas { uint8_t* px; int w, h; };

void blend(const Canvas& c, int x, int y, uint32_t rgb, int a) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h || a <= 0) return;
  if (a > 255) a = 255;
  uint8_t* p = c.px + ((size_t)y * c.w + x) * 4;
  if (!p[3]) return;  // outside the panel shape
  if (p[3] < 255) p[3] = (uint8_t)(p[3] + (255 - p[3]) * a / 255);  // translucent layers (HUD)
  int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  p[0] = (uint8_t)((p[0] * (255 - a) + r * a) / 255);
  p[1] = (uint8_t)((p[1] * (255 - a) + g * a) / 255);
  p[2] = (uint8_t)((p[2] * (255 - a) + b * a) / 255);
}

void draw_text(const Canvas& c, int x, int y, const char* s, uint32_t rgb, const Font& f = kRegular) {
  for (int cp; (cp = next_cp(s));) {
    const AddonGlyph& g = f.g[cp];
    for (int gy = 0; gy < f.h; gy++)
      for (int gx = 0; gx < g.w; gx++) blend(c, x + gx, y + gy, rgb, f.px[gy * f.w + g.x + gx]);
    x += g.adv;
  }
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Signed distance to a rounded rectangle (negative inside).
float round_rect_sd(float px, float py, float x0, float y0, float x1, float y1, float r) {
  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  float hx = (x1 - x0) * 0.5f - r, hy = (y1 - y0) * 0.5f - r;
  float dx = fabsf(px - cx) - hx, dy = fabsf(py - cy) - hy;
  float ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
  float inside = dx > dy ? dx : dy;
  return sqrtf(ox * ox + oy * oy) + (inside < 0 ? inside : 0) - r;
}

void fill_round(const Canvas& c, float x0, float y0, float x1, float y1, float r, uint32_t rgb) {
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
      float d = round_rect_sd(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
      blend(c, x, y, rgb, (int)(clampf(0.5f - d, 0.f, 1.f) * 255.f));
    }
}

void fill_rect(const Canvas& c, int x0, int y0, int x1, int y1, uint32_t rgb) {
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) blend(c, x, y, rgb, 255);
}

// Anti-aliased segment of thickness t.
void line(const Canvas& c, float x0, float y0, float x1, float y1, float t, uint32_t rgb) {
  float minx = (x0 < x1 ? x0 : x1) - t, maxx = (x0 > x1 ? x0 : x1) + t;
  float miny = (y0 < y1 ? y0 : y1) - t, maxy = (y0 > y1 ? y0 : y1) + t;
  float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
  for (int y = (int)miny; y <= (int)maxy; y++)
    for (int x = (int)minx; x <= (int)maxx; x++) {
      float px = x + 0.5f - x0, py = y + 0.5f - y0;
      float k = len2 > 0 ? clampf((px * vx + py * vy) / len2, 0.f, 1.f) : 0.f;
      float dx = px - k * vx, dy = py - k * vy;
      float d = sqrtf(dx * dx + dy * dy) - t * 0.5f;
      blend(c, x, y, rgb, (int)(clampf(0.5f - d, 0.f, 1.f) * 255.f));
    }
}

void ring(const Canvas& c, float cx, float cy, float r, float t, uint32_t rgb) {
  for (int y = (int)(cy - r - t); y <= (int)(cy + r + t); y++)
    for (int x = (int)(cx - r - t); x <= (int)(cx + r + t); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      float d = fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f;
      blend(c, x, y, rgb, (int)(clampf(0.5f - d, 0.f, 1.f) * 255.f));
    }
}

void disc(const Canvas& c, float cx, float cy, float r, uint32_t rgb) {
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      float d = sqrtf(dx * dx + dy * dy) - r;
      blend(c, x, y, rgb, (int)(clampf(0.5f - d, 0.f, 1.f) * 255.f));
    }
}

// Opaque panel shape: rounded rectangle with binary outer alpha.
void base_panel(const Canvas& c, uint32_t rgb, int radius) {
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      uint8_t* p = c.px + ((size_t)y * c.w + x) * 4;
      float d = round_rect_sd(x + 0.5f, y + 0.5f, 0, 0, (float)c.w, (float)c.h, (float)radius);
      p[0] = (rgb >> 16) & 255; p[1] = (rgb >> 8) & 255; p[2] = rgb & 255;
      p[3] = d <= 0.f ? 255 : 0;
    }
}

// ---- Icons (24x24, centred on cx, cy) --------------------------------------
void icon_timer(const Canvas& c, float cx, float cy, uint32_t col) {
  ring(c, cx, cy + 1, 9, 2.2f, col);
  line(c, cx, cy + 1, cx, cy - 4.5f, 2.2f, col);
  line(c, cx, cy + 1, cx + 4, cy + 3, 2.2f, col);
  line(c, cx - 3, cy - 10, cx + 3, cy - 10, 2.2f, col);
}
void icon_screen_off(const Canvas& c, float cx, float cy, uint32_t col, uint32_t bg) {
  fill_round(c, cx - 11, cy - 8, cx + 11, cy + 7, 2.5f, col);
  fill_round(c, cx - 9, cy - 6, cx + 9, cy + 5, 1.f, bg);
  line(c, cx - 4, cy + 10, cx + 4, cy + 10, 2.2f, col);
  // moon inside the screen
  disc(c, cx - 1, cy - 0.5f, 4.2f, col);
  disc(c, cx + 1.4f, cy - 2.2f, 3.6f, bg);
}
void icon_camera(const Canvas& c, float cx, float cy, uint32_t col, uint32_t bg) {
  fill_round(c, cx - 11, cy - 6, cx + 11, cy + 9, 3.f, col);
  fill_round(c, cx - 5, cy - 9, cx + 4, cy - 4, 1.5f, col);
  disc(c, cx, cy + 1.5f, 5.2f, bg);
  disc(c, cx, cy + 1.5f, 3.2f, col);
}
void icon_lock(const Canvas& c, float cx, float cy, uint32_t col, uint32_t bg) {
  ring(c, cx, cy - 3, 6, 2.4f, col);                       // shackle
  fill_round(c, cx - 9, cy - 2, cx + 9, cy + 10, 2.5f, col);  // body
  disc(c, cx, cy + 3.5f, 2.2f, bg);                        // keyhole
}
void icon_lr(const Canvas& c, float cx, float cy, uint32_t col, uint32_t bg) {
  fill_round(c, cx - 12, cy - 8, cx - 1, cy + 8, 4, col);
  fill_round(c, cx + 1, cy - 8, cx + 12, cy + 8, 4, col);
  line(c, cx - 8, cy - 4, cx - 8, cy + 4, 2.f, bg);   // L
  line(c, cx - 8, cy + 4, cx - 4, cy + 4, 2.f, bg);
  line(c, cx + 5, cy - 4, cx + 5, cy + 4, 2.f, bg);   // R
  line(c, cx + 5, cy - 4, cx + 8, cy - 2, 2.f, bg);
  line(c, cx + 8, cy - 2, cx + 5, cy, 2.f, bg);
  line(c, cx + 5, cy, cx + 8.5f, cy + 4, 2.f, bg);
}
void icon_download(const Canvas& c, float cx, float cy, uint32_t col) {
  line(c, cx, cy - 10, cx, cy + 6, 2.4f, col);
  line(c, cx - 6, cy, cx, cy + 6, 2.4f, col);
  line(c, cx, cy + 6, cx + 6, cy, 2.4f, col);
  line(c, cx - 9, cy + 10, cx + 9, cy + 10, 2.4f, col);
}
void icon_sun(const Canvas& c, float cx, float cy, uint32_t col) {
  disc(c, cx, cy, 4.5f, col);
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159265f / 4;
    line(c, cx + cosf(a) * 7.5f, cy + sinf(a) * 7.5f, cx + cosf(a) * 10.5f, cy + sinf(a) * 10.5f, 2.f, col);
  }
}
void icon_gear(const Canvas& c, float cx, float cy, uint32_t col, uint32_t bg) {
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159265f / 4;
    line(c, cx + cosf(a) * 6, cy + sinf(a) * 6, cx + cosf(a) * 10.5f, cy + sinf(a) * 10.5f, 3.4f, col);
  }
  disc(c, cx, cy, 7.5f, col);
  disc(c, cx, cy, 3.2f, bg);
}
void icon_chart(const Canvas& c, float cx, float cy, uint32_t col) {
  fill_round(c, cx - 10, cy + 2, cx - 5, cy + 10, 1, col);
  fill_round(c, cx - 2.5f, cy - 4, cx + 2.5f, cy + 10, 1, col);
  fill_round(c, cx + 5, cy - 10, cx + 10, cy + 10, 1, col);
}
void icon_close(const Canvas& c, float cx, float cy, uint32_t col) {
  line(c, cx - 7, cy - 7, cx + 7, cy + 7, 2.4f, col);
  line(c, cx + 7, cy - 7, cx - 7, cy + 7, 2.4f, col);
}
void glyph_cross(const Canvas& c, float cx, float cy) {
  line(c, cx - 5, cy - 5, cx + 5, cy + 5, 2.2f, kCrossBlue);
  line(c, cx + 5, cy - 5, cx - 5, cy + 5, 2.2f, kCrossBlue);
}
void glyph_circle(const Canvas& c, float cx, float cy) { ring(c, cx, cy, 6, 2.2f, kCircleRed); }

// Footer hint: icon or key name, then the action.
int hint(const Canvas& c, int x, int y, int key, const char* label) {
  const int cy = y + kAddonFontH / 2;
  if (key == 1) { glyph_cross(c, x + 8, cy); x += 22; }
  else if (key == 2) { glyph_circle(c, x + 8, cy); x += 22; }
  else {
    int kw = text_width("SELECT") + 12;
    fill_round(c, x, cy - 10, x + kw, cy + 10, 10, kSelected);
    draw_text(c, x + 6, y, "SELECT", kTextDim);
    x += kw + 6;
  }
  draw_text(c, x, y, label, kTextDim);
  return x + text_width(label) + 22;
}

const char* T(const char* en, const char* pt) { return g_pt ? pt : en; }

void timer_value(char* out, size_t n) {
  if (g_timer_deadline_us) {
    uint64_t now = now_us();
    int left = g_timer_deadline_us > now ? (int)((g_timer_deadline_us - now) / 60000000ull) + 1 : 0;
    snprintf(out, n, T("%d min left", "faltam %d min"), left);
  } else {
    snprintf(out, n, "%s", T("Off", "Desligado"));
  }
}

constexpr int kRowY0 = 62, kRowH = 42, kRowX0 = 14;
constexpr const char* kQualities[] = {"360p", "480p", "720p"};
constexpr const char* kQualityValues[] = {"360", "480", "720"};

int page_count() {
  if (g_page == kPagePlayer) return 6;
  if (g_page == kPageDownload) return 4;
  if (g_page == kPageSettings) return 7;
  if (g_page == kPageDisplay) return 5;
  return 5;
}
int panel_height() { return kRowY0 + page_count() * kRowH + 64; }
const char* menu_title() {
  switch (g_page) {
    case kPagePlayer: return T("Player settings", "Configurações do player");
    case kPageDownload: return T("Download", "Baixar");
    case kPageSettings: return T("Settings", "Configurações");
    case kPageDisplay: return T("Screen and info", "Tela e informações");
    default: return "Youtube on Vita";
  }
}
const char* item_text(int i) {
  if (g_page == kPagePlayer) {
    const char* labels[] = {
      T("Download", "Baixar"),
      T("Screenshot", "Capturar tela"),
      T("Sleep timer", "Timer para pausar"),
      T("Black Screen", "Tela preta"),
      T("Darken Screen", "Escurecer tela"),
      T("Back", "Voltar"),
    };
    return labels[i];
  }
  if (g_page == kPageDownload) {
    const char* labels[] = {
      T("Video (MP4)", "Vídeo (MP4)"),
      T("Audio (MP3)", "Áudio (MP3)"),
      T("Cancel current download", "Cancelar download atual"),
      T("Back", "Voltar"),
    };
    return labels[i];
  }
  if (g_page == kPageSettings) {
    const char* labels[] = {
      T("AdBlock (partial)", "AdBlock (parcial)"),
      T("Download quality", "Qualidade download"),
      T("Download format", "Formato download"),
      T("L / R buttons", "Botões L / R"),
      T("Lock input", "Bloquear entrada"),
      T("Language", "Idioma"),
      T("Back", "Voltar"),
    };
    return labels[i];
  }
  if (g_page == kPageDisplay) {
    const char* labels[] = {
      T("Full HD (beta)", "Full HD (beta)"),
      T("Performance", "Desempenho"),
      T("Clock and battery", "Relógio e bateria"),
      T("Navigation sounds", "Sons de navegação"),
      T("Back", "Voltar"),
    };
    return labels[i];
  }
  const char* labels[] = {
    T("Player settings", "Configurações do player"),
    T("Settings", "Configurações"),
    T("Screen and info", "Tela e informações"),
    T("Vita Settings", "Configurações do Vita"),
    T("Close", "Fechar"),
  };
  return labels[i];
}
void icon_item(const Canvas& c, int i, float x, float y, uint32_t col, uint32_t bg) {
  if (g_page == kPagePlayer) {
    switch(i) {
      case 0: icon_download(c,x,y,col); break;
      case 1: icon_camera(c,x,y,col,bg); break;
      case 2: icon_timer(c,x,y,col); break;
      case 3: icon_screen_off(c,x,y,col,bg); break;
      case 4: icon_sun(c,x,y,col); break;
      default: icon_close(c,x,y,col); break;
    }
  } else if (g_page == kPageDownload) {
    if(i<2) icon_download(c,x,y,col);
    else icon_close(c,x,y,col);
  } else if (g_page == kPageSettings) {
    switch(i) {
      case 0: icon_lock(c,x,y,col,bg); break;
      case 1: icon_download(c,x,y,col); break;
      case 2: icon_download(c,x,y,col); break;
      case 4: icon_lock(c,x,y,col,bg); break;
      case 3: icon_lr(c,x,y,col,bg); break;
      case 5: icon_timer(c,x,y,col); break;
      default: icon_close(c,x,y,col); break;
    }
  } else if (g_page == kPageDisplay) {
    switch(i) {
      case 0: icon_screen_off(c,x,y,col,bg); break;
      case 1: icon_chart(c,x,y,col); break;
      case 2: icon_timer(c,x,y,col); break;
      case 3: icon_lr(c,x,y,col,bg); break;
      default: icon_close(c,x,y,col); break;
    }
  } else {
    if (i == 0) icon_screen_off(c,x,y,col,bg);
    else if (i == 1) icon_lr(c,x,y,col,bg);
    else if (i == 2) icon_sun(c,x,y,col);
    else if (i == 3) icon_gear(c,x,y,col,bg);
    else icon_close(c,x,y,col);
  }
}
const char* item_value(int i, const char* timer, char* scratch, size_t capacity) {
  if (g_page == kPagePlayer) {
    if (i == 0) {
      ytv_native_download_get_status(scratch,capacity);
      return scratch;
    }
    if (i == 2) return timer;
    if (i == 4) {
      if (!kDimLevels[g_dim_idx]) return T("Off","Desligado");
      snprintf(scratch,capacity,"%d%%",kDimLevels[g_dim_idx]);
      return scratch;
    }
  }
  if (g_page == kPageDownload) {
    if(i==0) return kQualities[g_quality_idx];
    if(i==1) return "160 kbps";
  }
  if (g_page == kPageSettings) {
    switch(i) {
      case 0: return __atomic_load_n(&g_ytv_adblock_enabled,__ATOMIC_RELAXED) ? T("On","Ligado") : T("Off","Desligado");
      case 1: return kQualities[g_quality_idx];
      case 2: return g_audio_only ? "MP3" : "MP4";
      case 3: return g_lr_enabled ? T("On","Ligados") : T("Off","Desligados");
      case 5: return g_language_pref == 0 ? T("System","Sistema") : (g_language_pref == 1 ? "PT-BR" : "English");
    }
  }
  if (g_page == kPageDisplay) {
    switch(i) {
      case 0:
        if (g_fhd_cfg != g_fhd_boot) return T("Reopen the app","Reabra o app");
        return g_fhd_cfg ? T("On","Ligado") : T("Off","Desligado");
      case 1: return g_perf_hud ? T("On","Ligado") : T("Off","Desligado");
      case 2: return g_clock_hud ? T("On","Ligado") : T("Off","Desligado");
      case 3: return g_ui_sound ? T("On","Ligado") : T("Off","Desligado");
    }
  }
  return nullptr;
}
void compose_panel() {
  const int h = panel_height();
  Canvas c = {g_img, kPanelW, h};
  base_panel(c, kBg, 16);
  fill_round(c,20,18,46,36,5,kRed);
  for (int i=0;i<6;i++)
    line(c,30,22.5f+i*1.5f,30+(i<3?i:5-i)*2.6f+1,
         22.5f+i*1.5f,1.6f,0xffffff);
  draw_text(c,56,13,menu_title(),kText,kBold);
  {
    SceDateTime t={};
    sceRtcGetCurrentClockLocalTime(&t);
    int batt=scePowerGetBatteryLifePercent();
    char right[32];
    if (batt>=0 && batt<=100)
      snprintf(right,sizeof(right),"%02d:%02d  ·  %d%%",(int)t.hour,(int)t.minute,batt);
    else snprintf(right,sizeof(right),"%02d:%02d",(int)t.hour,(int)t.minute);
    draw_text(c,kPanelW-22-text_width(right),15,right,kTextDim);
  }
  fill_rect(c,20,52,kPanelW-20,53,kDivider);
  char timer[48]={};
  timer_value(timer,sizeof(timer));
  for (int i=0;i<page_count();i++) {
    const int y=kRowY0+i*kRowH;
    const bool sel=i==g_sel;
    const uint32_t row_bg=sel?kSelected:kBg;
    if (sel) {
      fill_round(c,kRowX0,y+2,kPanelW-kRowX0,y+kRowH-2,10,kSelected);
      fill_round(c,kRowX0,y+10,kRowX0+4,y+kRowH-10,2,kRed);
    }
    const float x=kRowX0+30, cy=y+kRowH/2.f;
    icon_item(c,i,x,cy,sel?kText:kTextDim,row_bg);
    const Font& font=sel?kBold:kRegular;
    draw_text(c,kRowX0+56,y+(kRowH-font.h)/2,
              item_text(i),sel?kText:0xdedede,font);
    char scratch[48]={};
    const char* value=item_value(i,timer,scratch,sizeof(scratch));
    if (value) {
      // Avoid colliding with long settings labels and the right-hand value pill.
      const int pw=text_width(value)+22;
      const int x1=kPanelW-kRowX0-16,x0=x1-pw;
      fill_round(c,x0,y+8,x1,y+kRowH-8,13,
                 sel?kRed:kSurface);
      draw_text(c,x0+11,y+(kRowH-kAddonFontH)/2,value,kText);
    }
  }
  const int fy=h-38;
  fill_rect(c,20,fy-10,kPanelW-20,fy-9,kDivider);
  int x=22;
  x=hint(c,x,fy,1,T("Select","Escolher"));
  x=hint(c,x,fy,2,T("Back","Voltar"));
  hint(c,x,fy,0,T("Close","Fechar"));
  g_img_w=kPanelW;
  g_img_h=h;
  __sync_add_and_fetch(&g_img_gen,1);
}

// icon: 0 none, 1 camera, 2 timer, 3 screen
void compose_toast(const char* text, int icon) {
  const int w = text_width(text) + (icon ? 76 : 48), h = kToastH;
  Canvas c = {g_img, w > kPanelW ? kPanelW : w, h};
  base_panel(c, kSurface, h / 2);
  int x = 24;
  if (icon) {
    const float cy = h / 2.f;
    if (icon == 1) icon_camera(c, x + 12, cy, kText, kSurface);
    else if (icon == 2) icon_timer(c, x + 12, cy, kText);
    else if (icon == 4) icon_lock(c, x + 12, cy, kText, kSurface);
    else icon_screen_off(c, x + 12, cy, kText, kSurface);
    x += 34;
  }
  draw_text(c, x, (h - kAddonFontH) / 2, text, kText);
  g_img_w = c.w;
  g_img_h = h;
  __sync_add_and_fetch(&g_img_gen, 1);
}

bool overlay_visible() {
  return g_menu_open || (int)(now_us() / 1000) < g_toast_until_ms;
}

void show_toast(const char* text, int ms, int icon = 0) {
  if (g_menu_open) return;
  compose_toast(text, icon);
  g_toast_until_ms = (int)(now_us() / 1000) + ms;
}

// ---- Media key -------------------------------------------------------------
void send_key(SbKey key) {
  SbWindow window = ytv_vita_current_window();
  if (!SbWindowIsValid(window)) return;
  for (int i = 0; i < 2; i++) {
    SbInputData e = {};
    e.window = window;
    e.type = i == 0 ? kSbInputEventTypePress : kSbInputEventTypeUnpress;
    e.device_type = kSbInputDeviceTypeKeyboard;
    e.device_id = 0;
    e.key = key;
    ytv_vita_lifecycle_send(kSbEventTypeInput, &e);
  }
}

// ---- Screen off ------------------------------------------------------------
// "Screen off" draws a black frame over everything (GL path every frame,
// CPU path on a static screen) and swallows input until SELECT. The panel is
// NOT turned off: scePowerRequestDisplayOff made the Vita lock, and the lock
// screen suspends the app (video and audio stopped).
uint8_t* g_black_backup = nullptr;   // display buffer under the CPU black frame
void* g_black_base = nullptr;
uint64_t g_black_after_swap = 0;
void black_idle_restore();
void set_screen_off(bool off) {
  if (off == g_screen_off) return;
  g_screen_off = off;
  if (!off) black_idle_restore();
  char line[80];
  snprintf(line, sizeof(line), "YTV_LIFECYCLE_ADDON screen_black=%d\n", (int)off);
  log_line(line);
}
// Static screen (no swaps): blacken the displayed buffer once, keep a copy.
void black_idle(uint64_t now) {
  if ((int64_t)(now - g_last_swap_us) < 150000) { g_black_base = nullptr; return; }
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || !fb.base) return;
  if (fb.base == g_black_base && g_black_after_swap == g_last_swap_us) return;
  const size_t bytes = (size_t)fb.pitch * kScreenH * 4;
  if (!g_black_backup) g_black_backup = (uint8_t*)malloc(bytes);
  if (g_black_backup) memcpy(g_black_backup, fb.base, bytes);
  memset(fb.base, 0, bytes);
  g_black_base = fb.base;
  g_black_after_swap = g_last_swap_us;
}
void black_idle_restore() {
  if (!g_black_base) return;
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  // Only if no new frame was presented since (that one is not black).
  if (g_black_backup && g_black_after_swap == g_last_swap_us &&
      sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0 && fb.base == g_black_base)
    memcpy(fb.base, g_black_backup, (size_t)fb.pitch * kScreenH * 4);
  g_black_base = nullptr;
}

// ---- Screenshot ------------------------------------------------------------
void be32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
bool write_chunk(SceUID fd, const char* type, const uint8_t* data, uint32_t len) {
  uint8_t hdr[8];
  be32(hdr, len);
  memcpy(hdr + 4, type, 4);
  uLong crc = crc32(0, (const Bytef*)type, 4);
  if (len) crc = crc32(crc, data, len);
  uint8_t tail[4];
  be32(tail, (uint32_t)crc);
  return sceIoWrite(fd, hdr, 8) == 8 && (!len || sceIoWrite(fd, data, len) == (int)len) &&
         sceIoWrite(fd, tail, 4) == 4;
}

// Reads the displayed framebuffer (what the user sees) and saves it as PNG.
void save_screenshot() {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || !fb.base) {
    show_toast(T("Screenshot failed", "Falha na captura"), 2000);
    return;
  }
  const int w = fb.width ? (int)fb.width : kScreenW, h = fb.height ? (int)fb.height : kScreenH;
  const size_t row = (size_t)w * 3 + 1;
  uint8_t* raw = (uint8_t*)malloc(row * h);
  uLongf zcap = compressBound((uLong)(row * h));
  uint8_t* z = (uint8_t*)malloc(zcap);
  bool ok = false;
  char path[96];
  path[0] = 0;
  if (raw && z) {
    for (int y = 0; y < h; y++) {
      const uint32_t* src = (const uint32_t*)fb.base + (size_t)y * fb.pitch;
      uint8_t* dst = raw + row * y;
      dst[0] = 0;  // filter: none
      for (int x = 0; x < w; x++) {
        uint32_t px = src[x];  // A8B8G8R8: R in the low byte
        dst[1 + x * 3] = px & 255;
        dst[2 + x * 3] = (px >> 8) & 255;
        dst[3 + x * 3] = (px >> 16) & 255;
      }
    }
    if (compress2(z, &zcap, raw, (uLong)(row * h), 3) == Z_OK) {
      sceIoMkdir("ux0:picture", 0777);
      sceIoMkdir("ux0:picture/YouTubeVita", 0777);
      SceDateTime t;
      memset(&t, 0, sizeof(t));
      sceRtcGetCurrentClockLocalTime(&t);
      snprintf(path, sizeof(path), "ux0:picture/YouTubeVita/YTV_%04d%02d%02d_%02d%02d%02d.png",
               (int)t.year, (int)t.month, (int)t.day, (int)t.hour, (int)t.minute, (int)t.second);
      SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
      if (fd >= 0) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        uint8_t ihdr[13];
        be32(ihdr, (uint32_t)w);
        be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;  // 8-bit RGB
        ok = sceIoWrite(fd, sig, 8) == 8 && write_chunk(fd, "IHDR", ihdr, 13) &&
             write_chunk(fd, "IDAT", z, (uint32_t)zcap) && write_chunk(fd, "IEND", nullptr, 0);
        sceIoClose(fd);
        if (!ok) sceIoRemove(path);
      }
    }
  }
  free(raw);
  free(z);
  char line[160];
  snprintf(line, sizeof(line), "YTV_LIFECYCLE_ADDON screenshot ok=%d path=%s\n", (int)ok, path);
  log_line(line);
  show_toast(ok ? T("Screenshot saved to Photos", "Captura salva em Fotos")
                : T("Screenshot failed", "Falha na captura"),
             2200, 1);
}

// ---- Idle path: paint into / restore the displayed framebuffer -------------
void restore_idle_paint() {
  if (!g_painted_base) return;
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  // Only restore when no frame was presented since (that frame has no panel).
  if (g_last_swap_us == g_painted_after_swap &&
      sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0 && fb.base == g_painted_base) {
    for (int y = 0; y < g_painted_h; y++)
      memcpy((uint32_t*)fb.base + (size_t)(g_painted_y + y) * fb.pitch + g_painted_x,
             g_backup + (size_t)y * g_painted_w * 4, (size_t)g_painted_w * 4);
  }
  g_painted_base = nullptr;
}

void idle_paint(uint64_t now) {
  // Cobalt is presenting frames: the GL path draws the overlay.
  // (signed: the renderer may have swapped after `now` was read)
  if ((int64_t)(now - g_last_swap_us) < 120000) { g_painted_base = nullptr; return; }
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || !fb.base) return;
  uint32_t gen = g_img_gen;
  int w = g_img_w, h = g_img_h;
  int x0 = (kScreenW - w) / 2, y0 = (kScreenH - h) / 2;
  bool same_place = fb.base == g_painted_base && g_last_swap_us == g_painted_after_swap &&
                    x0 == g_painted_x && y0 == g_painted_y && w == g_painted_w && h == g_painted_h;
  if (same_place && gen == g_painted_gen) return;
  if (!same_place) {
    restore_idle_paint();
    if (!g_backup) g_backup = (uint8_t*)malloc((size_t)kPanelW * kPanelH * 4);
    if (!g_backup) return;
    for (int y = 0; y < h; y++)
      memcpy(g_backup + (size_t)y * w * 4, (uint32_t*)fb.base + (size_t)(y0 + y) * fb.pitch + x0,
             (size_t)w * 4);
  }
  for (int y = 0; y < h; y++) {
    uint32_t* dst = (uint32_t*)fb.base + (size_t)(y0 + y) * fb.pitch + x0;
    const uint32_t* src = (const uint32_t*)(g_img + (size_t)y * w * 4);
    const uint32_t* under = (const uint32_t*)(g_backup + (size_t)y * w * 4);
    for (int x = 0; x < w; x++) dst[x] = (src[x] >> 24) ? src[x] : under[x];
  }
  {
    char line[128];
    snprintf(line, sizeof(line), "YTV_ADDON_IDLE_PAINT base=%p gen=%u since_swap_ms=%llu\n", fb.base,
             (unsigned)gen, (unsigned long long)((now - g_last_swap_us) / 1000));
    log_line(line);
  }
  g_painted_base = fb.base;
  g_painted_gen = gen;
  g_painted_after_swap = g_last_swap_us;
  g_painted_x = x0; g_painted_y = y0; g_painted_w = w; g_painted_h = h;
}

// ---- Menu actions ----------------------------------------------------------
void open_page(Page page) {
  g_toast_until_ms=0;
  g_page=page;
  g_sel=0;
  g_menu_open=true;
  compose_panel();
  const char* name = page == kPagePlayer ? "player" :
                     page == kPageDownload ? "download_format" :
                     page == kPageSettings ? "settings" :
                     page == kPageDisplay ? "display" : "home";
  char msg[80];
  snprintf(msg,sizeof(msg),"YTV_LIFECYCLE_ADDON menu=%s\n",name);
  log_line(msg);
}
void open_menu(bool open) {
  if (open) {
    open_page(kPageHome);
  } else if (g_menu_open) {
    g_menu_open=false;
    g_page=kPageNone;
    log_line("YTV_LIFECYCLE_ADDON menu=closed\n");
  }
}
void menu_back() {
  if (g_page == kPageHome) open_menu(false);
  else if (g_page == kPageDownload) open_page(kPagePlayer);
  else if (g_page == kPagePlayer && g_player_return == kPageNone) open_menu(false);
  else if (g_page == kPagePlayer && g_player_return == kPageSettings)
    open_page(kPageSettings);
  else open_page(kPageHome);
}
void update_language() {
  g_pt = g_language_pref == 0 ? g_system_pt : g_language_pref == 1;
}

void set_timer(int idx) {
  g_timer_idx = (idx + kTimerChoices) % kTimerChoices;
  // DIAGNOSTIC: ux0:data/youtubevita/addon_timer_seconds.txt counts the timer
  // in seconds instead of minutes (tests only).
  static int unit_s = -1;
  if (unit_s < 0) {
    SceIoStat st;
    unit_s = sceIoGetstat("ux0:data/youtubevita/addon_timer_seconds.txt", &st) >= 0 ? 1 : 60;
  }
  g_timer_deadline_us = kTimerMinutes[g_timer_idx]
                            ? now_us() + (uint64_t)kTimerMinutes[g_timer_idx] * unit_s * 1000000ull
                            : 0;
  char line[64];
  snprintf(line, sizeof(line), "YTV_LIFECYCLE_ADDON sleep_timer=%dmin\n", kTimerMinutes[g_timer_idx]);
  log_line(line);
}

void save_cfg() {
  sceIoMkdir("ux0:data/youtubevita",0777);
  SceUID fd=sceIoOpen(kAddonCfg,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
  if (fd<0) return;
  char buf[256];
  const int n=snprintf(buf,sizeof(buf),
      "lr=%d\nadblock=%d\nquality=%s\naudio_only=%d\nlanguage=%d\nfhd=%d\ndim=%d\nperf=%d\nclock=%d\nui_sound=%d\n",
      g_lr_enabled?1:0,__atomic_load_n(&g_ytv_adblock_enabled,__ATOMIC_RELAXED),
      kQualityValues[g_quality_idx],g_audio_only?1:0,g_language_pref,
      g_fhd_cfg?1:0,g_dim_idx,g_perf_hud?1:0,g_clock_hud?1:0,g_ui_sound?1:0);
  if (n>0 && n<(int)sizeof(buf)) sceIoWrite(fd,buf,n);
  sceIoClose(fd);
}
void load_cfg() {
  SceUID fd=sceIoOpen(kAddonCfg,SCE_O_RDONLY,0);
  if (fd<0) return;
  char buf[256]={};
  const int n=sceIoRead(fd,buf,sizeof(buf)-1);
  sceIoClose(fd);
  if (n<=0) return;
  if (strstr(buf,"lr=0")) g_lr_enabled=false;
  if (strstr(buf,"adblock=1"))
    __atomic_store_n(&g_ytv_adblock_enabled,1,__ATOMIC_RELAXED);
  if (strstr(buf,"quality=360")) g_quality_idx=0;
  if (strstr(buf,"quality=480")) g_quality_idx=1;
  if (strstr(buf,"quality=720")) g_quality_idx=2;
  if (strstr(buf,"audio_only=1")) g_audio_only=true;
  if (strstr(buf,"language=1")) g_language_pref=1;
  if (strstr(buf,"language=2")) g_language_pref=2;
  if (strstr(buf,"fhd=1")) g_fhd_cfg=true;
  if (const char* d=strstr(buf,"dim=")) { int v=atoi(d+4); if (v>=0 && v<kDimChoices) g_dim_idx=v; }
  if (strstr(buf,"perf=1")) g_perf_hud=true;
  if (strstr(buf,"clock=1")) g_clock_hud=true;
  if (strstr(buf,"ui_sound=1")) g_ui_sound=true;
  if (strstr(buf,"ui_sound=0")) g_ui_sound=false;
}

void set_lr(bool on) {
  g_lr_enabled = on;
  save_cfg();
  char line[48];
  snprintf(line, sizeof(line), "YTV_LIFECYCLE_ADDON lr_buttons=%d\n", (int)on);
  log_line(line);
}

void toggle_adblock() {
  const int next=!__atomic_load_n(&g_ytv_adblock_enabled,__ATOMIC_RELAXED);
  __atomic_store_n(&g_ytv_adblock_enabled,next,__ATOMIC_RELAXED);
  save_cfg();
  log_line(next?"YTV_ADBLOCK enabled (limited DNS filter)\n":"YTV_ADBLOCK disabled\n");
}
void adjust_item(int delta) {
  if (g_page==kPagePlayer && g_sel==2) {
    set_timer(g_timer_idx+delta);
  } else if (g_page==kPagePlayer && g_sel==4) {
    g_dim_idx=(g_dim_idx+kDimChoices+delta)%kDimChoices;
    save_cfg();
    char msg[64];
    snprintf(msg,sizeof(msg),"YTV_LIFECYCLE_ADDON dim_percent=%d\n",kDimLevels[g_dim_idx]);
    log_line(msg);
  } else if (g_page==kPageSettings) {
    switch(g_sel) {
      case 0: toggle_adblock(); break;
      case 1: g_quality_idx=(g_quality_idx+3+delta)%3;save_cfg();break;
      case 2: g_audio_only=!g_audio_only;save_cfg();break;
      case 3: set_lr(!g_lr_enabled);break;
      case 5: g_language_pref=(g_language_pref+3+delta)%3;
              update_language();save_cfg();break;
    }
  } else if (g_page==kPageDisplay) {
    char msg[64];
    switch(g_sel) {
      case 0: g_fhd_cfg=!g_fhd_cfg;save_cfg();
              snprintf(msg,sizeof(msg),"YTV_LIFECYCLE_ADDON fhd_next_start=%d\n",(int)g_fhd_cfg);
              log_line(msg);break;
      case 1: g_perf_hud=!g_perf_hud;save_cfg();break;
      case 2: g_clock_hud=!g_clock_hud;save_cfg();break;
      case 3: g_ui_sound=!g_ui_sound;ytv_ui_sound_enable(g_ui_sound);save_cfg();
              break;
    }
  }
  compose_panel();
}
// The format is chosen HERE, not implicitly from Settings. Confirm with X.
// Existing Settings value is updated too so URL keyboard fallback uses the
// same requested format when MediaSession has no verified video ID.
void begin_selected_download(bool mp3){
  g_audio_only=mp3;
  save_cfg();
#if defined(YTV_NATIVE_DOWNLOAD_TEST)
  // Diagnostic-only override for short reproducible Vita3K downloads.
  char url[128]={};
  SceUID fd=sceIoOpen("ux0:data/youtubevita/native_test_url.txt",SCE_O_RDONLY,0);
  if(fd>=0){
    int n=sceIoRead(fd,url,sizeof(url)-1);
    sceIoClose(fd);
    if(n>0){
      url[n]=0;
      char*e=strpbrk(url,"\r\n");
      if(e)*e=0;
      ytv_native_download_set_video(url);
    }
  }
#endif
  if(!ytv_native_download_has_video()){
    open_menu(false);
    if(!ytv_osk_open_native_video_url())
      show_toast(T("Video URL keyboard unavailable",
                   "Teclado URL indisponível"),2100,0);
    return;
  }
  const int rc=ytv_native_download_start(mp3,
                                         atoi(kQualityValues[g_quality_idx]));
  open_menu(false);
  if(rc==1)
    show_toast(mp3?T("Downloading MP3","Baixando MP3"):
                   T("Downloading MP4","Baixando MP4"),2000,0);
  else if(rc==2)
    show_toast(T("Added to download queue","Adicionado à fila"),2100,0);
  else if(rc==0)
    show_toast(T("Already downloading or queued",
                 "Já está baixando ou na fila"),2100,0);
  else if(rc==-3)
    show_toast(T("Download queue is full","Fila de downloads cheia"),2100,0);
  else
    show_toast(T("Could not start download","Não foi possível baixar"),2100,0);
}

void activate(int item) {
  if (g_page==kPageHome) {
    switch (item) {
      case 0: g_player_return=kPageHome;open_page(kPagePlayer);break;
      case 1: open_page(kPageSettings);break;
      case 2: open_page(kPageDisplay);break;
      case 3: {
        // The Vita Settings app opens on top; YouTube stays open in the
        // background and comes back from the LiveArea / PS button.
        open_menu(false);
        int r = sceAppMgrLaunchAppByUri(0xFFFFF, "settings_dlg:");
        char msg[64];
        snprintf(msg,sizeof(msg),"YTV_LIFECYCLE_ADDON vita_settings r=0x%x\n",(unsigned)r);
        log_line(msg);
        if (r < 0) show_toast(T("Could not open Vita Settings","Não deu para abrir as Configurações"),2500,0);
        break;
      }
      default: open_menu(false);break;
    }
  } else if (g_page==kPagePlayer) {
    switch(item) {
      case 0: open_page(kPageDownload);break;
      case 1: open_menu(false);g_shot_pending=true;
              g_shot_at_us=now_us()+250000;break;
      case 2: adjust_item(1);break;
      case 3: open_menu(false);
              show_toast(T("Black screen in a moment. SELECT brings it back",
                           "A tela vai escurecer. SELECT volta"),1800,3);
              g_screen_off_at_us=now_us()+1800000;break;
      case 4: adjust_item(1);break;
      default: menu_back();break;
    }
  } else if (g_page==kPageDownload) {
    switch(item){
      case 0: begin_selected_download(false);break; // Video MP4
      case 1: begin_selected_download(true);break;  // Audio MP3
      case 2: {
        const bool cancelled=ytv_native_download_cancel_current();
        open_menu(false);
        show_toast(cancelled?
                   T("Cancelling current download", "Cancelando download atual"):
                   T("No active download", "Nenhum download em andamento"),
                   2000,0);
        break;
      }
      default: menu_back();break;
    }
  } else if (g_page==kPageSettings) {
    switch(item) {
      case 0: case 1: case 2: case 3: case 5: adjust_item(1);break;
      case 4: open_menu(false);g_input_locked=true;
              log_line("YTV_LIFECYCLE_ADDON input_locked=1\n");
              show_toast(T("Input locked. SELECT unlocks",
                           "Entrada bloqueada. SELECT desbloqueia"),2500,4);break;
      default: menu_back();break;
    }
  } else if (g_page==kPageDisplay) {
    if (item>=0 && item<=3) adjust_item(1);
    else menu_back();
  }
}

// ---- On-screen info (HUD) -------------------------------------------------------
// Composed on the main thread once a second, drawn by the GL path every frame.
constexpr int kHudW = 340, kHudH = 120;
uint8_t* g_hud = nullptr;
volatile int g_hud_w = 0, g_hud_h = 0;
volatile uint32_t g_hud_gen = 0;
volatile int g_fps = 0, g_worst_ms = 0;  // written by the renderer thread
int g_dropped_per_s = 0;

void compose_hud() {
  if (!g_hud) return;
  char lines[5][64];
  int n = 0;
  if (g_clock_hud) {
    SceDateTime t;
    memset(&t, 0, sizeof(t));
    sceRtcGetCurrentClockLocalTime(&t);
    int batt = scePowerGetBatteryLifePercent();
    bool charging = scePowerIsBatteryCharging() > 0;
    if (batt >= 0 && batt <= 100)
      snprintf(lines[n++], 64, "%02d:%02d   %d%%%s", (int)t.hour, (int)t.minute, batt, charging ? " +" : "");
    else
      snprintf(lines[n++], 64, "%02d:%02d", (int)t.hour, (int)t.minute);
  }
  if (g_perf_hud) {
    SceKernelFreeMemorySizeInfo m;
    memset(&m, 0, sizeof(m));
    m.size = sizeof(m);
    sceKernelGetFreeMemorySize(&m);
    snprintf(lines[n++], 64, "%d fps   %s %d ms", g_fps, T("worst", "pior"), g_worst_ms);
    snprintf(lines[n++], 64, "RAM %d MB   VRAM %d MB", m.size_user >> 20, m.size_cdram >> 20);
    snprintf(lines[n++], 64, "GL: VRAM %d  RAM %d  PHY %d MB", (int)(g_vgl_free_vram >> 20),
             (int)(g_vgl_free_ram >> 20), (int)(g_vgl_free_phy >> 20));
    if (g_ytv_hud_video_h > 0)
      snprintf(lines[n++], 64, "%s %dp   %s %d/s", T("video", "vídeo"), g_ytv_hud_video_h,
               T("dropped", "perdidos"), g_dropped_per_s);
  }
  if (!n) { g_hud_w = 0; return; }
  int w = 0;
  for (int i = 0; i < n; i++) { int tw = text_width(lines[i]); if (tw > w) w = tw; }
  w += 24;
  if (w > kHudW) w = kHudW;
  const int h = 8 + n * 22;
  Canvas c = {g_hud, w, h};
  base_panel(c, 0x000000, 10);
  for (int i = 0; i < w * h; i++) if (g_hud[i * 4 + 3]) g_hud[i * 4 + 3] = 170;  // translucent
  for (int i = 0; i < n; i++) draw_text(c, 12, 3 + i * 22, lines[i], i == 0 && g_clock_hud ? kText : 0xdedede);
  g_hud_w = w;
  g_hud_h = h;
  __sync_add_and_fetch(&g_hud_gen, 1);
}

}  // namespace

// Set by the player's audio thread whenever it outputs audio.
volatile uint64_t g_ytv_audio_active_us = 0;

extern "C" {

// Main thread, every input poll, before buttons reach Cobalt. Returns true
// when the add-on layer consumed this poll (Cobalt then sees no change).
bool ytv_addons_input(uint32_t previous, uint32_t current) {
  const uint32_t pressed=current & ~previous;
  if (g_input_locked) {
    if (pressed&SCE_CTRL_SELECT) {
      g_input_locked=false;
      log_line("YTV_LIFECYCLE_ADDON input_locked=0\n");
      show_toast(T("Input unlocked","Entrada liberada"),1500,4);
    }
    return pressed!=0;
  }
  if (g_screen_off) {
    if (pressed&SCE_CTRL_SELECT) set_screen_off(false);
    return pressed!=0;
  }
  // SELECT always enters the native Vita Home / Settings hub.
  if (pressed&SCE_CTRL_SELECT) {
    ytv_ui_sound(g_menu_open ? 3 : 6);
    open_menu(!g_menu_open);
    return true;
  }
  // Triangle opens a lightweight context menu ONLY while an SbPlayer exists.
  // It remains available even when playback is paused, and falls through to
  // YouTube on non-player screens (no global Triangle hijack).
  if ((pressed&SCE_CTRL_TRIANGLE) && ytv_native_player_present()) {
    ytv_ui_sound(g_menu_open && (g_page==kPagePlayer || g_page==kPageDownload) ? 3 : 6);
    if (g_menu_open && (g_page==kPagePlayer || g_page==kPageDownload)) {
      menu_back();
    } else {
      g_player_return=g_menu_open?g_page:kPageNone;
      open_page(kPagePlayer);
    }
    return true;
  }
  if (!g_menu_open) return false;
  if (pressed&SCE_CTRL_UP) {g_sel=(g_sel+page_count()-1)%page_count();ytv_ui_sound(1);}
  if (pressed&SCE_CTRL_DOWN) {g_sel=(g_sel+1)%page_count();ytv_ui_sound(1);}
  if (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
    const int delta=(pressed&SCE_CTRL_LEFT)?-1:1;
    if ((g_page==kPagePlayer && (g_sel==2 || g_sel==4)) ||
        (g_page==kPageSettings && (g_sel==0 || g_sel==1 ||
                                  g_sel==2 || g_sel==3 || g_sel==5)) ||
        (g_page==kPageDisplay && g_sel<=3))
      {adjust_item(delta);ytv_ui_sound(1);}
  }
  if (pressed&SCE_CTRL_CIRCLE) { ytv_ui_sound(3);menu_back();return true; }
  if (pressed&SCE_CTRL_CROSS) { ytv_ui_sound(2);activate(g_sel);return true; }
  if (pressed) compose_panel();
  return true;
}

int ytv_addons_cfg_download_quality() {return atoi(kQualityValues[g_quality_idx]);}
bool ytv_addons_cfg_download_mp3() {return g_audio_only;}

bool ytv_addons_menu_open() { return g_menu_open || g_screen_off || g_input_locked; }
// Menu or toast on screen: Cobalt is asked to keep presenting frames
// (graphics extension in port_system.cpp), so the overlay is always drawn by
// the GL path on top of a fresh frame. The CPU idle path painted pages over
// copies that could already contain the previous page (stacked menus).
bool ytv_addons_wants_frames() { return overlay_visible(); }

void ytv_addons_toggle() {
  if (g_input_locked || g_screen_off) return;
  open_menu(!g_menu_open);
}

// Buttons YouTube must not see (L/R disabled in the menu).
uint32_t ytv_addons_hidden_buttons() {
  return g_lr_enabled ? 0u : (uint32_t)(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
}

// A tap (window pixels) while the menu is open: pick a row or close.
void ytv_addons_tap(float x, float y) {
  if (g_input_locked || g_screen_off || !g_menu_open) return;
  const int px=(int)x-(kScreenW-kPanelW)/2;
  const int py=(int)y-(kScreenH-panel_height())/2;
  if (px<0 || py<0 || px>=kPanelW || py>=panel_height()) {
    ytv_ui_sound(3);
    menu_back();
    return;
  }
  for (int i=0;i<page_count();i++) {
    const int ry=kRowY0+i*kRowH;
    if (py>=ry && py<ry+kRowH) {
      g_sel=i;
      ytv_ui_sound(2);
      if (g_page==kPagePlayer && i==2 && px<kPanelW/3)
        adjust_item(-1);
      else if (g_page==kPageSettings &&
               (i==0 || i==1 || i==2 || i==3 || i==5) &&
               px<kPanelW/3)
        adjust_item(-1);
      else if (g_page==kPagePlayer && i==4 && px<kPanelW/3)
        adjust_item(-1);
      else activate(i);
      return;
    }
  }
}

// Saved Full HD choice, read straight from addons.cfg: the graphics and video
// start-up need it before the menu initialises.
//
// Crash guard: a run with Full HD leaves ux0:data/youtubevita/fhd_probe until
// it proves healthy (a 1080p decoder produced pictures, or two minutes passed
// without one being created). If the probe is still there at the next start,
// that run died with Full HD on: this start turns Full HD off (and saves it),
// so a crash can never repeat at every launch.
constexpr const char* kFhdProbe = "ux0:data/youtubevita/fhd_probe";
int g_fhd_state = -1;          // cached: -1 unknown, 0 off, 1 on
bool g_fhd_auto_off = false;   // disabled by the guard this run (toast)
int ytv_addons_cfg_fhd() {
  if (g_fhd_state >= 0) return g_fhd_state;
  g_fhd_state = 0;
  SceUID fd = sceIoOpen(kAddonCfg, SCE_O_RDONLY, 0);
  if (fd < 0) return 0;
  char buf[256] = {0};
  sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (!strstr(buf, "fhd=1")) {
    sceIoRemove(kFhdProbe);
    return 0;
  }
  SceIoStat st;
  if (sceIoGetstat(kFhdProbe, &st) >= 0) {
    g_fhd_auto_off = true;
    sceIoRemove(kFhdProbe);
    // Rewrite the setting off (keep the rest of the file as it is).
    char* at = strstr(buf, "fhd=1");
    at[4] = '0';
    SceUID w = sceIoOpen(kAddonCfg, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (w >= 0) { sceIoWrite(w, buf, strlen(buf)); sceIoClose(w); }
    SbLogRaw("YTV_LIFECYCLE_ADDON fhd=auto_off (previous Full HD run did not finish)\n");
    return 0;
  }
  SceUID p = sceIoOpen(kFhdProbe, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  if (p >= 0) sceIoClose(p);
  g_fhd_state = 1;
  return 1;
}
// Full HD proved healthy this run (called by the player or the tick).
void ytv_addons_fhd_ok() {
  static bool done = false;
  if (done || g_fhd_state != 1) return;
  done = true;
  sceIoRemove(kFhdProbe);
  SbLogRaw("YTV_LIFECYCLE_ADDON fhd=healthy\n");
}

// Main thread, every loop iteration.
void ytv_addons_tick() {
  static bool inited = false;
  if (!inited) {
    inited = true;
    int lang = 0;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) >= 0)
      g_system_pt = lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR || lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT;
    g_img = (uint8_t*)malloc((size_t)kPanelW * kPanelH * 4);
    g_hud = (uint8_t*)malloc((size_t)kHudW * kHudH * 4);
    load_cfg();
    update_language();
    if (g_fhd_auto_off) g_fhd_cfg = false;  // the guard turned it off this start
    g_fhd_boot = g_fhd_cfg;
    ytv_ui_sound_enable(g_ui_sound);
    if (g_fhd_auto_off)
      show_toast(T("Full HD failed last time: turned off", "Full HD falhou da última vez: desligado"),
                 4000, 0);
  }
  if (!g_img) return;
  uint64_t now = now_us();
  // Sleep timer.
  if (g_timer_deadline_us && now >= g_timer_deadline_us) {
    g_timer_deadline_us = 0;
    g_timer_idx = 0;
    log_line("YTV_LIFECYCLE_ADDON sleep_timer=expired pause\n");
    send_key(kSbKeyPause);
    if (g_menu_open) compose_panel();
    else show_toast(T("Sleep timer: video paused", "Timer: vídeo pausado"), 3000, 2);
  }
  if (g_screen_off_at_us && now >= g_screen_off_at_us) {
    g_screen_off_at_us = 0;
    set_screen_off(true);
  }
  if (g_menu_open) {
    static uint64_t last_refresh = 0;  // clock, battery, timer minutes
    if (now - last_refresh > 1000000ull) { last_refresh = now; compose_panel(); }
  }
  {
    static uint64_t last_hud = 0;
    static unsigned last_dropped = 0;
    if (now - last_hud >= 1000000ull) {
      unsigned d = g_ytv_hud_dropped_total;
      g_dropped_per_s = last_hud ? (int)(d - last_dropped) : 0;
      last_dropped = d;
      last_hud = now;
      if (g_perf_hud || g_clock_hud) compose_hud();
      else g_hud_w = 0;
    }
  }
  // Full HD guard: two minutes without a 1080p decoder also count as healthy.
  {
    static uint64_t start = 0;
    if (!start) start = now;
    extern volatile bool g_ytv_fhd_decoder_created;  // port_player.cpp
    if (g_fhd_state == 1 && !g_ytv_fhd_decoder_created && now - start > 120000000ull)
      ytv_addons_fhd_ok();
  }
  // Keep the system awake while audio plays: with the screen off only block
  // auto-suspend; otherwise also block dimming.
  static uint64_t last_tick = 0;
  bool playing = g_ytv_audio_active_us && (int64_t)(now - g_ytv_audio_active_us) < 3000000;
  if (playing && now - last_tick > 5000000ull) {
    last_tick = now;
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);  // no dimming, no auto-lock
  }
  // Screenshot after the menu is gone from the screen.
  if (g_shot_pending && now >= g_shot_at_us && !g_painted_base) {
    g_shot_pending = false;
    save_screenshot();
  }
  if (g_screen_off) black_idle(now);
  else if (overlay_visible() && !g_ytv_graphics_ext_active) idle_paint(now);
  else restore_idle_paint();
  // With the Graphics extension available, the Cobalt GPU presents a fresh
  // frame at <=33 ms. Writing directly to its display buffer concurrently
  // is unsafe and was a second source of stale/stacked menu artifacts.
}

}  // extern "C"

// ---- Renderer thread: GL overlay ------------------------------------------
namespace {
GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { glDeleteShader(s); return 0; }
  return s;
}

bool init_gl() {
  static const char* vs =
      "attribute vec2 a_pos;\n"
      "attribute vec2 a_uv;\n"
      "varying vec2 v_uv;\n"
      "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  static const char* fs =
      "precision mediump float;\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_tex;\n"
      "uniform float u_alpha;\n"
      "void main() { vec4 c = texture2D(u_tex, v_uv); gl_FragColor = vec4(c.rgb, c.a * u_alpha); }\n";
  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  if (!v || !f) return false;
  g_prog = glCreateProgram();
  glAttachShader(g_prog, v);
  glAttachShader(g_prog, f);
  glBindAttribLocation(g_prog, 0, "a_pos");
  glBindAttribLocation(g_prog, 1, "a_uv");
  glLinkProgram(g_prog);
  GLint ok = 0;
  glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
  if (!ok) return false;
  g_loc_pos = glGetAttribLocation(g_prog, "a_pos");
  g_loc_uv = glGetAttribLocation(g_prog, "a_uv");
  g_loc_tex = glGetUniformLocation(g_prog, "u_tex");
  g_loc_alpha = glGetUniformLocation(g_prog, "u_alpha");
  glGenTextures(1, &g_tex);
  glGenTextures(1, &g_tex_hud);
  glGenTextures(1, &g_tex_black);
  static const uint8_t black[16] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
  glBindTexture(GL_TEXTURE_2D, g_tex_black);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, g_tex_hud);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kHudW, kHudH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return g_tex != 0 && g_loc_pos >= 0 && g_loc_uv >= 0;
}

struct AttribState {
  GLint enabled, size, type, normalized, stride, buffer;
  void* pointer;
};
void save_attrib(GLuint i, AttribState& a) {
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
  a.pointer = nullptr;
  glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
}
void restore_attrib(GLuint i, const AttribState& a) {
  if (a.size > 0 && (a.buffer || a.pointer)) {
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)a.buffer);
    glVertexAttribPointer(i, a.size, (GLenum)a.type, (GLboolean)a.normalized, a.stride, a.pointer);
  }
  if (a.enabled) glEnableVertexAttribArray(i);
  else glDisableVertexAttribArray(i);
}
}  // namespace

// One textured quad in screen pixels (top-left origin); us/vs = texture extent.
void draw_quad(GLuint tex, int x, int y, int w, int h, float us, float vs, float alpha) {
  static float verts[16];
  const float x0 = (float)x / kScreenW * 2.f - 1.f, x1 = (float)(x + w) / kScreenW * 2.f - 1.f;
  const float y1 = 1.f - (float)y / kScreenH * 2.f, y0 = 1.f - (float)(y + h) / kScreenH * 2.f;
  const float v[16] = {x0, y0, 0.f, vs, x1, y0, us, vs, x0, y1, 0.f, 0.f, x1, y1, us, 0.f};
  memcpy(verts, v, sizeof(v));
  glBindTexture(GL_TEXTURE_2D, tex);
  if (g_loc_alpha >= 0) glUniform1f(g_loc_alpha, alpha);
  glVertexAttribPointer((GLuint)g_loc_pos, 2, GL_FLOAT, GL_FALSE, 16, verts);
  glVertexAttribPointer((GLuint)g_loc_uv, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

extern "C" void ytv_addons_before_swap() {
  const uint64_t swap_now = now_us();
  {  // frame rate for the HUD
    static uint64_t window_start = 0, prev = 0;
    static int frames = 0, worst = 0;
    if (prev) { int ms = (int)((swap_now - prev) / 1000); if (ms > worst && ms < 1000) worst = ms; }
    prev = swap_now;
    frames++;
    if (!window_start) window_start = swap_now;
    if (swap_now - window_start >= 1000000ull) {
      g_fps = frames; g_worst_ms = worst;
      frames = 0; worst = 0; window_start = swap_now;
    }
  }
  g_last_swap_us = swap_now;
  const int dim = kDimLevels[g_dim_idx];
  const bool hud = (g_perf_hud || g_clock_hud) && g_hud_w > 0;
  const bool panel = overlay_visible();
  const bool black = g_screen_off;
  if (g_gl_failed || !g_img || (!panel && !dim && !hud && !black)) return;
  // Save the state Cobalt/Skia relies on BEFORE init_gl(): initialising the
  // overlay creates textures and changes GL_TEXTURE_BINDING_2D. Capturing state
  // only afterwards restored the overlay texture as Cobalt's texture, causing
  // corrupted thumbnails/textures after visiting Add-ons.
  GLint prog, active, tex0, abuf, fbo, vp[4], sc[4], bsrc_rgb, bdst_rgb, bsrc_a, bdst_a, beq_rgb, beq_a;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_SCISSOR_BOX, sc);
  glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &bdst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrc_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &bdst_a);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &beq_rgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &beq_a);
  GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST),
            depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST),
            cull = glIsEnabled(GL_CULL_FACE);
  GLboolean color_mask[4] = {GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
  glGetBooleanv(GL_COLOR_WRITEMASK,color_mask);
  AttribState a0, a1;
  save_attrib(0, a0);
  save_attrib(1, a1);

  if (!g_prog && !init_gl()) {
    g_gl_failed = true;
    log_line("YTV_LIFECYCLE_ADDON gl_overlay=failed (idle path only)\n");
    goto restore_overlay_state;
  }
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, kScreenW, kScreenH);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
  glUseProgram(g_prog);
  if (g_loc_tex >= 0) glUniform1i(g_loc_tex, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray((GLuint)g_loc_pos);
  glEnableVertexAttribArray((GLuint)g_loc_uv);
  // 1. Internal darkening: black over the whole frame (not the backlight).
  //    "Black screen" mode: fully black, nothing else drawn on top.
  if (black) draw_quad(g_tex_black, 0, 0, kScreenW, kScreenH, 1.f, 1.f, 1.f);
  else if (dim) draw_quad(g_tex_black, 0, 0, kScreenW, kScreenH, 1.f, 1.f, dim / 100.f);
  // 2. HUD, top-right corner.
  if (hud && !black) {
    const int w = g_hud_w, h = g_hud_h;
    uint32_t gen = g_hud_gen;
    if (gen != g_hud_tex_gen) {
      g_hud_tex_gen = gen;
      glBindTexture(GL_TEXTURE_2D, g_tex_hud);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_hud);
    }
    draw_quad(g_tex_hud, kScreenW - w - 10, 10, w, h, (float)w / kHudW, (float)h / kHudH, 1.f);
  }
  // 3. Menu / toast, centred.
  if (panel) {
    const int w = g_img_w, h = g_img_h;
    glBindTexture(GL_TEXTURE_2D, g_tex);
    uint32_t gen = g_img_gen;
    if (gen != g_tex_gen) {
      g_tex_gen = gen;
      static bool allocated = false;
      if (!allocated) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kPanelW, kPanelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        allocated = true;
      }
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_img);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    draw_quad(g_tex, (kScreenW - w) / 2, (kScreenH - h) / 2, w, h, (float)w / kPanelW,
              (float)h / kPanelH, 1.f);
  }

  // Restore every state touched by the overlay, including the very first
  // frame when shaders and textures are created.
restore_overlay_state:
  restore_attrib(0, a0);
  restore_attrib(1, a1);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
  glUseProgram((GLuint)prog);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex0);
  glActiveTexture((GLenum)active);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glScissor(sc[0], sc[1], sc[2], sc[3]);
  glBlendEquationSeparate((GLenum)beq_rgb, (GLenum)beq_a);
  glBlendFuncSeparate((GLenum)bsrc_rgb, (GLenum)bdst_rgb, (GLenum)bsrc_a, (GLenum)bdst_a);
  glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
  if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
  if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
  if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}
