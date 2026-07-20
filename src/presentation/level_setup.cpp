// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-level asset loading / screen binding / static composition.
// Moved verbatim from game_app.cpp (CC2a-2) — see level_setup.hpp.

#include "presentation/level_setup.hpp"

#include "presentation/game_app.hpp"

#include "presentation/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "core/game_tables.hpp"
#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/cache_paths.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_files.hpp"
#include "presentation/debug_overlay.hpp"
#include "presentation/game_render.hpp"
#include "presentation/level_state.hpp"
#include "presentation/tile_patterns.hpp"
#include "presentation/hud_render.hpp"
#include "presentation/dialog_key_map.hpp"
#include "presentation/l3_end_level.hpp"
#include "presentation/menu.hpp"
#include "presentation/menu_model.hpp"
#include "presentation/banner_fx.hpp"
#include "presentation/menu_render.hpp"
#include "presentation/save_state.hpp"
#include "presentation/replay.hpp"
#include "presentation/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/bug_capture.hpp"
#include "presentation/screen_tiles.hpp"
#include "presentation/screens.hpp"
#include "presentation/smooth_present.hpp"
#include "presentation/text_overlay.hpp"
#include "presentation/title_menu_flow.hpp"
#include "presentation/transition_players.hpp"
#include "presentation/widescreen_presenter.hpp"
#include "presentation/widescreen.hpp"
#include "presentation/window_util.hpp"
#include "systems/frame_runner.hpp"
#include "systems/screen_topology.hpp"
#include "systems/spawning.hpp"
#include "presentation/opl_sfx.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <functional>
#include <map>
#include <optional>

#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/confirm_dialog.hpp"
#include "presentation/settings_apply.hpp"
#include "presentation/settings_flow.hpp"
#include "presentation/settings_session.hpp"
#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/upscale.hpp"
#include "formats/mdi.hpp"
#include "formats/voc.hpp"
#include "systems/cave_logic.hpp"
#include "systems/collision_dispatch.hpp"
#include "systems/fluid_bubbles.hpp"
#include "systems/monster_ai.hpp"
#include "systems/secret.hpp"
#include "systems/transitions.hpp"


namespace olduvai::presentation {

using formats::CurArchive;

// File-local archive reader (mirrors game_app.cpp's private slurp).
namespace {
std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}
}  // namespace

// Level music files by internal id (boss levels share one).
void install_exe_game_data(const std::vector<std::uint8_t>& exe) {
    core::GameTables tables;
    tables.cave_sizes = prepare::read_cave_size_table(exe);
    tables.secret_scores = prepare::read_secret_score_table(exe);
    core::install_game_tables(tables);
    install_adlib_sfx_voices(prepare::read_adlib_sfx_voices(exe));
}

const char* level_music_name(int internal) {
    switch (internal) {
        case 1: return "RIK1.MDI";
        case 2: case 4: case 6: return "ROCKY.MDI";
        case 3: return "BOY16.MDI";
        case 5: return "RIK6.MDI";
        case 7: return "RIK8.MDI";
        default: return nullptr;
    }
}


void load_sfx_bank(SdlAudio& audio,
                   const std::function<const std::vector<std::uint8_t>*(
                       const std::string&)>& entry) {
    for (const auto& s : kSfxVocs) {
        if (const auto* d = entry(s.voc)) {
            const auto voc = formats::parse_voc(*d);
            if (voc.audio() != nullptr) audio.load_sfx(s.id, *voc.audio());
        }
    }
}


// Per-frame secret-room scenery: floor every 48 px at y=168, plus the
// random bubble scatter (the room render consumes the LCG every tick —
// faithful to the original's per-frame draw loop).
// draw_scatter=false (enhanced fluid-bubble mode): still roll the LCG every
// iteration (replay parity) but DON'T push the EXE scatter bubble tiles — the
// 60 persistent fluid bubbles REPLACE the scatter, matching the Python
// reference (`if state.fluid_bubbles_animation: draw_fluid_bubbles +
// floor` else `floor + scatter`).  Drawing both stacks two bubble systems.
void refresh_secret_tiles(Loaded& g, bool draw_scatter) {
    g.render.tiles.clear();
    for (int si = 0; si < 0x2710; si += 0x30) {
        const int spr = static_cast<int>(core::global_rng().next() % 2) + 0x11;
        const int dy = static_cast<int>(core::global_rng().next() % 0x86) + 0x0A;
        const int dx = static_cast<int>(core::global_rng().next() % 0x140);
        if (draw_scatter && dx < 320) g.render.tiles.push_back({spr, dx, dy});
        if (si < 320)
            g.render.tiles.push_back({1, si, systems::kSecretFloorY});
    }
}

