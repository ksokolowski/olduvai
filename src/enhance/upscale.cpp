// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/upscale.hpp"

#include <cstdio>
#include <stdexcept>

#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/pixel_scalers.hpp"
#include "enhance/xbr.hpp"

namespace olduvai::enhance {

const std::vector<std::string>& supported_hd_profiles() {
    // Profiles olduvai actually renders.  Mirrors the reference engine's
    // profile catalog MINUS painterly/cinematic, which depend on a
    // per-asset-class bilinear/lanczos split that the flat whole-frame
    // upscale path here cannot reproduce (they are rejected by the CLI
    // with a clear "not yet supported" message rather than faked).
    static const std::vector<std::string> kProfiles = {
        "native", "retro", "smooth", "eagle", "xbr", "mmpx", "omniscale",
    };
    return kProfiles;
}

bool is_supported_hd_profile(const std::string& profile) {
    for (const auto& p : supported_hd_profiles())
        if (p == profile) return true;
    return false;
}

std::vector<std::uint8_t> upscale_rgba(const std::vector<std::uint8_t>& px,
                                       int w, int h, int scale,
                                       const std::string& profile) {
    if (scale == 1) return px;

    if (profile == "native") {
        // HD is disabled upstream for native; defensively block-replicate so
        // a caller that still asks for scale>1 gets correctly-sized output.
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "retro") {
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "smooth") {
        if (scale == 2) return scale2x(px, w, h);
        if (scale == 3) return scale3x(px, w, h);
        if (scale == 4) {
            auto up = scale2x(px, w, h);
            return scale2x(up, w * 2, h * 2);
        }
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "eagle") {
        if (scale == 2) return eagle_2x(px, w, h);
        if (scale == 4) {
            auto up = eagle_2x(px, w, h);
            return eagle_2x(up, w * 2, h * 2);
        }
        if (scale == 3) {
            // Eagle is strictly 2×-only; fall back to Scale3x (same
            // palette-preserving family) — matches the reference engine.
            std::fprintf(stderr,
                "olduvai: hd-profile 'eagle' has no native 3× form — using "
                "scale3x for this scale.\n");
            return scale3x(px, w, h);
        }
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "xbr") {
        if (scale == 2) return xbr_2x(px, w, h);
        if (scale == 4) {
            auto up = xbr_2x(px, w, h);
            return xbr_2x(up, w * 2, h * 2);
        }
        if (scale == 3) {
            std::fprintf(stderr,
                "olduvai: hd-profile 'xbr' has no native 3× form — using "
                "scale3x for this scale.\n");
            return scale3x(px, w, h);
        }
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "mmpx") {
        auto up = mmpx_2x(px, w, h);
        if (scale == 4) up = mmpx_2x(up, w * 2, h * 2);
        return up;
    }

    if (profile == "omniscale") {
        return omniscale(px, w, h, scale);
    }

    // Unreachable when the CLI validates with is_supported_hd_profile().
    // No silent OmniScale fall-through: an unknown/unimplemented profile is
    // a programmer error here, so fail loudly.
    throw std::invalid_argument("upscale_rgba: unsupported HD profile '" +
                                profile + "'");
}

}  // namespace olduvai::enhance
