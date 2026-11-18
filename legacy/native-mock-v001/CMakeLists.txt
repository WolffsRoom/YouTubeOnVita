cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita SDK toolchain")
  else()
    message(FATAL_ERROR "VITASDK environment variable required")
  endif()
endif()

project(youtubevita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

option(YTV_AUTOTEST "Run deterministic in-app emulator checks and exit" OFF)
set(VITA_APP_NAME "YouTube Vita Preview")
set(VITA_TITLEID "YTVP00001")
if(YTV_AUTOTEST)
  set(VITA_APP_NAME "YT Vita Self-Test")
  set(VITA_TITLEID "YTVT00001")
endif()

set(VITA_VERSION "00.01")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -std=gnu11")
add_executable(youtubevita src/main.c)
target_link_libraries(youtubevita
  vita2d SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub
  SceIofilemgr_stub SceLibKernel_stub SceAppMgr_stub SceCommonDialog_stub
  ScePgf_stub freetype png jpeg z bz2 m)
if(YTV_AUTOTEST)
  target_compile_definitions(youtubevita PRIVATE YTV_AUTOTEST=1)
endif()

vita_create_self(youtubevita.self youtubevita UNSAFE)
vita_create_vpk(youtubevita.vpk ${VITA_TITLEID} youtubevita.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE assets/font.ttf font.ttf
  FILE README.md README.md
  FILE LICENSES.md LICENSES.md)