int store_key(const Loaded& g, int screen) {
    if (screen >= 100 && g.state.secret_flag) return 2000 + (screen - 100);
    if (screen >= 100) return 1000 + (screen - 100);
    return screen;
}

// Save the live list back to its slot, then bind the new slot's list.
void bind_store(Loaded& g, int screen) {
    const int key = store_key(g, screen);
    if (g.bound_key >= 0) {
        g.store[g.bound_key] = std::move(g.state.entities);
    }
    auto it = g.store.find(key);
    g.state.entities = it != g.store.end()
                           ? std::move(it->second)
                           : std::vector<core::Entity>{};
    g.bound_key = key;
    // The rebound list is composed BEFORE any entity update — recompute
    // monster sprites from state so init placeholders / stale frames
    // don't leak into the first frame (which the transition slide now
    // shows for its whole duration).
    systems::refresh_entity_sprites_on_screen_bind(
        g.state.entities, g.state.l3a_phase_counter);
}

// (normalize_glider_water + l7_bridge_ceiling_to_wall live in
// presentation/screen_tiles.cpp now — build_screen_tiles applies both.)

// Assemble the shared pure-constructor inputs from the live session.  ONE
// builder for BOTH the bind path and the peek path — the whole point of
// OL-B2 is that the two can no longer drift apart at the call sites.
// `tile_sprites` is the caller's final atlas (surface tiles + GROT3 for L3):
// g.render.tile_sprites for bind_screen, the scratch ra.tile_sprites copy
// for the peek.
ScreenTileContext screen_tile_ctx(
    const Loaded& g, const std::vector<formats::Sprite>& tile_sprites) {
    ScreenTileContext ctx;
    ctx.level = g.config.internal_id;
    ctx.extend_top_backdrop = g.render.extend_top_backdrop;
    ctx.visual_background = g.config.visual_background;
    ctx.glider_water_y = g.glider_water_y;
    ctx.surface_tile_count = static_cast<int>(g.surface_tiles.size());
    ctx.level_tiles = &g.tiles;
    ctx.tile_sprites = &tile_sprites;
    return ctx;
}

