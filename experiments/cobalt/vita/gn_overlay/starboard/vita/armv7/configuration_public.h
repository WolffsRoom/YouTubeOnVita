#ifndef STARBOARD_VITA_ARMV7_CONFIGURATION_PUBLIC_H_
#define STARBOARD_VITA_ARMV7_CONFIGURATION_PUBLIC_H_
#define SB_HAS_SYS_TYPES_H 1
#define SB_HAS_SSIZE_T 1
#define SB_IS_WCHAR_T_UTF32 1
#define SB_C_FORCE_INLINE inline __attribute__((always_inline))
#define SB_C_INLINE inline
#define SB_EXPORT_PLATFORM __attribute__((visibility("default")))
#define SB_IMPORT_PLATFORM
#define SB_CAN_MAP_EXECUTABLE_MEMORY 0
#define SB_HAS_IPV6 0
#endif

