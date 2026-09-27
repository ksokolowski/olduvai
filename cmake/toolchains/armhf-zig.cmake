# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Measurement-instrument toolchain: zig c++ (clang/libc++) -> arm-linux-gnueabihf
# (ARMv7-A, Cortex-A7).  Thin shim — all logic lives in zig-linux.cmake.
#
# armhf glibc floor (2026-09-12, verified on macOS before device measurement):
#   target        arm-linux-gnueabihf.2.17
#   max GLIBC_     2.17
#   binfmt        ELF 32-bit LSB executable, ARM, EABI5 version 1
#   interpreter   /lib/ld-linux-armhf.so.3
#
# Use case: Powkiddy A12 rev A (RK3128, 4x Cortex-A7 @1.2 GHz, 256 MB,
# 1024x600 panel) running KNULLI Scarab rk3128 (Batocera-derived, glibc ≥2.27).
# The device provides SDL2 2.32.8 at runtime; this cross-links against a stubbed
# 2.28.5 built with all backends off, same pattern as the aarch64 leg.
#
# The armhf leg extends the same "one build story, two sysroots" that the aarch64
# zig leg demonstrated for the TrimUI Smart Pro; packaging/build_knulli_release.sh
# builds the A12 release bundle through it.
#
# CPU: cortex_a7.  The target's baseline is ARMv7-A + NEON WITHOUT hardware
# divide, so every a / b by a variable is an __aeabi_idiv call; the A7 has
# sdiv, and the code is scheduled for its in-order core.  The binary then
# needs an A7 or later: a PortMaster armhf build must choose its own floor.
#
# Requires two paths, both outside the tree:
#   -DOLDUVAI_ZIG_XTOOL=<dir with arm-linux-gnueabihf-g++/gcc/ar/ranlib wrappers>
#   -DOLDUVAI_ZIG_SYSROOT=<prefix holding a cross-built SDL2 (usr/lib, usr/include)>

set(OLDUVAI_ZIG_TRIPLE    "arm-linux-gnueabihf.2.17")
set(OLDUVAI_ZIG_PREFIX    "arm-linux-gnueabihf")
set(OLDUVAI_ZIG_PROCESSOR "arm")
set(OLDUVAI_ZIG_CPU       "cortex_a7")
include("${CMAKE_CURRENT_LIST_DIR}/zig-linux.cmake")