void bind_screen(Loaded& g, int screen) {
    if (screen >= 100 && g.state.secret_flag) {   // secret room
        systems::setup_secret_collision(g.state);
        g.render.visual_background = false;
        g.render.bg_fill_index = 4;
        g.render.palette.assign(std::begin(kSecretPalette),
                                std::end(kSecretPalette));
        g.render.tile_sprites = g.surface_tiles;
        // Bubble scatter is generated by the per-frame render gate (the
        // entry frame included — secret_flag is already set when the
        // render runs); a bind-time refresh here would double-consume
        // the LCG on entry and fork the streams.
        //
        // Enhanced-mode: init fluid bubbles on first entry to this room.
        // fluid_bubbles_initialized persists across secret entries so the
        // bubble positions are continuous (not reset every entry), matching
        // the Python reference (cleared only in _setup_level).
        if (!g.fluid_bubbles_initialized) {
            g.fluid_bubbles.init();
            g.fluid_bubbles_initialized = true;
        }
        bind_store(g, screen);
        g.state.current_screen = screen;
        return;
    }
    g.render.bg_fill_index = -1;
    if (screen >= 100) {   // cave
        const int cave_idx = screen - 100;
        g.render.visual_background = false;
        g.render.palette.clear();
        const systems::CaveRgb* pal = systems::kCavePaletteL1;
        if (g.config.internal_id == 3) pal = systems::kCavePaletteL3;
        else if (g.config.internal_id == 5) pal = systems::kCavePaletteL5;
        else if (g.config.internal_id == 7) pal = systems::kCavePaletteL7;
        for (int pi = 0; pi < 16; ++pi) {
            g.render.palette.push_back(
                {static_cast<std::uint8_t>(pal[pi].r),
                 static_cast<std::uint8_t>(pal[pi].g),
                 static_cast<std::uint8_t>(pal[pi].b)});
        }
        g.render.tiles.clear();
        if (g.config.internal_id == 3) {
            // Dark-woods caves: the table-driven multi-platform layout.
            // Sprites < 30 draw from the combined surface tile list AND
            // stamp their collision shapes; >= 30 draw decorative pieces
            // from the cave MAT (appended after the 33 surface tiles).
            const auto& recs = ((cave_idx - 22) & 1) ? g.l3_caves.odd
                                                     : g.l3_caves.even;
            g.render.tile_sprites = g.surface_tiles;
            g.render.tile_sprites.insert(g.render.tile_sprites.end(),
                                         g.grot3.begin(), g.grot3.end());
            g.state.collision.clear();
            for (const auto& r : recs) {
                if (r.final_si >= 30) {
                    const int idx = 33 + (r.final_si - 30);
                    if (idx < static_cast<int>(g.render.tile_sprites.size()))
                        g.render.tiles.push_back({idx, r.x, r.y});
                } else {
                    const int dur_idx = r.final_si - 1;
                    if (dur_idx >= 0 &&
                        dur_idx < static_cast<int>(g.dur.tiles.size())) {
                        g.state.collision.stamp_tile(
                            g.dur.tiles[static_cast<std::size_t>(dur_idx)]
                                .segments, r.x, r.y);
                    }
                    g.render.tiles.push_back({dur_idx, r.x, r.y});
                }
            }
        } else if (g.config.internal_id == 7) {
            // L7 (Volcanic) caves use NO GROT file — the interior is tiled
            // from ELEML7.MAT (the surface tile sheet): sprite 29 stalactite
            // wall at y=87 + sprite 31 ceiling strip at y=50, stepping 64 px,
            // sprite 30 as the right-edge cap.  EXE FUN_2759_033a; matches the
            // Python renderer._cave_tiles_l7.  Without this the cave had no
            // background (only objects drew → black screen).
            systems::setup_cave_collision(g.state);
            g.render.tile_sprites = g.surface_tiles;   // ELEML7 sheet
            if (g.surface_tiles.size() > 31) {
                const int width =
                    (cave_idx >= 0 &&
                     cave_idx < static_cast<int>(core::game_tables().cave_sizes.size()))
                        ? core::game_tables().cave_sizes[static_cast<std::size_t>(cave_idx)]
                        : 0;
                int x = -16;
                const int limit = width - 32;
                for (; x <= limit; x += 64) {
                    g.render.tiles.push_back({29, x, 87});
                    g.render.tiles.push_back({31, x + 16, 50});
                }
                g.render.tiles.push_back({30, x, 87});   // cap
            }
        } else {
            systems::setup_cave_collision(g.state);
            g.render.tile_sprites = g.cave_tiles;
            if (g.cave_tiles.size() >= 2) {
                const int width =
                    (cave_idx >= 0 &&
                     cave_idx < static_cast<int>(core::game_tables().cave_sizes.size()))
                        ? core::game_tables().cave_sizes[static_cast<std::size_t>(
                              cave_idx)]
                        : 0;
                int x = -32;
                for (; x < width; x += 64) {
                    g.render.tiles.push_back({1, x, 50});
                }
                g.render.tiles.push_back({0, x, 50});   // cap
            }
        }
        bind_store(g, screen);
        g.state.current_screen = screen;
        return;
    }
    g.render.visual_background = g.config.visual_background;
    if (g.config.internal_id == 3) {
        // Inside-the-big-tree trunk (S10/S11): the EXE reaches these screens
        // ONLY via the cave-exit warp (S9 right edge), which loads the L3 CAVE
        // palette (brown wood at idx 8/9) and holds it until the S11->S12
        // boundary — the cave palette IS the inside-the-tree palette by design.
        // The green surface (main) palette renders the trunk teal (wrong).
        // Finding: l3_s10_s11_cave_palette_persists.md (capstone-verified;
        // matches the Python port, which the C port had regressed).
        if (screen == 10 || screen == 11) {
            g.render.palette.clear();
            for (int pi = 0; pi < 16; ++pi)
                g.render.palette.push_back(
                    {static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].r),
                     static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].g),
                     static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].b)});
        } else {
            g.render.palette.assign(std::begin(kL3Palette), std::end(kL3Palette));
        }
    } else {
        g.render.palette = g.render.background.palette;
    }
    g.render.tile_sprites = g.surface_tiles;
    const int level = g.config.internal_id;
    // L3 surface screens: append GROT3.MAT sprites after the 33 surface tiles
    // so indices 33=GROT3[0] (body) and 34=GROT3[1] (cap) are addressable.
    // Matches the cave-screen convention (bind_screen cave path appends g.grot3
    // the same way).  Finding: l3_grot3_trunk_column_missing.md.
    if (level == 3 && !g.grot3.empty()) {
        g.render.tile_sprites.insert(g.render.tile_sprites.end(),
                                     g.grot3.begin(), g.grot3.end());
    }
    const int tile_screen = resolve_tile_screen(level, screen);

    // ── Collision side-effects (bind path ONLY — the pure constructor never
    // stamps; the read-only peek path must not touch the live bitmap). ──
    g.state.collision.clear();
    if (level == 7 && screen >= 10 && screen <= 12) {
        // EXE FUN_25b2_000c special-path floor for the L7 lava-cave-warp
        // area (screens 10-12; screen-range gate at capstone 0x0047-0x00ae).
        // Per iteration with si in {0,64,128,192,256,320} it stamps DUR
        // idx 29 — (dx=0, dy=81, width=64), raw-byte-verified in
        // LEVEL7.DUR — at (si, 79), laying a CONTINUOUS COLLISION floor at
        // Y=79+81=160 across X=0..319 (the si=320 stamp is bounds-clipped
        // to a no-op, mirroring Collision_SetPixel's x-bound check at
        // capstone 0x00f3-0x00f7).  idx 31 has no segments in LEVEL7.DUR →
        // those stamps are no-ops in the EXE too.
        //
        // The screen-9→10 warp (enter_cave marker, x=10 y=131) is the ONLY
        // way onto screen 10 (screen 9's right edge is x-clamped), and there
        // are no level-data tiles under x<48; without this collision floor
        // the player drops straight through the lava to a y>180 death.  The
        // collision bitmap is 320-wide and built identically in classic +
        // widescreen, so this restores EXE fidelity in both modes (it is a
        // faithfulness fix, not a widescreen change).
        // Mirrors the Python reference.
        for (int si : {0, 64, 128, 192, 256, 320}) {
            if (29 < static_cast<int>(g.dur.tiles.size()))
                g.state.collision.stamp_tile(g.dur.tiles[29].segments, si,
                                             79);
        }
    }
    if (tile_screen >= 0 &&
        tile_screen < static_cast<int>(g.tiles.screens.size())) {
        for (const auto& tp : g.tiles.screens[static_cast<std::size_t>(
                 tile_screen)].tiles) {
            const int idx = resolve_sprite_idx(level, tp.sprite_idx);
            if (idx < 0) continue;   // alias chain says skip (draw + collision)
            if (idx < static_cast<int>(g.dur.tiles.size())) {
                g.state.collision.stamp_tile(
                    g.dur.tiles[static_cast<std::size_t>(idx)].segments,
                    tp.x, tp.y);
            }
        }
    }

    // ── Render tile list: the shared pure constructor (screen_tiles.cpp) —
    // per-level backdrops, authored placements, L7 bridge, glider water,
    // HUD-band column extension.  Identical for bind + peek by construction.
    g.render.backdrop_tile_count = build_screen_tiles(
        screen_tile_ctx(g, g.render.tile_sprites), screen, g.render.tiles);
    bind_store(g, screen);
    g.state.current_screen = screen;
}

