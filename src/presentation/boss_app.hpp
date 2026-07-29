// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Boss arena presentation: the shared player/HUD render and the per-boss
// fight runners.  Arena background is the ring picture with the baked
// boss-energy pip bar (columns 272..316) drained column-by-column as the
// boss health drops.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "presentation/enhance_flags.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation { class SdlAudio; }

namespace olduvai::presentation {

// Reinit-class display/audio Options the player Applied in the boss pause menu.
// The boss has no mid-fight reinit path, so these are handed back to run_game,
// which applies them to rt and rebuilds the display/audio pipeline when the
// fight ends — the next level (or a Restart Fight) uses them ("after the
// fight").  Seeded from the fight-entry settings; Applied changes override.
// Meaningful mainly in enhanced/HD sessions: in classic mode hd_profile /
// render_scale are inert (scale stays 1) so no rebuild results.
struct BossReinit {
    bool enhanced = false;
    int render_scale = 2;
    std::string hd_profile = "native";
    std::string music_device = "auto";
    std::string sfx_backend = "auto";
    std::string aspect = "keep";
};

struct BossRunResult {
    bool survived = false;
    int lives = 3;
    long score = 0;
    bool quit = false;
    bool restart = false;        // Pause -> Restart Fight (redo this level)
    bool quit_program = false;   // Pause -> Quit to Desktop
    // Set iff a reinit-class display/audio Option (or aspect) was Applied in
    // the boss pause; run_game applies it after the fight (see above).
    std::optional<BossReinit> reinit;
};

// Run a boss fight (display levels 2/4/6 → internal 2/4/6).
// `max_frames` > 0 exits after N frames; `shot`/`shot_frame` dump a BMP.
struct BossEnhanceOptions {
    bool enhanced = false;    // HD-render substrate active (any feature / hd_profile)
    EnhanceFlags flags;       // what survived the collapse: smooth_motion only
    std::string hd_profile;   // "" native | "omniscale" | "mmpx"
    int render_scale = 2;
    // --hd-font vector face file (default Freckle; "NotoSans-Regular.ttf"
    // for --hd-font noto).  Threaded to HdText::load for the boss HUD/tally.
    std::string hd_font = "FreckleFace-Regular.ttf";
    // Aspect chosen on the title/surface ("keep" | "4:3" | "stretch").  The
    // boss has no in-game aspect UI; it just renders with the session's choice.
    std::string aspect = "keep";
    bool vga_scan = false;    // classic hold-frame re-present (see GameOptions)
    // Session audio devices (OL-B6): the boss has no device machinery of its
    // own — these are display + tier-classification baselines for the pause
    // Options menu (a device change staged mid-fight is persist-only and
    // applies after the fight).
    std::string music_device = "auto";
    std::string sfx_backend = "auto";
    // Persist a (config-key, value) pair to play.json — INJECTED from the app
    // layer (which owns config I/O; presentation must not include app/), same
    // contract as GameOptions::persist.  No-op if unset.
    std::function<void(const std::string&, const std::string&)> persist;
};

// `replay_path`/`trace_path`: scripted-input replay + frame-trace JSONL
// (the cross-engine harness convention used by run_platform_level).
// `record_inputs_path`: capture live boss inputs as replay-schema JSONL,
// re-playable via `replay_path` (records at the frame the boss reader
// resolves, `at(frame)`, so a record→replay round-trips).
BossRunResult run_boss_level(const std::filesystem::path& game_dir,
                             int internal_level, int lives, long score,
                             const ScaledWindow& sw,
                             int max_frames = -1,
                             const std::string& shot = "",
                             int shot_frame = 1,
                             SdlAudio* audio = nullptr,
                             const BossEnhanceOptions& enhance = {},
                             const std::string& replay_path = "",
                             const std::string& trace_path = "",
                             const std::string& record_inputs_path = "");

}  // namespace olduvai::presentation
