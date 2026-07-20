// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/game_app.hpp"

#include "presentation/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/cache_paths.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_files.hpp"
#include "presentation/autofire.hpp"
#include "presentation/debug_overlay.hpp"
#include "presentation/game_render.hpp"
#include "presentation/level_save.hpp"
#include "presentation/parse_util.hpp"
#include "presentation/pause_bindings.hpp"
#include "presentation/pause_flow.hpp"
#include "presentation/level_setup.hpp"
#include "presentation/level_state.hpp"
#include "presentation/tile_patterns.hpp"
#include "presentation/hud_render.hpp"
#include "presentation/dialog_key_map.hpp"
#include "presentation/l3_end_level.hpp"
#include "presentation/menu.hpp"
#include "presentation/menu_script_util.hpp"
#include "presentation/menu_model.hpp"
#include "presentation/banner_fx.hpp"
#include "presentation/menu_render.hpp"
#include "presentation/save_state.hpp"
#include "presentation/replay.hpp"
#include "presentation/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/bug_capture.hpp"
#include "presentation/report_templates.hpp"
#include "presentation/text_overlay_edit.hpp"
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
#include "core/rng.hpp"
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

namespace {

using formats::CurArchive;

std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}




}  // namespace

enum class LevelOutcome { kComplete, kGameOver, kQuit, kQuitProgram, kRestartLevel,
                          kLoadCheckpoint, kReinitDisplay, kWarpLevel };


// flow_key_from_sym (the SDL→SettingsFlow::Key bridge, shared by the in-game
// Pause and main-menu call sites) now lives in presentation/dialog_key_map.hpp
// so it is shared with boss_app instead of mirrored (CC3).  build_display_changes
// moved to settings_flow.cpp with the rest of the flow (OL-B1).

