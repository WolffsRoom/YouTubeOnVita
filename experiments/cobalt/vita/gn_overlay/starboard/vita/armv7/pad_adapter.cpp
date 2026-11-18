#include "pad_adapter.h"
#include <psp2/ctrl.h>
#include <cstring>

namespace {
struct Mapping {
  uint32_t vita_button;
  SbKey key;
};
constexpr Mapping kButtons[] = {
  {SCE_CTRL_UP, kSbKeyGamepadDPadUp},
  {SCE_CTRL_DOWN, kSbKeyGamepadDPadDown},
  {SCE_CTRL_LEFT, kSbKeyGamepadDPadLeft},
  {SCE_CTRL_RIGHT, kSbKeyGamepadDPadRight},
  {SCE_CTRL_CROSS, kSbKeyGamepad1},
  {SCE_CTRL_CIRCLE, kSbKeyGamepad2},
  {SCE_CTRL_SQUARE, kSbKeyGamepad3},
  {SCE_CTRL_TRIANGLE, kSbKeyGamepad4},
  {SCE_CTRL_LTRIGGER, kSbKeyGamepadLeftBumper},
  {SCE_CTRL_RTRIGGER, kSbKeyGamepadRightBumper},
  {SCE_CTRL_SELECT, kSbKeyGamepad5},
  {SCE_CTRL_START, kSbKeyGamepad6},
};
}
int YtvTranslateVitaButtons(uint32_t previous, uint32_t current, SbWindow window,
                            SbInputData* events, int max_events) {
  if (max_events < 0 || (max_events > 0 && !events)) return -1;
  int count=0;
  for (const auto& map: kButtons) {
    bool was=(previous&map.vita_button)!=0;
    bool now=(current&map.vita_button)!=0;
    if (was==now) continue;
    if (count>=max_events) return -1; // no truncated event batches
    SbInputData e = {};
    e.window=window;
    e.type=now?kSbInputEventTypePress:kSbInputEventTypeUnpress;
    e.device_type=kSbInputDeviceTypeGamepad;
    e.device_id=0;
    e.key=map.key;
    events[count++]=e;
  }
  return count;
}

