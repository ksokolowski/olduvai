# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Cross-compile for the TrimUI Smart Pro running KNULLI (Allwinner A133, aarch64).
#
# WHY -static-libstdc++, even though it turned out not to be needed.  The device
# ships libstdc++ 6.0.32, whose highest symbol set is GLIBCXX_3.4.32 (GCC 13).
# A newer compiler will happily emit references past that, which resolve on the
# build host and fail on the device with a loader error naming a symbol rather
# than a cause.  KNULLI's own SDK turned out to carry GCC 12.3, which is OLDER
# than the device's runtime and so cannot trip that ceiling — but this flag stays
# anyway.  It costs 1-2 MB and removes a variable from a first port, and it keeps
# the file correct if someone later points OLDUVAI_KNULLI_CXX at a distro GCC.
#
# OLDUVAI_KNULLI_SYSROOT is for the fallback path only (a distro cross toolchain
# linking against a rootfs extracted from a device image).  KNULLI's SDK carries
# its own sysroot and needs none, which is the configuration this was resolved to
# on 2026-09-07 — see docs/internal/specs/2026-09-07-trimui-port-design.md.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# THE try_compile PROBLEM, and why this uses the environment.
#
# CMake re-includes this file in try_compile sub-projects, and those get a fresh
# cache: -D values from the outer configure do NOT reach them.  Two separate
# mechanisms hit it here — compiler-ABI detection (which honours
# CMAKE_TRY_COMPILE_PLATFORM_VARIABLES) and check_ipo_supported's LTO probe
# (which does not).  The sub-project also has no CMAKE_CXX_COMPILER yet, so
# testing for one does not help either.  The symptom is
# "CMAKE_CXX_COMPILER not set, after EnableLanguage", which blames CMake for an
# argument the user did supply.
#
# Environment variables DO reach the child cmake, so accept -D for ergonomics
# and mirror it into the environment, which is what actually survives.
if(DEFINED OLDUVAI_KNULLI_CXX AND NOT OLDUVAI_KNULLI_CXX STREQUAL "")
    set(ENV{OLDUVAI_KNULLI_CXX} "${OLDUVAI_KNULLI_CXX}")
elseif(DEFINED ENV{OLDUVAI_KNULLI_CXX})
    set(OLDUVAI_KNULLI_CXX "$ENV{OLDUVAI_KNULLI_CXX}")
endif()
if(DEFINED OLDUVAI_KNULLI_SYSROOT AND NOT OLDUVAI_KNULLI_SYSROOT STREQUAL "")
    set(ENV{OLDUVAI_KNULLI_SYSROOT} "${OLDUVAI_KNULLI_SYSROOT}")
elseif(DEFINED ENV{OLDUVAI_KNULLI_SYSROOT})
    set(OLDUVAI_KNULLI_SYSROOT "$ENV{OLDUVAI_KNULLI_SYSROOT}")
endif()

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     OLDUVAI_KNULLI_CXX OLDUVAI_KNULLI_SYSROOT)

# A toolchain file that silently falls back to the host compiler is worse than
# one that fails: it produces an x86-64 binary that looks fine until the device
# refuses to load it.  Fail loudly, and say where to look.
if(NOT DEFINED OLDUVAI_KNULLI_CXX OR OLDUVAI_KNULLI_CXX STREQUAL "")
    message(FATAL_ERROR
        "Set -DOLDUVAI_KNULLI_CXX=<path to the aarch64 g++>.  See Task 1 of "
        "docs/internal/plans/2026-09-07-trimui-port.md for how to obtain one.")
endif()

set(CMAKE_CXX_COMPILER "${OLDUVAI_KNULLI_CXX}")
string(REGEX REPLACE "g\\+\\+$" "gcc" _olduvai_cc "${OLDUVAI_KNULLI_CXX}")
set(CMAKE_C_COMPILER "${_olduvai_cc}")

if(DEFINED OLDUVAI_KNULLI_SYSROOT AND NOT OLDUVAI_KNULLI_SYSROOT STREQUAL "")
    set(CMAKE_SYSROOT "${OLDUVAI_KNULLI_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${OLDUVAI_KNULLI_SYSROOT}")
endif()

# Programs come from the host (cmake, git); libraries and headers must come from
# the target sysroot only, or a host libSDL2 would satisfy find_package and the
# link would fail confusingly late.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_link_options(-static-libstdc++ -static-libgcc)
