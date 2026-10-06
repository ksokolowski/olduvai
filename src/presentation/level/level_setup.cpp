// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-level asset loading, screen binding and static composition.

#include "presentation/level/level_setup.hpp"

#include "presentation/render/hd_warm.hpp"   // warm_hd_sprite_cache

#include "presentation/game_app.hpp"

#include "presentation/input/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "core/game_tables.hpp"
#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_archives.hpp"
#include "prepare/game_files.hpp"
#include "presentation/env_num.hpp"   // parse_int
#include "presentation/diag/debug_overlay.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/level/level_state.hpp"
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/hud_render.hpp"
#include "presentation/menu/dialog_key_map.hpp"
#include "presentation/sequence/l3_end_level.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/render/banner_fx.hpp"
#include "presentation/menu/menu_render.hpp"
#include "presentation/level/save_state.hpp"
#include "presentation/input/replay.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/render/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/diag/bug_capture.hpp"
#include "presentation/render/screen_tiles.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/render/smooth_present.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/title_menu_flow.hpp"
#include "presentation/sequence/transition_players.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/render/widescreen.hpp"
#include "presentation/window_util.hpp"
#include "systems/frame_runner.hpp"
#include "systems/screen_topology.hpp"
#include "systems/spawning.hpp"
#include "presentation/audio/opl_sfx.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <functional>
#include <map>
#include <optional>

#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/settings_apply.hpp"
#include "presentation/menu/settings_flow.hpp"
#include "presentation/menu/settings_session.hpp"
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
#include "systems/sprite_ids.hpp"          // is_monster — one truth table
#include "systems/transitions.hpp"


