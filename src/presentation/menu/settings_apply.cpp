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
    // Scale 3 is reachable as of the handheld spike.  It was clamped away by a
    // `>= 4 ? 4 : 2`, which silently turned a request for 3 into 2.
    //
    // WHY IT MATTERS, measured on a TrimUI Smart Pro (1280x720 panel, 356x200
    // logical widescreen): x2 renders 712x400 and the display stretches it 1.8x
    // — non-integer and soft.  x4 renders 1424x800 and then throws pixels away
    // shrinking to fit.  x3 renders 1068x600, a 1.2x stretch: the closest fit
    // and the least wasted work.  `smooth` implements a genuine scale3x; eagle
    // and xbr fall back to it with a warning.
    //
    // The general rule this serves is "the largest scale that does not exceed
    // the output", which is a display-fit question rather than a handheld one:
    // the old fixed 4 overshoots any output below 1424x800, a small desktop
    // window included.
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

    // Smooth-present keys: the persist hook folds them into the pacing config
    // (smooth_config.hpp) that every frame loop reads at its start.  Only the
    // handheld Enhanced preset stages them, beside the enhanced flip whose
    // rebuild picks them up — live, not next-launch.
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
        // Pre-seeded per parse_int's contract (it leaves `rs` untouched on
        // failure).  The previous form initialised `rs` and then immediately
        // overwrote it via stoi, or returned without reading it — a dead store
        // either way — and used an exception for ordinary control flow.
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
            // A malformed staged value simply keeps the current render_scale:
            // the menu cannot produce one, and a stray play.json value should
            // not spam.  That policy is now the callee's contract — parse_int
            // leaves its target untouched on failure — rather than an empty
            // catch block that had to be explained and silenced.
            parse_int(v, target.render_scale);
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
    // The bundle comes from the profile table, resolved within the session's
    // family — seeded into the bindings as "profile_family" (empty or
    // unknown = desktop) — so a handheld's Enhanced is its own member, not
    // omniscale x4.
    const ProfileDef p = resolve_preset(bind.get("profile_family"), preset);
    const bool enhanced = p.role == ProfileRole::Enhanced;
    for (std::size_t i = 0; i < p.pin_count; ++i) {
        const std::string key = p.pins[i].key;
        const std::string value = p.pins[i].value;
        if (key == "aspect") {
            // Style sets the MODE.  A deliberate 4:3 or stretch chosen in
            // Video is a display setting and survives a switch to Enhanced
            // (what the separate hd-43 preset used to express).  "" counts as
            // not-deliberate alongside "keep": an unseeded binding has no
            // aspect, and treating that as a choice would drop the headline
            // feature.  Classic always returns to keep.
            const std::string cur = bind.get("aspect");
            if (!enhanced || cur.empty() || cur == "keep")
                bind.set("aspect", value);
            continue;
        }
        // Classic stages only the master flag (and aspect, above).  Its other
        // pins — hd_profile, enhance — are inert while enhanced=false
        // (hd_scale_for forces compose scale 1), and staging them would put
        // spurious Reinit rows in the confirm dialog.
        if (!enhanced && key != "enhanced") continue;
        bind.set(key, value);
    }
}

}  // namespace olduvai::presentation