// Compose a SURFACE screen's background + terrain into a native 320×200
// FrameBuffer for the widescreen adjacent-screen peek (§8.7), WITHOUT entities
// and WITHOUT disturbing live `g`/RNG.
//
// Fidelity (the key point): this is strictly read-only w.r.t. the live session.
//   * It NEVER touches core::global_rng() — the surface tile list is built
//     deterministically from g.tiles (the static per-screen placement table),
//     not from refresh_secret_tiles (the only RNG-driven tile path, and it is
//     a secret-room concern excluded by widescreen_neighbors anyway).
//   * It NEVER mutates g.state, g.store, g.bound_key, g.render.tiles, or the
//     collision bitmap.  All neighbor work lands in caller-owned scratch
//     (`out`, a local LevelRenderAssets, a local SystemsState).
//   * The shared, already-loaded level assets (background, palette,
//     tile_sprites) are COPIED out of g.render by value into the scratch
//     LevelRenderAssets — g.render is observed, not changed.
//
// The tile-construction IS bind_screen's surface path — both call the shared
// pure build_screen_tiles (screen_tiles.cpp) with the same screen_tile_ctx
// inputs, so the margin reads identically to the center for any peek-enabled
// level.  Peek is enabled for internal 1/3/5/7 (widescreen.cpp
// level_supports_peek; warp-seam neighbours are suppressed there).
void build_surface_screen_assets(const Loaded& g, int screen,
                                        presentation::LevelRenderAssets& ra,
                                        systems::SystemsState& st) {
    const int level = g.config.internal_id;
    // Peek-neighbour frames carry the same top-HUD-band backdrop treatment as
    // the live screen, so a widescreen margin peeking this neighbour shows the
    // backdrop (not a black strip) behind the HUD line.
    ra.extend_top_backdrop = g.render.extend_top_backdrop;

    // Scratch assets: copy the shared, immutable level render data.  The
    // HUD-erased background (use_hd_text path clears rows 0-8 of
    // g.render.background ONCE at setup) is reused as-is, so no neighbor HUD
    // bleeds into the margin.
    ra.background = g.render.background;
    ra.visual_background = g.config.visual_background;
    // Match bind_screen: the trunk-interior screens (S10/S11) use the L3 CAVE
    // palette (brown), the rest of L3 the surface (green) palette — so a peeked
    // trunk neighbour is brown, not teal.  (See l3_s10_s11_cave_palette_persists.)
    if (g.config.internal_id == 3) {
        const bool trunk = (screen == 10 || screen == 11);
        ra.palette.clear();
        if (trunk)
            for (int pi = 0; pi < 16; ++pi)
                ra.palette.push_back(
                    {static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].r),
                     static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].g),
                     static_cast<std::uint8_t>(systems::kCavePaletteL3[pi].b)});
        else
            ra.palette.assign(std::begin(kL3Palette), std::end(kL3Palette));
    } else {
        ra.palette = g.render.background.palette;
    }
    ra.tile_sprites = g.surface_tiles;
    // Entity sprite atlas (LxSPR.MAT) is level-wide, not per-screen — needed so
    // the static-object peek (below) actually draws (draw_entities skips any
    // entity whose sprite index is out of the atlas; the old tiles-only peek
    // left this empty).
    ra.entity_sprites = g.render.entity_sprites;
    if (level == 3 && !g.grot3.empty()) {
        ra.tile_sprites.insert(ra.tile_sprites.end(), g.grot3.begin(),
                               g.grot3.end());
    }
    ra.bg_fill_index = g.render.bg_fill_index;
    // hud_strip intentionally left empty — the wide compositor only peeks the
    // margins of this buffer and the caller passes hud_rows so the HUD band is
    // excluded; the HUD-erased background already carries no baked labels.

    // Tile list: the SAME shared pure constructor bind_screen uses, fed from
    // the same screen_tile_ctx builder — backdrops, placements, L7 bridge,
    // glider-water normalisation and column extension included.  Read-only on
    // g by the constructor's purity contract; the bind path's collision
    // stamping is a call-site side-effect there and never runs here.
    ra.backdrop_tile_count = build_screen_tiles(
        screen_tile_ctx(g, ra.tile_sprites), screen, ra.tiles);

    // Scratch state: empty entities, no player, surface render mode.  Only the
    // fields compose_frame reads (current_level, current_screen, the
    // cave/secret flags = 0) matter; everything else is default-constructed.
    st.current_level = level;
    st.current_screen = screen;
    st.player.sprite = -1;   // suppress the player (draw_player=false too)
    // Enhanced widescreen peek (Option A): also show the neighbour screen's
    // STATIC-class objects — fixed-position scenery + collectibles that read
    // correctly FROZEN (stairs, springs, food/eggs, cave entrances/signs, vines,
    // breakable rocks, animated food).  Read straight from the already-pre-
    // spawned per-screen store (g.store[screen], populated once at level load) —
    // so this adds NO simulation and NO RNG draws (draw_entities is RNG-free; the
    // entities already exist).  Dynamic monsters / fish / birds / projectiles are
    // deliberately EXCLUDED: frozen they'd look wrong, and their real spawn on
    // entry re-rolls position so a peeked one wouldn't match.  g.store is live-
    // updated (bind_store moves the live list back on leave), so a collected food
    // does NOT reappear in the peek.  Only the margin columns of this buffer are
    // sampled by the compositor, so only objects near the shared edge show.
    // store_key(screen) == screen for surface screens (<100); peeks are always
    // surface neighbours, so a plain find(screen) is the right key.
    auto is_static_peek_obj = [](core::ObjType t) {
        switch (t) {
            case core::ObjType::Stairs:
            case core::ObjType::Peak:
            case core::ObjType::Egg:
            case core::ObjType::Rock:   // placed decorative rock (NOT the
                                        // dynamic rolling-stone hazard, which
                                        // is stone_state, not an entity)
            case core::ObjType::SecretFood:
            case core::ObjType::CaveEntrance:
            case core::ObjType::FoodCave:
            case core::ObjType::CaveSign:
            case core::ObjType::AnimatedFoodL3:
            case core::ObjType::VineL3:
            case core::ObjType::BreakableRockL3:
            case core::ObjType::PeakL7:
                return true;
            default:
                return false;
        }
    };
    if (auto sit = g.store.find(screen); sit != g.store.end())
        for (const auto& e : sit->second)
            if (e.active && is_static_peek_obj(e.obj_type))
                st.entities.push_back(e);
}

