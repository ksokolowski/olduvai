// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/upscale.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <stdexcept>

#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/pixel_scalers.hpp"
#include "enhance/xbrz_scale.hpp"

namespace olduvai::enhance {

namespace {

using Pixels = std::vector<std::uint8_t>;
using Fn2 = Pixels (*)(const Pixels&, int, int);        // a fixed-factor scaler
using FnN = Pixels (*)(const Pixels&, int, int, int);   // takes the factor

// How one profile reaches one factor.  The table below is the only place that
// says so: the dispatch runs it, describe_hd_scaler() words it, and the menu
// and the docs are tested against it, so a name cannot promise what is not run.
enum class Route {
    Native,            // a scaler that has this factor itself
    TwoPass,           // the scaler's 2x applied twice (x4)
    Scale3xFallback,   // the profile has no 3x form: Scale3x stands in
    Nearest,           // block replication
};
struct Plan {
    Route route;
    const char* name;  // the scaler, as the Runs row and the docs say it
    Fn2 f2;            // Native from a fixed scaler, and TwoPass
    FnN fn;            // Native from a scaler that takes the factor
};
constexpr Plan native2(const char* n, Fn2 f) { return {Route::Native, n, f, nullptr}; }
constexpr Plan nativeN(const char* n, FnN f) { return {Route::Native, n, nullptr, f}; }
constexpr Plan twice(const char* n, Fn2 f) { return {Route::TwoPass, n, f, nullptr}; }
constexpr Plan no3x(const char* n) { return {Route::Scale3xFallback, n, nullptr, nullptr}; }
constexpr Plan nearest() { return {Route::Nearest, "nearest", nullptr, nullptr}; }

struct Scaler {
    const char* id;            // the --hd-profile and play.json value
    const char* label;         // the Options menu's name for it
    bool preserves_palette;    // copies whole source pixels: alpha is re-stamped
    Plan plan[3];              // x2, x3, x4; any other factor is Nearest
};

// Menu order.  "native" is HD off upstream; asked for a factor anyway it
// replicates, so a caller sized for x2 still gets x2.
constexpr Scaler kScalers[] = {
    {"native", "Off", true, {nearest(), nearest(), nearest()}},
    {"retro", "Nearest", true, {nearest(), nearest(), nearest()}},
    {"smooth", "Scale2x/3x", true,
     {native2("Scale2x", scale2x), native2("Scale3x", scale3x), twice("Scale2x", scale2x)}},
    {"eagle", "Eagle", true, {native2("Eagle", eagle_2x), no3x("Eagle"), twice("Eagle", eagle_2x)}},
    {"xbrz", "xBRZ", false,
     {nativeN("xBRZ", xbrz_scale), nativeN("xBRZ", xbrz_scale), nativeN("xBRZ", xbrz_scale)}},
    {"mmpx", "MMPX", true, {native2("MMPX", mmpx_2x), no3x("MMPX"), twice("MMPX", mmpx_2x)}},
    {"omniscale", "OmniScale", false,
     {nativeN("OmniScale", omniscale), nativeN("OmniScale", omniscale), nativeN("OmniScale", omniscale)}},
};
constexpr std::size_t kScalerCount = sizeof(kScalers) / sizeof(kScalers[0]);

const Scaler* find_scaler(const std::string& profile, std::size_t* index = nullptr) {
    for (std::size_t i = 0; i < kScalerCount; ++i)
        if (profile == kScalers[i].id) {
            if (index) *index = i;
            return &kScalers[i];
        }
    return nullptr;
}

const Plan& plan_for(const Scaler& s, int scale) {
    static constexpr Plan kNearest = nearest();
    return scale >= 2 && scale <= 4 ? s.plan[scale - 2] : kNearest;
}

// Said once per profile: x3 runs several times a frame, and the note is for the
// player who picked the name, not a per-call log.
void note_scale3x_fallback(std::size_t index) {
    static std::atomic<unsigned> said{0};
    const unsigned bit = 1u << index;
    if (said.fetch_or(bit, std::memory_order_relaxed) & bit) return;
    std::fprintf(stderr, "olduvai: hd-profile '%s' has no 3x form; x3 uses Scale3x.\n",
                 kScalers[index].id);
}

Pixels run_plan(const Plan& p, std::size_t index, const Pixels& px, int w, int h,
                int scale) {
    switch (p.route) {
        case Route::Native:
            return p.f2 ? p.f2(px, w, h) : p.fn(px, w, h, scale);
        case Route::TwoPass:
            return p.f2(p.f2(px, w, h), w * 2, h * 2);
        case Route::Scale3xFallback:
            note_scale3x_fallback(index);
            return scale3x(px, w, h);
        case Route::Nearest:
            break;
    }
    return nearest_scale(px, w, h, scale);
}

std::vector<std::uint8_t> upscale_rgba_impl(const std::vector<std::uint8_t>& px,
                                            int w, int h, int scale,
                                            const std::string& profile) {
    if (scale == 1) return px;
    std::size_t index = 0;
    const Scaler* s = find_scaler(profile, &index);
    // Unreachable after the CLI's is_supported_hd_profile() check; fail loudly
    // rather than fall through to a default scaler.
    if (!s) throw std::invalid_argument("upscale_rgba: unsupported HD profile '" +
                                        profile + "'");
    return run_plan(plan_for(*s, scale), index, px, w, h, scale);
}

// Atomic so a caller off the main thread stays safe (every call is on it
// today; the scalers thread inside); the cost is a relaxed add per call.
std::atomic<double> g_upscale_ms{0.0};
std::atomic<unsigned long> g_upscale_calls{0};

}  // namespace

const std::vector<std::string>& supported_hd_profiles() {
    // The reference's profile catalog minus painterly/cinematic, which need a
    // per-asset-class bilinear/lanczos split the whole-frame path cannot do
    // (the CLI rejects them).
    static const std::vector<std::string> kProfiles = [] {
        std::vector<std::string> v;
        for (const Scaler& s : kScalers) v.emplace_back(s.id);
        return v;
    }();
    return kProfiles;
}

std::string canonical_hd_profile(const std::string& profile) {
    return profile == "xbr" ? "xbrz" : profile;
}

bool is_supported_hd_profile(const std::string& profile) {
    return find_scaler(profile) != nullptr;
}

bool profile_preserves_palette(const std::string& profile) {
    // Whole-pixel scalers carry only source colours, so the alpha mask is
    // re-stamped as a nearest upscale of the source; the blenders (omniscale,
    // xbrz) keep their anti-aliased edge.
    const Scaler* s = find_scaler(profile);
    return s && s->preserves_palette;
}

std::string hd_profile_label(const std::string& profile) {
    const Scaler* s = find_scaler(profile);
    return s ? s->label : "";
}

std::string describe_hd_scaler(const std::string& profile, int scale) {
    if (scale <= 1) return "none";
    const Scaler* s = find_scaler(profile);
    if (!s) return "unsupported";
    const Plan& p = plan_for(*s, scale);
    switch (p.route) {
        case Route::Native: return p.name;
        case Route::TwoPass: return std::string(p.name) + ", two passes";
        case Route::Scale3xFallback:
            return "Scale3x (" + std::string(p.name) + " has no 3x form)";
        case Route::Nearest: break;
    }
    return "nearest";
}

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
    // fetch_add on a double needs a CAS loop; ~19 calls a frame, no contention.
    double cur = g_upscale_ms.load(std::memory_order_relaxed);
    while (!g_upscale_ms.compare_exchange_weak(cur, cur + ms,
                                               std::memory_order_relaxed)) {}
    g_upscale_calls.fetch_add(1, std::memory_order_relaxed);
    return out;
}

}  // namespace olduvai::enhance
