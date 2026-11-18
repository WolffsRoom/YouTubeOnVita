// Starboard 15 Vita window lifecycle. No EGL/GXM renderer is attached yet.
#include "starboard/window.h"
#include <string.h>
struct SbWindowPrivate {
  unsigned int magic;
  SbWindowSize size;
};
static SbWindowPrivate g_window = {};

extern "C" {
void SbWindowSetDefaultOptions(SbWindowOptions* options) {
  if (!options) return;
  memset(options, 0, sizeof(*options));
  options->size.width = 960;
  options->size.height = 544;
  options->size.video_pixel_ratio = 1.f;
  options->windowed = false;
}
SbWindow SbWindowCreate(const SbWindowOptions* options) {
  if (g_window.magic) return kSbWindowInvalid;
  if (options && options->windowed) return kSbWindowInvalid;
  g_window.magic = 0x57564954U;
  g_window.size.width = 960;
  g_window.size.height = 544;
  g_window.size.video_pixel_ratio = 1.f;
  return &g_window;
}
bool SbWindowGetSize(SbWindow window,SbWindowSize* size) {
  if (window != &g_window || !g_window.magic || !size) return false;
  *size = g_window.size;
  return true;
}
float SbWindowGetDiagonalSizeInInches(SbWindow window) {
  if (window != &g_window || !g_window.magic) return 0.f;
  return 5.f;
}
void* SbWindowGetPlatformHandle(SbWindow window) {
  // VitaGL's window surface adapter accepts an opaque native window
  // token. Use the actual Starboard window object, not a synthetic HWND.
  // The Vita EGL bridge treats it only as an identity token.
  return window==&g_window && g_window.magic?static_cast<void*>(window):nullptr;
}
bool SbWindowDestroy(SbWindow window) {
  if (window != &g_window || !g_window.magic) return false;
  g_window.magic = 0;
  return true;
}
}

