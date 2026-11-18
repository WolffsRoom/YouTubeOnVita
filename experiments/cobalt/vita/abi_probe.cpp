// ABI-only smoke test; not Cobalt runtime.
#include <cstdint>
#include <climits>
#include <psp2/types.h>
static_assert(sizeof(void*)==4, "32-bit pointers required");
static_assert(sizeof(int)==4 && sizeof(long)==4 && sizeof(long long)==8, "invalid integer layout");
static_assert(sizeof(double)==8 && alignof(double)==8, "invalid doubles");
static_assert(sizeof(wchar_t)==4, "need UTF-32 wchar_t");
static_assert(CHAR_MIN<0, "expected signed char");
enum VitaProbeEnum { kZero, kOne };
static_assert(sizeof(VitaProbeEnum)==4, "Cobalt ARM SABI requires 4-byte enums (use -fno-short-enums)");
#if !defined(__arm__) || !defined(__ARM_ARCH_7A__)
#error Need ARMv7-A target
#endif
#if !defined(__ARM_PCS_VFP)
#error Need hardfloat ABI
#endif
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Need little endian
#endif
extern "C" int ytv_starboard_abi_smoketest() { return sizeof(void*)*10 + sizeof(double); }


