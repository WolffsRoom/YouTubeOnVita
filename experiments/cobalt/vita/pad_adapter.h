#ifndef YTV_STARBOARD_VITA_PAD_ADAPTER_H_
#define YTV_STARBOARD_VITA_PAD_ADAPTER_H_
#include <stdint.h>
#include "starboard/input.h"
int YtvTranslateVitaButtons(uint32_t previous, uint32_t current, SbWindow window,
                            SbInputData* events, int max_events);
#endif

