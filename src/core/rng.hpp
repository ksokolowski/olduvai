// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// 16-bit game random source — a single global 32-bit LCG.
//
//   state = state * 0x015A4E35 + 1   (mod 2^32)
//   result = (state >> 16) & 0x7FFF  (15-bit)
//
// Initial seed is 1.  // FUN_2d5a_0019; seed at DS:0x87ac

#pragma once

#include <cstdint>

namespace olduvai::core {

class RandLcg16 {
public:
    explicit RandLcg16(std::uint32_t seed = 1u) : state_(seed) {}

    std::uint16_t next() {
        state_ = state_ * 0x015A4E35u + 1u;
        return static_cast<std::uint16_t>((state_ >> 16) & 0x7FFF);
    }

    std::uint32_t state() const { return state_; }
    void reseed(std::uint32_t s) { state_ = s; }

private:
    std::uint32_t state_;
};

// The single shared game LCG (the original keeps one global state that all
// call sites advance).  Callers that mirror an original Rand_LCG16 call
// site must use this instance so the shared sequence is consumed in order.
RandLcg16& global_rng();

}  // namespace olduvai::core
