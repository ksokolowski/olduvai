# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Measurement-instrument toolchain: zig c++ (clang/libc++) -> aarch64-linux-gnu.
# Thin shim — all logic lives in the shared zig-linux.cmake.
#
# WHY -static-libstdc++ is NOT set here: the KNULLI SDK path carries its own
# concern about GLIBCXX ceilings, but the zig path uses zig's own libc++ and
# links every external dependency as C, so no C++ ABI ceiling applies.
#
# This file exists alongside the shipping path (aarch64-knulli.cmake, GCC + SDK)
# to build measurement binaries — timer-fix pairs and the armhf leg cross-build
# — on one compiler from the Mac without a container.
#
# aarch64 CACHED glibc floor (2026-09-09, device: TrimUI Smart Pro, KNULLI Scarab):
#   target        aarch64-linux-gnu.2.17   (kernel floor from zig triple: linux5.10)
#   max GLIBC_     2.17                    (device provides 2.40)
#   DT_NEEDED     libSDL2-2.0.so.0, libm, libc, libpthread, libdl
#   binary        1.6 MB stripped  (-Wl,-s; zig objcopy --strip-all returns unimplemented)
#
# THREE TRAPS (2026-09-09, each cost time, none self-announces):
#   1. Apple's ar silently produces EMPTY archives from ELF objects — use zig ar.
#   2. zig objcopy --strip-all returns unimplemented (0.16) — use -Wl,-s.
#   3. CMAKE_SYSROOT must NOT be set to the SDL2-only sysroot — zig supplies
#      its own glibc headers; --sysroot hides stdio.h.
#
# Requires two paths, both outside the tree (mirrored into environment for
# try_compile survival — see zig-linux.cmake header for the full story):
#   -DOLDUVAI_ZIG_XTOOL=<dir with aarch64-linux-g++/gcc/ar/ranlib wrappers>
#   -DOLDUVAI_ZIG_SYSROOT=<prefix holding a cross-built SDL2 (usr/lib, usr/include)>

set(OLDUVAI_ZIG_TRIPLE    "aarch64-linux-gnu.2.17")
set(OLDUVAI_ZIG_PREFIX    "aarch64-linux")
set(OLDUVAI_ZIG_PROCESSOR "aarch64")
include("${CMAKE_CURRENT_LIST_DIR}/zig-linux.cmake")