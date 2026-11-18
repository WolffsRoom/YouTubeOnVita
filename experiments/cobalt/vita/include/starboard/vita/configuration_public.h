// Platform configuration: PS Vita Cortex-A9, Starboard API 15.
#ifndef YTV_STARBOARD_VITA_CONFIGURATION_PUBLIC_H_
#define YTV_STARBOARD_VITA_CONFIGURATION_PUBLIC_H_
#define SB_IS_ARCH_ARM 1
#define SB_IS_32_BIT 1
#define SB_IS_LITTLE_ENDIAN 1
#define SB_SIZE_OF_POINTER 4
#define SB_SIZE_OF_LONG 4
#define SB_HAS_SYS_TYPES_H 1
#define SB_HAS_SSIZE_T 1
#define SB_IS_WCHAR_T_UTF32 1
#define SB_C_FORCE_INLINE inline __attribute__((always_inline))
#define SB_C_INLINE inline
#define SB_EXPORT_PLATFORM __attribute__((visibility("default")))
#define SB_IMPORT_PLATFORM
// Until Vita memory executable pages / V8 JIT are actually validated, do not promise JIT.
#define SB_CAN_MAP_EXECUTABLE_MEMORY 0
#define SB_HAS_IPV6 0
#endif