// Enemies in the peek (owner request 2026-07-04): still-alive shared-machine
// monsters at their SPAWN POSTS (init_x/init_y, heading sprite) — the position
// they materialize at on entry, so the margin PREDICTS what you'll meet (a
// frozen mid-walk pose would mismatch the respawn re-roll, the reason monsters
// were excluded before).  Permanently dead ones (no respawns left) stay hidden
// — the live-updated g.store knows.  Tier-1 living margins animate these
// per logic tick (walk cycle IN PLACE — no translation, no RNG, no collision:
// anchoring at the spawn post is what makes entry pop-free, since the reset
// materializes them exactly where the margin showed them).
std::vector<core::Entity> collect_spawn_post_monsters(const Loaded& g,
                                                             int screen) {
    auto is_peek_monster = [](core::ObjType t) {
        switch (t) {
            case core::ObjType::RedDino:
            case core::ObjType::YellowFuzz:
            case core::ObjType::BrownBear:
            case core::ObjType::GreenDino:
            case core::ObjType::MonsterL3A:
            case core::ObjType::MonsterL3B:
            case core::ObjType::MonsterL5A:
            case core::ObjType::MonsterL5B:
            case core::ObjType::MonsterL7A:
            case core::ObjType::MonsterL7B:
                return true;
            default:
                return false;
        }
    };
    std::vector<core::Entity> out;
    auto sit = g.store.find(screen);
    if (sit == g.store.end()) return out;
    for (const auto& e : sit->second) {
        if (!e.active || !is_peek_monster(e.obj_type)) continue;
        if (e.state == static_cast<int>(core::MonsterState::Dead) &&
            e.respawns <= 0)
            continue;
        core::Entity m = e;
        m.x = m.init_x;
        m.y = m.init_y;
        m.visible = true;
        m.state_counter = 0;
        m.sprite = m.spr_num +
                   (m.walk_offsets.empty() ? 0 : m.walk_offsets[0]);
        out.push_back(std::move(m));
    }
    return out;
}

