// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/diag/bug_capture.hpp"

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

#include "core/build_id.hpp"
#include "core/types.hpp"
#include "enhance/parallel_rows.hpp"
#include "presentation/diag/debug_overlay.hpp"
#include "presentation/image_out.hpp"
#include "presentation/window_util.hpp"   // window_fullscreen

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

// Local time, formatted.  The strftime format attribute makes GCC/Clang treat
// `fmt` as a format string and check the callers' literals (-Wformat=2).
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(strftime, 1, 0)))
#endif
std::string local_time(const char* fmt) {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    // -Wformat-nonliteral still fires on the forwarded parameter; scoped out
    // here. Both callers pass literals.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    std::strftime(buf, sizeof buf, fmt, &tmv);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    return std::string(buf);
}

std::string timestamp_dir() { return local_time("%Y-%m-%d_%H%M%S"); }

std::string timestamp_iso() { return local_time("%Y-%m-%dT%H:%M:%S"); }

// Save a FrameBuffer (RGBA32, w*4 pitch) as PNG.
bool save_fb_png(const FrameBuffer& fb, const std::string& path) {
    return save_rgba_image(fb.px.data(), fb.w, fb.h, path);
}

// Active entities sorted by (obj_type, init_x, init_y, x, y), as
// state_dump._serialize_entities.  stable_sort: the reference's sort is stable,
// so ties keep their state.entities order in both engines.
std::vector<const core::Entity*> sorted_active_entities(
    const systems::SystemsState& state) {
    std::vector<const core::Entity*> rows;
    for (const auto& e : state.entities) {
        if (!e.active) continue;
        rows.push_back(&e);
    }
    // Orders by values read through the pointers, not addresses.
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
    std::stable_sort(rows.begin(), rows.end(),
              [](const core::Entity* a, const core::Entity* b) {
                  auto key = [](const core::Entity* e) {
                      return std::make_tuple(static_cast<int>(e->obj_type),
                                             e->init_x, e->init_y, e->x, e->y);
                  };
                  return key(a) < key(b);
              });
    return rows;
}


// The header block and the state summary; a boss arena gets its own table.
void md_summary(std::ostream& f, const systems::SystemsState& state,
                std::size_t n_entities, int display_level,
                int internal_level, const std::string& captured_at,
                const BugAnnotations& ann, const BossInfo& boss) {
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
    f << "**Engine:** olduvai " << OLDUVAI_VERSION << " ("
      << olduvai::build_id() << ")\n";
    f << "**Tag:** " << (ann.tag.empty() ? "f5" : ann.tag) << "\n";
    f << "**Reproducibility:** "
      << (ann.reproducibility.empty() ? "unknown" : ann.reproducibility)
      << "\n\n";
    f << "## State summary\n\n";
    f << "| Field | Value |\n";
    f << "|-------|-------|\n";
    f << "| Display level | " << display_level << " |\n";
    f << "| Internal level | " << internal_level << " |\n";
    if (boss.supplied) {
        // A boss arena has no screens, caves, food, timer or entity table;
        // the synthesised snapshot would print zeros that read as data.
        const char* na = "n/a (boss arena)";
        f << "| Screen | " << na << " |\n";
        f << "| Player position | (" << p.x << ", " << p.y << ") |\n";
        f << "| Cave / secret | " << na << " |\n";
        f << "| Lives | " << p.lives << " |\n";
        f << "| Food | " << na << " |\n";
        f << "| Score | " << state.score << " |\n";
        f << "| Timer | " << na << " |\n";
        f << "| Frame counter | " << na << " |\n";
        f << "| Active entities | " << na << " |\n\n";
        f << "## Boss fight\n\n";
        f << "| Field | Value |\n|-------|-------|\n";
        f << "| Boss health | " << boss.health << " (won at 272) |\n";
        f << "| Phase | " << (boss.phase.empty() ? "-" : boss.phase)
          << " |\n";
        f << "| Fight frame | " << boss.frame << " |\n\n";
    } else {
        f << "| Screen | " << state.current_screen << " |\n";
        f << "| Player position | (" << p.x << ", " << p.y << ") |\n";
        f << "| Cave / secret | " << cave_str << " / " << secret_str << " |\n";
        f << "| Energy / Lives | " << p.energy << " / " << p.lives << " |\n";
        f << "| Food | " << state.food_count << " / 45 |\n";
        f << "| Score | " << state.score << " |\n";
        f << "| Timer | " << state.timer << " |\n";
        f << "| Frame counter | " << state.frame_counter << " |\n";
        f << "| Active entities | " << n_entities << " |\n";
        f << "| God mode | " << (state.god_mode ? "yes" : "no") << " |\n\n";
    }

}

