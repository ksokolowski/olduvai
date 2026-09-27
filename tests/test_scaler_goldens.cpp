// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// BACKLOG §3.36: pin every HD profile's upscale output with a hash golden so
// the scalers become ordinary code.  No game data — a synthetic 64x48 source
// built to exercise the kernel classes (flat fields, hard stripes, checker,
// hash noise, mixed alpha) rather than one game sprite.
//
// The pin is cross-platform evidence, not a build product: the other
// profiles' kernels are integer, and omniscale's float arithmetic is compiled
// without FMA contraction (CMakeLists.txt), which arm64 clang otherwise applies
// and x86 GCC / MSVC do not.  A platform that differs is a finding, named by
// the failing cell.
#include "enhance/upscale.hpp"
#include "formats/hash64.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

using olduvai::enhance::supported_hd_profiles;
using olduvai::enhance::upscale_rgba;

namespace {

// Deterministic 64x48 RGBA covering what the scalers branch on.  A 4-colour
// alphabet keeps the EQUALITY branching dense — a scale2x/eagle/mmpx/xbr rule
// fires only when neighbours compare equal, so hash noise would exercise just
// the pass-through.  The bands lay flat runs, hard stripes, checker, diagonal
// edges, a gradient with fractional alpha and isolated dots next to each
// other; every pixel is a fixed function of (x, y), same bytes on every
// platform and rebuild.
std::vector<std::uint8_t> golden_source() {
    constexpr int w = 64, h = 48;
    const std::uint8_t pal[4][4] = {
        {0, 0, 0, 255}, {255, 255, 255, 255},
        {255, 0, 0, 255}, {0, 0, 255, 255},
    };
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int idx;
            if (y < 8) idx = 0;                     // flat field
            else if (y < 16) idx = (x / 4) % 4;     // vertical hard stripes
            else if (y < 24) idx = (x / 2 + y / 2) & 3;   // checker (2x2)
            else if (y < 32) idx = (x + y) & 3;     // diagonal stripes
            else if (x == 0 || x == 63) idx = 3;    // border columns
            else {
                std::uint32_t s =
                    static_cast<std::uint32_t>(x + 1) * 2654435761u ^
                    static_cast<std::uint32_t>(y + 31) * 40503u ^ 0x9e3779b9u;
                s ^= s >> 13;
                s *= 0x5bd1e995u;
                s ^= s >> 15;
                idx = static_cast<int>(s & 3u);
            }
            const std::size_t o =
                (static_cast<std::size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) px[o + c] = pal[idx][c];
        }
    // Gradient band with fractional alpha (rows 40-43): exercises the
    // blending scalers' alpha path, where nearby pixels differ by value, not
    // identity.
    for (int y = 40; y < 44; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t o =
                (static_cast<std::size_t>(y) * w + x) * 4;
            px[o] = 96;
            px[o + 1] = 160;
            px[o + 2] = 224;
            px[o + 3] = static_cast<std::uint8_t>(x * 3 + y * 11 + 16);
        }
    // Isolated dots with a unique colour-and-alpha (forces the lone-pixel and
    // one-colour neighbourhood classes instead of the anti-aliased default).
    for (const auto& dot : {std::pair<int, int>{32, 12},
                            {33, 25}, {4, 45}}) {
        const std::size_t o =
            (static_cast<std::size_t>(dot.second) * w + dot.first) * 4;
        px[o] = 200;
        px[o + 1] = 120;
        px[o + 2] = 20;
        px[o + 3] = 200;
    }
    for (int x = 0; x < w; x += 16) {   // A few fully transparent cells.
        const std::size_t o = (static_cast<std::size_t>(36) * w + x + 7) * 4;
        px[o] = px[o + 1] = px[o + 2] = px[o + 3] = 0;
    }
    return px;
}

std::uint64_t hash_of(const std::vector<std::uint8_t>& rgba) {
    olduvai::formats::Hash64 h;
    h.mix_bytes(rgba.data(), rgba.size());
    return h.value();
}

}  // namespace

int main() {
    constexpr int kWidth = 64, kHeight = 48;
    const auto src = golden_source();
    const auto& profiles = supported_hd_profiles();

    // Hash of upscale_rgba(src, 64, 48, scale, profile), keyed
    // "<profile>:<scale>".  Pinned 2026-09-24 on arm64 Apple clang; the CI
    // platform matrix re-verifies them — a differing cell is the finding.
    static const std::map<std::string, std::uint64_t> kGoldens = {
        {"native:2", 0x9e10db102671c0f3ull},
        {"native:3", 0xa75d99c9a220264aull},
        {"native:4", 0x23a8d1cbfbe43cc3ull},
        {"native:5", 0x67270a0f00e04342ull},
        {"retro:2", 0x9e10db102671c0f3ull},
        {"retro:3", 0xa75d99c9a220264aull},
        {"retro:4", 0x23a8d1cbfbe43cc3ull},
        {"retro:5", 0x67270a0f00e04342ull},
        {"smooth:2", 0xdda0fd3c3c1680a8ull},
        {"smooth:3", 0xc8fd78cc8b2c672full},
        {"smooth:4", 0x687604bd309d86acull},
        {"smooth:5", 0x67270a0f00e04342ull},
        {"eagle:2", 0x374d2ac61b61fd21ull},
        {"eagle:3", 0xc8fd78cc8b2c672full},   // no native 3x → scale3x
        {"eagle:4", 0xdc880b9065f95b4full},
        {"eagle:5", 0x67270a0f00e04342ull},
        {"xbr:2", 0xdd8e04fb4169bfa5ull},
        {"xbr:3", 0xc8fd78cc8b2c672full},     // no native 3x → scale3x
        {"xbr:4", 0x8da922bae5ebe30aull},
        {"xbr:5", 0x67270a0f00e04342ull},
        {"mmpx:2", 0x101f5befa0c421baull},
        {"mmpx:3", 0xc8fd78cc8b2c672full},    // no native 3x → scale3x
        {"mmpx:4", 0x6bcf631f031bee98ull},
        {"mmpx:5", 0x67270a0f00e04342ull},
        {"omniscale:2", 0xc3125eb48cdafd0full},
        {"omniscale:3", 0x464d610e77bb4728ull},
        {"omniscale:4", 0xce33f2477c131cecull},
        {"omniscale:5", 0x4e5fb3b93782b5b7ull},
    };

    int failures = 0;
    for (const auto& profile : profiles)
        for (int scale = 2; scale <= 5; ++scale) {
            const std::uint64_t got =
                hash_of(upscale_rgba(src, kWidth, kHeight, scale, profile));
            const auto it =
                kGoldens.find(profile + ":" + std::to_string(scale));
            if (it == kGoldens.end() || it->second != got) {
                std::fprintf(stderr,
                             "scaler_goldens: %s scale %d computed %016llx "
                             "(%s)\n",
                             profile.c_str(), scale,
                             static_cast<unsigned long long>(got),
                             it == kGoldens.end() ? "not yet pinned"
                                                  : "MISMATCH");
                ++failures;
            }
        }

    std::printf("scaler_goldens: %zu profile x 4 scale cells, %d differ\n",
                profiles.size() * 4, failures);
    return failures == 0 ? 0 : 1;
}