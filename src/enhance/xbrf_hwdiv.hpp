// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRF's second copy (xbrf_hwdiv.cpp): on armhf built with hardware divide, so call it only when hw_divide_kernels()
// (cpu_kernels.hpp) says the CPU has it; elsewhere built as the first, for test_upscale to hold the two the same.
#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance_hwdiv {

std::vector<std::uint8_t> xbrf_scale(const std::vector<std::uint8_t>& rgba, int w, int h, int scale);

}  // namespace olduvai::enhance_hwdiv
