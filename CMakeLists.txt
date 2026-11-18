cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "VITASDK required")
  endif()
endif()
project(YouTubeVita_OneToOne LANGUAGES C CXX)
# VitaGL built without the boot splash (same commit as the VitaSDK image).
option(YTV_VITAGL_NOSPLASH "Link VitaGL built with NO_SPLASHSCREEN=1" ON)
if(YTV_VITAGL_NOSPLASH)
  link_directories(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vitagl-nosplash/lib")
endif()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
option(YTV_BUILD_STARBOARD15_PROBE "Build minimal real Starboard 15 Vita API test" ON)
if(YTV_BUILD_STARBOARD15_PROBE)
  set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt")
  set(SB "${ROOT}/upstream-25")
  set(VITA "${ROOT}/vita")
  set(VITA_APP_NAME "Starboard Vita API Test")
  set(VITA_TITLEID "YTVSB0001")
  set(VITA_VERSION "00.02")
  add_library(starboard_vita15 STATIC "${VITA}/platform15.cpp" "${VITA}/port_window.cpp" "${VITA}/pad_adapter.cpp" "${VITA}/import_lookup.cpp" "${VITA}/port_files.cpp" "${VITA}/port_sync.cpp" "${VITA}/port_network.cpp" "${VITA}/port_socket_waiter.cpp" "${VITA}/port_runtime.cpp" "${VITA}/port_threads.cpp" "${VITA}/port_graphics.cpp" "${VITA}/port_virtual_memory.cpp" "${VITA}/port_storage.cpp" "${VITA}/port_system.cpp" "${VITA}/port_events.cpp" "${VITA}/port_constants.cpp" "${VITA}/port_optional_unsupported.cpp" "${VITA}/port_media_policy.cpp" "${VITA}/port_misc_utility.cpp" "${VITA}/port_audio_sink.cpp" "${VITA}/port_lifecycle.cpp" "${VITA}/port_player.cpp" "${VITA}/port_input.cpp" "${VITA}/port_microphone.cpp")
  add_executable(ytv_sb15_probe "${VITA}/main_test.cpp")
  option(YTV_GL_TRACE "Diagnostic: log GL calls made through the Starboard GLES table" OFF)
  if(YTV_GL_TRACE)
    target_compile_definitions(starboard_vita15 PRIVATE YTV_GL_TRACE=1)
  endif()
  # Hardware video/audio decoders (SbPlayer) and microphone input.
  target_link_libraries(starboard_vita15 PUBLIC SceVideodec_stub SceAudiodec_stub SceAudioIn_stub)
  foreach(tgt starboard_vita15 ytv_sb15_probe)
    target_include_directories(${tgt} PRIVATE "${VITA}/include" "${SB}")
    target_compile_options(${tgt} PRIVATE -O2 -Wall -Wextra -Wno-expansion-to-defined -std=gnu++17
      -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
      "-include" "${VITA}/include/port_config.h")
  endforeach()
  target_link_libraries(ytv_sb15_probe PRIVATE starboard_vita15
      SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub
      SceNet_stub SceNetCtl_stub SceAudio_stub SceSysmodule_stub SceAppUtil_stub SceSysmem_stub kubridge_stub
      vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub SceDisplay_stub SceGxm_stub
      SceKernelDmacMgr_stub ScePower_stub SceAppMgr_stub SceCtrl_stub SceTouch_stub
      SceCommonDialog_stub mathneon z pthread m)
  vita_create_self(ytv_sb15_probe.self ytv_sb15_probe UNSAFE)
  vita_create_vpk(StarboardVitaTest.vpk ${VITA_TITLEID} ytv_sb15_probe.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE experiments/cobalt/vita/README.md README.md
    FILE experiments/cobalt/vita/assets/player_test_854x480.h264 player_test_854x480.h264)
endif()


# Optional readonly inspection of the official Google Cobalt 25 Evergreen ELF.
# This is not a loader and does not run libcobalt.so.
option(YTV_BUILD_EVERGREEN_INSPECTOR "Build standalone ELF inspection Vita3K VPK" ON)
if(YTV_BUILD_EVERGREEN_INSPECTOR)
  set(COBALT_CORE "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/evergreen/core25/lib/libcobalt.so")
  if(NOT EXISTS "${COBALT_CORE}")
    message(FATAL_ERROR "Official Cobalt ELF missing: ${COBALT_CORE}")
  endif()
  set(VITA_APP_NAME "Cobalt ELF Inspector")
  set(VITA_TITLEID "YTVEL0001")
  set(VITA_VERSION "00.03")
  add_executable(ytv_elf_inspector experiments/cobalt/vita/evergreen_inspect.c)
  target_compile_options(ytv_elf_inspector PRIVATE -O2 -Wall -Wextra -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
  target_link_libraries(ytv_elf_inspector PRIVATE SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)
  vita_create_self(ytv_elf_inspector.self ytv_elf_inspector UNSAFE)
  vita_create_vpk(CobaltEvergreenInspect.vpk ${VITA_TITLEID} ytv_elf_inspector.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${COBALT_CORE} libcobalt.so
    FILE experiments/cobalt/evergreen/core25/manifest.json cobalt-manifest.json)
endif()

# Stage 3: evaluate ARM relocation machinery in non-executable Vita RW memory.
option(YTV_BUILD_EVERGREEN_RELOCATOR "Build official ELF RAM-only relocation validation" ON)
if(YTV_BUILD_EVERGREEN_RELOCATOR)
  set(COBALT_CORE "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/evergreen/core25/lib/libcobalt.so")
  if(NOT EXISTS "${COBALT_CORE}")
    message(FATAL_ERROR "Official Cobalt ELF missing: ${COBALT_CORE}")
  endif()
  set(VITA_APP_NAME "Cobalt Relocation Test")
  set(VITA_TITLEID "YTVRL0001")
  set(VITA_VERSION "00.04")
  add_executable(ytv_relocator experiments/cobalt/vita/relocate_elf.c)
  target_compile_options(ytv_relocator PRIVATE -O2 -Wall -Wextra -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums)
  target_link_libraries(ytv_relocator PRIVATE starboard_vita15 kubridge_stub
     vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
     SceDisplay_stub SceGxm_stub SceAudio_stub SceKernelDmacMgr_stub
     ScePower_stub SceAppMgr_stub SceCtrl_stub SceTouch_stub
     SceCommonDialog_stub SceIofilemgr_stub SceLibKernel_stub
     SceSysmem_stub SceNet_stub SceSysmodule_stub SceAppUtil_stub
     SceKernelThreadMgr_stub mathneon pthread z m)
  vita_create_self(ytv_relocator.self ytv_relocator UNSAFE)
  vita_create_vpk(CobaltEvergreenRelocations.vpk ${VITA_TITLEID} ytv_relocator.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${COBALT_CORE} libcobalt.so
    FILE experiments/cobalt/evergreen/core25/manifest.json cobalt-manifest.json)
endif()
# Stage 4: kubridge RX executable memory smoke test, no Cobalt code invoked.
option(YTV_BUILD_EXEC_MEMORY_PROBE "Probe RX W^X permissions via kubridge in Vita3K" ON)
if(YTV_BUILD_EXEC_MEMORY_PROBE)
  set(VITA_APP_NAME "Vita RX Memory Probe")
  set(VITA_TITLEID "YTVMX0001")
  set(VITA_VERSION "00.05")
  add_executable(ytv_exec_mem_probe experiments/cobalt/vita/executable_memory.c)
  target_compile_options(ytv_exec_mem_probe PRIVATE -O2 -Wall -Wextra -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
  target_link_libraries(ytv_exec_mem_probe PRIVATE kubridge_stub SceSysmem_stub SceIofilemgr_stub SceLibKernel_stub)
  vita_create_self(ytv_exec_mem_probe.self ytv_exec_mem_probe UNSAFE)
  vita_create_vpk(CobaltExecutableMemoryProbe.vpk ${VITA_TITLEID} ytv_exec_mem_probe.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 5: real SceNet init/socket test; intentionally no network request.
option(YTV_BUILD_OFFLINE_NET_PROBE "Build Vita native networking offline probe" ON)
if(YTV_BUILD_OFFLINE_NET_PROBE)
  set(VITA_APP_NAME "Starboard Vita Network Test")
  set(VITA_TITLEID "YTVNT0001")
  set(VITA_VERSION "00.06")
  add_executable(ytv_net_probe experiments/cobalt/vita/network_probe.c)
  target_compile_options(ytv_net_probe PRIVATE -O2 -Wall -Wextra -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
  target_link_libraries(ytv_net_probe PRIVATE SceNet_stub SceSysmodule_stub
   SceIofilemgr_stub SceLibKernel_stub)
  vita_create_self(ytv_net_probe.self ytv_net_probe UNSAFE)
  vita_create_vpk(StarboardVitaNetworkTest.vpk ${VITA_TITLEID} ytv_net_probe.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 6: real nonblocking SceNet / Starboard socket integration test.
option(YTV_BUILD_SOCKET_INTEGRATION_TEST "Build Starboard sockets end-to-end proof" ON)
if(YTV_BUILD_SOCKET_INTEGRATION_TEST)
  set(VITA_APP_NAME "Starboard Vita Socket Test")
  set(VITA_TITLEID "YTVSK0001")
  set(VITA_VERSION "00.07")
  add_executable(ytv_socket_test experiments/cobalt/vita/socket_test.cpp)
  target_include_directories(ytv_socket_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/upstream-25")
  target_compile_options(ytv_socket_test PRIVATE -O2 -Wall -Wextra -std=gnu++17
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include/port_config.h")
  target_link_libraries(ytv_socket_test PRIVATE starboard_vita15 SceNet_stub SceSysmodule_stub
    SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_socket_test.self ytv_socket_test UNSAFE)
  vita_create_vpk(StarboardVitaSocketTest.vpk ${VITA_TITLEID} ytv_socket_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 7: actual TCP stream loopback proof.
option(YTV_BUILD_TCP_INTEGRATION_TEST "Build PS Vita Starboard TCP proof" ON)
if(YTV_BUILD_TCP_INTEGRATION_TEST)
  set(VITA_APP_NAME "Starboard Vita TCP Test")
  set(VITA_TITLEID "YTVTC0001")
  set(VITA_VERSION "00.08")
  add_executable(ytv_tcp_test experiments/cobalt/vita/socket_tcp_test.cpp)
  target_include_directories(ytv_tcp_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/upstream-25")
  target_compile_options(ytv_tcp_test PRIVATE -O2 -Wall -Wextra -Wno-expansion-to-defined -std=gnu++17
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include/port_config.h")
  target_link_libraries(ytv_tcp_test PRIVATE starboard_vita15 SceNet_stub SceSysmodule_stub
    SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_tcp_test.self ytv_tcp_test UNSAFE)
  vita_create_vpk(StarboardVitaTcpTest.vpk ${VITA_TITLEID} ytv_tcp_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
option(YTV_BUILD_EXTERNAL_DNS_TEST "Build Vita3K external DNS test" ON)
if(YTV_BUILD_EXTERNAL_DNS_TEST)
  set(VITA_APP_NAME "Starboard Vita DNS Test")
  set(VITA_TITLEID "YTVDN0001")
  set(VITA_VERSION "00.09")
  add_executable(ytv_dns_test experiments/cobalt/vita/socket_dns_test.cpp)
  target_include_directories(ytv_dns_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/upstream-25")
  target_compile_options(ytv_dns_test PRIVATE -O2 -Wall -Wextra -Wno-expansion-to-defined -std=gnu++17
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include/port_config.h")
  target_link_libraries(ytv_dns_test PRIVATE starboard_vita15 SceNet_stub SceSysmodule_stub
    SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_dns_test.self ytv_dns_test UNSAFE)
  vita_create_vpk(StarboardVitaDnsTest.vpk ${VITA_TITLEID} ytv_dns_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
option(YTV_BUILD_EPOLL_TEST "Build native Vita epoll socket event test" ON)
if(YTV_BUILD_EPOLL_TEST)
  set(VITA_APP_NAME "Starboard Vita Epoll Test")
  set(VITA_TITLEID "YTVWT0001")
  set(VITA_VERSION "00.10")
  add_executable(ytv_epoll_test experiments/cobalt/vita/socket_waiter_test.cpp)
  target_include_directories(ytv_epoll_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/upstream-25")
  target_compile_options(ytv_epoll_test PRIVATE -O2 -Wall -Wextra -Wno-expansion-to-defined -std=gnu++17
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include/port_config.h")
  target_link_libraries(ytv_epoll_test PRIVATE starboard_vita15 SceNet_stub SceSysmodule_stub
    SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_epoll_test.self ytv_epoll_test UNSAFE)
  vita_create_vpk(StarboardVitaEpollTest.vpk ${VITA_TITLEID} ytv_epoll_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 8: genuine hardware-accelerated vitaGL EGL/GXM frame probe.
option(YTV_BUILD_GRAPHICS_PROBE "Build Vita GXM EGL hardware smoke test" ON)
if(YTV_BUILD_GRAPHICS_PROBE)
  set(VITA_APP_NAME "YouTube Vita Graphics Test")
  set(VITA_TITLEID "YTVGX0001")
  set(VITA_VERSION "00.11")
  # Links the same Starboard platform library the Cobalt bootstrap uses, so
  # the EGL/GLES bridge under test is exactly the one Cobalt calls.
  add_executable(ytv_graphics_test experiments/cobalt/vita/graphics_test.cpp)
  target_include_directories(ytv_graphics_test PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/upstream-25")
  target_compile_options(ytv_graphics_test PRIVATE -O2 -Wall -Wextra -std=gnu++17
       -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums
       "-include" "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/include/port_config.h")
  target_link_libraries(ytv_graphics_test PRIVATE starboard_vita15 kubridge_stub
    SceNet_stub SceNetCtl_stub SceAppUtil_stub
    vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
    SceSysmodule_stub SceDisplay_stub SceGxm_stub SceAudio_stub
    SceKernelDmacMgr_stub ScePower_stub SceSysmem_stub SceAppMgr_stub
    SceCtrl_stub SceTouch_stub SceCommonDialog_stub SceIofilemgr_stub
    SceKernelThreadMgr_stub SceLibKernel_stub mathneon pthread z m)
  vita_create_self(ytv_graphics_test.self ytv_graphics_test UNSAFE)
  vita_create_vpk(YouTubeVitaGraphicsTest.vpk ${VITA_TITLEID} ytv_graphics_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 9: public HTTPS/TLS endpoint probe; credentials are never used.
option(YTV_BUILD_HTTPS_PROBE "Build public SceHttp HTTPS capability probe" ON)
if(YTV_BUILD_HTTPS_PROBE)
  set(VITA_APP_NAME "YouTube Vita HTTPS Probe")
  set(VITA_TITLEID "YTVHT0001")
  set(VITA_VERSION "00.12")
  add_executable(ytv_https_probe experiments/cobalt/vita/https_probe.cpp)
  target_compile_options(ytv_https_probe PRIVATE -O2 -Wall -Wextra -std=gnu++17
      -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard)
  target_link_libraries(ytv_https_probe PRIVATE SceNet_stub SceHttp_stub SceSsl_stub
      SceSysmodule_stub SceIofilemgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_https_probe.self ytv_https_probe UNSAFE)
  vita_create_vpk(YouTubeVitaHttpsProbe.vpk ${VITA_TITLEID} ytv_https_probe.self
     VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
     FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 12: genuine HTTPS certificate-validation test via VitaSDK libcurl.
option(YTV_BUILD_TLS_TEST "Build real external HTTPS/TLS diagnostic" ON)
if(YTV_BUILD_TLS_TEST)
  set(VITA_APP_NAME "YouTube Vita TLS Test")
  set(VITA_TITLEID "YTVTL0001")
  set(VITA_VERSION "00.12")
  add_executable(ytv_tls_test experiments/cobalt/vita/tls_test.c)
  target_compile_options(ytv_tls_test PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ytv_tls_test PRIVATE curl ssl crypto zstd z
    SceNet_stub SceSysmodule_stub SceIofilemgr_stub
    SceKernelThreadMgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_tls_test.self ytv_tls_test UNSAFE)
  vita_create_vpk(YouTubeVitaTLSTest.vpk ${VITA_TITLEID} ytv_tls_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE experiments/cobalt/vita/ca-bundle.pem cacert.pem)
endif()
# Stage 13: real SceVideodec H.264 hardware decoder memory query.
option(YTV_BUILD_H264_PROBE "Build native Vita H264 decoder query" ON)
if(YTV_BUILD_H264_PROBE)
  set(VITA_APP_NAME "YouTube Vita H264 Test")
  set(VITA_TITLEID "YTVVD0001")
  set(VITA_VERSION "00.13")
  add_executable(ytv_h264_probe experiments/cobalt/vita/video_decoder_probe.c)
  target_compile_options(ytv_h264_probe PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ytv_h264_probe PRIVATE
      SceVideodec_stub SceSysmodule_stub SceIofilemgr_stub SceLibKernel_stub)
  vita_create_self(ytv_h264_probe.self ytv_h264_probe UNSAFE)
  vita_create_vpk(YouTubeVitaH264Probe.vpk ${VITA_TITLEID} ytv_h264_probe.self
     VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
     FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png)
endif()
# Stage 14: H.264 decoder creation using physical contiguous Vita buffer.
option(YTV_BUILD_H264_FRAME_TEST "Build native H264 decoder creation and synthetic frame test" ON)
if(YTV_BUILD_H264_FRAME_TEST)
  set(VITA_APP_NAME "YouTube Vita H264 Frame Test")
  set(VITA_TITLEID "YTVDF0001")
  set(VITA_VERSION "00.14")
  add_executable(ytv_h264_frame_test experiments/cobalt/vita/decode_frame_probe.c)
  target_compile_options(ytv_h264_frame_test PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ytv_h264_frame_test PRIVATE
    SceVideodec_stub SceSysmodule_stub SceSysmem_stub
    SceIofilemgr_stub SceLibKernel_stub)
  vita_create_self(ytv_h264_frame_test.self ytv_h264_frame_test UNSAFE)
  vita_create_vpk(YouTubeVitaH264FrameTest.vpk ${VITA_TITLEID} ytv_h264_frame_test.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE experiments/cobalt/vita/assets/h264_test.h264 h264_test.h264)
endif()
# Stage 15: diagnostic software FFmpeg H.264 decoder for independent pixel verification.
option(YTV_BUILD_SW_H264_PROBE "Build Vita libavcodec software H264 pixel validation" OFF)
if(YTV_BUILD_SW_H264_PROBE)
  set(VITA_APP_NAME "YouTube Vita SW H264 Test")
  set(VITA_TITLEID "YTVSW0001")
  set(VITA_VERSION "00.15")
  add_executable(ytv_software_decoder experiments/cobalt/vita/software_decoder_probe.c)
  target_compile_options(ytv_software_decoder PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ytv_software_decoder PRIVATE
    avcodec avutil swscale swresample mp3lame z bz2 lzma
    SceIofilemgr_stub SceKernelThreadMgr_stub SceLibKernel_stub pthread m)
  vita_create_self(ytv_software_decoder.self ytv_software_decoder UNSAFE)
  vita_create_vpk(YouTubeVitaSoftwareH264Test.vpk ${VITA_TITLEID} ytv_software_decoder.self
     VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
     FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
     FILE experiments/cobalt/vita/assets/h264_test.h264 h264_test.h264)
endif()
# Stage 16: native SceAvPlayer MP4 decoder with original synthetic video.
option(YTV_BUILD_AVPLAYER_PROBE "Build Vita native synthetic H264 MP4 playback probe" ON)
if(YTV_BUILD_AVPLAYER_PROBE)
  set(VITA_APP_NAME "YouTube Vita AV Player Test")
  set(VITA_TITLEID "YTVAP0001")
  set(VITA_VERSION "00.16")
  add_executable(ytv_avplayer_probe experiments/cobalt/vita/avplayer_probe.c)
  target_compile_options(ytv_avplayer_probe PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ytv_avplayer_probe PRIVATE
     SceAvPlayer_stub SceGxm_stub SceSysmodule_stub SceSysmem_stub
     SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub m)
  vita_create_self(ytv_avplayer_probe.self ytv_avplayer_probe UNSAFE)
  vita_create_vpk(YouTubeVitaAVPlayerTest.vpk ${VITA_TITLEID} ytv_avplayer_probe.self
      VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
      FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
      FILE experiments/cobalt/vita/assets/vita_avplayer_test.mp4 vita_avplayer_test.mp4)
endif()

# Stage 17: isolated Cobalt initializer execution, NOT a playable app.
option(YTV_BUILD_COBALT_BOOT_PROBE "Build isolated native Cobalt init probe" ON)
if(YTV_BUILD_COBALT_BOOT_PROBE)
  set(VITA_APP_NAME "YouTube")
  set(VITA_TITLEID "YTVCB0001")
  set(VITA_VERSION "00.28")
  add_executable(ytv_cobalt_boot experiments/cobalt/vita/relocate_elf.c)
  target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=2 YTV_COBALT_DIAG_EVENT_CUTOFF=1)
  target_compile_options(ytv_cobalt_boot PRIVATE -O2 -Wall -Wextra
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums)
  target_link_libraries(ytv_cobalt_boot PRIVATE starboard_vita15 kubridge_stub
    vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
    SceDisplay_stub SceGxm_stub SceAudio_stub SceKernelDmacMgr_stub
    ScePower_stub SceAppMgr_stub SceCtrl_stub SceTouch_stub
    SceCommonDialog_stub SceIofilemgr_stub SceLibKernel_stub
    SceSysmem_stub SceNet_stub SceNetCtl_stub SceSysmodule_stub SceAppUtil_stub
    SceKernelThreadMgr_stub mathneon pthread z m)
  vita_create_self(ytv_cobalt_boot.self ytv_cobalt_boot UNSAFE)
  # Package only content actually extracted from the matching official CRX.
  set(COBALT_EXTRACTED_CONTENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/evergreen/core25/content")
  file(GLOB_RECURSE COBALT_BOOT_ASSETS CONFIGURE_DEPENDS
       "${COBALT_EXTRACTED_CONTENT_DIR}/*")
  set(COBALT_BOOT_ASSET_FILES "")
  foreach(COBALT_BOOT_ASSET IN LISTS COBALT_BOOT_ASSETS)
    file(RELATIVE_PATH COBALT_BOOT_ASSET_DEST
      "${COBALT_EXTRACTED_CONTENT_DIR}" "${COBALT_BOOT_ASSET}")
    list(APPEND COBALT_BOOT_ASSET_FILES
      FILE "${COBALT_BOOT_ASSET}" "${COBALT_BOOT_ASSET_DEST}")
  endforeach()
  # Platform system fonts: Cobalt's official 'limited' font package, staged by
  # tools/stage_vita_system_fonts.py (Evergreen's own fonts.xml is empty).
  set(VITA_SYSTEM_FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/vita/system_fonts")
  file(GLOB VITA_SYSTEM_FONT_FILES CONFIGURE_DEPENDS "${VITA_SYSTEM_FONTS_DIR}/*")
  # Pre-compiled VitaGL shader cache (GXP from SceShaccCg), seeded on first run.
  file(GLOB VITA_SHADER_SEED CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shader_cache_seed/*")
  foreach(SHADER_FILE IN LISTS VITA_SHADER_SEED)
    get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)
    list(APPEND COBALT_BOOT_ASSET_FILES FILE "${SHADER_FILE}" "shader_cache/${SHADER_NAME}")
  endforeach()
  foreach(FONT_FILE IN LISTS VITA_SYSTEM_FONT_FILES)
    get_filename_component(FONT_NAME "${FONT_FILE}" NAME)
    list(APPEND COBALT_BOOT_ASSET_FILES FILE "${FONT_FILE}" "system_fonts/${FONT_NAME}")
  endforeach()
  # Extended memory mode (+~109 MiB user RAM; param.sfo ATTRIBUTE2=12),
  # scoped to this package only.
  # vita_create_vpk is a macro: VITA_PACK_VPK_FLAGS / resources from the
  # earlier test VPKs in this file leaked into this package (their legacy
  # icon0.png came first and won over the LiveArea icon). Start clean.
  set(VITA_PACK_VPK_FLAGS "")
  set(resources "")
  set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
  vita_create_vpk(CobaltNativeBootstrapTest.vpk ${VITA_TITLEID} ytv_cobalt_boot.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE assets/livearea/icon0.png sce_sys/icon0.png
    FILE assets/livearea/template.xml sce_sys/livearea/contents/template.xml
    FILE assets/livearea/labg.png sce_sys/livearea/contents/bg0.png
    FILE assets/livearea/gate.png sce_sys/livearea/contents/startup.png
    FILE ${COBALT_CORE} libcobalt.so
    ${COBALT_BOOT_ASSET_FILES})
  unset(VITA_MKSFOEX_FLAGS)
endif()
