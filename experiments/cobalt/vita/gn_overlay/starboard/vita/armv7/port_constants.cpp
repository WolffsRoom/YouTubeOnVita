/*
 * Starboard 15 platform configuration values for real PlayStation Vita.
 * These are policy/ABI values of this port, not Nintendo Switch identifiers.
 * The Cobalt ELF references addresses of these exported data objects.
 */
#include "starboard/configuration_constants.h"
#include "starboard/configuration.h"

const char kSbFileAltSepChar='\0';
const char* kSbFileAltSepString="";
const int32_t kSbFileMaxName=255;       // SceIoDirent.d_name is 256 incl NUL
const uint32_t kSbFileMaxPath=1023;     // SceIo path is capped to 1024 incl NUL
const char kSbFileSepChar='/';
const char* kSbFileSepString="/";
const uint32_t kSbMaxSystemPathCacheDirectorySize=16u*1024u*1024u;
const int32_t kSbMaxThreadNameLength=32; // matches Vita pthread name storage
const uint32_t kSbMaxThreads=128;        // conservative port policy
const size_t kSbMemoryPageSize=4096;     // kernel and kubridge page alignment
const uint32_t kSbNetworkReceiveBufferSize=128u*1024u;
const int kSbPreferredRgbaByteOrder=SB_PREFERRED_RGBA_BYTE_ORDER_RGBA;

// Vita mount prefixes use ':' (ux0:, app0:), so never use ':' to split
// search-path lists; otherwise legitimate paths would be corrupted.
extern "C" const char kSbPathSepChar=';';

