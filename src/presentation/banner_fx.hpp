// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Per-pixel banner colour effects (enhanced flair).  Shared by the in-game
// GET READY / NOT ENOUGH FOOD banners and the main-menu OLDUVAI title, so the
// "caveman fire-and-blood" look lives in exactly one place.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "enhance/hd_text.hpp"

namespace olduvai::presentation {

// Build a per-pixel shader by name.  tsec = wall-clock seconds (animation
// phase, refresh-rate-independent).  u spans the text width L→R, v spans the
// cap height top→bottom.  Names: "caveman" (default primal fire-and-blood),
// "fire", "rainbow", "gold", "pulse"; any unknown name falls back to caveman.
inline enhance::HdText::ShadeFn make_banner_shade(const std::string& fx,
                                                  float tsec) {
    return [fx, tsec](float u, float v, std::uint8_t& r, std::uint8_t& g2,
                      std::uint8_t& b2) {
        const float TAU = 6.2831853f;
        auto c8 = [](float x) {
            return static_cast<std::uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
        };
        auto hsv = [&](float h, float s, float vv) {
            h -= std::floor(h);
            const float i = std::floor(h * 6.0f), f = h * 6.0f - i;
            const float p = vv * (1 - s), q = vv * (1 - s * f),
                        t = vv * (1 - s * (1 - f));
            float rr, gg, bb;
            switch (static_cast<int>(i) % 6) {
                case 0: rr = vv; gg = t; bb = p; break;
                case 1: rr = q; gg = vv; bb = p; break;
                case 2: rr = p; gg = vv; bb = t; break;
                case 3: rr = p; gg = q; bb = vv; break;
                case 4: rr = t; gg = p; bb = vv; break;
                default: rr = vv; gg = p; bb = q; break;
            }
            r = c8(rr * 255); g2 = c8(gg * 255); b2 = c8(bb * 255);
        };
        if (fx == "rainbow") {
            (void)v;
            hsv(u + tsec * 0.66f, 0.95f, 1.0f);
            return;
        }
        if (fx == "gold") {
            const float bx = tsec * 0.6f - std::floor(tsec * 0.6f);
            const float br = 255.0f, bg = 205 - 95 * v, bb = 70 - 55 * v;
            float band = 1.0f - std::min(1.0f, std::fabs(u - bx) * 5.0f);
            band = band < 0 ? 0 : band;
            r = c8(br); g2 = c8(bg + (255 - bg) * band);
            b2 = c8(bb + (255 - bb) * band);
            return;
        }
        if (fx == "pulse") {
            const float p = 0.55f + 0.45f * std::sin(tsec * TAU * 0.5f);
            r = c8(235 * p); g2 = c8(235 * p); b2 = c8(235 * p);
            return;
        }
        if (fx == "fire") {
            const float flick = 0.07f * std::sin(u * 9.0f + tsec * 7.0f);
            float t = v + flick;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            if (t < 0.5f) {
                const float k = t / 0.5f;
                r = 255; g2 = c8(255 - 90 * k); b2 = c8(150 * (1 - k));
            } else {
                const float k = (t - 0.5f) / 0.5f;
                r = 255; g2 = c8(165 - 150 * k); b2 = c8(10 * (1 - k));
            }
            return;
        }
        // "caveman" (default) — primal fire-and-blood: ochre-ember top, burning
        // orange-red middle, dark blood-crimson bottom; fire flicker + a slow
        // ember breath.  Cave-painting palette (ochre / red-oxide / charcoal).
        const float flick = 0.06f * std::sin(u * 8.0f + tsec * 9.0f);
        float t = v + flick;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float rr, gg, bb;
        if (t < 0.5f) {            // ochre-ember → fire orange-red
            const float k = t / 0.5f;
            rr = 255 + (205 - 255) * k;
            gg = 188 + (45 - 188) * k;
            bb = 70 + (18 - 70) * k;
        } else {                   // fire → dark blood-crimson
            const float k = (t - 0.5f) / 0.5f;
            rr = 205 + (95 - 205) * k;
            gg = 45 + (3 - 45) * k;
            bb = 18 + (10 - 18) * k;
        }
        const float breath = 0.9f + 0.1f * std::sin(tsec * 3.0f);
        r = c8(rr * breath); g2 = c8(gg * breath); b2 = c8(bb * breath);
    };
}

}  // namespace olduvai::presentation
