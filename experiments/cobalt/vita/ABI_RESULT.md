# Starboard / VitaSDK ABI test result

Date: 2026-11-16
Source: youtube/cobalt 25.lts.1+ commit da2dfd8
Spec: starboard/sabi/arm/hardfp/sabi-v16.json
Probe source: experiments/cobalt/vita/abi_probe.cpp

Compiler: arm-vita-eabi-g++ 15.2.0
Flags passing probe: -std=c++17 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
Output: abi_probe.o, valid ARM ELF object only; NOT a running Cobalt application.

Verified readelf -A:
CPU arch v7, VFPv3, NEONv1, 4-byte wchar_t, hardfloat VFP arguments, enum size int.

Important incompatibility discovered:
Default VitaSDK GCC compiles simple enums with sizeof(enum)=1 (readelf reports "small").
Cobalt hardfp SABI v16 requires sizeof(enum)=4. The default ABI probe FAILED with
"static assertion failed ... 1 == 4". Adding -fno-short-enums PASSED and readelf
reports "Tag_ABI_enum_size: int".

Risk: not all VitaSDK/homebrew libraries use 4-byte enums. Public C/C++ structures
that cross ABI boundaries need inspection or wrapper translation.

Still untested:
Starboard target creation, GN toolchain, Cobalt runtime building/running, V8 JIT,
Skia/GXM, application delivery/service authentication and real video playback.

