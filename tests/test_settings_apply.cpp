// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/settings_apply.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace olduvai::presentation;

// NDEBUG-proof check macro: prints failing line + description, returns 1.
#define REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: %s  (line %d)\n", #cond, __LINE__); \
            return 1; \
        } \
    } while (0)

int main() {
    // Baseline: enhanced session at smooth/scale 2 (hd_scale 2), MT-32 audio.
    const DisplaySettings cur{/*enhanced=*/true, "smooth", 2, "mt32-builtin", "opl"};

    // Volumes + fullscreen are always live.
    REQUIRE(classify_change("music_volume", "50", cur) == ApplyTier::Live);
    REQUIRE(classify_change("sfx_volume", "80", cur) == ApplyTier::Live);
    REQUIRE(classify_change("fullscreen", "1", cur) == ApplyTier::Live);

    // Aspect only calls SDL_RenderSetLogicalSize — no window/audio rebuild → live.
    REQUIRE(classify_change("aspect", "stretch", cur) == ApplyTier::Live);
    REQUIRE(classify_change("aspect", "4:3", cur) == ApplyTier::Live);
    REQUIRE(classify_change("aspect", "keep", cur) == ApplyTier::Live);

    // hd_profile among same-scale non-native profiles → live.
    REQUIRE(classify_change("hd_profile", "xbr", cur) == ApplyTier::Live);
    REQUIRE(classify_change("hd_profile", "omniscale", cur) == ApplyTier::Live);

    // hd_profile crossing to native (hd_scale 2 -> 1) → reinit (window resize).
    REQUIRE(classify_change("hd_profile", "native", cur) == ApplyTier::Reinit);

    // render_scale change while enhanced (hd_scale 2 -> 4) → reinit.
    REQUIRE(classify_change("render_scale", "4", cur) == ApplyTier::Reinit);

    // Audio device / backend change → reinit (audio rebuild).
    REQUIRE(classify_change("music_device", "gm-builtin", cur) == ApplyTier::Reinit);
    REQUIRE(classify_change("sfx_backend", "sb-dac", cur) == ApplyTier::Reinit);

    // No-op: same value → PersistOnly (nothing to rebuild).
    REQUIRE(classify_change("music_device", "mt32-builtin", cur) == ApplyTier::PersistOnly);
    REQUIRE(classify_change("render_scale", "2", cur) == ApplyTier::PersistOnly);

    // Non-enhanced session: hd_scale is always 1, so render_scale / hd_profile
    // never alter the live window → PersistOnly.
    const DisplaySettings dos{/*enhanced=*/false, "native", 2, "opl", "opl"};
    REQUIRE(classify_change("render_scale", "4", dos) == ApplyTier::PersistOnly);
    REQUIRE(classify_change("hd_profile", "smooth", dos) == ApplyTier::PersistOnly);

    // hd_scale_for spot checks.
    REQUIRE(hd_scale_for(true, "smooth", 2) == 2);
    REQUIRE(hd_scale_for(true, "smooth", 4) == 4);
    REQUIRE(hd_scale_for(true, "native", 4) == 1);
    REQUIRE(hd_scale_for(false, "smooth", 4) == 1);

    std::puts("settings_apply: OK");

    // ── apply_preset: fan-out order + values ──
    struct Rec : MenuBindings {
        std::vector<std::pair<std::string, std::string>> calls;
        std::string get(const std::string&) override { return {}; }
        void set(const std::string& k, const std::string& v) override {
            calls.emplace_back(k, v);
        }
    } rec;
    auto has = [&rec](const char* k, const char* v) {
        for (const auto& [kk, vv] : rec.calls)
            if (kk == k && vv == v) return true;
        return false;
    };
    apply_preset(rec, "hd");
    // "enhanced" MUST be first: sessions drain in stage order and the hd-key
    // rebuild reads the master flag.
    REQUIRE(!rec.calls.empty());
    REQUIRE(rec.calls.front().first == "enhanced");
    REQUIRE(rec.calls.front().second == "true");
    REQUIRE(has("hd_profile", "omniscale"));
    REQUIRE(has("render_scale", "4"));
    REQUIRE(has("aspect", "widescreen"));
    REQUIRE(has("enhance.smooth_motion", "1"));
    REQUIRE(has("enhance.descent_pan", "1"));

    rec.calls.clear();
    apply_preset(rec, "hd-43");
    REQUIRE(has("aspect", "4:3"));

    rec.calls.clear();
    apply_preset(rec, "dos");
    REQUIRE(rec.calls.front().first == "enhanced");
    REQUIRE(rec.calls.front().second == "false");
    REQUIRE(has("aspect", "keep"));
    REQUIRE(has("enhance.smooth_motion", "0"));
    // dos deliberately leaves hd_profile/render_scale untouched: the master
    // flag alone forces compose scale 1 (hd_scale_for); not staging them
    // avoids spurious Reinit noise in the confirm dialog.
    REQUIRE(!has("hd_profile", "omniscale"));

    // ── classify: enhanced master crossing = Reinit ──
    const DisplaySettings classic2{false, "omniscale", 4, "auto", "auto"};
    REQUIRE(classify_change("enhanced", "true", classic2) == ApplyTier::Reinit);
    REQUIRE(classify_change("enhanced", "false", classic2) ==
            ApplyTier::PersistOnly);
    const DisplaySettings hd2{true, "omniscale", 4, "auto", "auto"};
    REQUIRE(classify_change("enhanced", "false", hd2) == ApplyTier::Reinit);

    // ── classify_change_in_set: the Style preset's keys cross the
    // classic<->HD boundary TOGETHER even when no single key does (the
    // fresh-classic first-run scenario: enhanced=false + native profile).
    const DisplaySettings fresh{false, "native", 2, "auto", "auto"};
    const std::vector<std::pair<std::string, std::string>> hd_set = {
        {"enhanced", "true"}, {"hd_profile", "omniscale"},
        {"render_scale", "4"}};
    // Per-key against the same baseline: every display key is PersistOnly…
    REQUIRE(classify_change("enhanced", "true", fresh) ==
            ApplyTier::PersistOnly);
    REQUIRE(classify_change("hd_profile", "omniscale", fresh) ==
            ApplyTier::PersistOnly);
    // …but as a SET each classifies Reinit.
    REQUIRE(classify_change_in_set("enhanced", "true", fresh, hd_set) ==
            ApplyTier::Reinit);
    REQUIRE(classify_change_in_set("hd_profile", "omniscale", fresh, hd_set) ==
            ApplyTier::Reinit);
    REQUIRE(classify_change_in_set("render_scale", "4", fresh, hd_set) ==
            ApplyTier::Reinit);
    // Non-display keys keep their per-key classification inside a set.
    REQUIRE(classify_change_in_set("aspect", "widescreen", fresh, hd_set) ==
            ApplyTier::Live);
    // A set that does NOT cross the boundary falls back to per-key tiers
    // (same-scale profile swap stays Live).
    const std::vector<std::pair<std::string, std::string>> swap_set = {
        {"hd_profile", "xbr"}};
    REQUIRE(classify_change_in_set("hd_profile", "xbr", hd2, swap_set) ==
            ApplyTier::Live);
    // Empty set degenerates to classify_change exactly.
    REQUIRE(classify_change_in_set("enhanced", "true", classic2, {}) ==
            ApplyTier::Reinit);

    // ── encode_enhance_persist: the shared enhance.* Apply encoding ──
    {
        // Preset drain (master staged): only the FIRST enhance.* key
        // flushes, the list covers every "1" flag in mem (alphabetical,
        // dashed), and there is NO enhanced=false companion — the staged
        // master's own persisted value must stand.
        std::map<std::string, std::string> mem = {
            {"enhance.cinematic_cue", "1"}, {"enhance.descent_pan", "1"},
            {"enhance.fluid_bubbles", "1"}, {"enhance.hd_text", "1"},
            {"enhance.hud_overlay", "1"},   {"enhance.secret_slide", "1"},
            {"enhance.smooth_motion", "1"}, {"hd_profile", "omniscale"}};
        std::vector<StagedChange> staged = {
            {"enhanced", "enhanced", "false", "true"},
            {"enhance.smooth_motion", "", "0", "1"},
            {"enhance.hd_text", "", "0", "1"},
            {"hd_profile", "", "native", "omniscale"}};
        auto w = encode_enhance_persist(mem, staged, "enhance.smooth_motion");
        REQUIRE(w.size() == 1);
        REQUIRE(w[0].first == "enhance");
        REQUIRE(w[0].second ==
                "cinematic-cue,descent-pan,fluid-bubbles,hd-text,"
                "hud-overlay,secret-slide,smooth-motion");
        // Later enhance.* keys of the same drain: already flushed → nothing.
        REQUIRE(encode_enhance_persist(mem, staged, "enhance.hd_text")
                    .empty());

        // Granular drain (master NOT staged): the flush converts a bundle
        // config into the explicit list, so the companion is required.
        mem["enhance.hd_text"] = "0";
        staged = {{"enhance.hd_text", "", "1", "0"}};
        w = encode_enhance_persist(mem, staged, "enhance.hd_text");
        REQUIRE(w.size() == 2);
        REQUIRE(w[0].first == "enhance");
        REQUIRE(w[0].second ==
                "cinematic-cue,descent-pan,fluid-bubbles,"
                "hud-overlay,secret-slide,smooth-motion");
        REQUIRE(w[1].first == "enhanced");
        REQUIRE(w[1].second == "false");

        // dos preset (all flags off, master staged): empty list, no
        // companion.
        for (auto& [k, v] : mem)
            if (k.rfind("enhance.", 0) == 0) v = "0";
        staged = {{"enhanced", "enhanced", "true", "false"},
                  {"enhance.smooth_motion", "", "1", "0"}};
        w = encode_enhance_persist(mem, staged, "enhance.smooth_motion");
        REQUIRE(w.size() == 1);
        REQUIRE(w[0].first == "enhance");
        REQUIRE(w[0].second.empty());
    }

    return 0;
}