// `out_ra` (optional): also hand the composed screen's resolved render assets
// to the caller — WidescreenPresenter::update_cache derives the seam-column
// tile lists
// from them, so the neighbour's assets are built ONCE per bind instead of
// twice (compose + a separate seam collection).
void compose_surface_screen_static(const Loaded& g, int screen,
                                   presentation::FrameBuffer& out,
                                   presentation::LevelRenderAssets* out_ra,
                                   // Optional UNDERLAY tiles (in THIS screen's
                                   // coordinates), inserted at the backdrop/
                                   // level split: they draw over the backdrop
                                   // but UNDER the screen's authored level
                                   // tiles.  Used to complete the ADJACENT
                                   // screen's seam-straddling tiles inside
                                   // this peek with correct z-order — S13's
                                   // rock (4,288,141) reaches 16px into S14,
                                   // where S14's dirt-top row (1,0,159) must
                                   // still draw over it.
                                   const std::vector<
                                       presentation::LevelRenderAssets::
                                           TileDraw>* underlay,
                                   // frozen_full: overlay the screen's stored
                                   // entity list VERBATIM (visible flags and
                                   // positions as-is) instead of the peek
                                   // treatment.  Used for the panorama pan's
                                   // OUTGOING slot: after the rebind the store
                                   // holds exactly the last live state, and
                                   // the EXE pans the last presented frame
                                   // with its sprites frozen in place
                                   // (Finding transition_pan_content_frozen_
                                   // sprites.md: WipeDown dst=[0x8bfa]+0x1f40
                                   // — the visible page is never touched).
                                   bool frozen_full,
                                   // peek_monsters: bake spawn-post monsters
                                   // into the static compose.  The STEADY
                                   // widescreen peek passes false — its
                                   // monsters are drawn live per frame by the
                                   // Tier-1 animated-margin overlay instead
                                   // (baked + live would double them).
                                   bool peek_monsters) {
    presentation::LevelRenderAssets ra;
    systems::SystemsState st;
    build_surface_screen_assets(g, screen, ra, st);
    if (peek_monsters && !frozen_full)
        for (auto& m : collect_spawn_post_monsters(g, screen))
            st.entities.push_back(std::move(m));
    if (frozen_full) {
        st.entities.clear();
        if (auto sit = g.store.find(screen); sit != g.store.end())
            for (const auto& e : sit->second)
                if (e.active) st.entities.push_back(e);
    }
    if (underlay != nullptr && !underlay->empty()) {
        const int at = std::max(0, ra.backdrop_tile_count);
        ra.tiles.insert(ra.tiles.begin() + at, underlay->begin(),
                        underlay->end());
        ra.backdrop_tile_count = at + static_cast<int>(underlay->size());
    }
    out = presentation::FrameBuffer{};   // native 320×200
    // Render-only: advance_state=false so the static-peek entity draw runs no
    // per-entity "advance" side effects.  (draw_entities makes no RNG calls and
    // st is a throwaway scratch copy, so g.state / g.store / the global RNG are
    // never perturbed — the compose_surface_screen_static contract holds.)
    presentation::RenderTarget rt{out.px.data(), out.w, out.h, 1, nullptr,
                                  nullptr};
    rt.advance_state = false;
    presentation::compose_frame(rt, st, ra, /*draw_player=*/false);
    if (out_ra != nullptr) *out_ra = std::move(ra);
}

