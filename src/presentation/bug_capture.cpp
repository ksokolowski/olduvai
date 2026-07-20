// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/bug_capture.hpp"

#include <SDL.h>

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "core/types.hpp"
#include "presentation/debug_overlay.hpp"
#include "presentation/image_out.hpp"

#ifndef OLDUVAI_VERSION
#define OLDUVAI_VERSION "0.0.0"
#endif

namespace olduvai::presentation {

namespace {

namespace fs = std::filesystem;

// ── ObjType → name (mirrors the Python reference's ObjType enum names) ──────
const char* obj_type_name(core::ObjType t) {
    switch (t) {
        case core::ObjType::Stairs:          return "Stairs";
        case core::ObjType::Peak:            return "Peak";
        case core::ObjType::Egg:             return "Egg";
        case core::ObjType::Rock:            return "Rock";
        case core::ObjType::AncestorGhost:   return "AncestorGhost";
        case core::ObjType::HiddenFood:      return "HiddenFood";
        case core::ObjType::SecretFood:      return "SecretFood";
        case core::ObjType::Balloons:        return "Balloons";
        case core::ObjType::Fish:            return "Fish";
        case core::ObjType::RedDino:         return "RedDino";
        case core::ObjType::YellowFuzz:      return "YellowFuzz";
        case core::ObjType::BrownBear:       return "BrownBear";
        case core::ObjType::GreenDino:       return "GreenDino";
        case core::ObjType::Chimp:           return "Chimp";
        case core::ObjType::Bird:            return "Bird";
        case core::ObjType::Platform:        return "Platform";
        case core::ObjType::CaveEntrance:    return "CaveEntrance";
        case core::ObjType::Fire:            return "Fire";
        case core::ObjType::FoodCave:        return "FoodCave";
        case core::ObjType::CaveSign:        return "CaveSign";
        case core::ObjType::CaveSpider:      return "CaveSpider";
        case core::ObjType::CaveBat:         return "CaveBat";
        case core::ObjType::AnimatedFoodL3:  return "AnimatedFoodL3";
        case core::ObjType::VineL3:          return "VineL3";
        case core::ObjType::MonsterL3A:      return "MonsterL3A";
        case core::ObjType::MonsterL3B:      return "MonsterL3B";
        case core::ObjType::BreakableRockL3: return "BreakableRockL3";
        case core::ObjType::SnakeL3:         return "SnakeL3";
        case core::ObjType::ProjectileL3:    return "ProjectileL3";
        case core::ObjType::JumpingFishL5:   return "JumpingFishL5";
        case core::ObjType::ChimpL5:         return "ChimpL5";
        case core::ObjType::MonsterL5A:      return "MonsterL5A";
        case core::ObjType::MonsterL5B:      return "MonsterL5B";
        case core::ObjType::PteriyakiL7:     return "PteriyakiL7";
        case core::ObjType::ChimpL7:         return "ChimpL7";
        case core::ObjType::PeakL7:          return "PeakL7";
        case core::ObjType::MonsterL7A:      return "MonsterL7A";
        case core::ObjType::MonsterL7B:      return "MonsterL7B";
    }
    return "UNKNOWN";
}

// Suggested EXE main-loop function per internal level (mirrors Python
// bug_capture._suggest_exe_function main_funcs map).
const char* level_main_func(int internal) {
    switch (internal) {
        case 1: return "FUN_21f3_006f (Level1_Main)";
        case 2: return "FUN_23cf_0a20 (Level2_BossMain - T-Rex)";
        case 3: return "FUN_2276_06f2 (Level3_Main - Dark Woods, displays as L5)";
        case 4: return "FUN_24cc_02f2 (Level4_BossMain - Triceratops)";
        case 5: return "FUN_2361_006c (Level5_Main - Icy Land, displays as L3)";
        case 6: return "FUN_254f_02b5 (Level6_BossMain - Giant)";
        case 7: return "FUN_25b2_020b (Level7_Main - Volcanic)";
        default: return "(unknown)";
    }
}

std::string timestamp_dir() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H%M%S", &tmv);
    return std::string(buf);
}

std::string timestamp_iso() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tmv);
    return std::string(buf);
}

// Save a FrameBuffer (RGBA32, w*4 pitch) as PNG.
bool save_fb_png(const FrameBuffer& fb, const std::string& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<std::uint8_t*>(fb.px.data()), fb.w, fb.h, 32, fb.w * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) return false;
    const bool ok = save_surface_image(s, path);
    SDL_FreeSurface(s);
    return ok;
}

