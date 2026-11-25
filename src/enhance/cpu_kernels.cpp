// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/cpu_kernels.hpp"

#include <cstdlib>
#include <cstring>

#if defined(OLDUVAI_HWDIV_KERNELS) && defined(__linux__)
#include <sys/auxv.h>
#endif

namespace olduvai::enhance {
namespace {

bool cpu_has_idiva() {
#if defined(OLDUVAI_HWDIV_KERNELS) && defined(__linux__)
    // The kernel's HWCAP_IDIVA bit (arch/arm/include/uapi/asm/hwcap.h).
    constexpr unsigned long kIdiva = 1UL << 17;
    return (getauxval(AT_HWCAP) & kIdiva) != 0;
#else
    return false;
#endif
}

bool forced_baseline() {
    const char* e = std::getenv("OLDUVAI_KERNELS");
    return e != nullptr && std::strcmp(e, "baseline") == 0;
}

}  // namespace

bool hw_divide_available() {
    static const bool v = cpu_has_idiva();
    return v;
}

bool hw_divide_kernels() {
    static const bool v = hw_divide_available() && !forced_baseline();
    return v;
}

std::string kernel_description() {
#ifndef OLDUVAI_HWDIV_KERNELS
    return "baseline";
#else
    if (hw_divide_kernels()) return "hardware divide (xBRF)";
    if (hw_divide_available()) return "baseline (OLDUVAI_KERNELS=baseline)";
    return "baseline (no hardware divide on this CPU)";
#endif
}

}  // namespace olduvai::enhance