LevelOutcome run_platform_level(GameOptions& opts, int display_level,
                                int internal, CarriedState& carry,
                                SdlAudio& audio, const ScaledWindow& sw,
                                const std::optional<SaveState>& restore_in,
                                std::optional<SaveState>& out_load,
                                std::optional<PendingReinit>& out_reinit,
                                int& out_warp_display) {
    InputReplay replay;
    if (!opts.replay.empty() && !replay.load(opts.replay)) {
        // load() returns false when the file has zero input events — almost
        // always because a --trace file (one FrameState object per line, no
        // key/action/time_ms) was passed to --replay by mistake.  Silence here
        // reads as "replay does nothing"; say what to do instead.
        std::fprintf(stderr,
            "olduvai: --replay '%s' contained no input events — nothing to "
            "replay.  This file is likely a --trace capture (game state), not "
            "inputs.  Record a replayable session with --record-inputs "
            "<file>, then --replay that file.\n",
            opts.replay.c_str());
    }
    TraceWriter trace;
    if (!opts.trace.empty()) trace.open(opts.trace);
    InputRecorder input_rec;
    if (!opts.record_inputs.empty()) input_rec.open(opts.record_inputs);
    // HD upscaling + the enhanced (vector) HUD/menu are one mode: they require
    // --enhanced.  An hd_profile alone (e.g. a stray play.json key) no longer
    // forces HD — that left the bitmap HUD suppressed but the vector HUD off
    // (no HUD).  No --enhanced ⇒ classic 320×200, bitmap HUD + bitmap menu.
    // Computed BEFORE load_level so bind-time decisions (extend_top_backdrop
    // below) can key on the FULL vector-HUD gate, not a wider approximation.
    const bool hd = opts.enhanced && opts.hd_profile != "native";
    const int hd_scale = hd ? (opts.render_scale >= 4 ? 4 : 2) : 1;
    enhance::HdText hd_text;
    if (hd) {
        std::string base = ".";
        if (char* p = SDL_GetBasePath()) {  // exe dir (non-bundle) or Contents/Resources/ (bundle)
            base = p;
            SDL_free(p);
            if (!base.empty() && base.back() == '/') base.pop_back();
        }
        if (!hd_text.load(base, hd_scale, opts.hd_font) &&
            (opts.enhance.hd_text || opts.enhance.hud_overlay)) {
            enhance::HdText::report_missing(base, opts.hd_font);
        }
    }
    // The vector-HUD + vector-text subsystem (BG-label erase, in-buffer HUD,
    // output-res text overlay) is a single coupled unit in olduvai; either the
    // hd-text or hud-overlay feature activates it.  Under --enhanced both flags
    // are set so behaviour is identical to before.
    const bool use_hd_text =
        hd && hd_text.ok() && (opts.enhance.hd_text || opts.enhance.hud_overlay);
    Loaded g;
    // Enhanced vector HUD: continue the level backdrop up through the top
    // HUD-strip band (no "black bar" behind Score/Lives/Time).  Set BEFORE
    // load_level so the initial bind_screen already extends the backdrop tiling
    // (L7 lavarock adds a row at y=-54 in bind_screen; PC1 levels use the
    // compose-time mirror).  MUST be gated on use_hd_text — the same gate as
    // the rows-0-8 label erase / hud_strip clear / vector HUD below — not on
    // enhanced+HD alone: with hd-text off (an --enhance subset, or the font
    // missing) the baked HUD labels still render in rows 0-8, and extending the
    // backdrop would overwrite (PC1 mirror) or cover (L7 y=-54 tile) them with
    // no replacement.  Classic keeps the EXE black strip.
    g.render.extend_top_backdrop = use_hd_text;
    if (!load_level(opts.game_dir, g, internal, opts.start_screen)) {
        std::fprintf(stderr, "game: could not load level data from %s\n",
                     opts.game_dir.string().c_str());
        return LevelOutcome::kQuit;
    }
    // Enhanced icy-glider sea-level normalisation: the icy level (internal 5)
    // authors its decorative water (sprite 7, no collision) at a Y that rises
    // mid-flight and drops back at the landing.  Under --enhanced, flatten it to
    // the flight-START (screen 9) sea level across the flight screens so it reads
    // as one continuous body during the glider.  Visual-only (water has no
    // collision; death is the fixed y>180 fall).  bind_screen applies it per
    // screen; apply it once here too for the already-bound entry screen.
    if (opts.enhanced && internal == 5) {
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
    g.state.player.lives = carry.lives;
    g.state.score = carry.score;
    // --god: 99 lives / 999 energy / no death (debug).  Off during replay so
    // recorded scenarios stay deterministic (mirrors the --cheats gating).
    // Matches Python op play --god (set at start, refreshed per
    // level, with timer-death suppression).
    // Mutable so the in-game Pause→Cheats menu can toggle god live.
    bool god_active = opts.god && !replay.active();
    g.state.god_mode = god_active;
    // Hold-to-swing pacing state (presentation/autofire.hpp); cooldown is
    // re-read each frame so the Options-menu choice applies live.
    Autofire autofire;
    g.state.enhanced_active = opts.enhanced;   // render-only cosmetic gates
    if (god_active) {
        g.state.player.energy = 999;
        g.state.player.lives = 99;     // EXE cap
        g.state.food_count = systems::kFoodGate;  // full belly
    }
    // Enhanced #20b — level-start arrival materialization (owner idea
    // 2026-07-05): the mid-air spawn plays the teleport ARRIVAL sequence
    // (empty → clouds growing → PLAYER_TURN pose) before the drop.
    // Fresh level entries only — a save restore repositions the player,
    // so the spawn-anchored clouds would play at the wrong spot.
    // Surface levels only by construction (bosses run in boss_app).
    if (opts.enhanced && !restore_in) {
        g.state.teleport_in_ticks = 15;
        g.state.teleport_fx_x = g.state.player.x;
        g.state.teleport_fx_y = g.state.player.y;
    }
    // Full-state restore: apply the saved header (player + scalars + exact
    // current_screen/mode), reseed RNG, overlay every stored screen's entity
    // runtime state, re-bind the exact screen, overlay the live entities.
    if (restore_in) apply_save(*restore_in, g);
    // Stage-2 HD-bake disk persistence: when enhanced/HD is active, route the
    // per-asset upscale cache through the platform cache dir so OmniScale runs
    // once across runs.  HD blocks are cosmetic and content-addressed, so a
    // disk hit can never change gameplay or rendered output — only skip a
    // recompute.  Off under --no-config-style disables only by absence of HD.
    if (hd) g.hd_cache.enable_disk(prepare::hd_dir());
    SDL_Window* const win = sw.win;
    SDL_Renderer* const ren = sw.ren;
    // Enhanced mode substitutes the pre-baked GET-READY / NOT-ENOUGH-FOOD
    // sprite banners with cartoony vector text (drawn in the output overlay,
    // below).  Flag the render assets so draw_entities suppresses the pre-baked
    // food-gate cue (g.render fields persist across in-place level reloads).
    g.render.enhanced_vector_banners = use_hd_text;
    if (use_hd_text) {
        // Erase the PC1-baked HUD labels + gauge outline (rows 0-8)
        // from the background asset ONCE — the vector HUD replaces
        // them at HD resolution.  Source-level masking instead of a
        // per-frame band fill: cheaper, and sprites crossing the band
        // (the rising death angel) stay intact IN FRONT of it, as in
        // the original where sprites draw over the HUD area.  An
        // earlier per-frame rows-8..15-copy mask duplicated such
        // sprites half-over-themselves (user-reported doubled halo).
        auto& bgp = g.render.background.pixels;
        if (g.render.background.width == 320 &&
            bgp.size() >= 9 * 320 && !bgp.empty()) {
            std::fill(bgp.begin(), bgp.begin() + 9 * 320, bgp[0]);
        }
        // The cave/secret label strip carries the same baked labels.
        g.render.hud_strip.clear();
    }
    // ── Widescreen adjacent-screen peek (§8.7), enhanced-only ──────────────
    // The widescreen presentation state + machinery (margin math, resize
    // recompute, peek cache, wrap_wide* family, the steady wide present) live
    // in WidescreenPresenter (widescreen_presenter.cpp, OL-B5), built over a
    // narrow WidescreenShellCtx.  Level entry computes the margin at
    // construction; the level-derived state (peek cache + FOND backdrop) is
    // built by the explicit wsp.update_cache()/wsp.build_backdrop() calls
    // below, at the original level-entry sites.
    // Output-resolution vector-text overlay (HD mode only).  Reused across
    // frames; re-allocates only on window/output-size change.  (Declared
    // before the presenter, whose ctx points at it.)
    TextOverlay text_overlay;
    // The HD logical canvas SDL scales onto the window; the text overlay
    // disables logical scaling, draws at output res, then restores this.
    // Widescreen: logical size = the wide buffer's own size (aspect ≈ display),
    // so it fills the screen with no bars.  Otherwise the aspect_logical rule.
    // `_fallback_ld` = the non-widescreen (margin-0) logical, used by the resize
    // recompute when the toggled display is 16:10 (margin collapses to 0).
    const LogicalDims _fallback_ld = aspect_logical(hd_scale, opts.aspect);
    // Mutable: a live Aspect change (Tier-1) recomputes these and the
    // renderer's logical size mid-level (PauseBindings::apply_aspect); the
    // widescreen resize recompute keeps them in lockstep with SDL's logical
    // size.  Initialised from _ld right after the presenter exists.
    int logical_w = 0;
    int logical_h = 0;
    // The classic streaming texture stays 320*hd_scale wide for EVERY non-
    // widescreen path (loading / tally / transitions / pause / classic present)
    // — unchanged.  Widescreen present uses the presenter's own WIDE texture.
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         320 * hd_scale, 200 * hd_scale);
    WidescreenShellCtx wsctx;
    wsctx.ren = ren;
    wsctx.hd = hd;
    wsctx.hd_scale = hd_scale;
    wsctx.use_hd_text = use_hd_text;
    wsctx.aspect = &opts.aspect;          // Tier-1 live changes tracked
    wsctx.hd_profile = &opts.hd_profile;
    wsctx.state = &g.state;
    wsctx.render = &g.render;
    wsctx.hd_cache = &g.hd_cache;
    wsctx.hd_text = &hd_text;
    wsctx.text_overlay = &text_overlay;
    wsctx.internal_level_id = g.config.internal_id;
    wsctx.surface_screen_count = static_cast<int>(g.tiles.screens.size());
    wsctx.logical_w = &logical_w;
    wsctx.logical_h = &logical_h;
    wsctx.fallback_ld = _fallback_ld;
    wsctx.compose_static =
        [&g](int s, FrameBuffer& out, LevelRenderAssets* ra,
             const std::vector<LevelRenderAssets::TileDraw>* underlay,
             bool frozen_full, bool peek_monsters) {
            compose_surface_screen_static(g, s, out, ra, underlay,
                                          frozen_full, peek_monsters);
        };
    wsctx.collect_monsters = [&g](int s) {
        return collect_spawn_post_monsters(g, s);
    };
    // (draw_overlay_tail + draw_banners are wired via the setters below
    // once the shell lambdas they wrap exist — same ordering as before, when
    // those lambdas were defined after this block.)
    WidescreenPresenter wsp(std::move(wsctx));
    const LogicalDims _ld =
        wsp.active() ? LogicalDims{wsp.native_w() * hd_scale, 200 * hd_scale}
                     : _fallback_ld;
    if (wsp.active()) SDL_RenderSetLogicalSize(ren, _ld.w, _ld.h);
    logical_w = _ld.w;
    logical_h = _ld.h;

    // --cheats interactive power-up picker (F7 opens; UP/DOWN select;
    // ENTER/1-6 grant; ESC closes).  Pauses the world while open.
    bool cheat_open = false;
    int cheat_sel = 0;
    static const char* const kPowerupNames[6] = {
        "SPRING", "BOMB", "TIMER", "EXTRA LIFE", "SHIELD", "AXE"};
    // Shared row label: "> N NAME" for the selected row (N = the 1-6 hotkey),
    // "  N NAME" otherwise — same text in the HD vector and classic bitmap
    // paths so both look identical and advertise the number keys.
    auto cheat_row_label = [&](int i) {
        return (i == cheat_sel ? std::string("> ") : std::string("  ")) +
               std::to_string(i + 1) + " " + kPowerupNames[i];
    };
    // HD path: cartoon vector font into the output-res text overlay.
    auto draw_cheat_rows = [&](std::vector<std::uint8_t>& b, int ow, int oh) {
        // Semi-transparent backdrop panel (native 50..270 x, 44..182 y → output)
        // — wide enough for the 190px-wide hint line with margins.
        const int px0 = ow * 50 / 320, px1 = ow * 270 / 320;
        const int py0 = oh * 44 / 200, py1 = oh * 182 / 200;
        for (int y = py0; y < py1 && y < oh; ++y)
            for (int x = px0; x < px1 && x < ow; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * ow + x) * 4;
                b[o] = 16; b[o + 1] = 16; b[o + 2] = 36; b[o + 3] = 214;
            }
        draw_centered_overlay_row(b, ow, oh, hd_text, 52, "- POWER-UP CHEAT -");
        for (int i = 0; i < 6; ++i)
            draw_centered_overlay_row(b, ow, oh, hd_text, 72 + i * 14,
                                      cheat_row_label(i));
        draw_centered_overlay_row(b, ow, oh, hd_text, 170,
                                  "1-6  UP/DOWN  ENTER  ESC");
    };
    // Classic path: CHARSET1 bitmap font into the native 320×200 buffer.  A
    // fixed menu palette keeps the text readable over ANY level palette; a
    // blended panel dims the frozen scene behind it.  Mirrors the HD layout.
    std::vector<formats::Rgb> cheat_pal(16, formats::Rgb{200, 200, 200});
    cheat_pal[7] = formats::Rgb{170, 170, 185};    // unselected rows + hint
    cheat_pal[14] = formats::Rgb{252, 224, 64};    // selected row (yellow)
    cheat_pal[15] = formats::Rgb{252, 252, 252};   // title (white)
    auto cheat_text_w = [&](const std::string& s) {
        int w = 0;
        for (char ch : s) {
            const int idx = static_cast<unsigned char>(ch) - 0x20;
            w += (idx >= 0 && idx < static_cast<int>(g.charset.size()))
                     ? g.charset[static_cast<std::size_t>(idx)].width
                     : 8;
        }
        return w;
    };
    auto draw_cheat_rows_native = [&](FrameBuffer& f) {
        if (g.charset.empty()) return;
        constexpr int A = 200;   // panel opacity (/256) over the scene
        for (int y = 44; y < 182; ++y)
            for (int x = 50; x < 270; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * 320 + x) * 4;
                f.px[o]     = static_cast<std::uint8_t>((f.px[o]     * (256 - A) + 16 * A) >> 8);
                f.px[o + 1] = static_cast<std::uint8_t>((f.px[o + 1] * (256 - A) + 16 * A) >> 8);
                f.px[o + 2] = static_cast<std::uint8_t>((f.px[o + 2] * (256 - A) + 40 * A) >> 8);
                f.px[o + 3] = 255;
            }
        auto row = [&](int baseline, const std::string& s, int col) {
            draw_text(f, g.charset, cheat_pal, (320 - cheat_text_w(s)) / 2,
                      baseline, s, col);
        };
        row(56, "- POWER-UP CHEAT -", 15);
        for (int i = 0; i < 6; ++i)
            row(76 + i * 12, cheat_row_label(i), i == cheat_sel ? 14 : 7);
        row(176, "1-6  UP/DOWN  ENTER  ESC", 7);
    };

    // In HD mode gameplay buffers are sized at the target resolution so every
    // compose goes directly through the per-asset cache path (no whole-frame
    // upscale).  Classic buffers stay 320×200.  The default FrameBuffer ctor
    // produces 320×200 (used by loading/tally/PC1 screens via present()).
    //
    // In widescreen mode `fb` is STILL the HD-sized center buffer: it is the
    // non-widescreen-present fallback (pause/transition/screenshot) AND the
    // single authoritative per-frame state-advance vehicle (compose_frame
    // advances club_flag once on it, before the smooth-motion save/restore, so
    // the advance survives).  wsp.present does NOT reuse fb's entities —
    // it composes its OWN native bg center, assembles the wide buffer, and draws
    // the entities ONCE over it (advance_state = false) so they overflow the
    // 320 edge into the margins.  Two entity DRAWS per frame (fb + overflow) but
    // exactly ONE advance.
    const int fb_w = hd ? 320 * hd_scale : 320;
    const int fb_h = hd ? 200 * hd_scale : 200;
    FrameBuffer fb{fb_w, fb_h};
    // Enhanced L3 descent dust tail: after the trunk slams down, keep faint
    // settling puffs for ~2 s on the steady screen-18 view (the EXE cuts the
    // smoke at the landing iter).  Armed after Phase 2 (descent-pan gated),
    // decremented once per logic tick, drawn over every present path.
    // Render-only: hash jitter, never the game LCG.
    constexpr int kL3SmokeTailTicks = 36;   // ~2 s at 18 Hz
    int l3_smoke_tail = 0;
    auto draw_l3_smoke_tail = [&](RenderTarget& t) {
        if (l3_smoke_tail <= 0) return;
        const auto& spr = g.render.entity_sprites;
        auto hash_jit = [](int i, int k) -> int {
            std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u ^
                              (static_cast<std::uint32_t>(k) * 0x9E3779B9u);
            return static_cast<int>((h >> 16) & 7u);
        };
        // Dissipate by COUNT (3 → 2 → 1 puffs), not alpha — reads as the dust
        // settling with indexed-palette sprites.
        const int n = 1 + (l3_smoke_tail * 3) / (kL3SmokeTailTicks + 1);
        static constexpr int kTailX[3] = {65, 49, 95};
        for (int k = 0; k < n; ++k) {
            const int idx = 85 + (((l3_smoke_tail >> 1) + k) & 1);   // 85/86
            if (idx < static_cast<int>(spr.size()))
                presentation::blit_sprite(
                    t, spr[static_cast<std::size_t>(idx)], g.render.palette,
                    kTailX[k], 173 + hash_jit(l3_smoke_tail, k));
        }
    };
    // Enhanced #20 — teleport cloud sequence (cave-sign teleports).
    // Departure (ticks 3/2/1): clouds 87→86→85, big→small, at the
    // sign-cross spot.  Arrival (ticks 4/3/2/1): empty, 85, 86, 85 at the
    // destination.  Player hidden by game_render while either phase runs;
    // clouds feet-anchored on the 32x30 player box.  Armed in
    // collision_dispatch when enhanced_active; the departure completion
    // below applies the DEFERRED teleport.  No RNG of any kind.  Mirrors
    // the reference implementation's teleport-fx tables.
    // Ghost pacing (owner v4): each cloud stage HOLDS 3 ticks.  Departure
    // 9 ticks shrinking 87/86/85; arrival 12 ticks — 3-tick empty beat,
    // then growing 85/86/87.  Player frozen by frame_runner while either
    // phase runs.  Mirrors the reference implementation's STAGE_HOLD tables.
    auto draw_teleport_fx = [&](RenderTarget& t) {
        // Owner v5 bookends: depart ticks 12-10 and arrive ticks 3-1 are
        // the PLAYER_TURN pose (game_render draws 134); no cloud there.
        int idx = -1;
        if (g.state.teleport_out_ticks > 0) {
            if (g.state.teleport_out_ticks <= 9)
                idx = 85 + (g.state.teleport_out_ticks - 1) / 3;
        } else if (g.state.teleport_in_ticks > 0) {
            const int t = g.state.teleport_in_ticks;
            if (t >= 4 && t <= 12) idx = 85 + (12 - t) / 3;
        }
        if (idx < 0) return;
        const auto& spr = g.render.entity_sprites;
        if (idx >= static_cast<int>(spr.size())) return;
        const auto& s = spr[static_cast<std::size_t>(idx)];
        presentation::blit_sprite(
            t, s, g.render.palette,
            g.state.teleport_fx_x + (32 - s.width) / 2,
            g.state.teleport_fx_y + (30 - s.height));
    };
    // The widescreen present draws the smoke tail over its wide foreground
    // (same site the in-loop wsp.present lambda called it from).
    wsp.set_draw_overlay_tail([&](RenderTarget& t) {
        draw_l3_smoke_tail(t);
        draw_teleport_fx(t);
    });
    bool running = true;
    // ESC / window-close → abort the run to the title via the game-over path.
    // Kept SEPARATE from g.state.game_over because --god resets game_over every
    // frame to suppress death; routing the abort through its own flag means ESC
    // still works under --god.  Consumed at the game-over block below.
    bool abort_to_title = false;
    // ── In-game Pause menu (ESC) ──────────────────────────────────────────
    // Declarative model from data/menus.json, drawn by draw_menu, freezes the
    // sim like the --cheats picker.  v1: dark backdrop behind the slab, god
    // toggle is live; other Options are navigable placeholders (live-apply is
    // the next slice).  Spec: 2026-06-19-re-game-menus-design.md.
    bool pause_open = false;
    bool want_quit_program = false;   // Pause → Quit to Desktop
    bool want_restart = false;        // Pause → Restart Level
    bool want_load = false;           // Pause → Load Game (out_load is set)
    int want_warp = 0;                // Pause → Cheats → Warp! (display level 1-7)
    bool want_reinit = false;         // Pause → Settings change needing re-init
    PendingReinit reinit_req;
    std::optional<MenuModel> menu_model_opt = load_pause_menu_model();
    MenuModel pause_model = menu_model_opt.value_or(MenuModel{});
    const bool menu_ok = menu_model_opt.has_value();
    if (!menu_ok) {
        std::fprintf(stderr, "menu: no menu model (disk or built-in) - ESC "
                     "falls back to quit-to-title\n");
    }
    PauseBindings pause_bind;
    // Batched staging: all Options edits go through session → confirm dialog.
    // The session is populated by PauseBindings::set; the confirm dialog is
    // opened when the user leaves the Options subtree with pending changes.
    SettingsSession pause_session;
    ConfirmDialog confirm_dlg;
    PauseBindWireDeps pause_bind_wire{
        &god_active, &audio, &sw, &opts, &pause_session,
        &want_reinit, &reinit_req, &logical_w, &logical_h, hd_scale,
        display_level};
    configure_pause_bind(pause_bind, pause_bind_wire);
    // Cheats → Spawn bonus: launch the EXE's own rising-bonus arc (the same
    // path a clubbed ancestor-ghost drop takes — club_bonus init at
    // collisions.cpp: mask bit 7 + counter 65 + rise dy/y, then update_bonus
    // arcs the icon over the player and Bonus_Activate fires on landing).
    // The menu IS the explicit opt-in, so no --cheats flag needed; replay
    // stays gated for trace determinism.
    PauseActionsDeps pause_actions_deps{
        &g, &replay, &pause_bind, &opts, &out_load,
        &pause_open, &abort_to_title, &want_quit_program, &want_restart,
        &want_load, &god_active, &want_warp, display_level};
    MenuActionTable pause_actions = make_pause_actions(&pause_actions_deps);
    Menu pause_menu(pause_model, pause_bind, pause_actions);

    // ── F5 bug-report form ─────────────────────────────────────────────────
    // F5 freezes the sim and opens an in-engine form (tag/repro choice rows +
    // a multi-line description edited in a full-canvas overlay).  Leaving the
    // form opens a Save/Discard confirm (the Options-Apply pattern): Save
    // writes the report WITH the annotations, Discard drops the whole capture.
    struct ReportBind : MenuBindings {
        std::map<std::string, std::string> mem;
        std::string get(const std::string& k) override {
            auto it = mem.find(k);
            return it == mem.end() ? std::string{} : it->second;
        }
        void set(const std::string& k, const std::string& v) override {
            mem[k] = v;
        }
    } report_bind;
    Menu report_menu(pause_model, report_bind);
    ConfirmDialog report_confirm;
    bool report_open = false, report_edit_open = false;
    bool report_save_pending = false, report_frame_ready = false;
    FrameBuffer report_frame{320, 200};
    EditOverlayState report_edit;
    auto report_seed = [&]() {
        report_bind.mem["report.tag"] = "collision";
        report_bind.mem["report.repro"] = "unknown";
        report_bind.mem["report.description"] = report_template("collision");
    };
    auto report_retemplate_if_untouched = [&]() {
        // Re-fill the description with the new tag's skeleton ONLY while it is
        // still an unedited template — once the user types, it is theirs.
        if (is_report_template(report_bind.get("report.description")))
            report_bind.set("report.description",
                            report_template(report_bind.get("report.tag")));
    };
    auto open_report_confirm = [&]() {
        const std::string desc = report_bind.get("report.description");
        int nlines = 1;
        for (char c : desc) if (c == '\n') ++nlines;
        std::vector<StagedChange> rows = {
            {"report.tag", "Tag", "", report_bind.get("report.tag")},
            {"report.repro", "Reproducibility", "",
             report_bind.get("report.repro")},
            {"report.description", "Description", "",
             std::to_string(nlines) + (nlines == 1 ? " line" : " lines")},
        };
        report_confirm.open("SAVE BUG REPORT?", rows, "");
    };

    // Debug: OLDUVAI_PAUSE_SHOT force-opens the Pause overlay on frame 1 and
    // dumps it to a PNG (see the pause block below) — headless render check.
    if (std::getenv("OLDUVAI_PAUSE_SHOT") && menu_ok) {
        // OLDUVAI_PAUSE_SCREEN picks the screen to capture (default "pause")
        // — lets the headless render check reach submenus (Cheats, Options).
        const char* ps = std::getenv("OLDUVAI_PAUSE_SCREEN");
        pause_menu.open(ps != nullptr ? ps : "pause");
        pause_open = pause_menu.is_open();   // unknown screen id → no overlay
    }
    // HD hybrid font: the Pause menu's text is drawn with the SAME vector font
    // the HUD uses (FreckleFace via hd_text) at output resolution, so it stays
    // crisp instead of being a bitmap upscaled by the pixel-art profile.  The
    // slab + accent bar come from draw_menu (native, upscaled); only the glyphs
    // move to the overlay, via the shared draw_menu_vector() (also used by the
    // title-screen Main menu).
    // F5 in-game bug capture: set by the F5 key (and the env-gated test
    // trigger), serviced once the frame is composed.  Allowed during normal
    // play and --replay (capturing replay bugs is useful).
    int frame = 0;
    // OLDUVAI_FRAME_STATS: per-frame timing health — the headless twin of
    // OLDUVAI_AUDIO_STATS.  Measures a frame's WORK (loop top -> just before the
    // pacing wait; the intentional sleep is excluded) at perf-counter
    // resolution, tracking the worst frame vs the ~55 ms tick budget, how many
    // frames blew it, and the present/upload phase within that worst frame.
    // Lets slow-HW frame-budget violations be diagnosed without a display;
    // near-zero cost when the env var is unset.
    const bool frame_stats = std::getenv("OLDUVAI_FRAME_STATS") != nullptr;
    const double fs_perf_ms =
        1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    const double fs_budget_ms = 1000.0 / 18.2065;   // one DosTicker period
    std::uint64_t fs_frames = 0, fs_overruns = 0;
    double fs_worst_ms = 0.0, fs_worst_present_ms = 0.0, fs_present_ms = 0.0;
    Uint64 fs_t0 = 0;
    // RAII timer that folds upload_and_show's wall time into fs_present_ms,
    // robust to that lambda's early-return paths; inert when disabled.
    struct FsPresentTimer {
        double* accum;
        double perf_ms;
        bool on;
        Uint64 t0;
        FsPresentTimer(double* a, double pm, bool o)
            : accum(a), perf_ms(pm), on(o),
              t0(o ? SDL_GetPerformanceCounter() : 0) {}
        ~FsPresentTimer() {
            if (on)
                *accum += static_cast<double>(SDL_GetPerformanceCounter() - t0) *
                          perf_ms;
        }
    };
    const Uint32 frame_ms = 1000 / 18;   // 18 Hz logic (aux pacing sites)
    DosTicker dos_ticker;                // drift-free 18.2065 Hz main pacing
    unsigned long vga_fill_presents = 0, vga_fill_ticks = 0;  // --vga-scan stats
    bool vga_scan_ok = true;   // cleared when the driver clearly refused vsync

    // Smooth-motion sub-frame count.  The logic stays 18 Hz; the renderer draws
    // smooth_N interpolated frames per tick.  Scaled to the display refresh so
    // the present cadence tracks the panel (finer per-sub-frame motion = slow
    // sprites like the fluid bubbles step in smaller increments).  Clamped
    // [4,5]: 4 is already finer than the legacy 3 on a 60 Hz panel; 5 is the
    // perf ceiling — the worst-case WS-omniscale compose measured ~9.6 ms, so
    // 5 × 9.6 = 48 ms fits the 55 ms tick budget (6 overshoots → ~17.4 Hz).
    // Logic is untouched so this is cosmetic-only.  Override for tuning:
    // OLDUVAI_SMOOTH_SUBFRAMES=<n> (e.g. on a light profile a 120 Hz panel can
    // afford 6-7).
    int smooth_N = 3;
    {
        int refresh_hz = 60;
        SDL_DisplayMode dm;
        const int di = win != nullptr ? SDL_GetWindowDisplayIndex(win) : 0;
        if (di >= 0 && SDL_GetCurrentDisplayMode(di, &dm) == 0 &&
            dm.refresh_rate > 0) {
            refresh_hz = dm.refresh_rate;
        }
        smooth_N = std::clamp(
            static_cast<int>(std::lround(refresh_hz / 18.0)), 4, 5);
        if (const char* ov = std::getenv("OLDUVAI_SMOOTH_SUBFRAMES")) {
            const int n = std::atoi(ov);
            if (n >= 1 && n <= 12) smooth_N = n;
        }
    }

    // Enhanced smooth-motion presents via vsync-locked render interpolation:
    // logic stays a fixed 18 Hz, but the tick's wall-time is filled with
    // interpolated frames paced by the panel's vsync at a CONTINUOUS alpha
    // (elapsed / tick) — so motion is smooth at any refresh (60/120/144/VRR)
    // with no fixed-sub-frame quantisation and no 54-vs-60 beat.  This is the
    // general "fix-your-timestep + render interpolation" technique.  vsync is
    // requested at runtime (SDL>=2.0.18); if the driver refuses, vsync_active
    // stays false and the loop falls back to the discrete smooth_N pacing.
    // OLDUVAI_NO_VSYNC=1 forces the discrete fallback (for A/B testing).
    bool vsync_active = false;
    if (opts.enhance.smooth_motion && ren != nullptr &&
        std::getenv("OLDUVAI_NO_VSYNC") == nullptr) {
        vsync_active = (SDL_RenderSetVSync(ren, 1) == 0);
    }
    // Carryover (ms) of render-fill overshoot beyond one tick — subtracted from
    // the next tick's render budget so the long-term logic cadence stays 18 Hz
    // even though an integer number of vsync frames rarely divides the 55 ms
    // tick exactly (e.g. 3.3 refreshes/tick at 60 Hz).
    Uint32 smooth_carryover = 0;

    // Helper: build a RenderTarget over a gameplay FrameBuffer.  In HD the
    // target carries the cache and profile so blit_sprite uses the per-asset
    // upscale path; in classic it is a plain scale-1 wrapper.
    auto make_rt = [&](FrameBuffer& b) -> RenderTarget {
        // A native 320-wide buffer in HD mode (loading/tally scratch buffers,
        // and wsp.present's own native center which it builds directly,
        // not via make_rt) must use scale 1 + no cache so blits land at NATIVE
        // coordinates; it is upscaled whole-frame later.  Only a genuinely
        // HD-sized buffer (320*hd_scale wide) drives the per-asset cache path.
        // Branch on the actual buffer width, not just `hd`.
        if (hd && b.w == 320 * hd_scale)
            return RenderTarget{b.px.data(), b.w, b.h, hd_scale,
                                &g.hd_cache, &opts.hd_profile};
        return RenderTarget{b.px.data(), b.w, b.h, 1, nullptr, nullptr};
    };

    // ── Widescreen level-derived state (§8.7) — presenter-owned ────────────
    // Compose the neighbour peek cache for the entry screen (bind_screen ran
    // in load_level / the setup path before this point), then the pure-FOND
    // backdrop for the no-neighbour margin extension.  Cache/backdrop/seam
    // internals live in WidescreenPresenter (OL-B5); the present-path
    // predicate is wsp.present_path().
    wsp.update_cache();
    wsp.build_backdrop();

    // HUD helper for a gameplay FrameBuffer.
    //
    // In classic mode: calls draw_hud(fb, ...) verbatim (bitmap digits/bars +
    // GET READY + state mutations) — byte-identical to the old path.
    //
    // In HD mode with use_hd_text:
    //   • draw_hud(fb, ..., true) is called on a NATIVE 320×200 scratch so
    //     the state mutations (food cap writeback, GET READY counter
    //     decrement) happen exactly once, and the GET READY banner sprites are
    //     drawn at native coords.  Then the GET READY banner is re-drawn at HD
    //     via the make_rt path so it appears correctly at the HD resolution.
    //   • draw_enhanced_hud is NOT called here — it is called inside
    //     upload_and_show so the vector labels composite after the frame is
    //     fully composed (same placement as before).
    //
    // In HD mode without use_hd_text (fonts not found — rare edge case):
    //   calls draw_hud on the native scratch for state mutations; bitmap HUD
    //   is absent (same result as before — the enhanced path never had bitmap
    //   HUD at HD).  Use classic mode (-enhanced) for the bitmap HUD.
    //
    // `with_hd_text` mirrors the caller's use_hd_text flag (captured by lambda).
    FrameBuffer hud_scratch{};   // native 320×200 scratch — state mutations
    auto draw_hud_for_fb = [&](FrameBuffer& target) {
        const bool with_hd_text = use_hd_text;
        if (!hd) {
            // Classic: bitmap HUD directly onto the 320×200 buffer.
            draw_hud(target, g.state, g.charset, g.render.entity_sprites,
                     g.render.palette, false);
            return;
        }
        // HD: run draw_hud on the native scratch for state mutations only.
        // The scratch pixels are discarded; we only care about side effects
        // (food_count cap, get_ready_counter decrement, GET READY sprites are
        // re-drawn at HD below).
        // Capture GET READY visibility BEFORE draw_hud mutates the counter —
        // draw_hud draws the banner then decrements; we must draw at HD if the
        // banner was visible at entry (even on the tick the counter reaches 1).
        const bool get_ready_visible = (g.state.get_ready_counter >= 2 &&
                                        g.state.get_ready_counter <= 17);
        // Reset scratch alpha so blit_sprite writes are visible (just in case).
        for (std::size_t i = 3; i < hud_scratch.px.size(); i += 4)
            hud_scratch.px[i] = 255;
        draw_hud(hud_scratch, g.state, g.charset, g.render.entity_sprites,
                 g.render.palette, with_hd_text);
        // Re-draw the GET READY banner at HD via the scale-aware RenderTarget
        // so it appears crisp at the target resolution.  In enhanced vector
        // mode (with_hd_text) the pre-baked sprites are SUPPRESSED here and the
        // cartoony vector "GET READY!" is drawn in the output overlay instead
        // (draw_enhanced_banners) — that path also shows in widescreen, where
        // this center re-draw is recomposed away.
        if (get_ready_visible && !with_hd_text) {
            auto rt = make_rt(target);
            if (132 < static_cast<int>(g.render.entity_sprites.size()))
                blit_sprite(rt, g.render.entity_sprites[132],
                            g.render.palette, 0x80, 0x64);
            if (133 < static_cast<int>(g.render.entity_sprites.size()))
                blit_sprite(rt, g.render.entity_sprites[133],
                            g.render.palette, 0xA2, 0x61);
        }
    };

    // Debug aid: OLDUVAI_DRAW_LOG=<file> logs every RENDERED frame's
    // draw positions (player + entities, sub-frames included) as JSONL.
    // an offline analysis tool flags
    // single-frame outliers — transient glitches no eyeball catches.
    FILE* draw_log = nullptr;
    if (const char* dl = std::getenv("OLDUVAI_DRAW_LOG")) {
        draw_log = std::fopen(dl, "w");
    }
    auto log_draw = [&](int sub) {
        if (draw_log == nullptr) return;
        std::fprintf(draw_log,
                     "{\"f\":%d,\"sub\":%d,\"px\":%d,\"py\":%d,\"ps\":%d,"
                     "\"e\":[",
                     frame, sub, g.state.player.x, g.state.player.y,
                     g.state.player.sprite);
        bool first = true;
        for (const auto& e : g.state.entities) {
            if (!e.active) continue;
            std::fprintf(draw_log, "%s[%d,%d,%d,%d,%d]", first ? "" : ",",
                         static_cast<int>(e.obj_type), e.sprite, e.x, e.y,
                         e.visible ? 1 : 0);
            first = false;
        }
        std::fprintf(draw_log, "]}\n");
    };
    // Debug aid: OLDUVAI_DUMP_STEADY=<dir> — windowed sibling of the
    // widescreen presenter's steady dump: saves the native fb on each
    // non-widescreen steady present (steady_fb_NNNN.bmp).
    auto dump_steady_fb = [&]() {
        const char* dir = std::getenv("OLDUVAI_DUMP_STEADY");
        if (dir == nullptr) return;
        static int seq = 0;
        char path[512];
        std::snprintf(path, sizeof path, "%s/steady_fb_%04d.bmp", dir, seq++);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
            fb.px.data(), fb.w, fb.h, 32, fb.w * 4, SDL_PIXELFORMAT_RGBA32);
        save_surface_image(s, path);
        SDL_FreeSurface(s);
    };

    // One upload pipeline for every frame this window shows.
    //
    // `with_hud=true`: draw the enhanced vector HUD over the frame (gameplay +
    // transition frames; loading/tally/PC1 screens pass false).
    //
    // Dimension handling:
    //   • f.w == 320*hd_scale (HD gameplay buffer): already at the right
    //     resolution — upload directly at f.w pitch.  upscale_rgba is NOT
    //     called (per-asset composition already produced the HD pixels).
    //   • f.w == 320 (native buffer): used by loading/tally/show_pc1_screen
    //     via present(); still upscale when hd so the texture stays correct.
    //   • classic (hd==false): upload at 320*4 pitch, no upscale.
    // Draw the enhanced vector HUD text over the CENTRE 320 sub-region of a
    // WIDESCREEN frame: map native x into [wsp.margin(), wsp.margin()+320] then to
    // output by ow/wsp.native_w(), and size the font cap to that wide-domain scale
    // (8 * ow/wsp.native_w()).  EVERY widescreen present must use this same mapping
    // — the steady peek frame (wsp.present), the steady bezel/pillarbox
    // frame (upload_and_show), and the wide transitions (present_wide_transition)
    // — so the HUD text sits at one fixed place and never floats/jumps between a
    // transition and the steady frame.  The non-widescreen path keeps
    // draw_enhanced_hud_text's full-width ow/320 mapping.  Does NOT restore the
    // font cap; a caller that draws more text in the same overlay pass (a pause
    // menu) must save hd_text.cap_px() before and restore it after.
    // Enhanced cartoony vector substitutes for the pre-baked sprite "banners"
    // (GET READY 132/133; NOT-ENOUGH-FOOD gate cue 82/91), drawn into the
    // output-resolution overlay so they appear crisp AND survive the widescreen
    // re-compose (the center pre-baked draw is recomposed away there).
    // draw_centered_overlay_row centres at the output midpoint, which maps to
    // native x≈160 in BOTH the plain and widescreen canvases (the center 320
    // sits at wsp.margin()), so one call serves every present path.  The font cap
    // is sized to the pre-baked box's glyph height (native px → output px) so
    // the text "fits the box, centered"; cap is saved/restored so a HUD/menu
    // draw sharing this overlay pass keeps its size.  Gates mirror the pre-baked
    // draws: GET-READY counter window [2,17] (FUN_27f7_1277); food gate screen +
    // food < 45 (FUN_263c_09ab).
    // GET READY rainbow fly-away latch (wall-clock based).  Armed on the
    // level-start rising edge of get_ready_counter in the run loop below; the
    // banner draw reads these.  gr_prev_counter inits to a non-0x11 value so the
    // very first level's arm (counter 0→0x11) is detected.
    Uint32 gr_anim_start = 0;
    bool gr_anim_active = false;
    int gr_prev_counter = 0;
    // ── Animated enhanced banners (wall-clock driven, so motion is smooth at
    // any refresh rate and independent of the 18 Hz logic tick) ──────────────
    // GET READY!       → rainbow; fully visible for HOLD ms, then rockets
    //                    straight up off the top over FLY ms (quadratic accel),
    //                    then gone.  Armed on the level-start rising edge of
    //                    get_ready_counter (load_level sets 0x11); the gr_anim_*
    //                    latch lives in the run-loop scope (declared above).
    // NOT ENOUGH FOOD! → fire gradient + a gentle vertical bob in place; a
    //                    persistent status banner, so it rides the wall clock.
    // Both are enhanced-only by construction (this runs only on the use_hd_text
    // overlay path); classic keeps the pre-baked sprites untouched.
    // Banner colour effects, selectable via OLDUVAI_BANNER_FX (overrides BOTH
    // banners for experimentation).  DEFAULTS: GET READY = "caveman" (primal
    // fire-and-blood), NOT ENOUGH FOOD = "fire".  Rainbow stays available but is
    // no longer the default.  `tsec` is wall-clock seconds → all animation is
    // refresh-rate-independent.  u spans the word L→R, v spans cap top→bottom.
    const char* bfx_env = std::getenv("OLDUVAI_BANNER_FX");
    const std::string gr_effect = bfx_env ? bfx_env : opts.banner_fx;
    const std::string food_effect = bfx_env ? bfx_env : opts.banner_fx;
    auto draw_enhanced_banners = [&](std::vector<std::uint8_t>& b,
                                     int ow, int oh) {
        const int saved_cap = hd_text.cap_px();
        const Uint32 now = SDL_GetTicks();
        auto emit = [&](int cap_native, int baseline, const char* text,
                        const enhance::HdText::ShadeFn& shade) {
            hd_text.set_cap_px(
                std::max(1, static_cast<int>(cap_native * oh / 200.0 + 0.5)));
            draw_centered_overlay_row_styled(b, ow, oh, hd_text, baseline, text,
                                             shade);
        };

        // GET READY! — caveman (default), hold then rocket up off the top.
        if (gr_anim_active) {
            const long HOLD = 2000, FLY = 450;   // ms
            const long t = static_cast<long>(now - gr_anim_start);
            if (t < HOLD + FLY) {
                float yoff = 0.0f;
                if (t > HOLD) {
                    float p = static_cast<float>(t - HOLD) / FLY;
                    if (p > 1.0f) p = 1.0f;
                    yoff = -(p * p) * 175.0f;   // quadratic accel, off the top
                }
                emit(12, 112 + static_cast<int>(yoff), "GET READY!",
                     make_banner_shade(gr_effect, t / 1000.0f));
            } else {
                gr_anim_active = false;   // animation finished
            }
        }

        // NOT ENOUGH FOOD! — fire (default) + gentle vertical bob.
        const int gate_screen = (g.state.current_level == 3) ? 17 : 18;
        if ((g.state.current_level == 1 || g.state.current_level == 3 ||
             g.state.current_level == 5 || g.state.current_level == 7) &&
            g.state.current_screen == gate_screen && g.state.food_count < 45) {
            const long ft = static_cast<long>(now);
            const float bob = 3.0f * std::sin(ft * 0.006f);
            emit(11, 111 + static_cast<int>(std::lround(bob)),
                 "NOT ENOUGH FOOD!", make_banner_shade(food_effect, ft / 1000.0f));
        }
        hd_text.set_cap_px(saved_cap);
    };
    // Banner substitutes are shell-owned (state-driven); the wide HUD-text
    // mapping itself lives in the presenter (wsp.draw_wide_hud_text — CC2d).
    wsp.set_draw_banners([&](std::vector<std::uint8_t>& b, int ow, int oh) {
        draw_enhanced_banners(b, ow, oh);
    });
    // OLDUVAI_MENU_SCRIPT: drive the menus headlessly with synthetic SDL key
    // events (same SDL_PushEvent path the gamepad uses), one token per frame.
    // Tokens: esc up down left right enter space 1..6 | wait | shot | quit.
    // `shot` dumps the composed frame to OLDUVAI_MENU_SCRIPT_DIR/NNN.png; `quit`
    // exits cleanly (kQuitProgram). Turns interactive menu paths into automatable
    // regression tests — see tests/menu_script.sh.
    // CAVEAT: these are locals of run_platform_level, so an Apply that
    // triggers a display reinit re-enters the level and REPLAYS the script
    // from the first token (shots restart at 000.png and overwrite).  Walks
    // that apply a reinit-class change must account for the second pass
    // (or use OLDUVAI_REINIT_TEST, which reinit_smoke drives instead).
    // Parsing + key injection live in menu_script_util.hpp (shared with the
    // title-menu walk); the type:/chord tokens below stay local to this loop.
    std::vector<std::string> menu_script =
        parse_menu_script(std::getenv("OLDUVAI_MENU_SCRIPT"));
    std::size_t menu_script_idx = 0;
    int menu_shot_ctr = 0;
    std::string menu_script_dir = ".";
    std::string menu_shot_path;   // set for one frame when a `shot` token fires
    bool menu_script_quit = false;
    if (const char* d = std::getenv("OLDUVAI_MENU_SCRIPT_DIR"))
        menu_script_dir = d;

    auto upload_and_show = [&](FrameBuffer& f, bool with_hud = true,
                               bool do_present = true) {
        FsPresentTimer fs_pt(&fs_present_ms, fs_perf_ms, frame_stats);
        wsp.rebuild_if_resized();   // Alt+Enter / resize: recompute wide state
        // The vector HUD TEXT is no longer drawn into the HD compose buffer —
        // it is rendered at OUTPUT resolution into text_overlay after the scene
        // is RenderCopy'd, so it stays crisp at any window scale.  Only the
        // NON-text HUD (gauge boxes, food fill, energy pips) goes in the buffer.
        const bool draw_hud_overlay = hd && with_hud && use_hd_text;
        enhance::EnhancedHudLayout hud_layout;
        if (draw_hud_overlay) {
            hud_layout = enhance::compute_enhanced_hud_layout(hd_text, g.state);
        }
        if (hd) {
            if (f.w == 320 * hd_scale) {
                // Already-HD gameplay buffer: no upscale needed.
                if (draw_hud_overlay) {
                    enhance::draw_enhanced_hud_bars(f.px, f.w, f.h, hd_scale,
                                                    hud_layout);
                }
                SDL_UpdateTexture(tex, nullptr, f.px.data(), f.w * 4);
            } else {
                // Native-320 buffer (loading/tally/PC1): upscale whole-frame.
                // Single fullscreen opaque image — per-asset and whole-frame
                // produce identical output, so whole-frame upscale is fine here.
                std::vector<std::uint8_t> up =
                    enhance::upscale_rgba(f.px, 320, 200, hd_scale,
                                          opts.hd_profile);
                if (draw_hud_overlay) {
                    enhance::draw_enhanced_hud_bars(up, 320 * hd_scale,
                                                    200 * hd_scale, hd_scale,
                                                    hud_layout);
                }
                SDL_UpdateTexture(tex, nullptr, up.data(),
                                  320 * hd_scale * 4);
            }
        } else {
            // Classic (320×200): draw the cheat picker with the bitmap font
            // straight into the native buffer (recomposed clean each frame).
            if (cheat_open) draw_cheat_rows_native(f);
            SDL_UpdateTexture(tex, nullptr, f.px.data(), 320 * 4);
        }
        SDL_RenderClear(ren);
        if (wsp.active()) {
            // Widescreen but using the 320-wide `tex` (transitions / loading /
            // tally / pause — peek is disabled there per §8.7 v1): pillarbox the
            // center into the wide logical canvas with a PURE-BLACK bezel.  Pure
            // black (not the old 12,12,16) so the bars vanish against black-
            // background screens (caves, the score tally, the level-end fade) —
            // a dark-gray bezel reads as visibly grayish next to a cave's true-
            // black tiles.  Against coloured screens (L3/L7/boss) a bar is visible
            // either way; black is the cleaner, conventional letterbox.
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, nullptr);
            SDL_Rect dst{wsp.margin() * hd_scale, 0, 320 * hd_scale,
                         200 * hd_scale};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
        } else {
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
        }
        // Output-resolution vector-HUD text overlay (crisp at the window size).
        // One vector-text overlay pass for the HUD text and/or the --cheats
        // power-up picker (a second begin/flush in the same frame would not
        // composite, so both share one pass).
        // Gate on hd_text.ok() (fonts loaded) rather than use_hd_text so the
        // picker shows in HD even when the hd-text/hud-overlay enhance flags
        // are off — fonts load whenever hd is on.
        const bool show_cheat = cheat_open && hd && hd_text.ok();
        // Menu glyphs follow the HUD's font rule: the cartoony vector font only
        // in enhanced mode (use_hd_text); otherwise the classic bitmap glyphs
        // (drawn into the native pause buffer below, upscaled with the frame).
        const bool show_menu    = pause_open && use_hd_text && !confirm_dlg.is_open();
        const bool show_confirm = pause_open && use_hd_text && confirm_dlg.is_open();
        // Save whenever the pause overlay is up — classic mode draws the
        // bitmap menu into the native frame (no vector pass), and the shot
        // must capture that path too.
        const char* show_menu_shot =
            !menu_shot_path.empty() ? menu_shot_path.c_str()
            : (pause_open ? std::getenv("OLDUVAI_PAUSE_SHOT") : nullptr);
        if (draw_hud_overlay || show_cheat || show_menu || show_confirm) {
            int ow = 0, oh = 0;
            if (text_overlay.begin(ren, hd_text, ow, oh)) {
                auto& b = text_overlay.buffer();
                if (draw_hud_overlay) {
                    if (wsp.active()) {
                        // Pillarboxed widescreen frame: the centre 320 sits at
                        // the margin, so the HUD text must use the wide mapping
                        // (matches wsp.present() / the wide transitions) —
                        // otherwise it floats relative to the bars.  Restore the
                        // cap so a pause menu in this same pass keeps its size.
                        const int saved_cap = hd_text.cap_px();
                        wsp.draw_wide_hud_text(b, ow, oh, hud_layout);
                        hd_text.set_cap_px(saved_cap);
                    } else {
                        enhance::draw_enhanced_hud_text(b, ow, oh, hd_text,
                                                        hud_layout);
                        // Non-WS banner substitutes (the WS branch gets them via
                        // draw_wide_hud_text, so only the else-branch adds here).
                        draw_enhanced_banners(b, ow, oh);
                    }
                }
                if (show_cheat) draw_cheat_rows(b, ow, oh);
                // Widescreen: the pause frame is PILLARBOXED at the margin
                // (the dst rect above) — pass that frame rect so the glyphs
                // land on the slab instead of stretching across the bars.
                int mfx = -1, mfy = -1, mfw = -1, mfh = -1;
                if (wsp.active()) {
                    mfx = wsp.margin() * hd_scale * ow / logical_w;
                    mfw = 320 * hd_scale * ow / logical_w;
                    mfy = 0;
                    mfh = oh;
                }
                if (show_menu)
                    draw_menu_vector(b, ow, oh, hd_text, pause_menu, 0.0f, mfx,
                                     mfy, mfw, mfh);
                if (show_confirm)
                    draw_confirm_vector(b, ow, oh, hd_text, confirm_dlg, mfx,
                                        mfy, mfw, mfh);
                text_overlay.flush(ren, logical_w, logical_h);
            }
        }
        if (show_menu_shot) {
            // Debug: read back the fully-composited frame (scene + slab + vector
            // text) right before present so we can verify the HD overlay.
            int rw = 0, rh = 0;
            SDL_GetRendererOutputSize(ren, &rw, &rh);
            std::vector<std::uint8_t> rb(static_cast<std::size_t>(rw) * rh * 4);
            if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                     rb.data(), rw * 4) == 0) {
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                    rb.data(), rw, rh, 32, rw * 4, SDL_PIXELFORMAT_RGBA32);
                if (s) { save_surface_image(s, show_menu_shot); SDL_FreeSurface(s); }
            }
            menu_shot_path.clear();   // consume a menu-script `shot` request
        }
        // do_present=false leaves the composited frame in the backbuffer for a
        // caller-side RenderReadPixels (Metal reads black AFTER present).
        if (do_present) SDL_RenderPresent(ren);
    };
    // ── Widescreen present (§8.7, Option A) — moved to WidescreenPresenter
    // (wsp.present(), OL-B5) together with the Tier-1 margin-monster draw and
    // the club-mechanic advance_state=false discipline documented there. ──
    // smooth_player_f{x,y} carry the player's interpolated render position
    // into the fb sub-frame compose AND (via wsp.set_float_pos) into the wide
    // overflow draw (player render pos lives on RenderTarget, not
    // PlayerState — see RenderTarget::player_fx).
    float smooth_player_fx = 0.0f, smooth_player_fy = 0.0f;
    // (The steady widescreen present is wsp.present() — OL-B5; the wide-
    // transition present is wsp.present_transition() — CC2d.)
    // (wrap_wide / wrap_wide_bezel / wrap_wide_for / wrap_wide_static and
    // reapply_seam_bands are wsp.* methods now — OL-B5.)
    auto present = [&](const FrameBuffer& f) -> bool {
        const Uint32 t0 = SDL_GetTicks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, win)) continue;
            if (ev.type == SDL_QUIT) return false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                return false;
        }
        FrameBuffer copy = f;   // upload may mutate (HD label masking)
        upload_and_show(copy, /*with_hud=*/false);
        // Pace to 18 Hz by ABSORBING the upscale cost into the frame budget.
        // The L3 trunk-descent runs this per animation frame; at omniscale ×4
        // the upscale is ~36 ms, so an unconditional SDL_Delay(frame_ms) made
        // each frame ~91 ms (~11 fps, the "pathetic" descent stutter).  Delay
        // only the remainder.  No state/RNG touched (wall-clock pacing only;
        // headless returns before here).
        const Uint32 elapsed = SDL_GetTicks() - t0;
        if (elapsed < frame_ms) SDL_Delay(frame_ms - elapsed);
        return true;
    };
    auto skip_held = []() -> bool {
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        return k[SDL_SCANCODE_SPACE] != 0 ||
               (k[SDL_SCANCODE_RETURN] != 0 && enter_skip_allowed()) ||
               gamepad::fire_held();
    };

    // Enhanced score tally: route text through the cartoon vector font at HD
    // resolution.  present_hd uploads a ready HD buffer; the
    // tally builds it (upscaled black base + cartoon rows).  Classic → null.
    TallyHd tally_hd_text;
    if (use_hd_text) {
        tally_hd_text.hd_text = &hd_text;
        tally_hd_text.scale = hd_scale;
        tally_hd_text.upscale =
            [&](const std::vector<std::uint8_t>& px) {
                return enhance::upscale_rgba(px, 320, 200, hd_scale,
                                             opts.hd_profile);
            };
        tally_hd_text.present_hd =
            [&](const std::vector<std::uint8_t>& hd_px, int w, int h,
                const std::vector<HdTextRow>& rows) -> bool {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (handle_fullscreen_toggle(ev, win)) continue;
                    if (ev.type == SDL_QUIT) return false;
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE)
                        return false;
                }
                SDL_UpdateTexture(tex, nullptr, hd_px.data(), w * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                // Fixed-anchor tally rows at OUTPUT resolution (crisp overlay):
                // title rows centred, counting rows label/value-aligned so the
                // proportional font does not slide as the digits change width.
                if (!rows.empty()) {
                    int ow = 0, oh = 0;
                    if (text_overlay.begin(ren, hd_text, ow, oh)) {
                        draw_tally_rows_overlay(text_overlay.buffer(), ow, oh,
                                                hd_text, rows);
                        text_overlay.flush(ren, logical_w, logical_h);
                    }
                }
                SDL_RenderPresent(ren);
                SDL_Delay(frame_ms);
                (void)h;
                return true;
            };
    }

    LevelOutcome outcome = LevelOutcome::kQuit;
    // Enhanced loading screen: route the two text rows through the cartoon
    // vector font at HD res (same handle shape as tally_hd_text; reference
    // records the loading lines into a TextLayer).  Classic →
    // null hd_text (byte-identical bitmap path).
    LoadingHd loading_hd_text;
    if (use_hd_text) {
        loading_hd_text.hd_text = tally_hd_text.hd_text;
        loading_hd_text.scale = tally_hd_text.scale;
        loading_hd_text.upscale = tally_hd_text.upscale;
        loading_hd_text.present_hd = tally_hd_text.present_hd;
    }
    // Level-entry loading screen.
    if (!show_loading_screen(nullptr, display_level, g.charset,
                             g.render.palette, present, loading_hd_text)) {
        running = false;
    }
    // Level music starts AFTER the loading screen, with the level itself —
    // the reference plays it post-setup (_show_loading_screen →
    // _setup_level → play_level_music); starting it earlier had the track
    // running over the "Please Wait" text.
    if (running && audio.music_available()) {
        if (const char* mname = level_music_name(internal)) {
            formats::CurArchive fa2(slurp(opts.game_dir / "FILESA.CUR"));
            formats::CurArchive fb3(slurp(opts.game_dir / "FILESB.CUR"));
            const std::vector<std::uint8_t>* md = nullptr;
            if (fa2.contains(mname)) md = &fa2.get(mname).data;
            else if (fb3.contains(mname)) md = &fb3.get(mname).data;
            if (md != nullptr) {
                std::string lower = mname;
                for (auto& ch2 : lower) {
                    lower[static_cast<std::size_t>(&ch2 - lower.data())] =
                        static_cast<char>(std::tolower(ch2));
                }
                audio.play_music(*md, formats::mdi_track_id(lower));
            }
        }
    }

    // Screen-change transition bookkeeping: the old screen's last frame
    // plus the classified effect, played back after the new screen's
    // first compose.  HD-sized to match the gameplay fb.
    FrameBuffer transition_old{fb_w, fb_h};
    int transition_kind = 0;   // 0 = none, 1 = pan-scroll, 2 = fade pair
                               // 3 = enhanced secret-entry slide (12f down)
                               // 4 = enhanced secret-exit slide (30f up + arc)
    char transition_dir = 'R';
    // Widescreen transitions (§8.7): when the transition involves a
    // ws_present_path screen we present the WHOLE transition at the WIDE texture
    // width so the bars + HUD don't pop in/jump mid-pan/fade.  transition_old_wide
    // is the outgoing frame composed WIDE *before* the rebind (old neighbours);
    // the incoming is composed wide after the rebind (new neighbours) inside the
    // playback block.  ws_transition gates the whole wide-transition path; when
    // false (non-widescreen, or neither side is a peek/secret screen) the legacy
    // 320 upload_and_show path runs unchanged.
    std::vector<std::uint8_t> transition_old_wide;
    bool ws_transition = false;
    // Wide-wrapped OLD frame for the kind 3/4 secret slides.  Built INSIDE the
    // kind 3/4 classification blocks (kind 4's outgoing frame needs the transient
    // secret_flag/player-at-exit state that is restored before the kind 1/2 wide
    // block runs), so it gets its own holder rather than transition_old_wide.
    std::vector<std::uint8_t> slide_old_wide;
    bool slide_old_wide_ok = false;
    // Saved state for the kind=4 arc overlay (set when classifying exit).
    int slide_secret_exit_x = 0;   // departure x (= state.secret_exit_x)
    int slide_end_x = 0, slide_end_y = 0;   // return position

    // --debug-perf: rolling FPS / frame-time over ~30 frames.  `perf_last_t`
    // is the wall clock at the previous frame top (inter-frame interval =
    // wall FPS incl. the 18 Hz throttle); `perf_ms_accum` sums per-frame
    // compute time.  Updated at frame top; smoothed value drawn into fb.
    const bool any_debug_overlay =
        opts.debug_collision || opts.debug_entities || opts.debug_perf;
    Uint32 perf_last_t = SDL_GetTicks();
    Uint32 perf_interval_accum = 0;
    Uint32 perf_ms_accum = 0;
    int perf_samples = 0;
    double perf_fps = 0.0;
    double perf_frame_ms = 0.0;
    const int overlay_scale = hd ? hd_scale : 1;
    // Draw the requested dev overlays into a gameplay FrameBuffer just before
    // it is presented.  Gated entirely on the --debug-* flags (no-op when
    // none set) so default rendering is byte-identical.
    auto apply_debug_overlays = [&](FrameBuffer& target) {
        if (opts.debug_collision)
            draw_debug_collision(target, g.state, overlay_scale);
        if (opts.debug_entities)
            draw_debug_entities(target, g.state, g.render.entity_sprites,
                                overlay_scale);
        if (opts.debug_perf)
            draw_debug_perf(target, g.charset, g.render.palette, perf_fps,
                            perf_frame_ms, overlay_scale);
    };

    // OLDUVAI_REINIT_TEST: headless integration hook — triggers a render_scale
    // reinit once (frame 5) and captures the post-reinit output size + player
    // position on re-entry.  Two static latches carry state across the two
    // run_platform_level invocations (pre-reinit → kReinitDisplay → run_game
    // rebuilds → re-enters with restore).  The result file path comes from the
    // env var; the hook is a no-op unless that var is set.
    // statics are zero-initialised; reset between test runs by the process.
    static bool s_reinit_triggered = false;  // have we fired the set() call?
    static bool s_reinit_done      = false;  // have we written the result file?
    static int  s_pre_x = 0, s_pre_y = 0;   // player pos before reinit
    static int  s_pre_entcount = -1;        // live entity count before reinit
    static unsigned s_pre_entsum = 0;       // entity CONTENT checksum pre-reinit
    // Content checksum over the mutable per-entity fields: an equal COUNT of
    // freshly-respawned (reset) entities must NOT pass the round-trip check —
    // that's the exact false-confidence shape this hook was built to close.
    const auto ent_checksum = [](const std::vector<core::Entity>& es) {
        unsigned h = 2166136261u;                    // FNV-1a
        const auto mix = [&h](int v) {
            h ^= static_cast<unsigned>(v);
            h *= 16777619u;
        };
        for (const auto& e : es) {
            mix(static_cast<int>(e.obj_type));
            mix(e.x); mix(e.y); mix(e.state); mix(e.counter);
            mix(e.ko_counter); mix(e.active ? 1 : 0);
        }
        return h;
    };
    // Read once; reused in both the pre-loop block and the in-loop trigger.
    const char* const reinit_test_path = std::getenv("OLDUVAI_REINIT_TEST");
    if (reinit_test_path) {
        // Post-reinit entry: we are in the re-spawned level, restore applied.
        // Capture output size + player position and write the result file.
        if (s_reinit_triggered && !s_reinit_done) {
            s_reinit_done = true;
            int out_w = 0, out_h = 0;
            // Use logical window size (SDL_GetWindowSize) not renderer output
            // size so the result is DPI-independent — on macOS Retina
            // SDL_GetRendererOutputSize returns 2× the logical size, which
            // would make the assertion fail on HiDPI displays.
            SDL_GetWindowSize(win, &out_w, &out_h);
            const int post_x = g.state.player.x;
            const int post_y = g.state.player.y;
            // Entity-count round-trip check: the full-state save must restore the
            // live screen's entities, not just the player.  (A save→reinit→restore
            // on the load_level default screen used to drop them all — the L1
            // screen-0 spike vanished.)
            const int post_entcount = static_cast<int>(g.state.entities.size());
            const unsigned post_entsum = ent_checksum(g.state.entities);
            // "wb": the file is machine-parsed by reinit_smoke.sh; text mode
            // on Windows would append \r to the last field and break the
            // shell string compare.
            if (FILE* f = std::fopen(reinit_test_path, "wb")) {
                std::fprintf(f, "%d %d %d %d %d %d %d %d %u %u\n",
                             out_w, out_h,
                             s_pre_x, s_pre_y,
                             post_x, post_y,
                             s_pre_entcount, post_entcount,
                             s_pre_entsum, post_entsum);
                std::fclose(f);
            }
            running = false;
        }
    }

    // SettingsFlow: the Options staging/confirm/apply controller (OL-B1). The
    // pause-specific hooks + wiring live in pause_flow.cpp (CC2c) — they capture
    // pointers into these locals via PauseFlowDeps (which lives on this frame).
    PauseFlowDeps pause_flow_deps{&pause_menu, &opts, &pause_bind,
                                  &reinit_req, &want_reinit};
    SettingsFlow pause_flow = make_pause_flow(pause_model, pause_session,
                                              confirm_dlg, &pause_flow_deps);

    // Close-without-apply detection: if pause was open last frame and is now
    // closed (Resume) while the session is dirty, treat it as Discard.
    // APPLY already clears pause_session, so it will be empty — no
    // double-revert.  Placed at the TOP of the loop so it fires on the first
    // iteration after pause closes, before any input that could reopen pause.
    bool was_pause_open = false;

    while (running) {
        cursor_autohide_frame();   // keyboard game: park the OS arrow
        // Detect pause closing via Resume with dirty session (§8.6 step 4).
        if (was_pause_open && !pause_open && !pause_session.empty())
            pause_flow.discard();
        was_pause_open = pause_open;

        if (frame_stats) {
            fs_present_ms = 0.0;
            fs_t0 = SDL_GetPerformanceCounter();
        }
        const Uint32 t0 = SDL_GetTicks();
        if (any_debug_overlay) {
            const Uint32 now = SDL_GetTicks();
            perf_interval_accum += now - perf_last_t;
            perf_last_t = now;
            ++perf_samples;
            if (perf_samples >= 30) {
                const double avg_interval =
                    static_cast<double>(perf_interval_accum) / perf_samples;
                perf_fps = avg_interval > 0.0 ? 1000.0 / avg_interval : 0.0;
                perf_frame_ms =
                    static_cast<double>(perf_ms_accum) / perf_samples;
                if (std::getenv("OLDUVAI_PERF_LOG"))
                    std::fprintf(stderr, "[PERF] frame_ms=%.2f fps=%.1f\n",
                                 perf_frame_ms, perf_fps);
                perf_interval_accum = 0;
                perf_ms_accum = 0;
                perf_samples = 0;
            }
        }
        // OLDUVAI_REINIT_TEST: pre-reinit trigger on frame 5 — player has a
        // valid spawn position by then (GET READY counter started at 0x11).
        if (reinit_test_path &&
            !s_reinit_triggered && menu_ok && frame == 5) {
            s_reinit_triggered = true;
            s_pre_x = g.state.player.x;
            s_pre_y = g.state.player.y;
            s_pre_entcount = static_cast<int>(g.state.entities.size());
            s_pre_entsum = ent_checksum(g.state.entities);
            // Force the save→reinit→restore MECHANISM directly, decoupled from
            // the Pause classifier (which is interim-disabled for in-game
            // reinit-class changes).  Seed all four target fields + raise
            // want_reinit so the pause block below captures the snapshot and
            // returns kReinitDisplay.
            reinit_req.enhanced     = opts.enhanced;
            reinit_req.render_scale = 4;
            reinit_req.hd_profile   = opts.hd_profile;
            reinit_req.music_device = opts.music_device;
            reinit_req.sfx_backend  = opts.sfx_backend;
            want_reinit = true;
            pause_open = true;
        }
        // Pre-frame snapshot for transition classification: the player
        // position before this frame's movement/teleport (direction
        // inference) and the inside-ness before cave/secret entry.
        const int prev_px = g.state.player.x;
        const int prev_py = g.state.player.y;
        const int prev_screen = g.state.current_screen;
        const bool was_secret = g.state.secret_flag != 0;
        const bool was_cave = g.state.cave_flag != 0;
        // cave_index BEFORE this frame's logic (a cave-sign exit sets it to -1):
        // the wide kind-2 fade re-composes the OUTGOING cave frame and needs the
        // ORIGINAL index back, because the cave STOP-sign render is gated on
        // cave_index in range (game_render.cpp) — without it the sign blanks the
        // instant the fade starts while the rest of the cave still fades.
        const int prev_cave_index = g.state.cave_index;
        const bool was_inside = g.state.cave_flag || g.state.secret_flag;
        // Player DRAW state before this frame's logic — i.e. exactly what the
        // LAST PRESENTED frame showed (frozen-frame principle, Finding
        // transition_pan_content_frozen_sprites.md).  The wide kind-2 fade
        // re-composes its outgoing frame from live state, but the transition
        // tick has already mutated the player's presentation:
        //   • cave ENTRY consume tick (tick_cave_descent) resets
        //     sprite→kSprPlayerStand, dx→0, and enter_cave clears facing_left/
        //     club_flag — recomposing with those drew a STANDING player at the
        //     hole instead of holding descent frame 46 (owner F5
        //     2026-07-05_171337_L1_S1);
        //   • cave EXIT (exit_cave) arms cave_emerge_frames, so the emerge
        //     PLAYER_TURN override leaked into the cave-side outgoing frame
        //     instead of the last-presented walk sprite.
        // Locals only — PlayerState is memcpy'd whole into the POD SaveHeader
        // (player.hpp), so no prev-shadow fields may live there.
        const int prev_psprite = g.state.player.sprite;
        const int prev_pdx = g.state.player.dx;
        const int prev_pdy = g.state.player.dy;
        const int prev_pfacing = g.state.player.facing_left;
        const int prev_pclub = g.state.player.club_flag;
        const int prev_emerge = g.state.cave_emerge_frames;
        // OLDUVAI_MENU_SCRIPT: consume one token before the poll so the synthetic
        // key is processed by this frame's event loop (drives pause/menus exactly
        // like a human — open via ESC, navigate, activate, cheats).
        if (!menu_script.empty()) {
            if (menu_script_idx >= menu_script.size()) {
                menu_script_quit = true;   // auto-exit at end of script
            } else {
                const std::string tok = menu_script[menu_script_idx++];
                if (tok == "quit") menu_script_quit = true;
                else if (tok == "wait") { /* idle one frame */ }
                else if (tok == "shot") {
                    char nm[32];
                    std::snprintf(nm, sizeof nm, "%03d.png", menu_shot_ctr++);
                    menu_shot_path = menu_script_dir + "/" + nm;
                } else if (tok.rfind("type:", 0) == 0) {
                    // Text-editor typing: '_' → space.  Dispatched STRAIGHT
                    // to the editor's event handler, NOT via SDL_PushEvent:
                    // sdl2-compat (Homebrew's SDL2 since 2026-07) refuses
                    // app-pushed TEXTINPUT events — its Event2to3 returns
                    // NULL ("we shouldn't be getting text input events this
                    // direction") and SDL3_PushEvent(NULL) segfaults.  The
                    // direct call reaches the same consumer the poll loop
                    // feeds; a TEXTINPUT can only insert (kNone), so the
                    // save/cancel handling there is not needed here.
                    std::string txt = tok.substr(5);
                    for (char& c : txt) if (c == '_') c = ' ';
                    if (report_open && report_edit_open) {
                        SDL_Event te{};
                        te.type = SDL_TEXTINPUT;
                        std::snprintf(te.text.text, sizeof te.text.text,
                                      "%s", txt.c_str());
                        edit_handle_event(report_edit, te);
                    }
                } else if (tok == "stab" || tok == "ctrlenter") {
                    // Modifier chords the plain key-pusher can't express.
                    const SDL_Keycode ms =
                        tok == "stab" ? SDLK_TAB : SDLK_RETURN;
                    const Uint16 mod =
                        tok == "stab" ? KMOD_LSHIFT : KMOD_LCTRL;
                    for (bool down : {true, false}) {
                        SDL_Event e{};
                        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                        e.key.keysym.sym = ms;
                        e.key.keysym.scancode = SDL_GetScancodeFromKey(ms);
                        e.key.keysym.mod = mod;
                        SDL_PushEvent(&e);
                    }
                } else {
                    const SDL_Keycode sym = menu_token_sym(tok);
                    if (sym != SDLK_UNKNOWN) push_menu_key(sym);
                }
            }
            if (menu_script_quit) {
                outcome = LevelOutcome::kQuitProgram;
                running = false;
                break;
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, win)) continue;
            // ESC / window-close ABORT the run back to the title via the normal
            // game-over path (MORT death music + THE END), matching the Python
            // reference.  The EXE quits straight to DOS on
            // ESC (its INT 9 ISR latches DS:0x87eb → FUN_210c_0c53 teardown →
            // INT 21h/4Ch) — but that 90s hard-exit is DELIBERATELY NOT
            // preserved: it's hostile UX (no menu, no save).  Treated as an
            // EXE-bug-to-fix; an in-game menu is the future home for quit.
            // Finding: esc_quits_to_dos_via_int9_flag.md (intentional-divergence).
            if (ev.type == SDL_QUIT) abort_to_title = true;
            // ── F5 bug-report form owns input while open (before pause) ──
            if (report_open) {
                if (report_edit_open) {
                    const EditResult er = edit_handle_event(report_edit, ev);
                    if (er == EditResult::kSave) {
                        report_bind.set("report.description",
                                        report_edit.editor.text());
                        SDL_StopTextInput();
                        report_edit_open = false;
                    } else if (er == EditResult::kCancel) {
                        SDL_StopTextInput();
                        report_edit_open = false;
                    }
                    continue;
                }
                if (ev.type != SDL_KEYDOWN) continue;
                const auto rsym = ev.key.keysym.sym;
                if (report_confirm.is_open()) {
                    if (rsym == SDLK_LEFT || rsym == SDLK_RIGHT ||
                        rsym == SDLK_UP || rsym == SDLK_DOWN ||
                        rsym == SDLK_a || rsym == SDLK_d)
                        report_confirm.move(1);
                    else if (rsym == SDLK_ESCAPE)
                        report_confirm.close();          // back to the form
                    else if (rsym == SDLK_RETURN || rsym == SDLK_SPACE) {
                        if (report_confirm.apply_selected())
                            report_save_pending = true;  // freeze block writes
                        else
                            report_open = false;         // Discard
                        report_confirm.close();
                    }
                    continue;
                }
                if (rsym == SDLK_UP || rsym == SDLK_w) report_menu.move(-1);
                else if (rsym == SDLK_DOWN || rsym == SDLK_s) report_menu.move(+1);
                else if (rsym == SDLK_LEFT || rsym == SDLK_a) {
                    const std::string tb = report_bind.get("report.tag");
                    report_menu.adjust(-1);
                    if (report_bind.get("report.tag") != tb)
                        report_retemplate_if_untouched();
                } else if (rsym == SDLK_RIGHT || rsym == SDLK_d) {
                    const std::string tb = report_bind.get("report.tag");
                    report_menu.adjust(+1);
                    if (report_bind.get("report.tag") != tb)
                        report_retemplate_if_untouched();
                } else if (rsym == SDLK_RETURN || rsym == SDLK_SPACE) {
                    const std::string tb = report_bind.get("report.tag");
                    const std::string a = report_menu.activate();
                    if (a.rfind("__edit_text:", 0) == 0) {
                        report_edit.editor.set_text(
                            report_bind.get("report.description"));
                        report_edit.title = "Description";
                        report_edit.focus = EditFocus::kText;
                        SDL_StartTextInput();
                        report_edit_open = true;
                    } else if (!report_menu.is_open()) {
                        open_report_confirm();           // 'Back' left the form
                    } else if (report_bind.get("report.tag") != tb) {
                        report_retemplate_if_untouched();
                    }
                } else if (rsym == SDLK_ESCAPE) {
                    open_report_confirm();
                }
                continue;
            }
            if (ev.type == SDL_KEYDOWN) {
                const auto sym = ev.key.keysym.sym;
                // In-game Pause menu owns input while open; swallow gameplay keys.
                if (pause_open) {
                    // Confirm dialog intercepts all input while open (§8.6
                    // step 4).  SettingsFlow resolves move/apply/discard/
                    // cancel through the pause hooks above (OL-B1).
                    if (confirm_dlg.is_open()) {
                        pause_flow.handle_key(flow_key_from_sym(sym));
                        continue;
                    }
                    if (sym == SDLK_ESCAPE) {
                        pause_menu.back();
                        if (!pause_menu.is_open()) pause_open = false;
                    } else if (sym == SDLK_UP || sym == SDLK_w) {
                        pause_menu.move(-1);
                    } else if (sym == SDLK_DOWN || sym == SDLK_s) {
                        pause_menu.move(+1);
                    } else if (sym == SDLK_LEFT || sym == SDLK_a) {
                        pause_menu.adjust(-1);
                    } else if (sym == SDLK_RIGHT || sym == SDLK_d) {
                        pause_menu.adjust(+1);
                    } else if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
                        pause_menu.activate();
                    }
                    continue;
                }
                // --cheats interactive power-up picker (non-EXE-faithful test
                // aid; off during replay so traces stay deterministic).
                if (cheat_open) {
                    auto grant = [&](int bt) {
                        systems::dispatch_bonus_activate(g.state, bt);
                        std::printf("cheat: granted %s\n", kPowerupNames[bt]);
                        cheat_open = false;
                    };
                    if (sym == SDLK_ESCAPE || sym == SDLK_F7) cheat_open = false;
                    else if (sym == SDLK_UP || sym == SDLK_w)
                        cheat_sel = (cheat_sel + 5) % 6;
                    else if (sym == SDLK_DOWN || sym == SDLK_s)
                        cheat_sel = (cheat_sel + 1) % 6;
                    else if (sym == SDLK_RETURN || sym == SDLK_SPACE)
                        grant(cheat_sel);
                    else if (sym >= SDLK_1 && sym <= SDLK_6)
                        grant(static_cast<int>(sym - SDLK_1));
                    continue;   // swallow other keys while the menu is up
                }
                if (sym == SDLK_ESCAPE) {
                    // ESC now opens the Pause overlay (Resume / Options / Cheats
                    // / Restart / Quit to Title / Quit to Desktop), superseding
                    // the bare ESC→game-over.  Quit to Title still routes through
                    // abort_to_title (the game-over→title path).  Falls back to a
                    // direct title-abort if menus.json failed to load.
                    if (menu_ok) { pause_open = true; pause_menu.open("pause"); }
                    else abort_to_title = true;
                }
                else if (sym == SDLK_F5) {
                    // F5 opens the in-engine bug-report form: freeze the sim,
                    // seed fresh fields, and let the form/editor/confirm own
                    // input until Save (writes) or Discard.  The screenshot is
                    // the pre-form frame stashed by the freeze block below.
                    report_seed();
                    report_menu.open("bug_report");
                    report_open = report_menu.is_open();
                    report_edit_open = false;
                    report_frame_ready = false;
                }
                else if (opts.cheats && !replay.active() && sym == SDLK_F7) {
                    cheat_open = true;
                    cheat_sel = 0;
                }
            }
        }
        // ── Options-exit detection (§8.6 step 2): after input handling,
        // SettingsFlow checks whether the menu just transitioned from inside
        // the Options subtree (membership derived from menus.json) back to
        // the pause root, and opens the confirm dialog if changes are
        // staged.  ──
        if (pause_open && pause_menu.is_open() && !confirm_dlg.is_open())
            pause_flow.track_screen(pause_menu.current_screen());

        // ── Pause overlay: handle exit actions, else freeze the sim and draw
        // the menu (or confirm dialog) over a dark backdrop.  The `continue`
        // skips frame-counter, run_frame, post-frame logic and gameplay render
        // — a full freeze.  Quit to Title routes through the normal
        // game-over→title path; Quit to Desktop / Restart use new outcomes. ──
        // ── F5 report form: freeze + draw over the frozen scene (before the
        // pause block; the two are mutually exclusive since F5 only fires
        // outside pause).  Save writes the report from the stashed frame. ──
        if (report_open) {
            g.state.god_mode = god_active;
            FrameBuffer pf{320, 200};
            compose_frame(pf, g.state, g.render, /*draw_player=*/true);
            if (!report_frame_ready) {   // clean scene = the screenshot source
                report_frame = pf;
                report_frame_ready = true;
            }
            if (report_save_pending) {
                report_save_pending = false;
                report_open = false;
                const BugAnnotations ann{report_bind.get("report.tag"),
                                         report_bind.get("report.repro"),
                                         report_bind.get("report.description")};
                const bool want_presented = hd || wsp.present_path();
                const std::string dir = write_bug_report(
                    g.state, report_frame, g.render.entity_sprites,
                    display_level, internal, overlay_scale, ann,
                    /*has_presented=*/want_presented);
                // screenshot_presented.png — what the player actually saw: the
                // scene run through the live present (HD upscale + widescreen
                // margins), which the native report_frame skips.  Re-render the
                // (frozen) scene WITHOUT presenting so RenderReadPixels sees the
                // backbuffer (a post-present read is black on Metal), then read
                // the output-resolution pixels.  Only meaningful when the
                // present path transforms the frame (HD or widescreen); classic
                // 1× is pixel-equal to the native shot.  Empty bubble hook: the
                // L1-secret cosmetic bubbles are immaterial to a bug shot.
                if (!dir.empty() && want_presented) {
                    if (wsp.present_path())
                        wsp.present(std::function<void(RenderTarget&)>{},
                                    /*do_present=*/false);
                    else
                        upload_and_show(report_frame, /*with_hud=*/true,
                                        /*do_present=*/false);
                    int ow = 0, oh = 0;
                    SDL_GetRendererOutputSize(ren, &ow, &oh);
                    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                        0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                    if (s != nullptr &&
                        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                             s->pixels, s->pitch) == 0) {
                        save_surface_image(s, dir + "/screenshot_presented.png");
                    }
                    if (s != nullptr) SDL_FreeSurface(s);
                }
                continue;
            }
            if (report_edit_open) {
                draw_edit_overlay(pf, g.charset, report_edit);
            } else if (report_confirm.is_open()) {
                draw_confirm(pf, report_confirm, g.charset, /*dim=*/true,
                             /*draw_text=*/true);
            } else {
                draw_menu(pf, report_menu, g.charset, /*dim=*/true,
                          /*draw_text=*/true,
                          g.render.entity_sprites.size() > 33
                              ? &g.render.entity_sprites[33]
                              : nullptr,
                          &g.render.palette);
            }
            upload_and_show(pf, /*with_hud=*/false);
            SDL_Delay(frame_ms);
            continue;
        }
        if (pause_open) {
            if (want_quit_program) { outcome = LevelOutcome::kQuitProgram; break; }
            if (want_restart)      { outcome = LevelOutcome::kRestartLevel; break; }
            if (want_load)         { outcome = LevelOutcome::kLoadCheckpoint; break; }
            if (want_warp) {
                out_warp_display = want_warp;
                outcome = LevelOutcome::kWarpLevel;
                break;
            }
            if (want_reinit) {
                reinit_req.state = capture_save(g, display_level);
                out_reinit = reinit_req;
                outcome = LevelOutcome::kReinitDisplay;
                break;
            }
            if (abort_to_title)    { outcome = LevelOutcome::kGameOver;     break; }
            g.state.god_mode = god_active;   // keep systems in sync if toggled
            FrameBuffer pf{320, 200};
            // Backdrop = the frozen game scene, composed at NATIVE 320×200 (the
            // classic overload, so it works regardless of --hd-profile), then
            // dimmed behind the menu slab.  State is frozen, so re-composing
            // each pause frame is idempotent.
            compose_frame(pf, g.state, g.render, /*draw_player=*/true);
            if (confirm_dlg.is_open()) {
                // Confirm dialog replaces the menu while open (§8.6 step 5).
                draw_confirm(pf, confirm_dlg, g.charset, /*dim=*/true,
                             /*draw_text=*/!use_hd_text);
            } else {
                // In HD the glyphs are drawn crisply by the vector overlay
                // (draw_menu_rows_hd in upload_and_show); here draw the slab +
                // accent only.  In classic, draw the bitmap glyphs too.
                draw_menu(pf, pause_menu, g.charset, /*dim=*/true,
                          /*draw_text=*/!use_hd_text,   // bitmap unless enhanced
                          g.render.entity_sprites.size() > 33
                              ? &g.render.entity_sprites[33]
                              : nullptr,    // blank score-bone pointer …
                          &g.render.palette);   // … in the level's colours
            }
            upload_and_show(pf, /*with_hud=*/false);  // also dumps the shot (below)
            if (std::getenv("OLDUVAI_PAUSE_SHOT")) {
                // Quit the whole program, not just this level's frame loop —
                // otherwise the sequencer advances to the next level and never
                // exits (the pause_shot hung until timeout). kQuitProgram is the
                // "Quit to Desktop" outcome run_game exits on.
                outcome = LevelOutcome::kQuitProgram;
                running = false;
                break;
            }
            SDL_Delay(frame_ms);
            continue;
        }
        // Frame-counter wrap drives the timer (1 Hz-ish) and food-out
        // death.  The original resets when the PRE-increment value
        // exceeded 0x3C — i.e. after fc=61 has been used — giving a
        // 62-value cycle (1..61, then 0..61).  // DS:0x985a
        if (!cheat_open && g.state.frame_counter > 0x3D) {
            g.state.frame_counter = 0;
            if (g.state.timer > 0) {
                --g.state.timer;
            } else if (g.state.player.death_counter == 0) {
                if (god_active) {
                    g.state.timer = 99;   // god refills timer
                } else {
                    systems::trigger_death(g.state);
                }
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        systems::FrameInputs in;
        if (replay.active()) {
            // The reference replay convention reads key state for the
            // NEXT frame (its oracle injects keys for frame+1 after
            // tracing frame N) — match it or every input lands one
            // frame late.
            in = replay.at(frame + 1);
            if (frame > replay.last_frame() + 18) running = false;
        } else {
            in.left = keys[SDL_SCANCODE_LEFT] != 0 || gamepad::left();
            in.right = keys[SDL_SCANCODE_RIGHT] != 0 || gamepad::right();
            in.up = keys[SDL_SCANCODE_UP] != 0 || gamepad::up();
            in.down = keys[SDL_SCANCODE_DOWN] != 0 || gamepad::down();
            const bool attack_held = keys[SDL_SCANCODE_SPACE] != 0 ||
                                     keys[SDL_SCANCODE_LCTRL] != 0 ||
                                     gamepad::attack_held();
            // Autofire reads the PRE-frame latch/club state — exactly what
            // this frame's latch check will see — and must stay ahead of
            // input_rec.record below so recordings hold the resolved pulses.
            autofire.cooldown = autofire_cooldown(opts.autofire);
            in.attack = autofire.attack(attack_held,
                                        g.state.player.club_flag,
                                        g.state.player.attack_latch);
        }
        // Record the RESOLVED inputs (live or replay-injected) at the frame
        // the reader will resolve them — the loop reads replay.at(frame+1),
        // so emit at frame+1 to round-trip back to this same game frame.
        if (input_rec.active()) input_rec.record(frame + 1, in);

        // Previous-tick snapshot for the smooth-motion lerp — every
        // field the reference interpolates (via
        // save_prev_positions): player x/y + dx/dy (ghost rise),
        // entity x/y/current_y/throw/draw_dy, fireball, glider,
        // death halo, rolling stone, score popups.
        g.state.player.prev_x = g.state.player.x;
        g.state.player.prev_y = g.state.player.y;
        g.state.player.prev_dx = g.state.player.dx;
        g.state.player.prev_dy = g.state.player.dy;
        for (auto& e : g.state.entities) {
            e.prev_x = e.x;
            e.prev_y = e.y;
            e.prev_current_y = e.current_y;
            e.prev_throw_x = e.throw_x;
            e.prev_throw_y = e.throw_y;
            e.prev_draw_dy = e.draw_dy;
        }
        g.state.prev_stone_x = g.state.stone_x;
        g.state.prev_stone_y = g.state.stone_y;
        g.state.prev_fireball_x = g.state.fireball_x;
        g.state.prev_fireball_y = g.state.fireball_y;
        g.state.prev_glider_x = g.state.glider_x;
        g.state.prev_glider_y = g.state.glider_y;
        g.state.prev_death_halo_x = g.state.death_halo_x;
        g.state.prev_death_halo_y = g.state.death_halo_y;
        for (auto& b : g.state.score_bonuses) {
            b.prev_x = b.x;
            b.prev_y = b.y;
        }
        // Apply this frame's inputs before the pre-frame systems that
        // read them (flight physics steers from the live key state).
        g.state.input.left = in.left;
        g.state.input.right = in.right;
        g.state.input.jump = in.jump || in.up;
        g.state.input.down = in.down;
        g.state.input.attack = in.attack;

        // Enhanced widescreen: let birds fly fully off the WIDE edge before
        // despawning — the EXE bound (x<-50) is off the 320 screen, so in
        // widescreen the bird vanished ~a margin inside the strip.  Gated on
        // wsp.active() (wsp.margin()==0 ⇒ -50) so the classic 320 path keeps
        // the faithful threshold (extending it off-320 would shift the bird's
        // respawn cadence).
        // Replay/trace sessions keep the CLASSIC bounds even in widescreen:
        // the extension is a sim change (bird lifetime + respawn phase), so a
        // recording made in one mode would desync in the other.  A margin-
        // popping bird during a verification replay is the cheaper cost
        // (2026-07-03 review F4).
        const bool ws_bird_ext =
            wsp.active() && !replay.active() && !trace.active();
        for (auto& e : g.state.entities)
            if (e.obj_type == core::ObjType::Bird) {
                e.off_screen_left = ws_bird_ext ? -(wsp.margin() + 50) : -50;
                e.bird_spawn_x = ws_bird_ext ? (355 + wsp.margin()) : 355;
            }
        // Enhanced #20 — complete a deferred cave-sign teleport at the LOGIC
        // step, inside the snapshot<->classifier bracket: the pre-frame
        // snapshot above still holds the cave state (was_inside,
        // prev_cave_index, prev_screen), so the transition classifier below
        // sees the cave->surface edge and plays the cave fade pair.
        // Completing it in the end-of-tick block (where the countdown is
        // decremented) mutated the state AFTER the classifier, and the next
        // tick misclassified the change as a surface pan-scroll.
        if (!cheat_open) systems::try_complete_sign_teleport(g.state);
        g.state.skip_player_update = systems::tick_cave_descent(g.state);
        if (!cheat_open) systems::run_frame(g.state, in);
        // Tier-1 living margins: cycle the peek monsters' walk sprites IN
        // PLACE, once per logic tick.  No translation, no RNG, no collision —
        // pure sprite animation on cloned lists (the anchor at the spawn post
        // is what keeps entry pop-free).  L3A alternates ride the live global
        // phase counter so margin cadence matches the centre screen.
        if (!cheat_open && wsp.active()) wsp.tick_margin_monsters();
        // --god: hold the debug invariants every frame — energy/lives/food
        // topped and game-over masked (mirrors the Python per-level god
        // refresh + hit_player's energy refill).  Death, ghost animation and respawn run
        // NORMALLY — we deliberately do NOT clear death_counter.  A fall past
        // y=180 plays the regular ghost and respawns at the last safe spot; you
        // simply never run out of lives, and damage never kills (energy is
        // always full).  Clearing the fall death here used to strand a fallen
        // god player off-screen forever (clamp_player_position never clamps Y),
        // a soft-lock and a silent divergence from Python.  See Finding
        // god_mode_fall_void_vs_python_parity.md.
        if (god_active) {
            g.state.player.energy = 999;
            g.state.player.lives = 99;
            if (g.state.food_count < systems::kFoodGate)
                g.state.food_count = systems::kFoodGate;
            g.state.game_over = false;
        }
        // ── Post-frame, in the reference loop's order ──
        // 6b. Death halo.
        if (g.state.player.death_counter == 1) {
            systems::init_death_halo(g.state);
        } else if (g.state.player.death_counter > 1) {
            systems::tick_death_halo(g.state);
        } else {
            g.state.death_halo_active = false;
        }
        // 6c/6d. L5 glider entry + the screen-12 detach/fly-away.
        systems::check_l5_glider_entry(g.state);
        systems::handle_l5_screen12_glider(g.state);
        // 6e. Clamp + death-by-fall BEFORE exits and transitions (they
        // fire even in glider mode, where the player update returns
        // early).
        if (!g.state.screen_change) {
            systems::clamp_player_position(g.state);
        }
        systems::check_death_by_fall(g.state);
        // 7. Cave/secret exits (the trampoline fires before the exit
        // check so a bounce can reach the exit threshold same-frame).
        if (g.state.cave_flag) {
            g.state.secret_spring_bouncing = false;
            systems::check_cave_exit(g.state);
        } else if (g.state.secret_flag) {
            g.state.secret_spring_bouncing =
                systems::update_secret_trampoline(g.state);
            systems::check_secret_exit(g.state);
        }
        // 8. Surface transitions — secret entry takes priority.
        if (!g.state.cave_flag && !g.state.secret_flag) {
            g.state.secret_spring_bouncing = false;
            if (!systems::check_secret_entry(g.state)) {
                systems::check_screen_transition(g.state);
            }
        }
        // 8a. Cave-warp animation (not while inside a cave).
        if (!g.state.cave_flag) {
            systems::check_cave_warp_animation(g.state);
        }
        if (g.state.level_complete) {
            // The original jumps straight to its exit block — the
            // pseudo-exit screen never binds or renders.
            g.state.screen_change = false;
            // Fade to black from the LAST PRESENTED gameplay frame (already in
            // fb), then the tally.  Do NOT re-compose g.state here: by the time
            // level_complete is set, transitions.cpp has wrapped the player to
            // the LEFT edge of the final screen (the EXE pseudo-exit), so
            // re-composing flashes that one-frame glitch.  The EXE/Python jump
            // straight to the fade from the clean last frame (the reference
            // breaks before rendering the level-complete state).
            if (wsp.active()) {
                // Wide fade-to-black: keep the window FULL-WIDTH so the side bars
                // don't pop in at the start (the 320 `present` path pillarboxes
                // frame 0, shrinking the just-wide gameplay frame — the "kick off"
                // the user saw).  Compose a native-320 last frame with the player
                // restored to its pre-pseudo-exit position (transitions.cpp wrapped
                // it to the screen's left edge; composing as-is would flash that —
                // see line ~2384), wrap it wide, and fade the WIDE buffer via
                // present_wide_transition with_hud=false (the HUD darkens with the
                // scene, like the classic with_hud=false fade).
                FrameBuffer last_center{};
                {
                    const int sx = g.state.player.x, sy = g.state.player.y;
                    g.state.player.x = prev_px;
                    g.state.player.y = prev_py;
                    RenderTarget rt{last_center.px.data(), 320, 200, 1, nullptr,
                                    nullptr};
                    rt.advance_state = false;
                    compose_frame(rt, g.state, g.render, /*draw_player=*/true);
                    g.state.player.x = sx;
                    g.state.player.y = sy;
                }
                std::vector<std::uint8_t> last_wide;
                wsp.wrap_wide_static(last_center, last_wide);   // no edge-player mirror
                // Icy glider fly-away level-end: the glider exits to the RIGHT
                // past x=320, but wrap_wide_static baked the 320 centre (glider
                // clipped at the edge), so the fade-to-tally shows it cut off.
                // Re-draw the entities into the WIDE buffer at origin_x=wsp.margin()
                // with the right overflow allowed, so the glider flies into the
                // margin during the fade — matching the live fly-away.
                if (g.state.current_level == 5 && g.state.glider_active) {
                    const int sx = g.state.player.x, sy = g.state.player.y;
                    g.state.player.x = prev_px;
                    g.state.player.y = prev_py;
                    RenderTarget wrt{last_wide.data(), wsp.native_w(), 200, 1,
                                     nullptr, nullptr};
                    wrt.origin_x = wsp.margin();
                    wrt.advance_state = false;
                    wrt.clip_x_lo = wsp.margin();   // protect the left margin
                    presentation::draw_entities(wrt, g.state, g.render,
                                                /*draw_player=*/true);
                    g.state.player.x = sx;
                    g.state.player.y = sy;
                }
                FrameBuffer src{wsp.native_w(), 200};
                src.px = last_wide;
                FrameBuffer wf{wsp.native_w(), 200};
                bool quit = false;
                for (int f2 = 0; f2 <= kFadeFrames && !quit; ++f2) {
                    apply_fade(wf, src, static_cast<double>(f2) / kFadeFrames);
                    wsp.present_transition(wf.px, /*with_hud=*/false);
                    SDL_Event e2;
                    while (SDL_PollEvent(&e2)) {
                        if (handle_fullscreen_toggle(e2, win)) continue;
                        if (e2.type == SDL_QUIT) { running = false; quit = true; }
                    }
                    SDL_Delay(frame_ms);
                }
            } else {
                FrameBuffer work{fb_w, fb_h};
                for (int f2 = 0; f2 <= kFadeFrames; ++f2) {
                    apply_fade(work, fb, static_cast<double>(f2) / kFadeFrames);
                    if (!present(work)) break;
                }
            }
            // The score tally plays BONUS.MDI (FUN_270a_01b4,
            // play_music(MUSIC_BONUS)) — same as the boss tally.  BONUS.MDI is
            // in FILESA.CUR (also FILESB.CUR); track id 1.  Buzzer variant
            // (BONUSBUZ.MDI when DS:0x8db5=='I') is a follow-up.
            if (audio.music_available()) {
                // EXE Level_EndScreen fades the level track out (MDI_FadeStop
                // 1f75:00e4) before starting BONUS.MDI (1f75:01bb) — match
                // that fade so the music is never abruptly cut.
                audio.fade_out_music();
                formats::CurArchive ba(slurp(opts.game_dir / "FILESA.CUR"));
                formats::CurArchive bb(slurp(opts.game_dir / "FILESB.CUR"));
                const std::vector<std::uint8_t>* md = nullptr;
                if (ba.contains("BONUS.MDI")) md = &ba.get("BONUS.MDI").data;
                else if (bb.contains("BONUS.MDI")) md = &bb.get("BONUS.MDI").data;
                if (md != nullptr) {
                    audio.play_music(*md, formats::mdi_track_id("bonus.mdi"));
                }
            }
            show_score_tally(g.state, display_level, 500, g.charset,
                             g.render.palette, present, skip_held, tally_hd_text,
                             TallyAudio{&audio, opts.enhance.cinematic_cue});
            outcome = LevelOutcome::kComplete;
            running = false;
        }
        if (g.state.game_over || abort_to_title) {
            // The MORT.MDI death music + THEEND.PC1 picture are now shown by
            // the consolidated game-over sequence in run_game's post-loop
            // (FUN_2bd7_02e7 outer loop), so it fires for boss
            // deaths too — not just platform deaths.  Here we only signal the
            // outcome and stop the level loop.
            outcome = LevelOutcome::kGameOver;
            running = false;
        }
        if (g.state.screen_change) {
            // Classify the visual effect + capture the old screen's
            // frame before the rebind (playback happens after the new
            // screen's first compose, below).
            const bool now_inside =
                g.state.cave_flag || g.state.secret_flag;
            slide_old_wide_ok = false;   // rebuilt per kind 3/4 classification
            // warp_fade fires for cave-warp screen changes the EXE fades via
            // bp-6=0 → Sprite_DrawDispatch mode=2.  Two paths reach it:
            //  - cave_warp_freeze == 0x3E8: the L3 trunk-cave / L7 lava-cave
            //    descent warp (0xFA1>>2 lands here the teleport frame).
            //  - cave_warp_pending: the L3 11→12 trunk-cave EXIT and the
            //    L7-style screen-9 ENTRY, which set screen_change directly and
            //    never touch cave_warp_freeze.  Mirrors the reference implementation.
            bool warp_fade = g.state.player.cave_warp_freeze == 0x3E8 ||
                             g.state.player.cave_warp_pending;
            g.state.player.cave_warp_pending = false;   // per-frame transient
            // L3 (dark woods) 17→18: the trunk-descent animation IS the
            // visual transition — the reference suppresses the pan so the
            // descent isn't preceded by a flash of screen 18 (finding
            // l3_screen18_trunk_descent_missing.md).
            const bool l3_trunk_descent =
                prev_screen == 17 && g.state.current_screen == 18 &&
                systems::seam_kind(g.state.current_level, 17, 18) ==
                    systems::SeamKind::TrunkDescent;
            // L7 fake cave (12↔13): EXE entry is an instant warp (capstone
            // 25b2:07df jumps over the wipe).  The reference pans in
            // classic mode (documented simplification) and fades under
            // --enhanced (intentional-divergence-cosmetic, matching the
            // L3 cave-system convention) — mirror both.
            const bool l7_fake_cave =
                std::abs(prev_screen - g.state.current_screen) == 1 &&
                systems::seam_kind(g.state.current_level, prev_screen,
                                   g.state.current_screen) ==
                    systems::SeamKind::FakeCaveInstant;
            if (l7_fake_cave && opts.enhanced) warp_fade = true;
            if (l3_trunk_descent) {
                // ── Descent widescreen margins (#1) ──────────────────────
                // The trunk-descent composes a NATIVE 320×200 frame per step
                // and hands it to a present callback.  The default callback
                // (upload_and_show) pillarboxes that native frame with pure-
                // black bars under widescreen, so the margins VANISH for the
                // whole animation.  Instead, composite each native frame into
                // the bound screen's static wide background (backdrop + ground
                // extended to the edge — the SAME no-neighbour fill the steady
                // screen-18 view uses) so the bezel stays filled and the
                // descent ends seamlessly into the widescreen surface.  Pure
                // rendering: no LCG/state touched (the smoke jitter is rolled
                // logic-side), so the rng-critical descent is unaffected.
                // Rebuilt per phase because Phase 1 is bound to screen 17 and
                // the pan + Phase 2 to screen 18.
                std::vector<std::uint8_t> descent_wide;   // native wsp.native_w()×200
                // Phase-1 (screen 17) and Phase-2 (screen 18) wide margins, kept
                // so the camera pan can scroll BOTH (screen 17 up, screen 18 in
                // from below) in lockstep with the centre — no margin pop.
                std::vector<std::uint8_t> descent_m17, descent_m18;
                const bool descent_ws =
                    wsp.active() && hd && hd_scale > 1 && wsp.wide_tex() != nullptr;
                // The descent stages (Phase 1 / pan / Phase 2) render `substeps`
                // sub-frames per 18 Hz logical step in enhanced mode (l3_end_
                // level.cpp: substeps = enhanced ? 3 : 1) and rely on the CALLER
                // pacing each sub-frame at frame_ms/substeps to keep the wall-
                // clock duration constant.  Pacing every sub-frame at the full
                // frame_ms ran the whole descent 3× too slow (the "pathetic"
                // perf) and starved the pan's continuous interpolation.  Pace at
                // the sub-frame budget so the pan glides at 54 Hz and the total
                // duration matches the reference.
                const Uint32 descent_step_ms =
                    frame_ms / static_cast<Uint32>(opts.enhance.descent_pan ? 3 : 1);
                // F5 during the descent: the blocking descent loop never reaches
                // the frame-service bug-capture, so capture the full WS composite
                // here instead (standalone PNG — no g.state frame service).
                int descent_shot_seq = 0;
                bool descent_shot = false;
                bool descent_prev_f5 = false;   // F5 rising-edge latch
                auto build_descent_margins = [&]() {
                    if (!descent_ws) return;
                    // Build the descent margins EXACTLY like the steady view
                    // (get_static_wide_bg_hd): the SAME neighbour peeks
                    // (ws_left/ws_right for the bound screen), so the margins are
                    // identical to the steady screen before and after the descent
                    // — no pop-in/out at the boundaries.  Using REAL neighbours
                    // also kills the earlier bark strip: Phase-1's right margin is
                    // a peek of screen 18, not a no-neighbour MIRROR of screen
                    // 17's foreground tree column.  Only a genuine level-edge
                    // (screen 18's right) falls back to the layer extension.
                    wsp.compose_static_wide_bg(descent_wide);
                };
                auto present_ws_descent = [&](const FrameBuffer& f,
                                              bool with_hud) -> bool {
                    const Uint32 t0 = SDL_GetTicks();
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (handle_fullscreen_toggle(ev, win)) continue;
                        if (ev.type == SDL_QUIT) return false;
                        if (ev.type == SDL_KEYDOWN &&
                            ev.key.keysym.sym == SDLK_ESCAPE)
                            return false;
                    }
                    // F5 is consumed by the descent function's OWN SDL_PollEvent
                    // loop (it runs before this present callback), so the keydown
                    // never reaches the drain above.  Read the key STATE directly
                    // (level-triggered, not queue-drained) with a rising-edge
                    // latch so one press = one capture.
                    {
                        const Uint8* ks = SDL_GetKeyboardState(nullptr);
                        const bool f5 = ks != nullptr && ks[SDL_SCANCODE_F5] != 0;
                        if (f5 && !descent_prev_f5) descent_shot = true;
                        descent_prev_f5 = f5;
                    }
                    const std::size_t need =
                        static_cast<std::size_t>(wsp.native_w()) * 200 * 4;
                    if (!descent_ws || descent_wide.size() != need) {
                        // Classic / non-widescreen: pillarbox via upload_and_show.
                        FrameBuffer copy = f;
                        upload_and_show(copy, with_hud);
                        const Uint32 el = SDL_GetTicks() - t0;
                        if (el < descent_step_ms) SDL_Delay(descent_step_ms - el);
                        return true;
                    }
                    // Composite the native descent frame into the static margins.
                    std::vector<std::uint8_t> wide = descent_wide;
                    for (int y = 0; y < 200; ++y)
                        std::memcpy(
                            &wide[(static_cast<std::size_t>(y) * wsp.native_w() +
                                   wsp.margin()) * 4],
                            &f.px[static_cast<std::size_t>(y) * 320 * 4],
                            320 * 4);
                    // Neighbour seam overhang into the centre: descent_wide's
                    // margins carry the straddler completion, but the descent
                    // frame `f` is composed natively (320) without the
                    // neighbour's tiles, so the memcpy above buried it —
                    // re-apply band-limited (shared helper; Phase-2/pan seam
                    // lists are empty, so those present calls no-op).
                    wsp.reapply_seam_bands(wide);
                    enhance::EnhancedHudLayout hud_layout;
                    const bool hud = with_hud && use_hd_text;
                    if (hud) {
                        hud_layout =
                            enhance::compute_enhanced_hud_layout(hd_text, g.state);
                        enhance::draw_enhanced_hud_bars(wide, wsp.native_w(), 200, 1,
                                                        hud_layout, wsp.margin());
                    }
                    std::vector<std::uint8_t> up = enhance::upscale_rgba(
                        wide, wsp.native_w(), 200, hd_scale, opts.hd_profile);
                    SDL_UpdateTexture(wsp.wide_tex(), nullptr, up.data(),
                                      wsp.native_w() * hd_scale * 4);
                    SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, wsp.wide_tex(), nullptr, nullptr);
                    if (hud && hd_text.ok()) {
                        int ow = 0, oh = 0;
                        if (text_overlay.begin(ren, hd_text, ow, oh)) {
                            wsp.draw_wide_hud_text(text_overlay.buffer(), ow,
                                                   oh, hud_layout);
                            text_overlay.flush(ren, logical_w, logical_h);
                        }
                    }
                    if (descent_shot) {   // F5 pressed mid-descent — save full WS
                        descent_shot = false;
                        int ow = 0, oh = 0;
                        SDL_GetRendererOutputSize(ren, &ow, &oh);
                        SDL_Surface* sh = SDL_CreateRGBSurfaceWithFormat(
                            0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                        if (sh != nullptr &&
                            SDL_RenderReadPixels(ren, nullptr,
                                SDL_PIXELFORMAT_RGBA32, sh->pixels,
                                sh->pitch) == 0) {
                            // Same root as the F5 report dirs (this IS the
                            // descent's F5 capture) — never the cwd.
                            const std::filesystem::path root =
                                bug_report_root();
                            std::error_code ec;
                            std::filesystem::create_directories(root, ec);
                            char p[64];
                            std::snprintf(p, sizeof p, "descent_ws_%03d.png",
                                          descent_shot_seq++);
                            const std::string path = (root / p).string();
                            save_surface_image(sh, path);
                            std::fprintf(stderr, "[WS-SHOT] saved %s\n",
                                         path.c_str());
                        }
                        if (sh != nullptr) SDL_FreeSurface(sh);
                    }
                    SDL_RenderPresent(ren);
                    const Uint32 work = SDL_GetTicks() - t0;
                    if (work < descent_step_ms) SDL_Delay(descent_step_ms - work);
                    return true;
                };
                auto descent_present = [&](const FrameBuffer& f) -> bool {
                    return present_ws_descent(f, /*with_hud=*/false);
                };
                // Phase 1 (FUN_2276_0282): run BEFORE bind_screen so the
                // animation shows screen 17's platform sliding down against
                // screen 17's backdrop.  LCG jitter is rolled logic-side
                // AFTER Phase 1 and BEFORE Phase 2 (reference ordering:
                // step 8c — roll_l3_descent_smoke_jitter then
                // run_l3_trunk_descent).
                //
                // Headless / replay: Phase 1 and Phase 2 animations are
                // blocking display loops — in headless (opts.frames > 0)
                // the present lambda already returns immediately, so they
                // burn through frames quickly.  The LCG consumption
                // (jitter roll = 40 draws) MUST happen regardless.
                //
                // Build the L3-surface-specific sprite vectors needed by
                // the descent renderer (already loaded into g.render / g.grot3).
                // The descent functions compose into a NATIVE 320×200 buffer
                // and pass it to `present`; `present` then upscales for HD.
                // We must NOT pass the HD-sized gameplay fb directly — the
                // descent does `fb = bg_static` (native copy) which would
                // resize it and corrupt subsequent gameplay composition.
                {
                    // g.render.tile_sprites = ELEML3(28) + ELEML3B(5) + GROT3(2)
                    // (appended in bind_screen L3 surface path).
                    // Decoration tiles use indices 0/1 (= ELEML3[0/1]).
                    // The transition already advanced current_screen to 18, but
                    // these are SCREEN-17's margins — restore it to 17 so the
                    // per-screen dead-end rules (trunk-skip + dirt-void in
                    // compose_static_wide_bg_native) fire; otherwise the giant
                    // trunk spills into the strip for the whole descent.
                    const int saved_cs = g.state.current_screen;
                    g.state.current_screen = prev_screen;   // 17
                    build_descent_margins();   // screen 17's static wide margins
                    descent_m17 = descent_wide;   // saved for the pan scroll
                    g.state.current_screen = saved_cs;       // back to 18
                    FrameBuffer native_fb{};   // native 320×200 for descent
                    if (!run_l3_screen17_descent(
                            g.state, g.render.tile_sprites,
                            g.render.entity_sprites, g.render.palette,
                            g.tiles, g.grot3, native_fb,
                            opts.enhance.descent_pan,
                            g.render.extend_top_backdrop, descent_present)) {
                        running = false;
                    }
                }
                // Roll smoke jitter logic-side — 40 LCG draws consumed in
                // the same order as the EXE regardless of rendering mode.
                // FUN_2276_03d9:0x0554 (smoke A) + 0x0586 (smoke B), iters 0..19.
                systems::roll_l3_descent_smoke_jitter(g.state);

                // Bind screen 18 now (before Phase 2).  Phase 2 renders
                // screen-17 records descending into screen-18's backdrop.
                systems::clear_per_screen_state(g.state);
                bind_screen(g, g.state.current_screen);
                wsp.update_cache();   // recompute peek for the new screen
                build_descent_margins();     // screen 18's static wide margins
                descent_m18 = descent_wide;  // saved for the pan scroll
                g.state.screen_change = false;
                g.state.transition_skip = true;

                // Enhancement #11 (opt-in, NOT EXE-faithful): camera-pan
                // bridging Phase 1 → Phase 2.  The EXE hard-swaps the backdrop
                // here; this glides screen 17 up/out and screen 18 in from
                // below.  Gated on opts.enhance.descent_pan inside the helper
                // (no-op + early return when the flag is off → the default path
                // is byte-identical).  The pan touches NO LCG/RNG state — it
                // builds backdrops from the already-bound L3 surface tiles and
                // scrolls them — so the trace/corpus is unaffected.  Runs AFTER
                // bind_screen(18) so screen 18's tiles are available for the
                // incoming surface, mirroring the reference where
                // the pan precedes Phase 2.  Smoke jitter was already rolled
                // above; pan order relative to the roll is irrelevant (pan = 0
                // LCG draws).
                //
                // present_hud: like `present` but draws the enhanced HUD over
                // each pan frame (with_hud=true) so the HUD stays visible across
                // the pan — reference _present_frame draws the HUD every pan
                // frame.  No state mutation:
                // draw_enhanced_hud_* only reads g.state.
                if (running && opts.enhance.descent_pan) {
                    // present_hud: the descent present WITH the enhanced HUD on
                    // each pan frame.  Also scrolls the wide MARGINS in lockstep
                    // with the centre pan — screen-17 margins recede UP, screen-18
                    // margins enter from BELOW, the SAME geometry run_l3_descent_
                    // pan uses for the centre (ody=-t*H, ndy=H+ody) — so the bezel
                    // transitions screen 17 → 18 seamlessly with the centre
                    // instead of popping at the phase boundary.
                    const int pan_n = 12 * (opts.enhance.descent_pan ? 3 : 1);
                    int pan_i = 0;
                    auto present_hud = [&](const FrameBuffer& f) -> bool {
                        if (descent_ws &&
                            descent_m17.size() == descent_wide.size() &&
                            descent_m18.size() == descent_wide.size()) {
                            const double t =
                                static_cast<double>(++pan_i) / pan_n;
                            const int H = 200;
                            const int ody = -static_cast<int>(t * H);
                            const int ndy = H + ody;
                            const std::size_t rowb =
                                static_cast<std::size_t>(wsp.native_w()) * 4;
                            std::fill(descent_wide.begin(), descent_wide.end(), 0);
                            auto shift = [&](const std::vector<std::uint8_t>& src,
                                             int sdy) {
                                for (int y = 0; y < H; ++y) {
                                    const int sy = y - sdy;
                                    if (sy < 0 || sy >= H) continue;
                                    std::memcpy(
                                        &descent_wide[static_cast<std::size_t>(y) *
                                                      rowb],
                                        &src[static_cast<std::size_t>(sy) * rowb],
                                        rowb);
                                }
                            };
                            shift(descent_m17, ody);   // screen 17 recedes up
                            shift(descent_m18, ndy);   // screen 18 enters below
                        }
                        return present_ws_descent(f, /*with_hud=*/true);
                    };
                    FrameBuffer native_pan{};   // native 320×200 for the pan
                    if (!run_l3_descent_pan(
                            g.state, g.render.tile_sprites,
                            g.render.entity_sprites, g.render.palette,
                            g.tiles, g.grot3, native_pan,
                            opts.enhance.descent_pan,
                            g.render.extend_top_backdrop, present_hud)) {
                        running = false;
                    }
                    descent_wide = descent_m18;   // Phase 2 = static screen-18
                }

                // Phase 2 (FUN_2276_03d9): 21 iters, y_offset −80→0.
                if (running) {
                    const auto& td = g.tiles;
                    FrameBuffer native_fb2{};   // native 320×200 for Phase 2
                    if (!run_l3_trunk_descent(
                            g.state, g.render.tile_sprites,
                            g.render.entity_sprites, g.render.palette,
                            td, g.grot3, native_fb2,
                            opts.enhance.descent_pan,
                            g.render.extend_top_backdrop, descent_present)) {
                        running = false;
                    } else {
                        // Stamp the descent-overlay tiles onto screen 18's
                        // collision + render list (mirrors EXE
                        // FUN_2276_03d9 Collision_StampDur on iter 20;
                        // reference: _setup_screen_collision after run_l3_trunk_descent).
                        std::vector<presentation::LevelRenderAssets::TileDraw>
                            overlay;
                        for (const auto& tp : l3_descent_overlay_tiles(td)) {
                            const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
                            if (idx >= 0 &&
                                idx < static_cast<int>(g.dur.tiles.size())) {
                                g.state.collision.stamp_tile(
                                    g.dur.tiles[static_cast<std::size_t>(idx)].segments,
                                    tp.x, tp.y);
                            }
                            overlay.push_back({idx, tp.x, tp.y});
                        }
                        // Draw order matches the reference:
                        // the overlay renders BEFORE screen 18's own records,
                        // so the ground row covers the descended trunk's
                        // bottom — the trunk stays BEHIND the ground exactly
                        // as during the descent animation.  Appending it
                        // (drawn last) popped the trunk bottom OVER the
                        // ground on the first steady frame.  Insert after the
                        // bind-injected backdrop, before the level tiles.
                        const auto ins =
                            g.render.tiles.begin() +
                            std::min<std::ptrdiff_t>(
                                g.render.backdrop_tile_count,
                                static_cast<std::ptrdiff_t>(
                                    g.render.tiles.size()));
                        g.render.tiles.insert(ins, overlay.begin(),
                                              overlay.end());
                        // Enhanced descent package: arm the settling-dust tail
                        // on the steady screen (same gate as the in-animation
                        // dust extension).
                        if (opts.enhance.descent_pan)
                            l3_smoke_tail = kL3SmokeTailTicks;
                    }
                }
                transition_kind = 0;   // no additional transition needed
            } else if (!now_inside && !was_inside && !warp_fade) {
                transition_kind = 1;   // surface pan-scroll
                const int ddx = g.state.player.x - prev_px;
                const int ddy = g.state.player.y - prev_py;
                if (std::abs(ddx) >= std::abs(ddy)) {
                    transition_dir = ddx < 0 ? 'R' : 'L';
                } else {
                    transition_dir = ddy < 0 ? 'D' : 'U';
                }
                // Pan-scroll: re-compose the outgoing frame WITHOUT the
                // player — the old screen's assets and entity binding
                // are still live here (bind_screen runs below).  Both
                // slide surfaces carrying a player shows two of them
                // mid-pan; player-less old frame = the player "rides"
                // the incoming screen, as in the original.  (Reference
                // fix: renders old_surf with draw_player=False.)
                {
                    auto rt = make_rt(transition_old);
                    compose_frame(rt, g.state, g.render,
                                  /*draw_player=*/false);
                }
                draw_hud_for_fb(transition_old);
            } else {
                // Detect enhanced-mode secret-entry / secret-exit slides.
                // Secret entry: was on surface (!was_inside), now in secret
                //   (now_inside && g.state.secret_flag).
                // Secret exit:  was in secret (was_secret), now on surface
                //   (!now_inside).
                const bool is_secret_entry =
                    opts.enhance.secret_slide && !was_inside && now_inside &&
                    g.state.secret_flag;
                const bool is_secret_exit =
                    opts.enhance.secret_slide && was_secret && !now_inside;
                if (is_secret_entry) {
                    // kind 3: 12-frame downward slide (surface→secret).
                    // Old frame: current surface, player-less so the player
                    // "rides" into the new screen.
                    transition_kind = 3;
                    {
                        auto rt = make_rt(transition_old);
                        compose_frame(rt, g.state, g.render,
                                      /*draw_player=*/false);
                    }
                    draw_hud_for_fb(transition_old);
                    if (wsp.active()) {
                        // Wide OLD frame for the slide: a native-320 sibling of
                        // transition_old (same state → same content), wrapped with
                        // the OLD surface cache (peek), still live before the
                        // rebind.  Built here because the kind 1/2 wide block below
                        // only handles those kinds.
                        FrameBuffer oc{};
                        RenderTarget rt{oc.px.data(), 320, 200, 1, nullptr,
                                        nullptr};
                        rt.advance_state = false;
                        compose_frame(rt, g.state, g.render, /*draw_player=*/false);
                        wsp.wrap_wide(oc, slide_old_wide);
                        slide_old_wide_ok = true;
                    }
                } else if (is_secret_exit) {
                    // kind 4: 30-frame upward slide (secret→surface) with
                    // player-arc overlay.  Both surfaces rendered player-less;
                    // the overlay draws the jump sprite traversing from
                    // secret-exit-x to return-x.
                    transition_kind = 4;
                    // Save arc parameters before the state is mutated by
                    // bind_screen / clear_per_screen_state below.
                    slide_secret_exit_x = g.state.secret_exit_x;
                    slide_end_x = g.state.player.x;
                    slide_end_y = g.state.player.y;
                    // Old (secret) frame: re-render player-less at the exit
                    // position.  Move player to exit pos for render, then
                    // restore.
                    const int saved_rx = g.state.player.x;
                    const int saved_ry = g.state.player.y;
                    g.state.player.x = slide_secret_exit_x;
                    g.state.player.y = systems::kSecretFloorY - 30;
                    // Outgoing secret frame uses the last live scatter in
                    // g.render.tiles — no new LCG draws (parity with the
                    // classic path, which rolls 0 here).
                    // Note: the secret_flag is now 0 (exit cleared it), but
                    // the secret render assets are still live until bind_screen
                    // replaces them below.  Temporarily re-set secret_flag so
                    // compose_frame uses secret assets.
                    g.state.secret_flag = 1;
                    // Build bubble hook for the secret-side old surface.
                    std::function<void(RenderTarget&)> old_bubble_hook;
                    if (g.fluid_bubbles_initialized) {
                        const auto& bsnap = g.fluid_bubbles.bubbles();
                        const auto& tspr = g.render.tile_sprites;
                        const auto& tpal = g.render.palette;
                        old_bubble_hook = [&bsnap, &tspr, &tpal](RenderTarget& frm) {
                            for (const auto& b2 : bsnap) {
                                const int i2 = b2.sprite_idx;
                                if (i2 >= 0 && i2 < static_cast<int>(tspr.size()))
                                    blit_sprite_keyed(frm, tspr[static_cast<std::size_t>(i2)],
                                                      tpal, static_cast<int>(b2.x),
                                                      static_cast<int>(b2.y));
                            }
                        };
                    }
                    {
                        auto rt = make_rt(transition_old);
                        compose_frame(rt, g.state, g.render,
                                      /*draw_player=*/false, old_bubble_hook);
                    }
                    draw_hud_for_fb(transition_old);
                    if (wsp.active()) {
                        // Wide OLD frame for the slide, built WHILE the transient
                        // secret state is live (secret_flag=1, player at exit,
                        // old_bubble_hook) — a native-320 sibling of transition_old
                        // wrapped with the secret-room cache (no neighbours, null
                        // backdrop → self-tile margins).  Must precede the restore.
                        FrameBuffer oc{};
                        RenderTarget rt{oc.px.data(), 320, 200, 1, nullptr,
                                        nullptr};
                        rt.advance_state = false;
                        compose_frame(rt, g.state, g.render, /*draw_player=*/false,
                                      old_bubble_hook);
                        wsp.wrap_wide(oc, slide_old_wide);
                        slide_old_wide_ok = true;
                    }
                    g.state.secret_flag = 0;   // restore
                    g.state.player.x = saved_rx;
                    g.state.player.y = saved_ry;
                } else {
                    transition_kind = 2;   // cave/secret/warp fade pair
                    // Fades keep the player on the old frame (the blend to
                    // black hides it) — snapshot last frame as displayed.
                    transition_old = fb;
                }
            }
            // ── Widescreen transitions (§8.7) — outgoing frame composed WIDE
            // BEFORE the rebind, while the cache still reflects the OLD screen ──
            // Decide whether this transition involves a ws_present_path screen on
            // the OLD side (the new side is checked after the rebind, below).  We
            // widen the two flavors whose 320 path pops the bars / jumps the HUD:
            //   • kind 1 (surface↔surface pan) — the user's main complaint
            //   • kind 2 (cave/secret/warp fade pair)
            // The enhanced secret SLIDES (kind 3/4) stay on the 320 path: they
            // are vertical pans between a surface and a self-tile secret room, and
            // kind 4 temporarily mutates secret_flag/player to render its outgoing
            // frame — too entangled to widen cleanly without regression risk.  In
            // widescreen those slides are consistently 320-pillarboxed (no
            // mid-flavor pop); widening them is a documented follow-up.
            ws_transition = false;
            transition_old_wide.clear();
            bool ws_old = false;
            const bool ws_eligible_kind =
                (transition_kind == 1 || transition_kind == 2 ||
                 transition_kind == 3 || transition_kind == 4);
            if (wsp.active() &&
                (transition_kind == 1 || transition_kind == 2)) {
                ws_old = wsp.present_path();   // OLD cache still live here
                // Native-320 outgoing center matching the kind's content, composed
                // from g.state (still the OLD screen — assets not yet rebound):
                //   kind 1 (pan): player-LESS (the player rides the incoming
                //                 screen, mirroring the HD transition_old compose).
                //   kind 2 (fade): player-INCLUDED (the fade-to-black hides it; the
                //                  320 path snapshots `fb` with the player on it).
                FrameBuffer old_center{};   // 320×200
                {
                    // kind 2 (cave/secret/warp fade): by the time this block runs,
                    // enter_cave / exit_cave have ALREADY moved the player to the
                    // NEW screen's entry position and flipped cave_flag /
                    // current_screen.  The classic 320 path dodges this by
                    // snapshotting `fb` (the genuine last frame); the wide path
                    // re-composes, so without restoring the OLD screen state here
                    // the outgoing fade frame draws the player (and cave-vs-surface
                    // mode) at the NEW position over the OLD background — the
                    // "player flashes/jumps between the cave-exit and surface-entry
                    // position" regression (matches the early Python fix
                    // screen_scroll_transition_in_game.md, restricted there to the
                    // pan path).  The OLD screen's assets + entities are still bound
                    // (bind_screen runs below), so restoring the pre-change flags +
                    // screen + player position reproduces the true outgoing frame.
                    // advance_state=false: this is a render-only re-compose; the
                    // one authoritative per-tick advance is the main fb compose.
                    const int cur_px = g.state.player.x;
                    const int cur_py = g.state.player.y;
                    const int cur_screen = g.state.current_screen;
                    const int cur_cave = g.state.cave_flag;
                    const int cur_cave_index = g.state.cave_index;
                    const int cur_secret = g.state.secret_flag;
                    const int cur_psprite = g.state.player.sprite;
                    const int cur_pdx = g.state.player.dx;
                    const int cur_pdy = g.state.player.dy;
                    const int cur_pfacing = g.state.player.facing_left;
                    const int cur_pclub = g.state.player.club_flag;
                    const int cur_emerge = g.state.cave_emerge_frames;
                    if (transition_kind == 2) {
                        g.state.player.x = prev_px;
                        g.state.player.y = prev_py;
                        g.state.current_screen = prev_screen;
                        g.state.cave_flag = was_cave ? 1 : 0;
                        // Restore the outgoing cave's index too (the exit set it
                        // to -1) so the STOP-sign render fires in the fade frame.
                        g.state.cave_index = was_cave ? prev_cave_index : -1;
                        g.state.secret_flag = was_secret ? 1 : 0;
                        // Player presentation of the LAST PRESENTED frame (the
                        // classic 320 path gets this for free by snapshotting
                        // `fb`): descent frame 46 + its dx on cave entry; the
                        // pre-emerge walk sprite (emerge=prev, normally 0) on
                        // cave exit.  advance_state=false above keeps the
                        // club-flag draw from double-decrementing.
                        // (Teleport tick fields are deliberately NOT swapped:
                        // on the sign-teleport consume tick both prev and cur
                        // values hide the player entirely — identical output —
                        // and the cloud FX is drawn by game_app hooks, not by
                        // compose_frame.)
                        g.state.player.sprite = prev_psprite;
                        g.state.player.dx = prev_pdx;
                        g.state.player.dy = prev_pdy;
                        g.state.player.facing_left = prev_pfacing;
                        g.state.player.club_flag = prev_pclub;
                        g.state.cave_emerge_frames = prev_emerge;
                    }
                    RenderTarget rt{old_center.px.data(), 320, 200, 1, nullptr,
                                    nullptr};
                    rt.advance_state = false;
                    compose_frame(rt, g.state, g.render,
                                  /*draw_player=*/transition_kind == 2);
                    if (transition_kind == 2) {
                        g.state.player.x = cur_px;
                        g.state.player.y = cur_py;
                        g.state.current_screen = cur_screen;
                        g.state.cave_flag = cur_cave;
                        g.state.cave_index = cur_cave_index;
                        g.state.secret_flag = cur_secret;
                        g.state.player.sprite = cur_psprite;
                        g.state.player.dx = cur_pdx;
                        g.state.player.dy = cur_pdy;
                        g.state.player.facing_left = cur_pfacing;
                        g.state.player.club_flag = cur_pclub;
                        g.state.cave_emerge_frames = cur_emerge;
                    }
                }
                // Wrap with the OLD side's peek-vs-bezel rule.  Whether the wide
                // path actually runs is decided after the rebind (ws_old || ws_new).
                // A kind-2 fade's OUTGOING frame must carry the SAME margins as
                // the steady view it fades from — the peek cache + seam lists
                // (straddler completions, row bridges, black base) are still the
                // OLD screen's here, so wrap_wide_static reproduces it exactly.
                // wrap_wide (compose_widescreen torus/mirror) instead flashed
                // stale mirror content for the whole fade: the L3 trunk-entry
                // dirt clutter (this branch's original s9-only scope), the L7 S2
                // cave-entry rail gap, the S9→S10 warp-fade mirror, the S12→S13
                // fade's cave-hall backdrop.  Restore the outgoing screen number
                // so per-screen rules (L3 s9/s17 void, L7 cave-hall base) fire.
                // Cave/secret bezel sides (ws_old false) keep wrap_wide_for.
                if (ws_old && transition_kind == 2) {
                    const int cur_screen = g.state.current_screen;
                    g.state.current_screen = prev_screen;
                    wsp.wrap_wide_static(old_center, transition_old_wide);
                    g.state.current_screen = cur_screen;
                } else {
                    wsp.wrap_wide_for(old_center, ws_old, transition_old_wide);
                }
            } else if (wsp.active() &&
                       (transition_kind == 3 || transition_kind == 4)) {
                // kind 3/4: the wide OLD buffer was already built + wrapped inside
                // the classification block (slide_old_wide), while the secret
                // state was live; ws_old just records that the old side is wide.
                ws_old = slide_old_wide_ok;
            }
            // The L3 trunk-descent branch already called clear_per_screen_state,
            // bind_screen, and cleared screen_change above — skip the common
            // path when it ran.  All other transitions go through here.
            if (!l3_trunk_descent) {
                systems::clear_per_screen_state(g.state);
                bind_screen(g, g.state.current_screen);
                wsp.update_cache();   // recompute peek for the new screen
                g.state.screen_change = false;
                // The original skips walk/gravity for one frame after every
                // screen change (the screen-draw frame runs no gameplay).
                g.state.transition_skip = true;
                // The NEW screen's widescreen status is now known (cache updated).
                // The wide transition path runs when EITHER side is ws_present —
                // so neither side ever pillarbox-pops the bars mid-pan/fade.
                if (wsp.active() && ws_eligible_kind) {
                    const bool ws_new = wsp.present_path();
                    ws_transition = ws_old || ws_new;
                }
            }
            // Note: the L3 trunk-descent path keeps ws_transition false — its
            // descent animation is its own (non-peek) flow, left unchanged.
        }

        // Pending effect events from the frame logic.
        if (g.state.sfx_hit_pending) {
            audio.play_sfx("SFX_HIT");
            g.state.sfx_hit_pending = false;
        }
        if (g.state.sfx_generic_pending) {
            audio.play_sfx("SFX_GENERIC");
            g.state.sfx_generic_pending = false;
        }
        if (g.state.sfx_spring_pending) {
            audio.play_sfx("SFX_JUMP_APEX");
            g.state.sfx_spring_pending = false;
        }
        if (g.state.jump_apex_sfx_pending) {
            audio.play_sfx("SFX_JUMP_APEX");
            g.state.jump_apex_sfx_pending = false;
        }

        if (g.state.secret_flag) {
            const bool fluid = opts.enhance.fluid_bubbles &&
                               g.fluid_bubbles_initialized;
            refresh_secret_tiles(g, /*draw_scatter=*/!fluid);
            // Enhanced-mode: tick fluid bubbles AFTER the LCG pass so the
            // global LCG sequence is unaffected (cosmetic PRNG is separate).
            if (opts.enhance.fluid_bubbles && g.fluid_bubbles_initialized) {
                g.fluid_bubbles.tick();
            }
        }

        // Enhanced-mode secret-room bubble hook: draw 60 persistent rising
        // bubbles BEFORE the tile placements so the floor line at y=168
        // covers low-y bubbles (they emerge from behind the floor).
        // The hook is null in classic mode (LCG scatter draws normally).
        std::function<void(RenderTarget&)> bubble_hook;
        if (opts.enhance.fluid_bubbles && g.state.secret_flag &&
            g.fluid_bubbles_initialized) {
            const auto& bubbles_snap = g.fluid_bubbles.bubbles();
            const auto& tile_sprites = g.render.tile_sprites;
            const auto& palette = g.render.palette;
            // Widescreen: the secret room self-tiles its OWN walls into the
            // margins, but the fluid bubbles were drawn centre-only (origin_x
            // = margin), leaving the margins lifeless.  Mirror each ABOVE-floor
            // bubble into both margins so the whole room feels underwater.  The
            // mirror x's reflect the compose_widescreen self_tile mapping
            // (left: col c → output margin-1-c ⇒ centre-coord -1-x;
            //  right: col c → output margin+320+(319-c) ⇒ centre-coord 639-x)
            // and are MARGIN-INDEPENDENT; off-margin copies clip away on the
            // native fb.  Below-floor bubbles (y ≥ 168) are hidden by the floor
            // in the centre, so skipping them avoids poking through the
            // (non-redrawn) margin floor.  Cosmetic RNG only — no trace impact.
            const bool ws_mirror = wsp.active();
            bubble_hook = [&bubbles_snap, &tile_sprites, &palette,
                           ws_mirror](RenderTarget& frame) {
                // ELEML1.MAT sprites 17 and 18 — indices into tile_sprites
                // (surface_tiles for L1 = ELEML1.MAT; indices 0-based).
                for (const auto& b : bubbles_snap) {
                    const int idx = b.sprite_idx;   // 17 or 18
                    if (idx < 0 || idx >= static_cast<int>(tile_sprites.size()))
                        continue;
                    const formats::Sprite& spr =
                        tile_sprites[static_cast<std::size_t>(idx)];
                    // Float position → HD sub-pixel rounding: slow bubbles
                    // (1px/logic-frame) lerp to native sub-pixels across the 3
                    // sub-frames; rounding at HD (not native) keeps them moving
                    // each 54Hz sub-frame instead of collapsing to 18Hz steps.
                    blit_sprite_keyed(frame, spr, palette, b.x, b.y);
                    if (ws_mirror && b.y < 168.0f) {
                        blit_sprite_keyed(frame, spr, palette, -1.0f - b.x, b.y);
                        blit_sprite_keyed(frame, spr, palette, 639.0f - b.x, b.y);
                    }
                }
            };
        }
        {
            auto rt = make_rt(fb);
            // The SINGLE AUTHORITATIVE per-frame entity/player compose.  This is
            // the only pass that advances per-frame draw-state (club_flag swing
            // decrement, death/cave-warp clear) — advance_state stays at its
            // default true here.  It runs exactly once per gameplay tick and,
            // critically, BEFORE the smooth-motion sub-frame save/restore window
            // (saved_p is captured later), so the advance survives.
            //
            // In widescreen, wsp.present does NOT reuse this fb — it
            // composes its OWN bg center + draws the overflow entities with
            // advance_state = false (purely visual).  So even though entities are
            // DRAWN twice per widescreen frame (this fb + the overflow pass),
            // club_flag advances exactly ONCE (here).  fb itself is shown only on
            // the non-widescreen-present paths (pause, transitions, screenshot).
            compose_frame(rt, g.state, g.render, /*draw_player=*/true,
                          bubble_hook);
            draw_l3_smoke_tail(rt);
            draw_teleport_fx(rt);
        }
        // Arm the enhanced GET READY rainbow fly-away on the level-start rising
        // edge of get_ready_counter (load_level sets it to 0x11).  Wall-clock
        // start = now, so the hold + rocket-up play smoothly at any refresh rate.
        if (g.state.get_ready_counter == 0x11 && gr_prev_counter != 0x11) {
            gr_anim_start = SDL_GetTicks();
            gr_anim_active = true;
        }
        gr_prev_counter = g.state.get_ready_counter;
        // Descent dust tail dissipates once per logic tick (draw sites are
        // per-present; the countdown must not drain faster under smooth-motion).
        if (l3_smoke_tail > 0) --l3_smoke_tail;
        // Cave-EMERGE countdown: decremented ONCE PER TICK like the others,
        // but at the END of the tick (just before ++frame, below) — NOT here.
        // Reason (the fullscreen/widescreen "missing emerge" bug): the
        // windowed path shows `fb`, composed ABOVE this point, so a
        // decrement here was invisible to it — but wsp.present() RE-composes
        // from live state AFTER this point, so every widescreen tick showed
        // frames-1: the first reveal stage never reached the screen and the
        // wide kind-2 fade-in target lost a stage too.  End-of-tick keeps
        // every within-tick consumer (fb, wide fade new_center, steady wide
        // present, F5 re-present) on the SAME value the player sees.
        //
        // Enhanced #20 teleport clouds — same once-per-tick rule.  The
        // departure completion applies the DEFERRED sign teleport and
        // arms the arrival sequence.
        if (g.state.teleport_out_ticks > 0) {
            // Countdown ONLY — the deferred teleport itself completes at the
            // next tick's logic step (try_complete_sign_teleport, before
            // run_frame), inside the classifier's snapshot bracket, so the
            // cave->surface change plays the cave fade pair.
            --g.state.teleport_out_ticks;
        } else if (g.state.teleport_in_ticks > 0) {
            --g.state.teleport_in_ticks;
        }
        draw_hud_for_fb(fb);
        // GET READY banner counter: advance EXACTLY ONCE per logic tick, here at
        // the authoritative HUD point (draw_hud_for_fb runs once per tick), NOT
        // inside draw_hud — which is also called per smooth-motion sub-frame and
        // would drain the counter ~Nx faster (the "GET READY flashes too
        // briefly" bug in enhanced/smooth mode; classic was unaffected).  Drawn
        // BEFORE this decrement (draw_hud_for_fb just ran), preserving the EXE's
        // draw-then-decrement order (FUN_27f7_1277, DS:0x97e0, even frames,
        // window [2,17]).  Classic timing is byte-identical (one tick = one
        // decrement, same frame_counter parity as the old in-draw_hud site).
        if (g.state.get_ready_counter >= 2 && g.state.get_ready_counter <= 17 &&
            (g.state.frame_counter & 1) == 0) {
            --g.state.get_ready_counter;
        }
        // Screen-change transition playback — runs AFTER the new screen's
        // first frame is composed, animating from the last frame of the
        // old screen into it.  Surface↔surface = the pan-scroll the
        // original drives via CRTC start-address writes (2bd7 wipe family
        // → Video_SetDisplayStartAddress; the reference's
        // scroll_surface_transition, classic 12 frames at 18 Hz);
        // cave/secret enter+exit and the L3/L7 in-level warp = the
        // palette fade pair (FUN_1052_0c15 shape: fade-out → fade-in).
        // The HUD pans with the screen — both buffers carry their own
        // baked HUD, matching the original's full-screen CRTC pan.
        if (transition_kind != 0) {
            // Narrow shell context for the extracted blocking players
            // (transition_players.cpp, OL-B3).  Built per played transition —
            // the by-value fields carry this frame's values and pace_last
            // starts at 0, exactly like the old in-loop lambdas + the
            // per-frame `Uint32 pace_last = 0;` local they replace.
            TransitionShellCtx tctx;
            tctx.win = win;
            tctx.running = &running;
            tctx.draw_log = draw_log;
            tctx.frame_ms = frame_ms;
            tctx.smooth_motion = opts.enhance.smooth_motion;
            tctx.hd = hd;
            tctx.hd_scale = hd_scale;
            tctx.hd_profile = &opts.hd_profile;
            tctx.ws_margin = wsp.margin();
            tctx.ws_native_w = wsp.native_w();
            tctx.ws_backdrop_ok = wsp.backdrop_ok();
            tctx.ws_backdrop = &wsp.backdrop();
            tctx.hd_cache = &g.hd_cache;
            tctx.state = &g.state;
            tctx.render = &g.render;
            tctx.screen_count = static_cast<int>(g.tiles.screens.size());
            tctx.slide_secret_exit_x = slide_secret_exit_x;
            tctx.slide_end_x = slide_end_x;
            tctx.slide_end_y = slide_end_y;
            tctx.upload_and_show = [&](FrameBuffer& f) { upload_and_show(f); };
            tctx.present_wide_transition =
                [&wsp](std::vector<std::uint8_t>& wide, bool with_hud,
                       bool pre_upscaled) {
                    wsp.present_transition(wide, with_hud, pre_upscaled);
                };
            tctx.make_rt = [&](FrameBuffer& b) { return make_rt(b); };
            tctx.compose_static = [&](int s, FrameBuffer& out,
                                      bool frozen_full) {
                compose_surface_screen_static(g, s, out, nullptr, nullptr,
                                              frozen_full);
            };
            tctx.compose_wide_native =
                [&](int s, int margin, const FrameBuffer* backdrop,
                    std::vector<std::uint8_t>& wide) {
                    compose_surface_screen_wide_native(g, s, margin, backdrop,
                                                       wide);
                };
            tctx.build_assets = [&](int s, LevelRenderAssets& ra,
                                    systems::SystemsState& st) {
                build_surface_screen_assets(g, s, ra, st);
            };
            if (ws_transition && (transition_kind == 1 || transition_kind == 2)) {
                // Widescreen path: build the INCOMING wide buffer from the NEW
                // screen's native-320 center (cache now reflects the new screen),
                // then slide/fade the wide buffers.  Player-included for both
                // kinds: kind 1's player rides the incoming surface; kind 2's fade
                // hides it.  ws_new chooses peek-vs-bezel for the new side.
                const bool ws_new = wsp.present_path();
                FrameBuffer new_center{};   // 320×200
                {
                    RenderTarget rt{new_center.px.data(), 320, 200, 1, nullptr,
                                    nullptr};
                    compose_frame(rt, g.state, g.render, /*draw_player=*/true,
                                  bubble_hook);
                }
                // kind 1 = surface pan-scroll: use the continuous PANORAMA pan
                // (no tear) when it's a simple ±1 horizontal surface step.
                // Anything unusual (non-adjacent screen, cave/secret, level
                // wrap) falls back to the legacy two-buffer slide.
                const bool panorama_ok =
                    transition_kind == 1 && !g.state.cave_flag &&
                    !g.state.secret_flag && g.state.current_screen < 100 &&
                    prev_screen < 100 &&
                    std::abs(g.state.current_screen - prev_screen) == 1 &&
                    (transition_dir == 'R' || transition_dir == 'L');
                if (std::getenv("OLDUVAI_WS_DEBUG") != nullptr)
                    std::fprintf(stderr,
                                 "[WS-TRANS] kind=%d dir=%c %d->%d cave=%d "
                                 "secret=%d => %s\n",
                                 transition_kind, transition_dir, prev_screen,
                                 g.state.current_screen, g.state.cave_flag,
                                 g.state.secret_flag,
                                 panorama_ok ? "panorama" : "legacy");
                if (panorama_ok) {
                    play_panorama_wide(tctx, prev_screen,
                                       g.state.current_screen, new_center);
                } else {
                    std::vector<std::uint8_t> new_wide;
                    // The fade/slide NEW frame must carry the SAME no-neighbour
                    // margins as the steady view it hands off to: the steady frame
                    // (compose_static_wide_bg_native) re-draws the bg-tile rows
                    // into the margin, whereas wrap_wide (compose_widescreen only)
                    // TORUS-wraps the screen's far-edge columns there instead.
                    // For the L3 trunk-cave EXIT (screen 12) that wrapped the
                    // RIGHT-edge branch PLATFORMS into the LEFT margin — which then
                    // vanished the instant the steady frame replaced them: the
                    // 1-frame "platforms on exit" glitch.  Use the static-bg wrap
                    // on the peek path so the fade ends pixel-identical to steady;
                    // bezel sides (cave/secret rooms) keep their black margins.
                    if (ws_new) wsp.wrap_wide_static(new_center, new_wide);
                    else        wsp.wrap_wide_for(new_center, ws_new, new_wide);
                    play_transition_wide(tctx, transition_old_wide, new_wide,
                                         transition_kind, transition_dir);
                }
                transition_kind = 0;
            } else if (transition_kind == 3 || transition_kind == 4) {
                if (ws_transition && slide_old_wide_ok) {
                    // Wide secret slide (no 320 pillarbox bars): build the NEW
                    // wide buffer — native-320 player-less, current = new screen,
                    // cache now reflects it (kind 3 secret → self-tile, kind 4
                    // surface → peek) — and slide the wide buffers.  The kind-4
                    // player arc is drawn by play_transition_wide at +wsp.margin().
                    FrameBuffer nc{};
                    {
                        RenderTarget rt{nc.px.data(), 320, 200, 1, nullptr,
                                        nullptr};
                        rt.advance_state = false;
                        compose_frame(rt, g.state, g.render,
                                      /*draw_player=*/false, bubble_hook);
                    }
                    std::vector<std::uint8_t> new_wide;
                    wsp.wrap_wide(nc, new_wide);
                    play_transition_wide(tctx, slide_old_wide, new_wide,
                                         transition_kind, transition_dir);
                } else {
                    // Both enhanced-mode secret slides need the new (current)
                    // screen player-less — the player arc overlay is drawn by the
                    // slide loop itself (kind 4) or the player is just not shown
                    // mid-entry (kind 3).  Re-compose fb without the player.
                    FrameBuffer fb_noplayer{fb_w, fb_h};
                    {
                        auto rt = make_rt(fb_noplayer);
                        compose_frame(rt, g.state, g.render,
                                      /*draw_player=*/false, bubble_hook);
                    }
                    draw_hud_for_fb(fb_noplayer);
                    play_transition(tctx, transition_old, fb_noplayer,
                                    transition_kind, transition_dir);
                }
            } else {
                play_transition(tctx, transition_old, fb, transition_kind,
                                transition_dir);
            }
            transition_kind = 0;
        }
        // Debug/test hook: OLDUVAI_AUTO_FULLSCREEN=<frame> programmatically
        // toggles desktop-fullscreen at that gameplay frame (simulates Alt+Enter)
        // so the surface widescreen recompute path can be captured headlessly
        // (pair with --screenshot/--screenshot-frame).  Mirrors the boss hook.
        if (const char* afs = std::getenv("OLDUVAI_AUTO_FULLSCREEN")) {
            if (win != nullptr && frame == std::atoi(afs))
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        // Recompute widescreen state BEFORE the present-path selection so a
        // resize/Alt+Enter this frame is reflected the SAME frame: ws_present_path
        // reads wsp.active() + the (refreshed) neighbour cache, and the
        // activation refresh has rebuilt the backdrop.  Without this the selector
        // would read stale state for one frame and pillarbox (black bars) until
        // the next screen transition.  No-op when the output size is unchanged.
        wsp.rebuild_if_resized();
        const bool smooth = opts.enhance.smooth_motion && opts.frames <= 0 &&
                            opts.screenshot.empty();
        // True when the vsync render-fill loop already consumed this tick's
        // wall-time — the outer frame delay below must then NOT add its own
        // frame_ms sleep (that would halve the rate to ~9 Hz).
        bool smooth_vsync_ran = false;
        if (smooth) {
            // Three sub-frames per logic tick (54 Hz), interpolating
            // every field the reference does (via
            // apply_interpolated / restore_logic_positions): player x/y,
            // player dx/dy during ghost rise only, entity x/y +
            // current_y + throw + draw_dy, fireball, glider, death
            // halo, rolling stone, score popups.  Each lerp sits
            // behind the reference's 16-px teleport guard: a larger
            // delta is a discontinuity (screen change, warp, respawn)
            // and must SNAP — lerping sweeps the sprite across the
            // screen for one tick.
            systems::PlayerState saved_p = g.state.player;
            struct SavedEnt { int x, y, cy, tx, ty, ddy; };
            std::vector<SavedEnt> saved_e;
            saved_e.reserve(g.state.entities.size());
            for (const auto& e : g.state.entities) {
                saved_e.push_back({e.x, e.y, e.current_y, e.throw_x,
                                   e.throw_y, e.draw_dy});
            }
            const int sv_stone_x = g.state.stone_x;
            const int sv_stone_y = g.state.stone_y;
            const int sv_fb_x = g.state.fireball_x;
            const int sv_fb_y = g.state.fireball_y;
            const int sv_gl_x = g.state.glider_x;
            const int sv_gl_y = g.state.glider_y;
            const int sv_dh_x = g.state.death_halo_x;
            const int sv_dh_y = g.state.death_halo_y;
            std::array<std::pair<int, int>, 10> sv_bonus{};
            for (std::size_t bi = 0; bi < g.state.score_bonuses.size();
                 ++bi) {
                sv_bonus[bi] = {g.state.score_bonuses[bi].x,
                                g.state.score_bonuses[bi].y};
            }
            constexpr int kSnap = 16;   // reference _SNAP_THRESHOLD
            // A screen or cave/secret-mode change this tick is a teleport by
            // definition — the discontinuity signal the 16-px distance guard
            // cannot see when the warp lands nearby (the L3 S4 cave entry is
            // a (9,11)px hop; bug report 2026-07-17_165051_L3_S4 — the
            // sprite swept from the hole onto the cave background for one
            // tick).  Snap every lerp for this tick.
            const bool warp_snap =
                g.state.current_screen != prev_screen ||
                (g.state.cave_flag != 0 || g.state.secret_flag != 0) !=
                    was_inside;
            // vsync path: fill the tick's wall-time with vsync-paced interpolated
            // frames at a CONTINUOUS alpha (elapsed/tick).  fallback path: the
            // discrete smooth_N evenly-spaced sub-frames.  render_budget pays
            // back any prior overshoot so the long-term cadence stays 18 Hz.
            const Uint32 tick_t0 = SDL_GetTicks();
            const Uint32 render_budget =
                (vsync_active && frame_ms > smooth_carryover)
                    ? frame_ms - smooth_carryover
                    : frame_ms;
            int sub = 0;
            while (true) {
                ++sub;
                const Uint32 sub_t0 = SDL_GetTicks();
                float alpha;
                if (vsync_active) {
                    const Uint32 el = sub_t0 - tick_t0;
                    alpha = el >= frame_ms
                                ? 1.0f
                                : static_cast<float>(el) /
                                      static_cast<float>(frame_ms);
                } else {
                    alpha = static_cast<float>(sub) /
                            static_cast<float>(smooth_N);
                    if (alpha > 1.0f) alpha = 1.0f;
                }
                auto pair_lerp = [&](int& x, int& y, int prevx, int prevy,
                                     int curx, int cury) {
                    if (warp_snap || std::abs(curx - prevx) > kSnap ||
                        std::abs(cury - prevy) > kSnap) {
                        x = curx;
                        y = cury;
                        return;
                    }
                    x = prevx + static_cast<int>(
                                    std::lround((curx - prevx) * alpha));
                    y = prevy + static_cast<int>(
                                    std::lround((cury - prevy) * alpha));
                };
                auto one_lerp = [&](int& v, int prevv, int curv) {
                    if (warp_snap || std::abs(curv - prevv) > kSnap) {
                        v = curv;
                        return;
                    }
                    v = prevv + static_cast<int>(
                                    std::lround((curv - prevv) * alpha));
                };
                // Float render position (Part 1): 1-HD-pixel granularity vs the
                // int lerp's 4-HD-pixel snap.  draw_*() read fx/fy only when
                // use_float_pos is set on the target (below).  Same snap guard.
                auto pair_lerp_f = [&](float& fx, float& fy, int prevx,
                                       int prevy, int curx, int cury) {
                    if (warp_snap || std::abs(curx - prevx) > kSnap ||
                        std::abs(cury - prevy) > kSnap) {
                        fx = static_cast<float>(curx);
                        fy = static_cast<float>(cury);
                        return;
                    }
                    fx = prevx + (curx - prevx) * alpha;
                    fy = prevy + (cury - prevy) * alpha;
                };
                auto one_lerp_f = [&](float& fv, int prevv, int curv) {
                    fv = (warp_snap || std::abs(curv - prevv) > kSnap)
                             ? static_cast<float>(curv)
                             : prevv + (curv - prevv) * alpha;
                };
                pair_lerp(g.state.player.x, g.state.player.y,
                          saved_p.prev_x, saved_p.prev_y, saved_p.x,
                          saved_p.y);
                pair_lerp_f(smooth_player_fx, smooth_player_fy,
                            saved_p.prev_x, saved_p.prev_y, saved_p.x,
                            saved_p.y);
                // dx/dy carry the gait offset — interpolating them
                // staggers the walk; the reference lerps them only
                // during the ghost rise (smooth death float).
                if (g.state.player.ghost_rise != 0) {
                    pair_lerp(g.state.player.dx, g.state.player.dy,
                              saved_p.prev_dx, saved_p.prev_dy, saved_p.dx,
                              saved_p.dy);
                }
                for (std::size_t ei = 0; ei < g.state.entities.size();
                     ++ei) {
                    auto& e = g.state.entities[ei];
                    const auto& sv = saved_e[ei];
                    pair_lerp(e.x, e.y, e.prev_x, e.prev_y, sv.x, sv.y);
                    pair_lerp_f(e.fx, e.fy, e.prev_x, e.prev_y, sv.x, sv.y);
                    one_lerp(e.current_y, e.prev_current_y, sv.cy);
                    one_lerp_f(e.f_current_y, e.prev_current_y, sv.cy);
                    pair_lerp(e.throw_x, e.throw_y, e.prev_throw_x,
                              e.prev_throw_y, sv.tx, sv.ty);
                    pair_lerp_f(e.f_throw_x, e.f_throw_y, e.prev_throw_x,
                                e.prev_throw_y, sv.tx, sv.ty);
                    one_lerp(e.draw_dy, e.prev_draw_dy, sv.ddy);
                    one_lerp_f(e.f_draw_dy, e.prev_draw_dy, sv.ddy);
                }
                if (g.state.fireball_flag != 0) {
                    pair_lerp(g.state.fireball_x, g.state.fireball_y,
                              g.state.prev_fireball_x,
                              g.state.prev_fireball_y, sv_fb_x, sv_fb_y);
                    pair_lerp_f(g.state.fireball_fx, g.state.fireball_fy,
                                g.state.prev_fireball_x,
                                g.state.prev_fireball_y, sv_fb_x, sv_fb_y);
                }
                pair_lerp(g.state.glider_x, g.state.glider_y,
                          g.state.prev_glider_x, g.state.prev_glider_y,
                          sv_gl_x, sv_gl_y);
                pair_lerp_f(g.state.glider_fx, g.state.glider_fy,
                            g.state.prev_glider_x, g.state.prev_glider_y,
                            sv_gl_x, sv_gl_y);
                if (g.state.death_halo_active) {
                    pair_lerp(g.state.death_halo_x, g.state.death_halo_y,
                              g.state.prev_death_halo_x,
                              g.state.prev_death_halo_y, sv_dh_x, sv_dh_y);
                    pair_lerp_f(g.state.death_halo_fx, g.state.death_halo_fy,
                                g.state.prev_death_halo_x,
                                g.state.prev_death_halo_y, sv_dh_x, sv_dh_y);
                }
                if (g.state.stone_state != 0) {
                    pair_lerp(g.state.stone_x, g.state.stone_y,
                              g.state.prev_stone_x, g.state.prev_stone_y,
                              sv_stone_x, sv_stone_y);
                    pair_lerp_f(g.state.stone_fx, g.state.stone_fy,
                                g.state.prev_stone_x, g.state.prev_stone_y,
                                sv_stone_x, sv_stone_y);
                }
                for (std::size_t bi = 0;
                     bi < g.state.score_bonuses.size(); ++bi) {
                    auto& b = g.state.score_bonuses[bi];
                    if (b.counter > 0) {
                        pair_lerp(b.x, b.y, b.prev_x, b.prev_y,
                                  sv_bonus[bi].first, sv_bonus[bi].second);
                        pair_lerp_f(b.fx, b.fy, b.prev_x, b.prev_y,
                                    sv_bonus[bi].first, sv_bonus[bi].second);
                    }
                }
                // Enhanced-mode fluid-bubble lerp — same snap-guard as the
                // other smooth-motion fields (16 px threshold).
                // Bubble positions are floats; lerp each from prev to cur
                // at sub/3.  Respawn frames (prev==cur) naturally produce
                // delta=0 — no sweep artefact.
                struct SavedBubble { float x, y; };
                std::vector<SavedBubble> sv_bubbles;
                if (opts.enhance.fluid_bubbles && g.state.secret_flag &&
                    g.fluid_bubbles_initialized) {
                    auto& bvec = g.fluid_bubbles.bubbles_mutable();
                    sv_bubbles.reserve(bvec.size());
                    constexpr float kBubbleSnap = systems::kSnapThreshold;
                    for (auto& b : bvec) {
                        sv_bubbles.push_back({b.x, b.y});
                        const float dx = b.x - b.prev_x;
                        const float dy = b.y - b.prev_y;
                        if (std::abs(dx) <= kBubbleSnap &&
                            std::abs(dy) <= kBubbleSnap) {
                            b.x = b.prev_x + dx * alpha;
                            b.y = b.prev_y + dy * alpha;
                        }
                    }
                    // Debug/test hook: OLDUVAI_BUBBLE_TRACE=1 dumps, for the first
                    // slow-moving bubble, the lerped sub-frame position and BOTH
                    // the old native-int rounding and the new HD (scale*lround)
                    // rounding — proving the HD path advances each 54Hz sub-frame
                    // where the native path collapsed to one pixel (18Hz step).
                    if (std::getenv("OLDUVAI_BUBBLE_TRACE") && !bvec.empty()) {
                        const int sc = wsp.native_w() > 320 || hd_scale > 1
                                           ? hd_scale : 1;
                        for (const auto& b : bvec) {
                            // Pick the SLOWEST rising bubble (vy≈1): ~0.33px per
                            // 54Hz sub-frame — the case where native rounding
                            // collapses all 3 sub-frames to one pixel.
                            if (b.vy <= 1.2f &&
                                std::abs(b.y - b.prev_y) > 0.01f &&
                                std::abs(b.y - b.prev_y) <= kBubbleSnap) {
                                std::fprintf(
                                    stderr,
                                    "[BUBTRACE] vy=%.2f f=%d sub=%d y=%.3f "
                                    "native_int=%d hd_int=%ld\n",
                                    b.vy, frame, sub, b.y,
                                    static_cast<int>(b.y),
                                    std::lround(b.y * sc));
                                break;
                            }
                        }
                    }
                }
                {
                    auto rt = make_rt(fb);
                    // Smooth-motion sub-frame RE-render for interpolated display
                    // only — the authoritative per-frame draw-state advance
                    // already happened once on the main fb compose above (before
                    // saved_p was captured).  advance_state = false so the club
                    // swing decrement / death clear do NOT fire again here: this
                    // keeps club_flag STABLE across the 3 sub-frames (so the club
                    // sprite — and its widescreen overflow in wsp.present —
                    // renders on every sub-frame instead of vanishing after the
                    // first), and guarantees the net advance per tick stays one.
                    rt.advance_state = false;
                    rt.use_float_pos = true;   // Part 1: 1-HD-px player/entities
                    rt.player_fx = smooth_player_fx;
                    rt.player_fy = smooth_player_fy;
                    compose_frame(rt, g.state, g.render, /*draw_player=*/true,
                                  bubble_hook);
                    draw_l3_smoke_tail(rt);
                    draw_teleport_fx(rt);
                }
                draw_hud_for_fb(fb);
                // Restore bubble logic positions after sub-frame render.
                if (!sv_bubbles.empty()) {
                    auto& bvec = g.fluid_bubbles.bubbles_mutable();
                    for (std::size_t bi = 0; bi < bvec.size() && bi < sv_bubbles.size(); ++bi) {
                        bvec[bi].x = sv_bubbles[bi].x;
                        bvec[bi].y = sv_bubbles[bi].y;
                    }
                }
                apply_debug_overlays(fb);
                // Part 1: this is a smooth sub-frame — the WS overflow pass must
                // read the float render positions too.
                wsp.set_float_pos(true, smooth_player_fx, smooth_player_fy);
                if (wsp.present_path()) {
                    wsp.present(bubble_hook);
                } else {
                    dump_steady_fb();
                    upload_and_show(fb);
                }
                wsp.set_float_pos(false);
                log_draw(sub);
                // Debug/test hook: OLDUVAI_PACE_TRACE=1 prints the wall-clock
                // interval between consecutive presents — the boundary present
                // (sub=1, first after the logic tick) reveals whether the
                // tick-boundary cadence matches the within-tick 18.5ms slots.
                if (std::getenv("OLDUVAI_PACE_TRACE")) {
                    static Uint32 last_present = 0;
                    const Uint32 now = SDL_GetTicks();
                    std::fprintf(stderr, "[PACE] sub=%d dt=%u\n",
                                 sub, last_present ? now - last_present : 0);
                    last_present = now;
                }
                if (vsync_active) {
                    // The present above vsync-blocked to the panel.  Keep
                    // filling the tick until the render budget is met, then
                    // carry the overshoot (bounded to one tick) into the next
                    // tick's budget so the logic cadence averages 18 Hz.
                    const Uint32 el = SDL_GetTicks() - tick_t0;
                    if (el >= render_budget || sub >= 64) {
                        smooth_carryover = el > frame_ms
                                               ? std::min(el - frame_ms, frame_ms)
                                               : 0;
                        smooth_vsync_ran = true;
                        break;
                    }
                } else {
                    const Uint32 sub_spent = SDL_GetTicks() - sub_t0;
                    const Uint32 sub_ms =
                        frame_ms / static_cast<Uint32>(smooth_N);
                    if (sub >= smooth_N) break;
                    if (sub_spent < sub_ms) SDL_Delay(sub_ms - sub_spent);
                }
            }
            // Restore every logic value the sub-frames touched.
            g.state.player = saved_p;
            for (std::size_t ei = 0; ei < g.state.entities.size(); ++ei) {
                auto& e = g.state.entities[ei];
                const auto& sv = saved_e[ei];
                e.x = sv.x;
                e.y = sv.y;
                e.current_y = sv.cy;
                e.throw_x = sv.tx;
                e.throw_y = sv.ty;
                e.draw_dy = sv.ddy;
            }
            g.state.stone_x = sv_stone_x;
            g.state.stone_y = sv_stone_y;
            g.state.fireball_x = sv_fb_x;
            g.state.fireball_y = sv_fb_y;
            g.state.glider_x = sv_gl_x;
            g.state.glider_y = sv_gl_y;
            g.state.death_halo_x = sv_dh_x;
            g.state.death_halo_y = sv_dh_y;
            for (std::size_t bi = 0; bi < g.state.score_bonuses.size();
                 ++bi) {
                g.state.score_bonuses[bi].x = sv_bonus[bi].first;
                g.state.score_bonuses[bi].y = sv_bonus[bi].second;
            }
        } else {
            apply_debug_overlays(fb);
            if (wsp.present_path()) {
                wsp.present(bubble_hook);
            } else {
                dump_steady_fb();
                upload_and_show(fb);
            }
            log_draw(0);
        }

        // F5 bug capture — service a pending request now that fb holds the
        // composed frame for this tick.  Screenshots are written from a copy
        // (the overlay variants tint copies; the live fb is untouched).  In
        // normal play the --debug-* overlays are off, so fb is the clean
        // gameplay frame; this is the screenshot.png source.
        // (F5 bug capture now happens in the report-form freeze block above,
        //  writing from the stashed pre-form frame with the user's
        //  annotations.  TODO: restore the widescreen-composite screenshot for
        //  margin bugs — it needs bubble_hook, which is out of scope there.)

        // Post-render snapshot — matches the reference's frame-top capture
        // (render-side mutations like the club decrement already applied).
        if (trace.active()) trace.write(frame, g.state);

        // Cave-EMERGE countdown — once per logic tick, at END-OF-TICK so
        // every present path this tick (windowed fb, widescreen re-compose,
        // wide fade target, F5 re-present) showed the SAME frames value.
        // See the block comment at the l3_smoke_tail/teleport decrement
        // cluster above for the widescreen off-by-one this placement fixes.
        // (The reference engine decrements after display.flip(),
        // so end-of-tick is also the cross-engine-parity position.)
        if (g.state.cave_emerge_frames > 0) --g.state.cave_emerge_frames;

        ++frame;
        if (!opts.screenshot.empty() && frame == opts.screenshot_frame) {
            if (wsp.active()) {
                // Widescreen: re-render through the SAME present the live frame
                // used so the capture matches the screen exactly — peek frames
                // via wsp.present (wide composite), bezel/pillarbox
                // frames (caves, bosses, L3/L7) via upload_and_show (centre 320
                // pillarboxed at wsp.margin() with the correctly-mapped HUD).  Then
                // RenderReadPixels the full output.
                // Render WITHOUT presenting (do_present=false) so RenderReadPixels
                // sees the frame in the backbuffer — on Metal a read AFTER present
                // returns black (the swapchain image is gone).
                if (wsp.present_path())
                    wsp.present(bubble_hook, /*do_present=*/false);
                else
                    upload_and_show(fb, /*with_hud=*/true, /*do_present=*/false);
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (shot != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         shot->pixels, shot->pitch) == 0) {
                    save_surface_image(shot, opts.screenshot);
                }
                if (shot != nullptr) SDL_FreeSurface(shot);
            } else if (hd) {
                // HD: the vector HUD text now lives in the output-resolution
                // overlay (drawn AFTER the scene RenderCopy), not in fb — so
                // capture the FINAL rendered output (scene + overlay).  Re-draw
                // the scene + overlay WITHOUT presenting (RenderReadPixels reads
                // the backbuffer, which a prior present may have swapped away),
                // then read the output-resolution pixels.
                SDL_UpdateTexture(tex, nullptr, fb.px.data(), fb.w * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                if (use_hd_text) {
                    enhance::EnhancedHudLayout hud_layout =
                        enhance::compute_enhanced_hud_layout(hd_text, g.state);
                    // fb already carries the bars (drawn by the last
                    // upload_and_show); only the text overlay is missing.
                    int ow2 = 0, oh2 = 0;
                    if (text_overlay.begin(ren, hd_text, ow2, oh2)) {
                        enhance::draw_enhanced_hud_text(text_overlay.buffer(),
                                                        ow2, oh2, hd_text,
                                                        hud_layout);
                        draw_enhanced_banners(text_overlay.buffer(), ow2, oh2);
                        if (cheat_open)
                            draw_cheat_rows(text_overlay.buffer(), ow2, oh2);
                        text_overlay.flush(ren, logical_w, logical_h);
                    }
                }
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (shot != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         shot->pixels, shot->pitch) == 0) {
                    save_surface_image(shot, opts.screenshot);
                }
                if (shot != nullptr) SDL_FreeSurface(shot);
            } else {
                // Classic: the bitmap HUD is in the 320×200 buffer — save it.
                SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormatFrom(
                    fb.px.data(), fb.w, fb.h, 32, fb.w * 4,
                    SDL_PIXELFORMAT_RGBA32);
                save_surface_image(shot, opts.screenshot);
                SDL_FreeSurface(shot);
            }
            running = false;
        }
        if (opts.frames > 0 && frame >= opts.frames) running = false;
        // Debug/test hook: OLDUVAI_SMOOTH_FRAMES=<n> caps the run at n frames
        // WITHOUT going through opts.frames — which would force smooth=false
        // (line ~3693) and the classic transition path.  This lets the smooth
        // 54Hz sub-frame loop (and OLDUVAI_BUBBLE_TRACE) run headlessly for a
        // bounded number of frames, the one combination --play-frames can't give.
        if (const char* sf = std::getenv("OLDUVAI_SMOOTH_FRAMES")) {
            if (frame >= std::atoi(sf)) running = false;
        }

        const Uint32 spent = SDL_GetTicks() - t0;
        if (any_debug_overlay) perf_ms_accum += spent;
        if (frame_stats) {
            const double work_ms =
                static_cast<double>(SDL_GetPerformanceCounter() - fs_t0) *
                fs_perf_ms;
            ++fs_frames;
            if (work_ms > fs_worst_ms) {
                fs_worst_ms = work_ms;
                fs_worst_present_ms = fs_present_ms;  // present of the worst frame
            }
            if (work_ms > fs_budget_ms) ++fs_overruns;
        }
        // The vsync render-fill already paced this tick to (about) frame_ms via
        // the panel; keep the ticker in phase without an extra sleep.  Classic
        // path: drift-free absolute-deadline wait at the DOS PIT rate — the
        // old `SDL_Delay(55 - spent)` oversleep-per-frame is what made classic
        // feel choppier than DOSBox (owner report 2026-07-04).
        if (smooth_vsync_ran) {
            dos_ticker.arm();
        } else if (opts.vga_scan && !hd && vga_scan_ok) {
            // --vga-scan (default on for classic): hold-frame scanout.
            // Between ticks, re-present the SAME uploaded frame every vblank
            // — the software twin of the VGA scanning VRAM at 70 Hz.
            // Pixel-identical; vsync (implied for classic) paces each
            // present.  A driver that REFUSED vsync returns from present
            // instantly — three consecutive <1.5 ms presents disable the
            // scanout for this level and fall back to timer pacing (a
            // default must degrade, not spin).
            int fast_presents = 0;
            while (dos_ticker.pending()) {
                const Uint64 p0 = SDL_GetPerformanceCounter();
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                ++vga_fill_presents;
                const double ms =
                    (SDL_GetPerformanceCounter() - p0) * 1000.0 /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                if (ms < 1.5) {
                    if (++fast_presents >= 3) {
                        vga_scan_ok = false;
                        std::fprintf(stderr,
                                     "pacing: vsync appears refused — "
                                     "vga-scan off, timer pacing\n");
                        break;
                    }
                } else {
                    fast_presents = 0;
                }
            }
            ++vga_fill_ticks;
            if (dos_ticker.pending()) dos_ticker.wait_next();
            else dos_ticker.advance();
        } else {
            dos_ticker.wait_next();
        }
    }

    if (draw_log != nullptr) std::fclose(draw_log);
    if (vga_fill_ticks > 0 && std::getenv("OLDUVAI_PACE_TRACE"))
        std::fprintf(stderr,
                     "[PACE] vga-scan: %.2f presents/tick over %lu ticks\n",
                     static_cast<double>(vga_fill_presents) /
                         static_cast<double>(vga_fill_ticks),
                     vga_fill_ticks);
    if (frame_stats && fs_frames > 0)
        std::fprintf(stderr,
                     "frame-stats L%d: frames=%llu overruns=%llu(>%.1fms) "
                     "worst_work=%.2fms (present=%.2fms) budget=%.2fms\n",
                     display_level,
                     static_cast<unsigned long long>(fs_frames),
                     static_cast<unsigned long long>(fs_overruns), fs_budget_ms,
                     fs_worst_ms, fs_worst_present_ms, fs_budget_ms);
    audio.stop_music();
    carry.lives = g.state.player.lives;
    carry.score = g.state.score;
    SDL_DestroyTexture(tex);
    return outcome;
}