// Display / present path: a visual report is not actionable without it.
void md_display(std::ostream& f, const DisplayInfo& display) {
    if (!display.supplied) return;
    f << "## Display\n\n";
    f << "| Field | Value |\n|-------|-------|\n";
    f << "| Renderer output | " << display.out_w << "x" << display.out_h
      << " (" << (display.out_h > 0
                      ? static_cast<double>(display.out_w) / display.out_h
                      : 0.0)
      << ":1) |\n";
    f << "| Logical size | "
      << (display.logical_w == 0 && display.logical_h == 0
              ? std::string("none (1:1)")
              : std::to_string(display.logical_w) + "x" +
                    std::to_string(display.logical_h))
      << " |\n";
    f << "| Fullscreen | " << (display.fullscreen ? "yes" : "no") << " |\n";
    f << "| HD | " << (display.hd ? "yes" : "no") << " (scale "
      << display.hd_scale << ") |\n";
    f << "| Aspect setting | " << (display.aspect.empty() ? "(unset)"
                                                          : display.aspect)
      << " |\n";
    f << "| Widescreen active | " << (display.ws_active ? "YES" : "NO")
      << " |\n";
    // Margin is only computed for aspect "widescreen", so say which reason
    // 0 has.
    f << "| Widescreen margin | " << display.ws_margin;
    if (display.ws_margin == 0) {
        if (display.aspect != "widescreen")
            f << "  (0 = widescreen not selected; Aspect is \""
              << (display.aspect.empty() ? "(unset)" : display.aspect)
              << "\")";
        else if (!display.hd)
            f << "  (0 = widescreen needs enhanced/HD mode)";
        else
            f << "  (0 = display not wider than 16:10)";
    }
    f << " |\n";
    f << "| Wide native width | " << display.ws_native_w << " |\n";
    f << "| Upscale threads | " << display.upscale_threads << " |\n\n";
}

void md_screenshots(std::ostream& f, bool has_presented, bool boss) {
    f << "## Screenshots\n\n";
    // The presented shot first (what the player saw); the rest are native
    // 320x200 analysis layers.
    if (has_presented)
        f << "- ![as seen (HD/widescreen)](screenshot_presented.png)\n";
    f << "- ![game (native)](screenshot.png)\n";
    if (!boss) {
        f << "- ![collision overlay](screenshot_collision.png)\n";
        f << "- ![entity overlay](screenshot_entities.png)\n";
    }
    f << "\n";
}

void md_description(std::ostream& f, const BugAnnotations& ann) {
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
}

void md_suspects(std::ostream& f, const systems::SystemsState& state,
                 int internal_level) {
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
}

void md_entities(std::ostream& f,
                 const std::vector<const core::Entity*>& ents) {
    f << "## Active entities\n\n";
    f << "| obj_type | name | pos | state | visible |\n";
    f << "|----------|------|-----|-------|---------|\n";
    const std::size_t shown = std::min<std::size_t>(ents.size(), 20);
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
}

void md_footer(std::ostream& f) {
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

void write_report_md(const fs::path& path, const BugReport& r,
                     const std::vector<const core::Entity*>& ents,
                     const std::string& captured_at) {
    std::ofstream f(path);
    if (!f) return;
    md_summary(f, r.state, ents.size(), r.display_level, r.internal_level,
               captured_at, r.ann, r.boss);
    md_display(f, r.display);
    md_screenshots(f, r.has_presented, r.boss.supplied);
    md_description(f, r.ann);
    md_suspects(f, r.state, r.internal_level);
    md_entities(f, ents);
    md_footer(f);
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

std::string write_bug_report(const BugReport& r) {
    const systems::SystemsState& state = r.state;
    const FrameBuffer& base_frame = r.frame;
    const std::string ts = timestamp_dir();
    const std::string iso = timestamp_iso();

    fs::path root = fs::path(bug_report_root()) /
                    (ts + "_L" + std::to_string(r.display_level) + "_S" +
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

    // The overlays scale native coordinates by `scale`, which must match the
    // frame's resolution (base_frame is 320 wide), not the HD render scale.
    const int shot_scale = std::max(1, base_frame.w / 320);

    // 1. Clean gameplay frame.
    save_fb_png(base_frame, (root / "screenshot.png").string());

    // 2-3. The collision and entity overlays read the platform level's
    // bitmap and entity table; a boss arena has neither, so they are skipped.
    if (!r.boss.supplied) {
        // 2. Collision overlay (on a copy so the live frame is untouched).
        {
            FrameBuffer copy = base_frame;
            draw_debug_collision(copy, state, shot_scale);
            save_fb_png(copy, (root / "screenshot_collision.png").string());
        }
        // 3. Entity overlay.
        {
            FrameBuffer copy = base_frame;
            draw_debug_entities(copy, state, r.entity_sprites, shot_scale);
            save_fb_png(copy, (root / "screenshot_entities.png").string());
        }
    }

    write_report_md(root / "report.md", r, ents, iso);

    std::printf("bug report: %s\n", root.string().c_str());
    std::fflush(stdout);
    return root.string();
}

void write_bug_report_as_shown(const BugReport& r, SDL_Renderer* ren,
                               const std::function<void()>& redraw) {
    const std::string dir = write_bug_report(r);
    if (dir.empty() || !r.has_presented) return;
    redraw();
    capture_renderer_output(ren, dir + "/screenshot_presented.png");
}

DisplayInfo read_display_info(SDL_Renderer* ren, SDL_Window* win) {
    DisplayInfo di;
    di.supplied = true;
    SDL_GetRendererOutputSize(ren, &di.out_w, &di.out_h);
    SDL_RenderGetLogicalSize(ren, &di.logical_w, &di.logical_h);
    if (win != nullptr) di.fullscreen = window_fullscreen(win);
    di.upscale_threads = enhance::parallel_row_threads();
    return di;
}

}  // namespace olduvai::presentation
