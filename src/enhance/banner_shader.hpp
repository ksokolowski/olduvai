// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Per-pixel colour effects for the enhanced vector banners (GET READY, NOT
// ENOUGH FOOD) and the main-menu OLDUVAI title — the "caveman" fire-and-blood
// look and its alternatives, in one place.
//
// A value type, not a std::function: HdText::draw_banner inlines it into the
// glyph loop.  Everything that depends only on the frame time (the breath,
// the gold band position, the pulse level) is computed once in the
// constructor, and the flicker — the one sine that depends on the pixel —
// depends on the COLUMN only, so draw_banner computes it once per column.
// On the Powkiddy A12 the NOT ENOUGH FOOD banner spent ~3.9 ms of every
// present in this code when it was a per-pixel std::function with two sines.
//
// u spans the text width left to right, v the cap height top to bottom,
// both in [0, 1]; tsec is wall-clock seconds, so the animation does not
// depend on the refresh rate.  Names: "caveman" (default), "fire",
// "rainbow", "gold", "pulse"; an unknown name falls back to caveman.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace olduvai::enhance {

class BannerShader {
public:
    BannerShader(const std::string& fx_name, float tsec)
        : fx_(fx_name == "rainbow" ? Fx::kRainbow
              : fx_name == "gold"  ? Fx::kGold
              : fx_name == "pulse" ? Fx::kPulse
              : fx_name == "fire"  ? Fx::kFire
                                   : Fx::kCaveman),
          tsec_(tsec),
          rainbow_phase_(tsec * 0.66f),
          gold_bx_(tsec * 0.6f - std::floor(tsec * 0.6f)),
          pulse_p_(0.55f + 0.45f * std::sin(tsec * kTau * 0.5f)),
          breath_(0.9f + 0.1f * std::sin(tsec * 3.0f)) {}

    // The part of the shade that depends on u alone (the flicker), so a
    // caller can compute it once per column.  0 for the effects without one.
    float column_term(float u) const {
        switch (fx_) {
            case Fx::kFire:    return 0.07f * std::sin(u * 9.0f + tsec_ * 7.0f);
            case Fx::kCaveman: return 0.06f * std::sin(u * 8.0f + tsec_ * 9.0f);
            default:           return 0.0f;
        }
    }

    // The colour at (u, v); `col` is column_term(u).
    void shade(float u, float v, float col, std::uint8_t& r, std::uint8_t& g,
               std::uint8_t& b) const {
        switch (fx_) {
            case Fx::kRainbow:
                hsv(u + rainbow_phase_, 0.95f, 1.0f, r, g, b);
                return;
            case Fx::kGold: {
                const float br = 255.0f, bg = 205 - 95 * v, bb = 70 - 55 * v;
                float band = 1.0f - std::min(1.0f, std::fabs(u - gold_bx_) * 5.0f);
                band = band < 0 ? 0 : band;
                r = c8(br);
                g = c8(bg + (255 - bg) * band);
                b = c8(bb + (255 - bb) * band);
                return;
            }
            case Fx::kPulse:
                r = c8(235 * pulse_p_);
                g = c8(235 * pulse_p_);
                b = c8(235 * pulse_p_);
                return;
            case Fx::kFire: {
                float t = v + col;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                if (t < 0.5f) {
                    const float k = t / 0.5f;
                    r = 255; g = c8(255 - 90 * k); b = c8(150 * (1 - k));
                } else {
                    const float k = (t - 0.5f) / 0.5f;
                    r = 255; g = c8(165 - 150 * k); b = c8(10 * (1 - k));
                }
                return;
            }
            case Fx::kCaveman: {
                // Primal fire-and-blood: ochre-ember top, burning orange-red
                // middle, dark blood-crimson bottom; fire flicker + a slow
                // ember breath.  Cave-painting palette (ochre / red-oxide /
                // charcoal).
                float t = v + col;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                float rr, gg, bb;
                if (t < 0.5f) {            // ochre-ember -> fire orange-red
                    const float k = t / 0.5f;
                    rr = 255 + (205 - 255) * k;
                    gg = 188 + (45 - 188) * k;
                    bb = 70 + (18 - 70) * k;
                } else {                   // fire -> dark blood-crimson
                    const float k = (t - 0.5f) / 0.5f;
                    rr = 205 + (95 - 205) * k;
                    gg = 45 + (3 - 45) * k;
                    bb = 18 + (10 - 18) * k;
                }
                r = c8(rr * breath_);
                g = c8(gg * breath_);
                b = c8(bb * breath_);
                return;
            }
        }
    }

private:
    enum class Fx { kCaveman, kRainbow, kGold, kPulse, kFire };
    static constexpr float kTau = 6.2831853f;

    static std::uint8_t c8(float x) {
        return static_cast<std::uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
    }
    static void hsv(float h, float s, float vv, std::uint8_t& r,
                    std::uint8_t& g, std::uint8_t& b) {
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
        r = c8(rr * 255);
        g = c8(gg * 255);
        b = c8(bb * 255);
    }

    Fx fx_;
    float tsec_;
    float rainbow_phase_;
    float gold_bx_;
    float pulse_p_;
    float breath_;
};

}  // namespace olduvai::enhance