int run_game(const GameOptions& opts) {
    // Runtime-mutable copy — Options edits (hd_profile, render_scale, audio
    // device, etc.) apply this session through rt; launch-fixed fields
    // (game_dir, frames, screenshot, replay, trace, level) stay on opts.
    GameOptions rt = opts;
    static const int kOrder[7] = {1, 2, 5, 4, 3, 6, 7};
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "game: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    // Gamepad: subsystem + hotplug watch + button mapping from play.json
    // pad_* keys (see presentation/gamepad.hpp for the two-prong design).
    {
        gamepad::Config pcfg;
        pcfg.jump = gamepad::button_from_string(opts.pad_jump,
                                                SDL_CONTROLLER_BUTTON_A);
        pcfg.attack = gamepad::button_from_string(opts.pad_attack,
                                                  SDL_CONTROLLER_BUTTON_X);
        pcfg.pause = gamepad::button_from_string(opts.pad_pause,
                                                 SDL_CONTROLLER_BUTTON_START);
        pcfg.confirm = gamepad::button_from_string(opts.pad_confirm,
                                                   SDL_CONTROLLER_BUTTON_A);
        pcfg.back = gamepad::button_from_string(opts.pad_back,
                                                SDL_CONTROLLER_BUTTON_B);
        pcfg.deadzone = opts.pad_deadzone;
        gamepad::init(pcfg);
    }
    // Gameplay tables + AdLib SFX voice patches live in the user's own
    // executable — read and install them up front so the audio backend can
    // pre-render the OPL SFX (content policy: the engine ships no game
    // data).  Missing/corrupt files surface later via the level loader.
    try {
        install_exe_game_data(prepare::load_game_executable(opts.game_dir));
    } catch (const std::exception&) {}
    std::optional<SdlAudio> audio_opt;
    audio_opt.emplace(rt.music_device, rt.rom_dir, rt.soundfont,
                      rt.sfx_backend, rt.audio_rate, rt.audio_buffer,
                      rt.midi_port);
    // Enhanced mix: raise SFX polyphony (no rapid-retrigger cutoff) + duck the
    // music under the effects.  Faithful mode (default) keeps single-voice SFX
    // at the original fixed balance.
    audio_opt->set_mix_balance(rt.enhanced);
    std::printf("audio: music backend = %s\n",
                audio_opt->active_music_backend().c_str());
    auto load_all_sfx = [&](SdlAudio& a) {
        // Non-fatal on corrupt archives: the game runs without SFX and the
        // level loader (which parses the same files) reports the real error.
        try {
            formats::CurArchive fa(slurp(opts.game_dir / "FILESA.CUR"));
            formats::CurArchive fb2(slurp(opts.game_dir / "FILESB.CUR"));
            formats::CurArchive va(slurp(opts.game_dir / "FILESA.VGA"));
            formats::CurArchive vb(slurp(opts.game_dir / "FILESB.VGA"));
            load_sfx_bank(a, [&](const std::string& n)
                                 -> const std::vector<std::uint8_t>* {
                for (formats::CurArchive* ar : {&fa, &fb2, &va, &vb}) {
                    if (ar->contains(n)) return &ar->get(n).data;
                }
                return nullptr;
            });
        } catch (const std::exception& e) {
            std::fprintf(stderr, "audio: SFX bank not loaded (%s)\n", e.what());
        }
    };
    load_all_sfx(*audio_opt);
    CarriedState carry;
    // Sequencer position (EXE FUN_2bd7_04be slots): 0 = attract (intro cards
    // + title + main menu), 1..7 = play levels (display numbering), 8 = win
    // ending.  Out-of-range defensively falls back to L1 (the old behavior
    // for direct GameOptions constructions).
    int display = (opts.level >= 0 && opts.level <= 8) ? opts.level : 1;
    // Headless verification (--play-frames/--play-shot) stays single-level.
    const bool single = opts.frames > 0 || !opts.screenshot.empty();
    // Headless / replay runs never show the attract; an unspecified level
    // (the CLI maps "no --level" → 0) means L1 there, keeping gameplay
    // frame 0 deterministic (golden_trace + the oracle diff depend on it).
    if (display == 0 &&
        (single || !opts.replay.empty() || !opts.record_inputs.empty()))
        display = 1;

    // ── Single window for the whole session — same logical size for every
    // phase so the physical window never jumps and fullscreen state is kept.
    // Drop const: reinit may update hd/hd_scale when settings change.
    bool hd = rt.enhanced && rt.hd_profile != "native";
    int hd_scale = hd_scale_for(rt.enhanced, rt.hd_profile, rt.render_scale);
    // --display-mode cpu → software renderer; --vsync → PRESENTVSYNC.
    const bool software = (rt.display_mode == "cpu");
    ScaledWindow sw =
        create_scaled_window("Olduvai", 320 * hd_scale, 200 * hd_scale,
                             software, opts.vsync, rt.aspect, opts.window_w,
                             opts.window_h, /*integer_scale=*/hd_scale == 1);
    if (sw.ren == nullptr) {
        std::fprintf(stderr, "game: window creation failed: %s\n",
                     SDL_GetError());
        if (sw.win != nullptr) SDL_DestroyWindow(sw.win);
        SDL_Quit();
        return 1;
    }
    // Recreate the session window/renderer at a new logical scale (settings
    // re-init).  run_platform_level builds its own SDL_Texture each entry, so we
    // only rebuild the window + renderer here.  Fullscreen state is re-applied.
    auto rebuild_window = [&](int new_scale) -> bool {
        const bool fs =
            (SDL_GetWindowFlags(sw.win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        if (sw.ren) SDL_DestroyRenderer(sw.ren);
        if (sw.win) SDL_DestroyWindow(sw.win);
        sw = create_scaled_window("Olduvai", 320 * new_scale, 200 * new_scale,
                                  software, opts.vsync, rt.aspect, opts.window_w,
                                  opts.window_h,
                                  /*integer_scale=*/new_scale == 1);
        if (sw.ren == nullptr) {
            std::fprintf(stderr, "settings: window rebuild failed: %s\n", SDL_GetError());
            return false;
        }
        if (fs && sw.win) SDL_SetWindowFullscreen(sw.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return true;
    };
    // -f/--fullscreen: start in desktop-fullscreen (the same flag Alt+Enter
    // toggles via handle_fullscreen_toggle, so the runtime toggle stays in
    // sync).  Mirrors the reference run_game's fullscreen path.
    if (rt.fullscreen && sw.win != nullptr) {
        SDL_SetWindowFullscreen(sw.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    // ── Outer attract loop: title → play → game-over/victory → back to title.
    // Mirrors the EXE Game_MainSequencer (FUN_2bd7_04be jumps back to the title
    // after BOTH the win and the game-over paths) and the reference's `while
    // True`.  After THE END + MORT (game over) or the win ending, the session
    // restarts from the title at level 1 instead of exiting.  Replay / one-shot
    // (headless --play-frames/--play-shot) modes run a single pass.
    bool quit_requested = false;
    int rc = 0;
    // Debug: OLDUVAI_AUTOLOAD=1 loads the quicksave once at startup and skips the
    // intro + main menu, dropping straight into the saved scene — used to
    // reproduce a saved state (e.g. the secret room) headlessly so the live
    // smooth path can be measured (pair with OLDUVAI_AUTO_FULLSCREEN /
    // OLDUVAI_WS_FORCE_MARGIN + OLDUVAI_SMOOTH_FRAMES + OLDUVAI_BUBBLE_TRACE).
    // Once-only: a game-over restart returns to the normal title/menu.
    bool autoload_pending =
        std::getenv("OLDUVAI_AUTOLOAD") != nullptr && !opts.save_path.empty();
    while (true) {
    bool game_over = false;
    // Main-menu → Continue bridges a loaded checkpoint to the level loop's
    // `restore` (declared after the attract block below).
    std::optional<SaveState> menu_continue;
    bool autoloaded = false;
    if (autoload_pending) {
        autoload_pending = false;
        if (auto s = load_from_file(opts.save_path)) {
            menu_continue = s;
            display = s->hdr.level;
            autoloaded = true;
        }
    }

    // ── Intro / title sequence (publisher logo -> title cards -> the
    // speech-bubble screen) with the intro music.  Sequence position 0
    // only — a bare boot or an explicit --level 0 — like the original's
    // attract entry (FUN_2bd7_04be slot 0).  An explicit --level N (1..7)
    // jumps straight into the level; replay/headless runs never get here
    // (position 0 was remapped to L1 above for deterministic frame 0).
    if (display == 0 && !single && opts.replay.empty() && !autoloaded) {
        TitleMenuCtx tmctx{sw, rt, audio_opt, opts, hd, hd_scale,
                           display, quit_requested, menu_continue,
                           autoloaded, rebuild_window, load_all_sfx};
        run_title_menu(tmctx);
    }
    // Leaving the attract: Start Game (and any fall-through when the menu
    // assets are missing) enters L1.  Continue / OLDUVAI_AUTOLOAD already
    // retargeted `display` to the saved level.
    if (display == 0) display = 1;
    rc = 0;
    // Checkpoint restore plumbing: `restore` is applied at the next surface
    // level's entry (then consumed); `load_request` is filled by the Pause →
    // Load Game action and triggers a jump to the saved level.
    std::optional<SaveState> restore;
    std::optional<SaveState> load_request;
    std::optional<PendingReinit> reinit_request;
    int warp_display = 0;   // Pause → Cheats → Warp! target (display level)
    // (OLDUVAI_AUTOLOAD is handled once at the top of the outer loop — it sets
    // menu_continue + display + autoloaded and skips the intro/menu above.)
    // Main-menu → Continue: apply the loaded checkpoint at the (saved) level.
    if (menu_continue) restore = std::move(menu_continue);
    if (!quit_requested) {
        for (; display <= 7; ++display) {
            const int internal = kOrder[display - 1];
            if (internal == 2 || internal == 4 || internal == 6) {
                BossEnhanceOptions be;
                be.enhanced = rt.enhanced;
                be.flags = rt.enhance;
                be.hd_profile = rt.hd_profile;
                be.render_scale = rt.render_scale;
                be.hd_font = rt.hd_font;
                be.aspect = rt.aspect;
                be.vga_scan = rt.vga_scan;
                // OL-B6 boss pause Options: session device baselines + the
                // app-injected play.json persist hook (layering: config I/O
                // stays app-side; boss_app only calls the std::function).
                be.music_device = rt.music_device;
                be.sfx_backend = rt.sfx_backend;
                be.persist = rt.persist;
                const auto r = run_boss_level(opts.game_dir, internal,
                                              carry.lives, carry.score, sw,
                                              opts.frames, opts.screenshot,
                                              opts.screenshot_frame, &*audio_opt,
                                              be, opts.replay, opts.trace,
                                              opts.record_inputs);
                carry.lives = r.lives;
                carry.score = r.score;
                if (r.quit_program) {          // boss Pause → Quit to Desktop
                    quit_requested = true;
                    break;
                }
                if (r.restart) {               // boss Pause → Restart Fight
                    --display;   // the loop's ++display redoes this level
                    continue;
                }
                if (r.quit) {
                    // ESC / window-close in a boss fight → game-over → title,
                    // same as a surface ESC (see the event-loop comment above).
                    // Headless (--play-frames/--shot) just stops, no game-over.
                    if (!single) game_over = true;
                    break;
                }
                if (!r.survived && !single) {        // game over inside the fight
                    game_over = true;
                    break;
                }
            } else {
                reinit_request.reset();
                warp_display = 0;
                const auto outcome =
                    run_platform_level(rt, display, internal, carry, *audio_opt,
                                       sw, restore, load_request, reinit_request,
                                       warp_display);
                restore.reset();     // applied at entry; don't re-apply next level
                if (outcome == LevelOutcome::kLoadCheckpoint && load_request) {
                    // Jump to the saved level and apply the checkpoint there.
                    carry.lives = load_request->hdr.player.lives;
                    carry.score = load_request->hdr.score;
                    display = load_request->hdr.level;   // display level
                    restore = load_request;
                    load_request.reset();
                    --display;       // the loop's ++display restores the target
                    continue;
                }
                if (outcome == LevelOutcome::kRestartLevel) {
                    --display;       // redo this level (the loop's ++display restores it)
                    continue;
                }
                if (outcome == LevelOutcome::kWarpLevel && warp_display >= 1 &&
                    warp_display <= 7) {
                    // Cheats → Warp!: fresh entry at the chosen level; lives +
                    // score carry over like normal progression.
                    display = warp_display - 1;   // loop's ++display restores it
                    continue;
                }
                if (outcome == LevelOutcome::kReinitDisplay && reinit_request) {
                    // The enhanced master flag rides the reinit too (Style
                    // preset) — adopt it BEFORE computing the target scale.
                    rt.enhanced = reinit_request->enhanced;
                    const int new_scale =
                        hd_scale_for(rt.enhanced, reinit_request->hd_profile,
                                     reinit_request->render_scale);
                    rt.render_scale = reinit_request->render_scale;
                    rt.hd_profile   = reinit_request->hd_profile;
                    if (reinit_request->music_device != rt.music_device ||
                        reinit_request->sfx_backend  != rt.sfx_backend) {
                        rt.music_device = reinit_request->music_device;
                        rt.sfx_backend  = reinit_request->sfx_backend;
                        audio_opt.reset();   // tear down device + synth handles
                        audio_opt.emplace(rt.music_device, rt.rom_dir, rt.soundfont,
                                          rt.sfx_backend, rt.audio_rate,
                                          rt.audio_buffer, rt.midi_port);
                        load_all_sfx(*audio_opt);
                    } else {
                        rt.music_device = reinit_request->music_device;  // keep in sync (no-op)
                        rt.sfx_backend  = reinit_request->sfx_backend;
                    }
                    if (new_scale != hd_scale) {
                        hd = rt.enhanced && rt.hd_profile != "native";
                        hd_scale = new_scale;
                        if (!rebuild_window(hd_scale)) {
                            std::fprintf(stderr, "settings: aborting after failed window rebuild\n");
                            quit_requested = true;
                            break;
                        }
                    }
                    restore = reinit_request->state;
                    reinit_request.reset();
                    --display;        // re-enter the same level (loop's ++display restores it)
                    continue;
                }
                if (outcome == LevelOutcome::kQuitProgram) {
                    quit_requested = true;   // Pause → Quit to Desktop
                    break;
                }
                if (outcome != LevelOutcome::kComplete) {
                    rc = outcome == LevelOutcome::kQuit ? 0 : 1;
                    if (outcome == LevelOutcome::kGameOver) game_over = true;
                    break;
                }
            }
            // --record-inputs is single-segment (the file is reopened "w" per
            // level entry); break the LEVEL loop after this one so the next
            // level does not truncate the recording.  The victory/tally ran
            // inside run_platform_level / run_boss_level above, so the recorded
            // demo already includes the level-end sequence.
            if (single || !opts.record_inputs.empty()) break;
        }
    }
    // ── Game-over sequence (FUN_2bd7_02e7 outer loop): the
    // MORT.MDI death music + THEEND.PC1 picture, shown for ANY game-over —
    // boss death or platform death.  Previously this lived inside
    // run_platform_level, so boss deaths exited silently.  THEEND.PC1 is in
    // FILESA.VGA; MORT.MDI is in FILESA.CUR (both confirmed present).
    if (!quit_requested && game_over && !single) {
        formats::CurArchive eva(slurp(opts.game_dir / "FILESA.VGA"));
        formats::CurArchive efa(slurp(opts.game_dir / "FILESA.CUR"));
        if (!eva.contains("THEEND.PC1")) {
            std::fprintf(stderr, "game-over: THEEND.PC1 not in FILESA.VGA\n");
            audio_opt->stop_music();
        } else {
            const formats::Pc1Image end = formats::parse_pc1(eva.get("THEEND.PC1").data);
            if (end.width != 320) {
                std::fprintf(stderr, "game-over: THEEND.PC1 unexpected width\n");
                audio_opt->stop_music();
            } else {
                SDL_Texture* gtex = SDL_CreateTexture(
                    sw.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                    320 * hd_scale, 200 * hd_scale);
                // MORT.MDI death music over the picture (no celebratory chime).
                if (audio_opt->music_available()) {
                    const std::vector<std::uint8_t>* md = nullptr;
                    if (efa.contains("MORT.MDI")) md = &efa.get("MORT.MDI").data;
                    if (md != nullptr) {
                        audio_opt->play_music(*md, formats::mdi_track_id("mort.mdi"));
                    }
                }
                // Compose THEEND.PC1 → RGBA once.
                FrameBuffer end_fb;
                for (std::size_t i = 0;
                     i < end.pixels.size() && i < 320u * 200u; ++i) {
                    const std::uint8_t idx = end.pixels[i];
                    const formats::Rgb c =
                        (idx < end.palette.size()) ? end.palette[idx]
                                                   : formats::Rgb{};
                    end_fb.px[i * 4]     = c.r;
                    end_fb.px[i * 4 + 1] = c.g;
                    end_fb.px[i * 4 + 2] = c.b;
                    end_fb.px[i * 4 + 3] = 255;
                }
                // Upload (HD-upscale if scaled) + present, mirroring the win
                // ending's render_at + the intro present pattern.
                if (hd_scale > 1) {
                    const auto up = enhance::upscale_rgba(
                        end_fb.px, 320, 200, hd_scale, rt.hd_profile);
                    SDL_UpdateTexture(gtex, nullptr, up.data(),
                                      320 * hd_scale * 4);
                } else {
                    SDL_UpdateTexture(gtex, nullptr, end_fb.px.data(), 320 * 4);
                }
                // Hold ~8 seconds (8*18 frames @ 18 Hz), re-presenting each
                // tick and polling QUIT/ESC to abort early.
                constexpr int kHoldFrames = 8 * 18;
                for (int f = 0; f < kHoldFrames; ++f) {
                    SDL_Event ev;
                    bool abort = false;
                    while (SDL_PollEvent(&ev)) {
                        if (handle_fullscreen_toggle(ev, sw.win)) continue;
                        if (ev.type == SDL_QUIT ||
                            (ev.type == SDL_KEYDOWN &&
                             ev.key.keysym.sym == SDLK_ESCAPE)) {
                            abort = true;
                            break;
                        }
                    }
                    if (abort) break;
                    SDL_RenderClear(sw.ren);
                    SDL_RenderCopy(sw.ren, gtex, nullptr, nullptr);
                    SDL_RenderPresent(sw.ren);
                    SDL_Delay(1000 / 18);
                }
                // FADE the death music out — do NOT hard-cut it.  EXE
                // FUN_2bd7_02e7 ends the game-over screen with FUN_1f75_00e4
                // (MDI_FadeStop, a gradual master-volume ramp) after the 8 s
                // wait, then FUN_1f75_017a (free slot).  A hard stop_music()
                // chopped MORT.MDI mid-loop, which is audibly wrong.
                audio_opt->fade_out_music();
                SDL_DestroyTexture(gtex);
            }
        }
    }
    // ── Ending: the win picture + music after the last level. ──
    // Reached by finishing L7 (the level loop leaves display == 8) or
    // directly via --level 8 (sequence position 8 = FUN_2bd7_04be's
    // Game_WinSequence slot) — great for testing the ending.  Afterwards the
    // normal post-win flow applies: loop back to the attract at position 0.
    if (!quit_requested && display > 7 && rc == 0 && !single) {
        formats::CurArchive eva(slurp(opts.game_dir / "FILESA.VGA"));
        formats::CurArchive efa(slurp(opts.game_dir / "FILESA.CUR"));
        SDL_Texture* etex = SDL_CreateTexture(
            sw.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
            320 * hd_scale, 200 * hd_scale);
        if (audio_opt->music_available() && efa.contains("FIN.MDI")) {
            audio_opt->play_music(efa.get("FIN.MDI").data, formats::mdi_track_id("fin.mdi"));
        }
        // ── Real ending sequence: Game_WinSequence (FUN_2bd7_0183, 356 B) ──
        // COOL3.PC1 (family cave scene) + COOL2.MAT[0] (192×125 caveman from
        // behind) in FILESA.VGA.  Caveman rises from y=198 to y=73 at −2/frame
        // (~63 frames @18 Hz).  Smooth-motion: 3 sub-frames lerping y.
        // Missing-assets fallback: return silently per spec §F6.
        // EXE evidence: capstone 2bd7_0183_sequencer_b.txt 0x0207-0x0279.
        [&]() {
            // Load from FILESA.VGA (confirmed archive; see spec F6 note).
            if (!eva.contains("COOL3.PC1") || !eva.contains("COOL2.MAT")) {
                std::fprintf(stderr, "ending: assets missing (COOL3.PC1 / "
                                     "COOL2.MAT not in FILESA.VGA)\n");
                audio_opt->stop_music();
                return;
            }
            const formats::Pc1Image bg =
                formats::parse_pc1(eva.get("COOL3.PC1").data);
            if (bg.width != 320) {
                std::fprintf(stderr, "ending: COOL3.PC1 unexpected width\n");
                audio_opt->stop_music();
                return;
            }
            const std::vector<formats::Sprite> mat_sprites =
                formats::MatFile(eva.get("COOL2.MAT").data, "COOL2.MAT")
                    .sprites();
            if (mat_sprites.empty()) {
                std::fprintf(stderr, "ending: COOL2.MAT has no sprites\n");
                audio_opt->stop_music();
                return;
            }
            const formats::Sprite& sprite = mat_sprites[0];

            // Build the RGBA background once.
            FrameBuffer bg_fb;
            for (std::size_t i = 0; i < bg.pixels.size() && i < 320u * 200u;
                 ++i) {
                const std::uint8_t idx = bg.pixels[i];
                const formats::Rgb c =
                    (idx < bg.palette.size()) ? bg.palette[idx]
                                              : formats::Rgb{};
                bg_fb.px[i * 4]     = c.r;
                bg_fb.px[i * 4 + 1] = c.g;
                bg_fb.px[i * 4 + 2] = c.b;
                bg_fb.px[i * 4 + 3] = 255;
            }

            // Build the palette vector once (blit_sprite takes const ref).
            const std::vector<formats::Rgb> pal(bg.palette.begin(),
                                                bg.palette.end());

            // OLDUVAI_ENDING_SHOT=<png>: headless verify — dump the first
            // composited ending frame (COOL3 backdrop + COOL2 caveman at
            // y=198) via renderer readback, then exit.  Same pattern as
            // OLDUVAI_MAINMENU_SHOT; pairs with --level 8 + dummy video.
            const char* const ending_shot = std::getenv("OLDUVAI_ENDING_SHOT");
            // Compose one frame at logical y, upload, and display.
            // delay_ms: wall-clock pause for this sub-step.
            // Returns false if the user quit or pressed ESC.
            auto render_at = [&](int render_y, Uint32 delay_ms) -> bool {
                FrameBuffer fb2 = bg_fb;   // copy background
                blit_sprite(fb2, sprite, pal, 64, render_y);
                // Poll for quit/ESC before uploading.
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (handle_fullscreen_toggle(ev, sw.win)) continue;
                    if (ev.type == SDL_QUIT) return false;
                    if (ev.type == SDL_KEYDOWN &&
                        ev.key.keysym.sym == SDLK_ESCAPE) return false;
                }
                if (hd_scale > 1) {
                    const auto up = enhance::upscale_rgba(
                        fb2.px, 320, 200, hd_scale, rt.hd_profile);
                    SDL_UpdateTexture(etex, nullptr, up.data(),
                                      320 * hd_scale * 4);
                } else {
                    SDL_UpdateTexture(etex, nullptr, fb2.px.data(), 320 * 4);
                }
                SDL_RenderClear(sw.ren);
                SDL_RenderCopy(sw.ren, etex, nullptr, nullptr);
                if (ending_shot) {
                    // Read back the composited frame before present (the
                    // OLDUVAI_MAINMENU_SHOT pattern), save it, and quit the
                    // session — a one-shot verify must not loop to the title.
                    int rw = 0, rh = 0;
                    SDL_GetRendererOutputSize(sw.ren, &rw, &rh);
                    std::vector<std::uint8_t> rb(
                        static_cast<std::size_t>(rw) * rh * 4);
                    if (SDL_RenderReadPixels(sw.ren, nullptr,
                                             SDL_PIXELFORMAT_RGBA32,
                                             rb.data(), rw * 4) == 0) {
                        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                            rb.data(), rw, rh, 32, rw * 4,
                            SDL_PIXELFORMAT_RGBA32);
                        if (s) { save_surface_image(s, ending_shot);
                                 SDL_FreeSurface(s); }
                    }
                    quit_requested = true;
                    return false;
                }
                SDL_RenderPresent(sw.ren);
                if (delay_ms > 0) SDL_Delay(delay_ms);
                return true;
            };

            // EXE 0x0207-0x0279: y = 198..73 (loop exits when y < 73).
            constexpr int kYStart = 198;
            constexpr int kYEnd   = 73;
            constexpr int kDY     = 2;
            constexpr Uint32 kFrameMs = 1000 / 18;   // 18 Hz logic cadence

            const bool smooth = rt.enhance.smooth_motion;
            // vsync render-interpolation for the credits scroll (general
            // technique — smooth_present.hpp): fill each 18Hz logic step (y-=2)
            // with vsync-paced frames at continuous alpha.  Without vsync, fall
            // back to discrete sub-frames.  render_at(y, 0) presents with no
            // extra delay so the vsync block alone paces it (a fixed delay would
            // stack on top and slow the scroll).
            const bool e_vsync = smooth_try_enable_vsync(sw.ren, smooth);
            bool aborted = false;
            int prev_y = kYStart;
            for (int y = kYStart; y >= kYEnd && !aborted; y -= kDY) {
                if (smooth && e_vsync) {
                    const Uint32 t0 = SDL_GetTicks();
                    while (!aborted) {
                        const Uint32 el = SDL_GetTicks() - t0;
                        const float a = el >= kFrameMs
                                            ? 1.0f
                                            : static_cast<float>(el) /
                                                  static_cast<float>(kFrameMs);
                        const int ly =
                            prev_y + static_cast<int>(
                                         std::lround((y - prev_y) * a));
                        aborted = !render_at(ly, 0);
                        if (SDL_GetTicks() - t0 >= kFrameMs) break;
                    }
                } else if (smooth) {
                    // No vsync: discrete sub-frames lerping y from prev_y to y.
                    constexpr Uint32 kSubMs = kFrameMs / 3;
                    for (int sub = 1; sub <= 3 && !aborted; ++sub) {
                        const int lerp_y = prev_y + (y - prev_y) * sub / 3;
                        aborted = !render_at(lerp_y, kSubMs);
                    }
                } else {
                    aborted = !render_at(y, kFrameMs);
                }
                prev_y = y;
            }

            if (aborted) {
                audio_opt->stop_music();
                return;
            }

            // Hold the final frame: wait for any-key press+release.
            // EXE 0x02a7: Keyboard_WaitPressRelease (KEYDOWN then KEYUP).
            // ESC / SDL_QUIT abort immediately at this point too.
            bool pressed = false;
            bool done = false;
            while (!done) {
                SDL_Event ev;
                if (SDL_WaitEvent(&ev)) {
                    if (ev.type == SDL_QUIT) { done = true; break; }
                    if (handle_fullscreen_toggle(ev, sw.win)) continue;
                    if (ev.type == SDL_KEYDOWN) {
                        if (ev.key.keysym.sym == SDLK_ESCAPE) {
                            done = true;
                            break;
                        }
                        pressed = true;
                    }
                    if (ev.type == SDL_KEYUP && pressed) {
                        done = true;
                    }
                }
            }

            // EXE 0x02d2-0x02da: MDI_FadeStop + MDI_FreeSlot(0).
            audio_opt->stop_music();
        }();  // IIFE — structured cleanup without goto
        SDL_DestroyTexture(etex);
    }
    // Attract-loop tail: replay / one-shot modes exit after a single pass;
    // interactive sessions restart from the attract at sequence position 0
    // (FUN_2bd7_04be loops back to its title slot after both the win and the
    // game-over paths; the reference start_level = 1 behind its own title screen).
    // Carry resets to full lives/score/food/energy; Start Game then enters L1.
    // --record-inputs is single-segment (the file is reopened "w" per level, so
    // advancing to the next level would truncate the recording) — exit after
    // the recorded level ends, like replay.
    if (single || !opts.replay.empty() || !opts.record_inputs.empty() ||
        quit_requested)
        break;
    carry = CarriedState{};
    display = 0;
    }  // while (true) — outer attract loop
    SDL_DestroyRenderer(sw.ren);
    SDL_DestroyWindow(sw.win);
    SDL_Quit();
    return rc;
}

}  // namespace olduvai::presentation
