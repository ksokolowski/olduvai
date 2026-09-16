# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Measurement-instrument toolchain: zig c++ (clang/libc++) -> one glibc Linux
# target, chosen by wrapper script.  NOT the shipping path — the port ships
# from the KNULLI SDK (GCC + libstdc++) via cmake/toolchains/aarch64-knulli.cmake.
# This exists to build a binary for a measured change on one compiler without
# sourcing a second SDK:
#   aarch64 (TrimUI Smart Pro):  aarch64-zig.cmake, its shim below
#   armhf (Powkiddy A12 / RK3128): -DOLDUVAI_ZIG_TRIPLE=arm-linux-gnueabihf.2.17
# with wrapper scripts named for the prefix (aarch64-linux-*, arm-linux-gnueabihf-*).
#
# CMAKE_SYSROOT is deliberately NOT set: zig supplies its own glibc headers,
# and pointing --sysroot at the minimal SDL2-only tree would hide stdio.h.  Only
# CMAKE_FIND_ROOT_PATH is set, so find_package(SDL2) resolves to the
# cross-built SDL2 and never to the host's sdl2-compat (on this Mac the host
# SDL2 is sdl2-compat over SDL3, whose headers are wrong for the device).
# Requires paths, all outside the tree:
#   -DOLDUVAI_ZIG_XTOOL=<dir with ${OLDUVAI_ZIG_PREFIX}-g++/gcc/ar/ranlib wrappers>
#   -DOLDUVAI_ZIG_SYSROOT=<prefix holding a cross-built SDL2 (usr/lib, usr/include)>
# The wrappers are two lines each, e.g.
#   #!/bin/sh
#   exec zig c++ -target arm-linux-gnueabihf.2.17 -Wno-nullability-completeness "$@"
# (the glibc number after the triple is a FLOOR — the device's own glibc is
# newer; -Wno-nullability-completeness silences zig's libc++ headers, which
# otherwise emit thousands of warnings unrelated to this project.)
#
# THE try_compile PROBLEM — the same one aarch64-knulli.cmake documents, and it
# bites identically here.  CMake re-includes this file in try_compile
# sub-projects with a FRESH cache, so the outer -D values do not reach them;
# check_ipo_supported's LTO probe does not even honour
# CMAKE_TRY_COMPILE_PLATFORM_VARIABLES.  Mirroring -D into the environment is
# only half of it: the sub-project must also read it back OUT, or the guard
# below fires inside the probe and reports "CMAKE_CXX_COMPILER not set, after
# EnableLanguage" — blaming CMake for an argument that WAS supplied.  Verified
# by hitting it.
foreach(v OLDUVAI_ZIG_XTOOL OLDUVAI_ZIG_SYSROOT OLDUVAI_ZIG_TRIPLE OLDUVAI_ZIG_PREFIX OLDUVAI_ZIG_PROCESSOR)
    if(DEFINED ${v} AND NOT "${${v}}" STREQUAL "")
        set(ENV{${v}} "${${v}}")
    elseif(DEFINED ENV{${v}})
        set(${v} "$ENV{${v}}")
    endif()
    if(NOT DEFINED ${v} OR "${${v}}" STREQUAL "")
        message(FATAL_ERROR "Set -D${v}=... — see docs/internal/SPIKE_HANDHELD.md")
    endif()
endforeach()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     OLDUVAI_ZIG_XTOOL OLDUVAI_ZIG_SYSROOT OLDUVAI_ZIG_TRIPLE
     OLDUVAI_ZIG_PREFIX OLDUVAI_ZIG_PROCESSOR)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${OLDUVAI_ZIG_PROCESSOR})
set(CMAKE_C_COMPILER   ${OLDUVAI_ZIG_XTOOL}/${OLDUVAI_ZIG_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${OLDUVAI_ZIG_XTOOL}/${OLDUVAI_ZIG_PREFIX}-g++)
set(CMAKE_FIND_ROOT_PATH ${OLDUVAI_ZIG_SYSROOT}/usr)
set(CMAKE_PREFIX_PATH    ${OLDUVAI_ZIG_SYSROOT}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Apple's ar/ranlib silently produce EMPTY archives from ELF objects:
# 129 aarch64 objects compiled clean, both .a files came out 96 bytes, and the
# only symptom was a wall of undefined symbols at the final link.  zig's LLVM
# ar understands ELF.
set(CMAKE_AR     ${OLDUVAI_ZIG_XTOOL}/${OLDUVAI_ZIG_PREFIX}-ar     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ${OLDUVAI_ZIG_XTOOL}/${OLDUVAI_ZIG_PREFIX}-ranlib CACHE FILEPATH "" FORCE)