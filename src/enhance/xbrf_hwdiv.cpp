// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRF compiled a second time, on armhf with hardware divide (CMake's OLDUVAI_HWDIV_CPU_FLAGS, from the toolchain file). Its blend
// divides by a variable weight four times a blended pixel, a library call without the instruction. The namespace is renamed so
// this copy shares no symbol with the first: a shared inline function would let the linker keep the copy that divides for both,
// and the first would stop on a CPU without it. The code goes to its own section, so scripts/check_hwdiv.sh (run by packaging/build_knulli_release.sh) can check that every
// hardware divide in the binary is in it, and that nothing in it calls the first copy; the other headers come first, so their
// inline code stays out of it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "enhance/parallel_rows.hpp"
#include "enhance/xbrf_hwdiv.hpp"
#include "enhance/xbrf_patterns.hpp"

// What xbrf.cpp takes from the rest of its namespace. Not xbrf.hpp: it's read first under the rename below, so XbrfTuning is this
// copy's own and the first copy's xbrf_scale is never declared here. Read above, xbrf.cpp's call from one xbrf_scale to the other
// finds the first copy's by argument-dependent lookup, and this copy runs the first's code (seen on the A12).
namespace olduvai::enhance_hwdiv {
using olduvai::enhance::parallel_rows;
namespace xbrf = olduvai::enhance::xbrf;  // NOLINT(misc-unused-alias-decls): xbrf.cpp's `using namespace xbrf` below takes it
}  // namespace olduvai::enhance_hwdiv

#if defined(__clang__) && defined(__ELF__)
#pragma clang section text = "olduvai_hwdiv"
#endif

#define enhance enhance_hwdiv
#include "xbrf.cpp"  // NOLINT(bugprone-suspicious-include): the second copy is this file's purpose
#undef enhance
