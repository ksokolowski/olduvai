// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/settings_apply.hpp"

#include <string>

namespace olduvai::presentation {

int hd_scale_for(bool enhanced, const std::string& hd_profile, int render_scale) {
    const bool hd = enhanced && hd_profile != "native";
    return hd ? (render_scale >= 4 ? 4 : 2) : 1;
}

ApplyTier classify_change(const std::string& key, const std::string& new_value,
                          const DisplaySettings& cur) {
    if (key == "music_volume" || key == "sfx_volume" || key == "fullscreen" ||
        key == "aspect")
        return ApplyTier::Live;

    // The enhanced master flag gates the HD pipeline (hd_scale_for): crossing
    // the classic<->HD boundary changes the compose scale = Reinit.
    if (key == "enhanced") {
        const bool en = new_value == "1" || new_value == "true";
        const int now =
            hd_scale_for(cur.enhanced, cur.hd_profile, cur.render_scale);
        const int next = hd_scale_for(en, cur.hd_profile, cur.render_scale);
        return next == now ? ApplyTier::PersistOnly : ApplyTier::Reinit;
    }

    if (key == "hd_profile") {
        if (new_value == cur.hd_profile) return ApplyTier::PersistOnly;
        const int now = hd_scale_for(cur.enhanced, cur.hd_profile, cur.render_scale);
        const int next = hd_scale_for(cur.enhanced, new_value, cur.render_scale);
        // No live HD when not enhanced: both scales are 1, nothing to rebuild.
        if (now == 1 && next == 1) return ApplyTier::PersistOnly;
        return next == now ? ApplyTier::Live : ApplyTier::Reinit;
    }

    if (key == "render_scale") {
        int rs = cur.render_scale;
        try { rs = std::stoi(new_value); } catch (...) { return ApplyTier::PersistOnly; }
        if (rs == cur.render_scale) return ApplyTier::PersistOnly;
        const int now = hd_scale_for(cur.enhanced, cur.hd_profile, cur.render_scale);
        const int next = hd_scale_for(cur.enhanced, cur.hd_profile, rs);
        return next == now ? ApplyTier::PersistOnly : ApplyTier::Reinit;
    }

    if (key == "music_device")
        return new_value == cur.music_device ? ApplyTier::PersistOnly : ApplyTier::Reinit;
    if (key == "sfx_backend")
        return new_value == cur.sfx_backend ? ApplyTier::PersistOnly : ApplyTier::Reinit;

    return ApplyTier::PersistOnly;
}



ApplyTier classify_change_in_set(
    const std::string& key, const std::string& new_value,
    const DisplaySettings& cur,
    const std::vector<std::pair<std::string, std::string>>& staged) {
    const auto is_display = [](const std::string& k) {
        return k == "enhanced" || k == "hd_profile" || k == "render_scale";
    };
    if (!is_display(key)) return classify_change(key, new_value, cur);
    DisplaySettings target = cur;
    const auto overlay = [&target](const std::string& k, const std::string& v) {
        if (k == "enhanced") {
            target.enhanced = v == "true" || v == "1";
        } else if (k == "hd_profile") {
            target.hd_profile = v;
        } else if (k == "render_scale") {
            try { target.render_scale = std::stoi(v); } catch (...) {}
        }
    };
    for (const auto& [k, v] : staged) overlay(k, v);
    overlay(key, new_value);   // this key last (it may or may not be staged yet)
    const int now = hd_scale_for(cur.enhanced, cur.hd_profile, cur.render_scale);
    const int next =
        hd_scale_for(target.enhanced, target.hd_profile, target.render_scale);
    if (next != now) return ApplyTier::Reinit;
    return classify_change(key, new_value, cur);
}

void apply_preset(MenuBindings& bind, const std::string& preset) {
    const bool hd = preset == "hd" || preset == "hd-43";
    // Master flag first (see header note on stage order).  "true"/"false"
    // matches the config-file convention for this key.
    bind.set("enhanced", hd ? "true" : "false");
    static const char* kFlags[] = {
        "enhance.smooth_motion", "enhance.hd_text",      "enhance.hud_overlay",
        "enhance.cinematic_cue", "enhance.fluid_bubbles",
        "enhance.secret_slide",  "enhance.descent_pan"};
    for (const char* f : kFlags) bind.set(f, hd ? "1" : "0");
    if (hd) {
        bind.set("hd_profile", "omniscale");
        bind.set("render_scale", "4");
        bind.set("aspect", preset == "hd" ? "widescreen" : "4:3");
    } else {
        bind.set("aspect", "keep");
    }
}

std::vector<std::pair<std::string, std::string>> encode_enhance_persist(
    const std::map<std::string, std::string>& mem,
    const std::vector<StagedChange>& staged, const std::string& draining_key) {
    const auto is_enhance = [](const std::string& k) {
        return k.rfind("enhance.", 0) == 0;
    };
    // Flush on the first enhance.* key of the set only — the list write
    // below already covers every flag.
    bool master_staged = false;
    const std::string* first_enhance = nullptr;
    for (const StagedChange& ch : staged) {
        if (ch.key == "enhanced") master_staged = true;
        if (!first_enhance && is_enhance(ch.key)) first_enhance = &ch.key;
    }
    if (first_enhance && *first_enhance != draining_key) return {};
    std::string list;
    for (const auto& [k, val] : mem) {
        if (!is_enhance(k) || val != "1") continue;
        std::string tok = k.substr(8);
        for (char& c : tok) if (c == '_') c = '-';
        if (!list.empty()) list += ',';
        list += tok;
    }
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back("enhance", list);
    if (!master_staged) out.emplace_back("enhanced", "false");
    return out;
}

}  // namespace olduvai::presentation
