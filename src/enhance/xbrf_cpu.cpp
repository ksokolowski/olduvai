// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/xbrf_cpu.hpp"

#include "enhance/cpu_kernels.hpp"
#include "enhance/xbrf.hpp"
#include "enhance/xbrf_hwdiv.hpp"

namespace olduvai::enhance {

namespace {

// Whether the second copy runs.  On armhf it divides in hardware and runs
// only where the CPU can; elsewhere it is built as the first.
bool second_copy(XbrfKernel kernel) {
    switch (kernel) {
        case XbrfKernel::Auto:
            return hw_divide_kernels();
        case XbrfKernel::HwDivide:
#ifdef OLDUVAI_HWDIV_KERNELS
            return hw_divide_available();
#else
            return true;
#endif
        case XbrfKernel::Baseline:
            break;
    }
    return false;
}

}  // namespace

std::vector<std::uint8_t> xbrf_for_cpu(const std::vector<std::uint8_t>& rgba,
                                       int w, int h, int s) {
    return xbrf_with_kernel(rgba, w, h, s, XbrfKernel::Auto);
}

std::vector<std::uint8_t> xbrf_with_kernel(const std::vector<std::uint8_t>& rgba,
                                           int w, int h, int s, XbrfKernel kernel) {
    // xBRF pads the picture by its own edge pixels, so an opaque frame keeps
    // an opaque border.
    return second_copy(kernel) ? enhance_hwdiv::xbrf_scale(rgba, w, h, s)
                               : xbrf_scale(rgba, w, h, s);
}

}  // namespace olduvai::enhance
