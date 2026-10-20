# Mario Kart Wii for the PS Vita.
#
#   cmake -G Ninja -B build/vita -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/vita
#
# Needs VITASDK (with vitaGL from vdpm) and the sources tools/bootstrap.py
# restores into vendor/ (Kinoko patched, citro3d, libctru headers).
cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to the Vita SDK folder")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(mkw_vita C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(MKW_VARIANT "retail" CACHE STRING "Game edition: retail, or rr (Retro Rewind, a separate app)")
if(MKW_VARIANT STREQUAL "rr")
  set(VITA_APP_NAME "MKW Retro Rewind")
  set(VITA_TITLEID "MKWR00001")
  set(MKW_DATA_ROOT "ux0:data/mkwrr")
  set(MKW_SCE_SYS ${CMAKE_SOURCE_DIR}/platform/vita-rr/sce_sys)
else()
  set(VITA_APP_NAME "Mario Kart Wii")
  set(VITA_TITLEID "MKWV00001")
  set(MKW_DATA_ROOT "ux0:data/mkw")
  set(MKW_SCE_SYS ${CMAKE_SOURCE_DIR}/platform/vita/sce_sys)
endif()
set(VITA_VERSION "01.00")
option(MKW_RELEASE "No test hooks or runtime log" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN ${CMAKE_BINARY_DIR}/gen)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vita/gen_headers.py ${GEN}
  RESULT_VARIABLE gen_result)
if(gen_result)
  message(FATAL_ERROR "tools/vita/gen_headers.py failed")
endif()

set(KINOKO ${CMAKE_SOURCE_DIR}/vendor/kinoko)
set(CITRO3D ${CMAKE_SOURCE_DIR}/vendor/citro3d)
set(LIBCTRU ${CMAKE_SOURCE_DIR}/vendor/libctru/libctru)

# Same floating-point contract as the 3DS build: IEEE single precision, no
# contraction, no fast-math, so Kinoko replays match the Wii bit for bit.
set(MKW_CODEGEN -mcpu=cortex-a9 -mfpu=neon -O2 -g -ffunction-sections -fdata-sections
  -fno-strict-aliasing -ffp-contract=off
  # Some state (Kinoko's Vector3f, members without initializers) relied on
  # memory the 3DS happened to hand over zeroed; zero every automatic.
  -ftrivial-auto-var-init=zero)
set(MKW_DEFINES __3DS__ __vita__ REVOLUTION "MKW_DATA_ROOT=\"${MKW_DATA_ROOT}\"")
if(MKW_VARIANT STREQUAL "rr")
  list(APPEND MKW_DEFINES MKW_RR)
endif()
if(MKW_RELEASE)
  list(APPEND MKW_DEFINES MKW_RELEASE KINOKO_QUIET)
else()
  # Development builds run with any compatible game-file set in ux0:data/mkw
  # (no content-stamp check) and keep the QA hooks and runtime.log.
  list(APPEND MKW_DEFINES MKW_EXTERNAL_ASSETS)
endif()
set(MKW_INCLUDES
  ${CMAKE_SOURCE_DIR}/platform/vita/include # <3ds.h> stand-in; before libctru's own
  ${LIBCTRU}/include
  ${CITRO3D}/include
  ${CMAKE_SOURCE_DIR}/include
  ${KINOKO}/include
  ${KINOKO}/source
  ${GEN})
if(MKW_VARIANT STREQUAL "rr")
  # The Retro Rewind cook writes its own course, lighting, menu and skin headers
  # here (tools/course_catalog.py generated()); include/mkw keeps the retail ones.
  set(MKW_RR_INCLUDE ${CMAKE_SOURCE_DIR}/build/rr-include)
  if(NOT EXISTS ${MKW_RR_INCLUDE}/mkw/course_catalog.hpp)
    message(FATAL_ERROR "No Retro Rewind headers in build/rr-include: cook first (tools/vita/build.py --rr ...)")
  endif()
  list(PREPEND MKW_INCLUDES ${MKW_RR_INCLUDE})
endif()

file(GLOB_RECURSE kinoko_src ${KINOKO}/source/*.cc)
list(FILTER kinoko_src EXCLUDE REGEX "/(main|KReplaySystem|Context)\\.cc$")
add_library(kinoko STATIC ${kinoko_src})
target_compile_options(kinoko PRIVATE ${MKW_CODEGEN} -std=gnu++23 -fno-exceptions -fno-rtti -fcheck-new -Wno-psabi)
target_compile_definitions(kinoko PRIVATE ${MKW_DEFINES})
target_include_directories(kinoko PRIVATE ${MKW_INCLUDES})

# citro3d's portable parts: matrix maths and the .t3x loader, plus libctru's
# LZ decompressor that loader uses. The GPU side is platform/vita/src/c3d_*.
file(GLOB c3d_maths ${CITRO3D}/source/maths/*.c)
add_library(ctrport STATIC ${c3d_maths} ${CITRO3D}/source/tex3ds.c ${LIBCTRU}/source/util/decompress/decompress.c)
target_compile_options(ctrport PRIVATE ${MKW_CODEGEN} -std=gnu11 -Wno-unused-function)
target_compile_definitions(ctrport PRIVATE ${MKW_DEFINES})
target_include_directories(ctrport PRIVATE ${MKW_INCLUDES})

file(GLOB game_src ${CMAKE_SOURCE_DIR}/src/*.cpp)
# 3DS only: serialized the LINEAR heap through linker --wrap.
list(FILTER game_src EXCLUDE REGEX "/linear_lock\\.cpp$")
file(GLOB platform_src ${CMAKE_SOURCE_DIR}/platform/vita/src/*.cpp ${CMAKE_SOURCE_DIR}/platform/vita/src/*.c)
add_executable(mkw ${game_src} ${platform_src})
target_compile_options(mkw PRIVATE ${MKW_CODEGEN} -Wall -Wno-psabi
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23 -fno-exceptions -fno-rtti -fcheck-new>)
target_compile_definitions(mkw PRIVATE ${MKW_DEFINES})
target_include_directories(mkw PRIVATE ${MKW_INCLUDES})
# __sce_headroom (VitaSDK's armelf.x): at least 64 KiB between the code and data
# segments. DATA_SEGMENT_ALIGN otherwise starts .data on the 64 KiB boundary
# right after the code whenever the code size lands just so, and vita-elf-create
# then crashes (heap corruption, 0xc0000374) while converting to mkw.velf.
target_link_options(mkw PRIVATE -Wl,--gc-sections -Wl,-q -Wl,--wrap=fopen -Wl,--defsym=__sce_headroom=0x10000)
target_link_libraries(mkw PRIVATE
  kinoko ctrport
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub
  ScePower_stub SceAudio_stub SceAppMgr_stub SceAppUtil_stub SceCommonDialog_stub
  SceSysmodule_stub SceIme_stub SceRtc_stub SceLibKernel_stub SceKernelThreadMgr_stub
  SceRazorCapture_stub_weak SceRazorHud_stub_weak
  m stdc++ pthread)

vita_create_self(eboot.bin mkw UNSAFE)
# LiveArea images must be 8-bit indexed PNGs.
vita_create_vpk(mkw.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${MKW_SCE_SYS} sce_sys)