// Active entities, sorted deterministically by (obj_type, init_x, init_y, x, y)
// — matches state_dump._serialize_entities ordering.
std::vector<const core::Entity*> sorted_active_entities(
    const systems::SystemsState& state) {
    std::vector<const core::Entity*> rows;
    for (const auto& e : state.entities) {
        if (!e.active) continue;
        rows.push_back(&e);
    }
    std::sort(rows.begin(), rows.end(),
              [](const core::Entity* a, const core::Entity* b) {
                  auto key = [](const core::Entity* e) {
                      return std::make_tuple(static_cast<int>(e->obj_type),
                                             e->init_x, e->init_y, e->x, e->y);
                  };
                  return key(a) < key(b);
              });
    return rows;
}


void write_report_md(const fs::path& path,
                     const systems::SystemsState& state,
                     const std::vector<const core::Entity*>& ents,
                     int display_level, int internal_level,
                     const std::string& captured_at,
                     const BugAnnotations& ann, bool has_presented) {
    std::ofstream f(path);
    if (!f) return;
    const auto& p = state.player;
    const std::string cave_str =
        state.cave_flag ? ("cave_idx=" + std::to_string(state.cave_index))
                        : std::string("-");
    const std::string secret_str =
        state.secret_flag
            ? ("secret_idx=" + std::to_string(state.secret_index))
            : std::string("-");

    f << "# Bug report - L" << display_level << " (int." << internal_level
      << ") screen " << state.current_screen << " - f5\n\n";
    f << "**When:** " << captured_at << "\n";
    f << "**Engine:** olduvai " << OLDUVAI_VERSION << "\n";
    f << "**Tag:** " << (ann.tag.empty() ? "f5" : ann.tag) << "\n";
    f << "**Reproducibility:** "
      << (ann.reproducibility.empty() ? "unknown" : ann.reproducibility)
      << "\n\n";
    f << "## State summary\n\n";
    f << "| Field | Value |\n";
    f << "|-------|-------|\n";
    f << "| Display level | " << display_level << " |\n";
    f << "| Internal level | " << internal_level << " |\n";
    f << "| Screen | " << state.current_screen << " |\n";
    f << "| Player position | (" << p.x << ", " << p.y << ") |\n";
    f << "| Cave / secret | " << cave_str << " / " << secret_str << " |\n";
    f << "| Energy / Lives | " << p.energy << " / " << p.lives << " |\n";
    f << "| Food | " << state.food_count << " / 45 |\n";
    f << "| Score | " << state.score << " |\n";
    f << "| Timer | " << state.timer << " |\n";
    f << "| Frame counter | " << state.frame_counter << " |\n";
    f << "| Active entities | " << ents.size() << " |\n";
    f << "| God mode | " << (state.god_mode ? "yes" : "no") << " |\n\n";
    f << "## Screenshots\n\n";
    // Presented shot first when it exists — it is what the player actually saw
    // (HD upscale + widescreen + HUD).  The other three are native 320x200
    // analytical layers.
    if (has_presented)
        f << "- ![as seen (HD/widescreen)](screenshot_presented.png)\n";
    f << "- ![game (native)](screenshot.png)\n";
    f << "- ![collision overlay](screenshot_collision.png)\n";
    f << "- ![entity overlay](screenshot_entities.png)\n\n";
    f << "## What happened\n\n";
    if (!ann.description.empty())
        f << ann.description << "\n\n";
    else
        f << "(describe the bug - what you expected, what you saw, how to "
             "reproduce)\n\n";
    f << "## Reproducibility\n\n";
    const std::string rep = ann.reproducibility.empty() ? "unknown"
                                                        : ann.reproducibility;
    auto box = [&](const char* k) { return rep == k ? "[x]" : "[ ]"; };
    f << "- " << box("every")     << " Reproducible every time\n";
    f << "- " << box("sometimes") << " Sometimes\n";
    f << "- " << box("once")      << " Once-off\n";
    f << "- " << box("unknown")   << " Unknown\n\n";
    f << "## Suspect EXE function\n\n";
    f << "Suggested: `" << level_main_func(internal_level)
      << "` - the active level main loop.\n\n";
    f << "Other candidates by current state:\n";
    if (state.cave_flag)
        f << "- (cave handler - FUN_2759_* family)\n";
    if (state.secret_flag)
        f << "- (secret-area handler - TBD)\n";
    f << "- `FUN_2A04_0003` (Objects_Update - entity dispatcher)\n";
    f << "- `FUN_27f7_093d` (Monster_SharedStateMachine)\n\n";
    f << "## Active entities\n\n";
    f << "| obj_type | name | pos | state | visible |\n";
    f << "|----------|------|-----|-------|---------|\n";
    std::size_t shown = std::min<std::size_t>(ents.size(), 20);
    for (std::size_t i = 0; i < shown; ++i) {
        const core::Entity* e = ents[i];
        char hex[8];
        std::snprintf(hex, sizeof hex, "0x%02x", static_cast<int>(e->obj_type));
        f << "| " << hex << " | " << obj_type_name(e->obj_type) << " | ("
          << e->x << ", " << e->y << ") | " << e->state << " | "
          << (e->visible ? "yes" : "no") << " |\n";
    }
    if (ents.size() > 20) {
        f << "| ... | ... | ... | ... | ... |\n";
        f << "| (and " << (ents.size() - 20)
          << " more not shown) |\n";
    }
    f << "\n## Related findings\n\n";
    f << "(link any related notes or issues)\n\n";
    f << "## Resolution\n\n";
    f << "| Field | Value |\n";
    f << "|-------|-------|\n";
    f << "| Status | `open` |\n";
    f << "| Investigated by | (TBD) |\n";
    f << "| Root cause | (TBD) |\n";
    f << "| Fix commit | (TBD) |\n";
    f << "| Finding doc | (TBD) |\n";
}

