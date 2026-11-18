// Real Cobalt 25 LTS Starboard API header smoke test, not a mock API.
#include "starboard/configuration.h"
#include "starboard/time.h"
#include "starboard/log.h"
#include "starboard/memory.h"
#include "starboard/system.h"
static_assert(SB_API_VERSION == 15, "Starboard 15 expected");
static_assert(sizeof(SbTime) == 8, "time is 64-bit");
static_assert(sizeof(SbLogPriority) == 4, "enums must be 4-byte in Cobalt SABI");
static_assert(sizeof(SbMemoryMapFlags) == 4, "enums must be 4-byte in Cobalt SABI");
extern "C" int ytv_header_test() {
  return sizeof(SbLogPriority) + sizeof(SbSystemPathId) + sizeof(SbMemoryMapFlags);
}

