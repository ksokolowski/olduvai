// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The `xbrf` HD profile: xBRF (xbrf.hpp) in the copy this CPU runs best.
#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// RGBA in, (W*s)x(H*s) RGBA out; s in 2..6 (the profile uses 2 to 4).  Alpha
// takes part in both the edge decision and the blend, so a sprite's silhouette
// rounds off like its interior and a transparent pixel's colour never leaks in.
std::vector<std::uint8_t> xbrf_for_cpu(const std::vector<std::uint8_t>& rgba,
                                       int w, int h, int s);

// Which compiled copy runs (cpu_kernels.hpp): Auto is the CPU's choice.
// HwDivide is the second copy (xbrf_hwdiv.cpp); on armhf it falls back to
// Baseline where hw_divide_available() is false.
enum class XbrfKernel { Auto, Baseline, HwDivide };
std::vector<std::uint8_t> xbrf_with_kernel(const std::vector<std::uint8_t>& rgba,
                                           int w, int h, int s, XbrfKernel kernel);

}  // namespace olduvai::enhance
