// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/settings_apply.hpp"

#include <string>

#include "presentation/env_num.hpp"
#include "presentation/menu/profile_table.hpp"

namespace olduvai::presentation {

bool hd_active(bool enhanced, const std::string& hd_profile) {
    return enhanced && hd_profile != "native";
}

int hd_scale_for(bool enhanced, const std::string& hd_profile, int render_scale) {
    // The largest scale that fits the output.  3 matters: on a 1280x720
    // handheld (356x200 logical widescreen) x2 is a soft 1.8x stretch, x4
    // renders 1424x800 and throws pixels away, x3 (1068x600) is a 1.2x stretch.
    // smooth has a real scale3x; eagle and mmpx fall back to it with a note.
    if (!hd_active(enhanced, hd_profile)) return 1;
    if (render_scale < 2) return 2;
    if (render_scale > 4) return 4;
    return render_scale;
}

ApplyTier classify_change(const std::string& key, const std::string& new_value,
                          const DisplaySettings& cur) {
    if (key == "music_volume" || key == "sfx_volume" || key == "fullscreen" ||
        key == "aspect")
        return ApplyTier::Live;

    // The pad and keyboard mappings: applied with the Apply (StagingBindings
    // persist hook).
    if (key.rfind("pad_", 0) == 0 || key.rfind("key_", 0) == 0)
        return ApplyTier::Live;

    // Smooth-present keys: the persist hook feeds them to the pacing config
    // every frame loop reads at its start; live.
    if (key == "smooth_subframes" || key == "smooth_vsync")
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
        // `rs` is left untouched when parse_int fails.
        int rs = cur.render_scale;
        if (!parse_int(new_value, rs)) return ApplyTier::PersistOnly;
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



bool set_display_key(DisplaySettings& d, const std::string& key,
                     const std::string& value) {
    if (key == "enhanced") {
        d.enhanced = value == "true" || value == "1";
    } else if (key == "render_scale") {
        parse_int(value, d.render_scale);   // untouched on a malformed value
    } else if (key == "hd_profile") {
        d.hd_profile = value;
    } else if (key == "music_device") {
        d.music_device = value;
    } else if (key == "sfx_backend") {
        d.sfx_backend = value;
    } else {
        return false;
    }
    return true;
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
        set_display_key(target, k, v);
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
    // The bundle comes from the profile table within the session's family
    // ("profile_family"; empty or unknown = desktop), so a handheld's Enhanced
    // is its own member.
    const ProfileDef p = resolve_preset(bind.get("profile_family"), preset);
    const bool enhanced = p.role == ProfileRole::Enhanced;
    for (std::size_t i = 0; i < p.pin_count; ++i) {
        const std::string key = p.pins[i].key;
        const std::string value = p.pins[i].value;
        if (key == "aspect") {
            // Style sets the mode.  A deliberate 4:3 or stretch survives the
            // switch to Enhanced; "keep" and "" (unseeded) do not count as
            // deliberate.  Classic always returns to keep.
            const std::string cur = bind.get("aspect");
            if (!enhanced || cur.empty() || cur == "keep")
                bind.set("aspect", value);
            continue;
        }
        // Classic stages only `enhanced` (and aspect, above): its other pins
        // are inert while enhanced=false, and staging them would add spurious
        // Reinit rows.
        if (!enhanced && key != "enhanced") continue;
        bind.set(key, value);
    }
}

}  // namespace olduvai::presentation
