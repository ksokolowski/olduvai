// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "core/rng.hpp"

namespace olduvai::core {

RandLcg16& global_rng() {
    static RandLcg16 instance;  // seed 1
    return instance;
}

}  // namespace olduvai::core
