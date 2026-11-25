// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Kernels with a CPU-specific variant, chosen once at run time.  The armhf
// binary's baseline has no hardware integer divide, so it runs on a
// Cortex-A8 / A9; where the CPU has one (AT_HWCAP's IDIVA: the A7, A15, and
// every 64-bit core in 32-bit state), the hot kernels use a copy compiled
// with it.  Every variant gives the baseline's bytes; test_upscale holds that.
#pragma once

#include <string>

namespace olduvai::enhance {

// The hardware-divide copies are in this build and the CPU can run them.
bool hw_divide_available();

// They run: available, and OLDUVAI_KERNELS=baseline does not force them off.
bool hw_divide_kernels();

// For the log and the F5 report: which variant runs, and why not the other.
std::string kernel_description();

}  // namespace olduvai::enhance