namespace olduvai::presentation {

// Level music files by internal id (boss levels share one).
void install_exe_game_data(const std::vector<std::uint8_t>& exe) {
    core::GameTables tables;
    tables.cave_sizes = prepare::read_cave_size_table(exe);
    tables.secret_scores = prepare::read_secret_score_table(exe);
    core::install_game_tables(tables);
    install_adlib_sfx_voices(prepare::read_adlib_sfx_voices(exe));
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


// Secret-room scenery, rebuilt every frame: floor every 48 px at y=168 plus
// the bubble scatter, which consumes the LCG each tick (as the original).
// draw_scatter=false (enhanced fluid bubbles): still roll the LCG (replay
// parity) but push no scatter tiles; the fluid bubbles replace them, as in the
// reference.
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
    // Recompute monster sprites before the first frame (which a transition
    // shows for its whole duration), so init placeholders do not leak.
    systems::refresh_entity_sprites_on_screen_bind(
        g.state.entities, g.state.l3a_phase_counter);
}


// The shared inputs for build_screen_tiles, for both the bind and the peek
// path.  `tile_sprites` is the caller's final atlas (surface tiles + GROT3 on
// L3).
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

void setup_enhanced_glider_water(Loaded& g, bool enhanced, int internal) {
    if (!enhanced || internal != 5) return;
    constexpr int kFlightStart = 9, kWaterSpr = 7;
    if (kFlightStart < static_cast<int>(g.tiles.screens.size())) {
        int maxy = -1;
        for (const auto& tp : g.tiles.screens[kFlightStart].tiles)
            if (tp.sprite_idx == kWaterSpr) maxy = std::max(maxy, tp.y);
        g.glider_water_y = maxy;   // -1 if screen 9 has no water (disabled)
    }
    if (g.glider_water_y >= 0 && g.state.current_screen >= 9 &&
        g.state.current_screen <= core::kLastScreen)
        normalize_glider_water(g.render.tiles, g.glider_water_y);
}

// One binder per screen kind; bind_screen dispatches and keeps the shared tail
// (bind_store + current_screen).

// Secret room: fixed palette, no visual background, surface tiles, fluid
// bubbles on first entry.
void bind_secret_screen(Loaded& g, int screen) {
    systems::setup_secret_collision(g.state);
    g.render.visual_background = false;
    g.render.bg_fill_index = 4;
    g.render.palette.assign(std::begin(kSecretPalette),
                            std::end(kSecretPalette));
    g.render.tile_sprites = g.surface_tiles;
    // No scatter here: the per-frame render gate generates it (entry frame
    // included); a bind-time refresh would consume the LCG twice on entry.
    // Enhanced: init the fluid bubbles on first entry only, so they continue
    // across entries (reset per level, as the reference).
    if (!g.fluid_bubbles_initialized) {
        g.fluid_bubbles.init();
        g.fluid_bubbles_initialized = true;
    }
    bind_store(g, screen);
    g.state.current_screen = screen;
}

// A cave: the level's cave palette, cave collision and its own tile list.
void bind_cave_screen(Loaded& g, int screen) {
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
        // Dark Woods caves: table-driven layout.  Sprites < 30 come from the
        // surface tiles and stamp collision; >= 30 are decorative pieces from
        // the cave MAT (after the 33 surface tiles).
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
        // L7 caves have no GROT file: tiled from ELEML7.MAT, sprite 29 wall at
        // y=87 + sprite 31 ceiling at y=50 every 64 px, sprite 30 as the right
        // cap (FUN_2759_033a).
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
}

void bind_screen(Loaded& g, int screen) {
    if (screen >= 100 && g.state.secret_flag) {   // secret room
        bind_secret_screen(g, screen);
        return;
    }
    g.render.bg_fill_index = -1;
    if (screen >= 100) {   // cave
        bind_cave_screen(g, screen);
        return;
    }
    g.render.visual_background = g.config.visual_background;
    if (g.config.internal_id == 3) {
        // Inside the tree (S10/S11): reachable only through the cave-exit warp,
        // which loads the L3 cave palette (brown at idx 8/9) and keeps it until
        // the S11->S12 boundary.  The surface palette would render the trunk
        // teal.
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
    // L3 surface: append GROT3 after the 33 surface tiles (33 = body, 34 =
    // cap), as the cave path does.
    if (level == 3 && !g.grot3.empty()) {
        g.render.tile_sprites.insert(g.render.tile_sprites.end(),
                                     g.grot3.begin(), g.grot3.end());
    }
    const int tile_screen = resolve_tile_screen(level, screen);

    // Collision stamping: bind path only (the peek must not touch the live
    // bitmap).
    g.state.collision.clear();
    if (level == 7 && screen >= 10 && screen <= 12) {
        // L7 screens 10-12 floor (FUN_25b2_000c, screen gate at 0x0047-0x00ae):
        // for si in {0..320 step 64} stamp DUR idx 29 (dx=0, dy=81, w=64) at
        // (si, 79), a continuous floor at y=160 (the si=320 stamp clips to
        // nothing, as Collision_SetPixel's x check at 0x00f3).  idx 31 has no
        // segments.  The S9->10 warp is the only way in, and without this floor
        // the player falls through the lava.
        for (const int si : {0, 64, 128, 192, 256, 320}) {
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

    // Render tile list from the shared pure constructor (screen_tiles.cpp),
    // identical for bind and peek.
    g.render.backdrop_tile_count = build_screen_tiles(
        screen_tile_ctx(g, g.render.tile_sprites), screen, g.render.tiles);
    bind_store(g, screen);
    g.state.current_screen = screen;
}

// Compose a surface screen's background + terrain (no entities) for the
// widescreen peek.  Read-only on the session: never touches the RNG (tiles
// come from the static table), never mutates g.state / g.store / g.render /
// the collision bitmap; all work lands in caller scratch.  Tiles come from the
// same build_screen_tiles call as bind_screen, so a margin matches the centre.
void build_surface_screen_assets(const Loaded& g, int screen,
                                        presentation::LevelRenderAssets& ra,
                                        systems::SystemsState& st) {
    const int level = g.config.internal_id;
    // Same HUD-band backdrop treatment as the live screen.
    ra.extend_top_backdrop = g.render.extend_top_backdrop;
    // Keep the enhanced banner flag, or transition frames of the food-gate
    // screen bring the classic sprites back.
    ra.enhanced_vector_banners = g.render.enhanced_vector_banners;

    // Scratch copy of the level render data; the background is already
    // HUD-erased.
    ra.background = g.render.background;
    ra.visual_background = g.config.visual_background;
    // As bind_screen: S10/S11 use the brown cave palette, the rest of L3 the
    // green one.
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
    // The level-wide entity atlas, needed for the static-object peek below.
    ra.entity_sprites = g.render.entity_sprites;
    if (level == 3 && !g.grot3.empty()) {
        ra.tile_sprites.insert(ra.tile_sprites.end(), g.grot3.begin(),
                               g.grot3.end());
    }
    ra.bg_fill_index = g.render.bg_fill_index;
    // No hud_strip: the compositor excludes the HUD band (hud_rows).

    // Tiles from the same constructor and inputs as bind_screen (collision
    // stamping stays on the bind path).
    ra.backdrop_tile_count = build_screen_tiles(
        screen_tile_ctx(g, ra.tile_sprites), screen, ra.tiles);

    // Scratch state: only the fields compose_frame reads matter.
    st.current_level = level;
    st.current_screen = screen;
    // The live food count, or a transition frame would show NOT ENOUGH FOOD.
    st.food_count = g.state.food_count;
    st.player.sprite = -1;   // suppress the player (draw_player=false too)
    // Enhanced peek: the neighbour's static objects (stairs, springs, food,
    // cave entrances/signs, vines, breakable rocks), read from the pre-spawned
    // g.store[screen]: no simulation, no RNG.  Moving things (monsters, fish,
    // birds, projectiles) are excluded: their entry spawn re-rolls position.
    // g.store is live, so collected food stays gone.
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

namespace {

// A live monster (the set is systems/sprite_ids.hpp's); dead ones with no
// respawns left stay hidden.
bool peekable_monster(const core::Entity& e) {
    return e.active && systems::is_monster(e.obj_type) &&
           !(e.state == static_cast<int>(core::MonsterState::Dead) &&
             e.respawns <= 0);
}

// `m` as it shows at its spawn post (init_x/init_y), walking.
void place_at_spawn_post(core::Entity& m) {
    m.x = m.init_x;
    m.y = m.init_y;
    m.visible = true;
    m.state_counter = 0;
    m.sprite = m.spr_num + (m.walk_offsets.empty() ? 0 : m.walk_offsets[0]);
}

}  // namespace

// Peek monsters: live shared-machine monsters at their spawn posts
// (init_x/init_y), where they appear on entry, so the margin predicts them
// and entry does not pop.
std::vector<core::Entity> collect_spawn_post_monsters(const Loaded& g,
                                                             int screen) {
    std::vector<core::Entity> out;
    auto sit = g.store.find(screen);
    if (sit == g.store.end()) return out;
    for (const auto& e : sit->second) {
        if (!peekable_monster(e)) continue;
        core::Entity m = e;
        place_at_spawn_post(m);
        out.push_back(std::move(m));
    }
    return out;
}

SpawnPostPreview::SpawnPostPreview(Loaded& g) : g_(g) {
    auto& es = g_.state.entities;
    const systems::PlayerState& p = g_.state.player;
    for (std::size_t i = 0; i < es.size(); ++i) {
        core::Entity& e = es[i];
        if (e.visible || !peekable_monster(e) ||
            e.state != static_cast<int>(core::MonsterState::Reset))
            continue;
        core::Entity before = e;
        if (systems::appear_if_player_level(e, p.x, p.y))
            saved_.emplace_back(i, std::move(before));
    }
}

SpawnPostPreview::~SpawnPostPreview() {
    auto& es = g_.state.entities;
    for (auto& [i, e] : saved_)
        if (i < es.size()) es[i] = std::move(e);
}

// `out_ra` (optional): the composed screen's render assets, from which
// update_cache derives the seam tile lists (built once per bind).
void compose_surface_screen_static(const Loaded& g, int screen,
                                   presentation::FrameBuffer& out,
                                   presentation::LevelRenderAssets* out_ra,
                                   // Optional underlay tiles (this screen's
                                   // coordinates), inserted at the
                                   // backdrop/level split: over the backdrop,
                                   // under the authored tiles.  Completes the
                                   // adjacent screen's straddlers (S13's rock
                                   // reaches into S14, under S14's dirt-top
                                   // row).
                                   const std::vector<
                                       presentation::LevelRenderAssets::
                                           TileDraw>* underlay,
                                   // frozen_full: overlay the stored entity
                                   // list verbatim.  For the panorama's
                                   // outgoing slot: the EXE pans the last
                                   // presented frame with sprites frozen
                                   // (WipeDown dst=[0x8bfa]+0x1f40: the visible
                                   // page is never touched).
                                   bool frozen_full,
                                   // peek_monsters: bake spawn-post monsters
                                   // in.  The steady peek passes false: the
                                   // live margin overlay draws them.
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
    out = presentation::FrameBuffer{};   // native 320x200
    // advance_state=false; st is scratch and draw_entities is RNG-free, so the
    // read-only contract holds.
    presentation::RenderTarget rt{out.px.data(), out.w, out.h, 1, nullptr,
                                  nullptr};
    rt.advance_state = false;
    presentation::compose_frame(rt, st, ra, /*draw_player=*/false);
    if (out_ra != nullptr) *out_ra = std::move(ra);
}

// Wide static background of any surface screen, exactly as the steady view
// composes it.  The panorama pan fills off-level slots from its outer margin,
// so the hand-off to the steady frame does not pop.
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

// Loader phases.  `entry_data` looks up archive entries; art and table phases
// return false for a missing file.
using EntryLookup =
    std::function<const std::vector<std::uint8_t>*(const std::string&)>;

// Decode the level's art: charset, entity sprites, backdrops, tile banks.
bool load_level_art(Loaded& g, const std::vector<std::uint8_t>& exe,
                    const EntryLookup& entry_data, int internal_level) {
    const auto* font = entry_data("CHARSET1.MAT");
    const auto* spr = entry_data(g.config.sprite_mat);
    if (font == nullptr || spr == nullptr) return false;
    g.charset = formats::load_mat_sprites(font, "CHARSET1.MAT");
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
        const auto sprites = formats::load_mat_sprites(d, mat);
        g.surface_tiles.insert(g.surface_tiles.end(), sprites.begin(),
                               sprites.end());
    }
    if (g.config.grot_mat != nullptr) {
        g.cave_tiles = formats::load_mat_sprites(entry_data(g.config.grot_mat),
                                                 g.config.grot_mat);
    }
    if (internal_level == 3) {
        g.l3_caves = prepare::read_l3_cave_tables(exe);
        g.grot3 = formats::load_mat_sprites(entry_data("GROT3.MAT"), "GROT3.MAT");
    }
    g.render.entity_sprites =
        formats::load_mat_sprites(spr, g.config.sprite_mat);

    // HUD labels: baked into the background on visual-background levels;
    // FOND7.PC1 (label bar only) supplies them elsewhere and in caves.
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
    return true;
}

// EXE tables + collision map: tile table, DUR, object / cave / secret screen
// lists, monster tables, ending picture.
bool load_level_tables(Loaded& g, const std::vector<std::uint8_t>& exe,
                       const EntryLookup& entry_data, int internal_level) {
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
    return true;
}

// Per-level reset.  Returns the screen to bind: `start_screen` clamped to the
// screen count, else 0.
int reset_level_state(Loaded& g, int internal_level, int start_screen) {
    g.fluid_bubbles_initialized = false;
    g.hd_cache.clear();

    // No reseed: the EXE seeds the LCG once at static init (DS:0x87ac=1); only
    // apply_save reseeds.  Then populate the per-screen store for the level.
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
    // GET READY counter: every surface level's init sets DS:0x97e0 = 0x11
    // (Level1_InitGlobals 21f3:0000 and the L3/L5/L7 equivalents).
    g.state.get_ready_counter = 0x11;
    // Includes the 40-frame spawn invulnerability (EXE-confirmed; the L1
    // screen-0 spike relies on it).
    g.state.player.reset_for_level(g.config.spawn_x, g.config.spawn_y);
    // The original pre-increments the frame counter, so the first frame updates
    // entities with fc=1 (parity-gated monster stepping).
    g.state.frame_counter = 1;
    // --start-screen (debug): bind that surface screen, clamped; no GET READY.
    int entry_screen = 0;
    if (start_screen > 0) {
        const int last = static_cast<int>(g.tiles.screens.size()) - 1;
        entry_screen = last >= 0 ? std::min(start_screen, last) : 0;
        if (entry_screen > 0) g.state.get_ready_counter = 0;
        // OLDUVAI_START_XY=x,y (debug): the player's position on that screen,
        // for reaching a spot no replay from the level start reaches cheaply
        // (the L7 hole and spring).
        if (const char* xy = std::getenv("OLDUVAI_START_XY")) {
            const std::string s(xy);
            const std::size_t comma = s.find(',');
            int x = 0, y = 0;
            if (comma != std::string::npos &&
                parse_int(s.substr(0, comma), x) &&
                parse_int(s.substr(comma + 1), y)) {
                g.state.player.x = x;
                g.state.player.y = y;
            }
        }
    }
    return entry_screen;
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
    const prepare::GameArchives archives(dir);

    auto entry_data = [&](const std::string& name)
        -> const std::vector<std::uint8_t>* { return archives.entry(name); };

    if (!load_level_art(g, exe, entry_data, internal_level)) return false;

    if (!load_level_tables(g, exe, entry_data, internal_level)) return false;

    // Reset enhanced-mode bubble state and HD asset cache for the new level.
    const int entry_screen =
        reset_level_state(g, internal_level, start_screen);
    bind_screen(g, entry_screen);
    return true;
}

// Format parsers throw on corrupt files; report one "could not load" message
// instead of terminating.
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

void warm_level_sprites(Loaded& g, int hd_scale, const std::string& profile) {
    const auto t0 = SDL_GetPerformanceCounter();
    const std::size_t n =
        warm_hd_sprite_cache(g.hd_cache, g.render.tile_sprites,
                             g.render.palette, hd_scale, profile) +
        warm_hd_sprite_cache(g.hd_cache, g.render.entity_sprites,
                             g.render.palette, hd_scale, profile);
    if (std::getenv("OLDUVAI_FRAME_STATS") == nullptr) return;
    const double ms = 1000.0 *
        static_cast<double>(SDL_GetPerformanceCounter() - t0) /
        static_cast<double>(SDL_GetPerformanceFrequency());
    std::fprintf(stderr, "hd-warm: %zu upscales in %.1f ms (cache now %zu)\n",
                 n, ms, g.hd_cache.size());
}

}  // namespace olduvai::presentation
