// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <atomic>
#include <chrono>
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

bool profile_preserves_palette(const std::string& profile) {
    // Nearest / whole-pixel scalers: the upscaled sprite carries only source
    // colours, so its binary alpha mask is re-stamped as a nearest upscale of
    // the source (crisp silhouette).  The blenders (omniscale, xbr) invent an
    // anti-aliased alpha edge from the binary input mask, which is kept.
    return profile == "native" || profile == "retro" || profile == "smooth" ||
           profile == "eagle" || profile == "mmpx";
}

static std::vector<std::uint8_t> upscale_rgba_impl(const std::vector<std::uint8_t>& px,
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
            // Eagle is strictly 2x-only; fall back to Scale3x (same
            // palette-preserving family) — matches the reference engine.
            std::fprintf(stderr,
                "olduvai: hd-profile 'eagle' has no native 3x form — using "
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
                "olduvai: hd-profile 'xbr' has no native 3x form — using "
                "scale3x for this scale.\n");
            return scale3x(px, w, h);
        }
        return nearest_scale(px, w, h, scale);
    }

    if (profile == "mmpx") {
        if (scale == 3) {
            // MMPX is strictly 2x-only.  This arm used to fall through and
            // return a 2x buffer for a 3x request, which every caller then
            // sized as 3x — HdAssetCache::build wrote the alpha re-stamp off
            // the end of the vector ("malloc(): corrupted top size", SIGABRT).
            // Unreachable while hd_scale_for clamped to 2-or-4, and live the
            // moment scale 3 was allowed through.  Fall back to Scale3x, the
            // same palette-preserving family, exactly as eagle and xbr do.
            std::fprintf(stderr,
                "olduvai: hd-profile 'mmpx' has no native 3x form — using "
                "scale3x for this scale.\n");
            return scale3x(px, w, h);
        }
        auto up = mmpx_2x(px, w, h);
        if (scale == 4) up = mmpx_2x(up, w * 2, h * 2);
        if (scale != 2 && scale != 4) return nearest_scale(px, w, h, scale);
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

// Atomic because the row-band threading planned for the scalers may one day
// call this from more than one thread; the cost is a relaxed add per call.
namespace {
std::atomic<double> g_upscale_ms{0.0};
std::atomic<unsigned long> g_upscale_calls{0};
}  // namespace

UpscaleStats upscale_stats() {
    return {g_upscale_ms.load(std::memory_order_relaxed),
            g_upscale_calls.load(std::memory_order_relaxed)};
}

void reset_upscale_stats() {
    g_upscale_ms.store(0.0, std::memory_order_relaxed);
    g_upscale_calls.store(0, std::memory_order_relaxed);
}

std::vector<std::uint8_t> upscale_rgba(const std::vector<std::uint8_t>& px,
                                       int w, int h, int scale,
                                       const std::string& profile) {
    const auto t0 = std::chrono::steady_clock::now();
    auto out = upscale_rgba_impl(px, w, h, scale, profile);
    const double ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    // fetch_add on a double needs a CAS loop; at ~19 calls a frame the
    // contention is nil and the alternative (a non-atomic double) is a data
    // race the moment the scalers are threaded.
    double cur = g_upscale_ms.load(std::memory_order_relaxed);
    while (!g_upscale_ms.compare_exchange_weak(cur, cur + ms,
                                               std::memory_order_relaxed)) {}
    g_upscale_calls.fetch_add(1, std::memory_order_relaxed);
    return out;
}

}  // namespace olduvai::enhance