// Compose the WIDE (320 + 2*margin) static background for an arbitrary surface
// screen the EXACT way the steady view does — compose_static_wide_bg_native:
// torus sky + mirror ground + the extended bg-tile rows (forest backdrop #31,
// dirt floor #1) re-drawn into the no-neighbour margin.  The panorama pan uses
// the OUTER margin of this to fill an off-level edge slot, so the pan's edge is
// pixel-identical to the static margin it hands off to (no 1-frame pop).
void compose_surface_screen_wide_native(
    const Loaded& g, int screen, int margin,
    const presentation::FrameBuffer* backdrop,
    std::vector<std::uint8_t>& wide) {
    presentation::LevelRenderAssets ra;
    systems::SystemsState st;
    build_surface_screen_assets(g, screen, ra, st);
    presentation::compose_static_wide_bg_native(
        st, ra, margin, /*left=*/nullptr, /*right=*/nullptr, backdrop, wide);
}

bool load_level_impl(const std::filesystem::path& dir, Loaded& g,
                     int internal_level, int start_screen) {
    const LevelConfig* cfg = nullptr;
    for (const auto& c : kLevels) {
        if (c.internal_id == internal_level) cfg = &c;
    }
    if (cfg == nullptr) return false;
    g.config = *cfg;
    // HISTORIK.EXE, or PREH.SQZ decoded (GOG / CD distributions).
    const auto exe = prepare::load_game_executable(dir);
    if (exe.empty()) return false;
    install_exe_game_data(exe);
    CurArchive fa(slurp(dir / "FILESA.CUR"));
    CurArchive fb(slurp(dir / "FILESB.CUR"));
    CurArchive va(slurp(dir / "FILESA.VGA"));
    CurArchive vb(slurp(dir / "FILESB.VGA"));

    auto entry_data = [&](const std::string& name)
        -> const std::vector<std::uint8_t>* {
        for (CurArchive* ar : {&fa, &fb, &va, &vb}) {
            if (ar->contains(name)) return &ar->get(name).data;
        }
        return nullptr;
    };

    const auto* font = entry_data("CHARSET1.MAT");
    const auto* spr = entry_data(g.config.sprite_mat);
    if (font == nullptr || spr == nullptr) return false;
    g.charset = formats::MatFile(*font, "CHARSET1.MAT").sprites();
    const auto* fond = (g.config.background_pc1 != nullptr)
                           ? entry_data(g.config.background_pc1) : nullptr;

    if (fond != nullptr) {
        g.render.background = formats::parse_pc1(*fond);
    }
    g.surface_tiles.clear();
    for (const char* mat : g.config.tile_mats) {
        if (mat == nullptr) continue;
        const auto* d = entry_data(mat);
        if (d == nullptr) return false;
        const auto sprites = formats::MatFile(*d, mat).sprites();
        g.surface_tiles.insert(g.surface_tiles.end(), sprites.begin(),
                               sprites.end());
    }
    if (g.config.grot_mat != nullptr) {
        if (const auto* grot = entry_data(g.config.grot_mat)) {
            g.cave_tiles = formats::MatFile(*grot, g.config.grot_mat)
                               .sprites();
        }
    }
    if (internal_level == 3) {
        g.l3_caves = prepare::read_l3_cave_tables(exe);
        if (const auto* g3 = entry_data("GROT3.MAT")) {
            g.grot3 = formats::MatFile(*g3, "GROT3.MAT").sprites();
        }
    }
    g.render.entity_sprites =
        formats::MatFile(*spr, g.config.sprite_mat).sprites();

    // HUD label strip: the level's own background bakes the labels for
    // visual-background levels; FOND7.PC1 (label-bar-only) supplies them
    // everywhere else (and for cave screens).
    if (const auto* f7 = entry_data("FOND7.PC1")) {
        const auto img = formats::parse_pc1(*f7);
        if (img.width == 320 && img.height >= 9) {
            g.render.hud_strip.assign(320 * 9 * 4, 0);
            const std::uint8_t key = img.pixels[0];   // (0,0) = colorkey
            for (int y = 0; y < 9; ++y) {
                for (int x = 0; x < 320; ++x) {
                    const std::uint8_t pi =
                        img.pixels[static_cast<std::size_t>(y) * 320 + x];
                    if (pi == key) continue;
                    const auto cc = (pi < img.palette.size())
                                        ? img.palette[pi] : formats::Rgb{};
                    const std::size_t off =
                        (static_cast<std::size_t>(y) * 320 + x) * 4;
                    g.render.hud_strip[off] = cc.r;
                    g.render.hud_strip[off + 1] = cc.g;
                    g.render.hud_strip[off + 2] = cc.b;
                    g.render.hud_strip[off + 3] = 255;
                }
            }
        }
    }

    g.tiles = prepare::read_tile_table(exe, internal_level);
    const auto* durd = entry_data(g.config.dur_file);
    if (durd == nullptr) return false;
    g.dur = formats::parse_dur(*durd);
    g.object_screens =
        prepare::read_object_table(exe, g.config.object_table_ds);
    g.cave_screens = prepare::read_object_table(exe, 0x29E2);
    g.secret_screens = prepare::read_object_table(exe, 0x2950);
    g.monster_tables = systems::MonsterTables::from_exe(exe);
    if (const auto* te = entry_data("THEEND.PC1")) {
        g.theend = formats::parse_pc1(*te);
    }

    // Reset enhanced-mode bubble state and HD asset cache for the new level.
    g.fluid_bubbles_initialized = false;
    g.hd_cache.clear();

    // NO reseed here: the EXE seeds the LCG once at static init (DS:0x87ac=1)
    // and never again — the Python oracle mirrors that (it seeds at boot
    // only).  A bootstrap-era reseed(1) at this site forked the whole-game
    // stream from the second surface level onward (2026-07-03 review F1).
    // The only legitimate reseed is the save-state restore in apply_save.
    // Populate the persistent store for every area of this level — the
    // once-per-level reset walk.
    for (std::size_t scr = 0; scr < g.object_screens.size(); ++scr) {
        g.store[static_cast<int>(scr)] = systems::spawn_screen_entities(
            g.object_screens[scr], g.monster_tables);
    }
    for (std::size_t ci = 0; ci < g.cave_screens.size(); ++ci) {
        g.store[1000 + static_cast<int>(ci)] = systems::spawn_screen_entities(
            g.cave_screens[ci], g.monster_tables, static_cast<int>(ci));
    }
    for (std::size_t si = 0; si < g.secret_screens.size(); ++si) {
        g.store[2000 + static_cast<int>(si)] =
            systems::spawn_screen_entities(g.secret_screens[si],
                                           g.monster_tables);
    }
    g.state.current_level = internal_level;
    // "GET READY !" banner counter — every surface level's init sets DS:0x97e0
    // = 0x11 (EXE Level1_InitGlobals 21f3:0000 + L3/L5/L7 equivalents; the
    // Python reference _setup_level state.get_ready_counter = 0x11).  Was only set by the
    // construction default → the banner showed on the first level but never on
    // subsequent ones; reset it per level so it shows on every level start.
    g.state.get_ready_counter = 0x11;
    // Full level-entry player reset — includes the 40-frame spawn
    // invulnerability (an EXE-confirmed write the spike on L1 screen 0
    // exists to be survived by).
    g.state.player.reset_for_level(g.config.spawn_x, g.config.spawn_y);
    // The original's loop pre-increments the frame counter, so the first
    // gameplay frame updates entities with fc=1 (parity-gated monster
    // stepping depends on this phase); the wrap then yields the same
    // 62-value cycle in both engines.
    g.state.frame_counter = 1;
    // --start-screen DEBUG jump: bind a non-zero surface screen at entry (clamped
    // to the level's screen count).  Suppress the GET READY banner — it belongs
    // to a real level start (screen 0) only.
    int entry_screen = 0;
    if (start_screen > 0) {
        const int last = static_cast<int>(g.tiles.screens.size()) - 1;
        entry_screen = last >= 0 ? std::min(start_screen, last) : 0;
        if (entry_screen > 0) g.state.get_ready_counter = 0;
    }
    bind_screen(g, entry_screen);
    return true;
}

// Corrupt/truncated game files throw from the format parsers (CurError,
// LzssError, MatError, Pc1Error…).  Catch here so the caller prints ONE
// clean "could not load" message instead of std::terminate with no context
// — the filename-presence preflight can't catch a bad-content file.
bool load_level(const std::filesystem::path& dir, Loaded& g,
                int internal_level, int start_screen) {
    try {
        return load_level_impl(dir, g, internal_level, start_screen);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "game: failed to parse game files in %s: %s\n",
                     dir.string().c_str(), e.what());
        return false;
    }
}

}  // namespace olduvai::presentation