std::string g_bug_report_dir;   // set_bug_report_dir(); "" = default

std::string home_dir() {
#if defined(_WIN32)
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return (h != nullptr && *h != '\0') ? std::string(h) : std::string();
}

std::string expand_tilde(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    if (p.size() > 1 && p[1] != '/' && p[1] != '\\') return p;  // ~user: no
    const std::string home = home_dir();
    if (home.empty()) return p;
    return home + p.substr(1);
}

}  // namespace

void set_bug_report_dir(const std::string& dir) {
    g_bug_report_dir = expand_tilde(dir);
}

std::string bug_report_root() {
    if (const char* env = std::getenv("OLDUVAI_BUG_DIR");
        env != nullptr && *env != '\0') {
        return expand_tilde(env);
    }
    if (!g_bug_report_dir.empty()) return g_bug_report_dir;
    const std::string home = home_dir();
    if (!home.empty())
        return (fs::path(home) / "olduvai" / "bug_reports").string();
    return "bug_reports";   // no resolvable home: last-resort cwd-relative
}

std::string write_bug_report(const systems::SystemsState& state,
                             const FrameBuffer& base_frame,
                             const std::vector<formats::Sprite>& entity_sprites,
                             int display_level, int internal_level,
                             int overlay_scale, const BugAnnotations& ann,
                             bool has_presented) {
    const std::string ts = timestamp_dir();
    const std::string iso = timestamp_iso();

    fs::path root = fs::path(bug_report_root()) /
                    (ts + "_L" + std::to_string(display_level) + "_S" +
                     std::to_string(state.current_screen));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        // resolved root not writable: fall back to the config dir, which the
        // save/config paths already resolve robustly.
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        const std::string cfg_base =
            (xdg != nullptr && *xdg != '\0')
                ? std::string(xdg)
                : std::string(home != nullptr ? home : ".") + "/.config";
        root = fs::path(cfg_base) / "olduvai" / "bug_reports" / root.filename();
        ec.clear();
        fs::create_directories(root, ec);
    }
    if (ec) {
        std::fprintf(stderr, "bug capture: could not create %s: %s\n",
                     root.string().c_str(), ec.message().c_str());
        return std::string();
    }

    const auto ents = sorted_active_entities(state);

    // The debug overlays scale native cell/entity coordinates by `scale`, so
    // `scale` MUST match the frame's actual resolution — NOT the game's HD
    // render scale.  base_frame is composed at native 320-wide, so an HD run
    // (overlay_scale=4) would push every marker off-canvas and the three
    // screenshots would come out identical (owner-reported).  Derive it.
    const int shot_scale = std::max(1, base_frame.w / 320);
    (void)overlay_scale;   // kept for the boss_app caller's signature parity

    // 1. Clean gameplay frame.
    save_fb_png(base_frame, (root / "screenshot.png").string());

    // 2. Collision overlay (on a copy so the live frame is untouched).
    {
        FrameBuffer copy = base_frame;
        draw_debug_collision(copy, state, shot_scale);
        save_fb_png(copy, (root / "screenshot_collision.png").string());
    }
    // 3. Entity overlay.
    {
        FrameBuffer copy = base_frame;
        draw_debug_entities(copy, state, entity_sprites, shot_scale);
        save_fb_png(copy, (root / "screenshot_entities.png").string());
    }

    write_report_md(root / "report.md", state, ents, display_level,
                    internal_level, iso, ann, has_presented);

    std::printf("bug report: %s\n", root.string().c_str());
    std::fflush(stdout);
    return root.string();
}

}  // namespace olduvai::presentation
