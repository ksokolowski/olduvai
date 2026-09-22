// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/game_app.hpp"

#include "presentation/input/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_archives.hpp"
#include "prepare/game_files.hpp"
#include "presentation/input/autofire.hpp"
#include "presentation/diag/debug_overlay.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/hd_warm.hpp"
#include "presentation/level/level_save.hpp"
#include "presentation/menu/parse_util.hpp"
#include "presentation/menu/pause_bindings.hpp"
#include "presentation/menu/pause_flow.hpp"
#include "presentation/diag/draw_log.hpp"
#include "presentation/diag/frame_stats.hpp"
#include "presentation/sequence/end_sequence.hpp"
#include "presentation/input/frame_input.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/lerp_snapshot.hpp"
#include "presentation/diag/reinit_test_hook.hpp"
#include "presentation/level/level_setup.hpp"
#include "presentation/level/level_state.hpp"
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/hud_render.hpp"
#include "presentation/menu/dialog_key_map.hpp"
#include "presentation/sequence/l3_end_level.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/diag/menu_script.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/render/banner_fx.hpp"
#include "presentation/render/rising_balloons.hpp"
#include "presentation/render/banners.hpp"
#include "presentation/menu/menu_render.hpp"
#include "presentation/level/save_state.hpp"
#include "presentation/input/replay.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/render/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/audio/game_music.hpp"
#include "presentation/diag/bug_capture.hpp"
#include "presentation/menu/pause_service.hpp"
#include "presentation/diag/report_form.hpp"
#include "presentation/menu/text_overlay_edit.hpp"
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
#include "presentation/menu/cheat_picker.hpp"
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
#include "systems/transitions.hpp"
#include "presentation/env_num.hpp"
#include "presentation/sequence/screen_presenter.hpp"
#include "presentation/sequence/text_screen_present.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"

namespace olduvai::presentation {

namespace {

// OLDUVAI_DUMP_LEVEL_FADE=<dir>: dump each level-complete fade frame as a
// pre-upscale PNG (stb's encoder: the same bytes on every platform, where
// SDL_SaveBMP's header differs between SDL 2.30 and sdl2-compat, and a hash
// gate over it failed on Linux with identical pixels).  Same device as OLDUVAI_DUMP_DESCENT and for the same
// reason — it makes the §3.7 slice-2 extraction provable byte-identical on a
// path that no test could reach before.  Debug-only, and it MOVES WITH the
// block it measures, so before/after dumps compare like with like.
// The frame's BEFORE picture — what the state looked like at the top of this
// frame, read by the transition classifier, step 9's screen change and the
// level-complete fade.  Thirteen `const` locals were describing one thing;
// §3.7 calls that shape "flat: N locals describing one thing" and its stage B
// did the same regrouping.
//
// A REGROUPING ONLY: no statement moves and nothing becomes mutable.  The old
// declarations were spread over 35 lines of which everything between them was
// COMMENT, so all thirteen reads already happened at the same instant — which
// is what makes hoisting them into one constructor provably behaviour-neutral
// rather than merely plausible.
struct PrevFrame {
    explicit PrevFrame(const systems::SystemsState& s)
        : px(s.player.x), py(s.player.y), screen(s.current_screen),
          secret(s.secret_flag != 0), cave(s.cave_flag != 0),
          cave_index(s.cave_index), inside(s.cave_flag || s.secret_flag),
          psprite(s.player.sprite), pdx(s.player.dx), pdy(s.player.dy),
          pfacing(s.player.facing_left), pclub(s.player.club_flag),
          emerge(s.cave_emerge_frames) {}

    // Position before this frame's movement/teleport (direction inference).
    int px, py;
    int screen;
    bool secret, cave;
    // cave_index BEFORE this frame's logic (a cave-sign exit sets it to -1):
    // the wide kind-2 fade re-composes the OUTGOING cave frame and needs the
    // ORIGINAL index back, because the cave STOP-sign render is gated on
    // cave_index in range (game_render.cpp) — without it the sign blanks the
    // instant the fade starts while the rest of the cave still fades.
    int cave_index;
    bool inside;
    // Player DRAW state before this frame's logic — i.e. exactly what the LAST
    // PRESENTED frame showed (frozen-frame principle, Finding
    // transition_pan_content_frozen_sprites.md).  The wide kind-2 fade
    // re-composes its outgoing frame from live state, but the transition tick
    // has already mutated the player's presentation:
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
    int psprite, pdx, pdy, pfacing, pclub, emerge;
};

void dump_level_fade(const std::vector<std::uint8_t>& px, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_LEVEL_FADE");
    if (dir == nullptr) return;
    static int level_fade_seq = 0;
    char p[512];
    std::snprintf(p, sizeof p, "%s/levelfade_%04d.png", dir, level_fade_seq++);
    save_rgba_image(px.data(), w, h, p);
}

void fade_wide_to_black(Loaded& g, WidescreenPresenter& wsp,
                        SDL_Window* win, int prev_px, int prev_py,
                        Uint32 frame_ms, bool& running) {
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
        dump_level_fade(wf.px, wsp.native_w(), 200);
        wsp.present_transition(wf.px, /*with_hud=*/false);
        SDL_Event e2;
        while (SDL_PollEvent(&e2)) {
            if (handle_fullscreen_toggle(e2, win)) continue;
            if (e2.type == SDL_QUIT) { running = false; quit = true; }
        }
        SDL_Delay(frame_ms);
    }
}

}  // namespace

enum class LevelOutcome { kComplete, kGameOver, kQuit, kQuitProgram, kRestartLevel,
                          kLoadCheckpoint, kReinitDisplay, kWarpLevel };


// flow_key_from_sym (the SDL→SettingsFlow::Key bridge, shared by the in-game
// Pause and main-menu call sites) now lives in presentation/dialog_key_map.hpp
// so it is shared with boss_app instead of mirrored (CC3).  build_display_changes
// moved to settings_flow.cpp with the rest of the flow (OL-B1).

// ── Dev instrumentation (BACKLOG 3.7 stage A) ────────────────────────────────
// Twenty per-frame locals that exist only to MEASURE or SCRIPT the loop, never
// to run it.  Every one is behind an OLDUVAI_* env gate or a --debug flag, and
// none participates in the frame contract, so grouping them is the cheapest
// reduction in what an owner has to hold: 20 mutable names become 1.
//
// Kept as four named sub-objects rather than one flat blob — they are four
// unrelated features that happen to share a lifetime, and flattening them is
// how a struct turns into the 17-member context this file warns about.
// -- The in-flight screen transition (BACKLOG 3.7 stage B) -------------------
// Ten locals describing ONE thing: the transition currently being played.
// Unlike LevelDiag these are not separable features, so this is deliberately
// flat -- nesting would invent a hierarchy the concept does not have.
struct TransitionState {
    explicit TransitionState(int w, int h) : old_frame(w, h) {}
    // HD-sized to match the gameplay fb: the outgoing frame, captured before
    // the rebind and played back after the new screen's first compose.
    FrameBuffer old_frame;
    int kind = 0;      // 0 = none, 1 = pan-scroll, 2 = fade pair
                       // 3 = enhanced secret-entry slide (12f down)
                       // 4 = enhanced secret-exit slide (30f up + arc)
    char dir = 'R';

    // Widescreen (S8.7): when the transition involves a ws_present_path screen
    // the WHOLE transition presents at the WIDE texture width so the bars +
    // HUD do not pop in or jump mid-pan/fade.  `old_wide` is the outgoing
    // frame composed WIDE *before* the rebind (old neighbours); the incoming
    // is composed wide after the rebind (new neighbours) inside the playback
    // block.  `wide` gates the whole wide path; when false the legacy 320
    // `fp.present` path runs unchanged.
    std::vector<std::uint8_t> old_wide;
    bool wide = false;

    // Wide-wrapped OLD frame for the kind 3/4 secret slides.  Built INSIDE the
    // kind 3/4 classification blocks (kind 4's outgoing frame needs the
    // transient secret_flag/player-at-exit state that is restored before the
    // kind 1/2 wide block runs), so it gets its own holder rather than
    // `old_wide`.
    std::vector<std::uint8_t> slide_old_wide;
    bool slide_old_wide_ok = false;

    // Saved state for the kind=4 arc overlay (set when classifying exit).
    int slide_secret_exit_x = 0;   // departure x (= state.secret_exit_x)
    int slide_end_x = 0, slide_end_y = 0;   // return position
};

struct LevelDiag {
    FrameStats stats;
    // OLDUVAI_PACE_TRACE: --vga-scan hold-frame scanout counters.
    struct VgaScan {
        unsigned long fill_presents = 0, fill_ticks = 0;
    } vga;
    // --debug-perf overlay: smoothed fps / frame-ms drawn into the frame.
    struct PerfOverlay {
        Uint32 last_t = 0, interval_accum = 0, ms_accum = 0;
        int samples = 0;
        double fps = 0.0, frame_ms = 0.0;
    } perf;
    // OLDUVAI_MENU_SCRIPT: headless menu walk (tests/menu_script.sh,
    // tests/report_form.sh) — diag/menu_script.hpp.
    MenuScript menu;
};

// Everything in the frame that must happen EXACTLY ONCE PER LOGIC TICK, in
// the order it must happen in.
//
// WHY THIS IS ONE FUNCTION.  Each of these counters had its own bug when it
// drained at the wrong rate or from the wrong place, and each comment inside
// is that scar: the dust tail must not drain faster under smooth-motion; the
// cave-emerge countdown sits at END of tick because a decrement here was
// invisible to the windowed path but not to the widescreen re-compose; the
// GET READY counter lives at the authoritative HUD point rather than inside
// draw_hud, which also runs per smooth-motion sub-frame.  Scattered down a
// 1500-line loop, "exactly once per tick" was a property you had to
// reconstruct by reading.  Here it is the function's name and its contract.
//
// The fp.draw_hud_for(fb) call in the middle is NOT a stray draw: the EXE's
// order is draw-then-decrement (FUN_27f7_1277, DS:0x97e0), so the HUD draw
// belongs BETWEEN the teleport countdown and the GET READY decrement.  Moving
// it out of here would silently reorder that pair.
void advance_once_per_tick(Loaded& g, FramePresenter& fp, FrameBuffer& fb,
                           BannerPresenter& banners, int& l3_smoke_tail) {
    // Arm the enhanced GET READY fly-away on the level-start rising edge
    // of get_ready_counter (once per logic tick — see banners.hpp).
    banners.arm_tick();

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
    fp.draw_hud_for(fb);

    // GET READY banner counter: advance EXACTLY ONCE per logic tick, here at
    // the authoritative HUD point (fp.draw_hud_for runs once per tick), NOT
    // inside draw_hud — which is also called per smooth-motion sub-frame and
    // would drain the counter ~Nx faster (the "GET READY flashes too
    // briefly" bug in enhanced/smooth mode; classic was unaffected).  Drawn
    // BEFORE this decrement (fp.draw_hud_for just ran), preserving the EXE's
    // draw-then-decrement order (FUN_27f7_1277, DS:0x97e0, even frames,
    // window [2,17]).  Classic timing is byte-identical (one tick = one
    // decrement, same frame_counter parity as the old in-draw_hud site).
    if (g.state.get_ready_counter >= 2 && g.state.get_ready_counter <= 17 &&
        (g.state.frame_counter & 1) == 0) {
        --g.state.get_ready_counter;
    }
}

// Per-frame perf sampling for the debug overlays (F3 stats / OLDUVAI_PERF_LOG).
//
// Diagnostics, like drive_menu_script before it, and inline in the frame loop
// for the same reason: it needs the frame's timing, so it grew where the
// timing is.  probe_slice measured the seam at TWO free names.
//
// The FRAME-PACING `t0` on the line above the old block stays in the loop —
// it is declared between this block and the frame_stats one, belongs to
// neither, and is consumed ~1400 lines later by the pacing calculation.  A
// first attempt swallowed it into this function and broke that use; the probe
// had already flagged it as a cascade and I read past the warning.
void sample_debug_perf(LevelDiag& diag, bool any_debug_overlay) {
if (any_debug_overlay) {
    const Uint32 now = SDL_GetTicks();
    diag.perf.interval_accum += now - diag.perf.last_t;
    diag.perf.last_t = now;
    ++diag.perf.samples;
    if (diag.perf.samples >= 30) {
        const double avg_interval =
            static_cast<double>(diag.perf.interval_accum) / diag.perf.samples;
        diag.perf.fps = avg_interval > 0.0 ? 1000.0 / avg_interval : 0.0;
        diag.perf.frame_ms =
            static_cast<double>(diag.perf.ms_accum) / diag.perf.samples;
        if (std::getenv("OLDUVAI_PERF_LOG"))
            std::fprintf(stderr, "[PERF] frame_ms=%.2f fps=%.1f\n",
                         diag.perf.frame_ms, diag.perf.fps);
        diag.perf.interval_accum = 0;
        diag.perf.ms_accum = 0;
        diag.perf.samples = 0;
    }
}
}

namespace {

// Classify the screen change and compose its outgoing 320 frame.  Which
// transition this is — surface pan, fade pair, or one of the two enhanced
// secret slides — follows from where the player was and where they are now,
// and each flavour composes its outgoing frame differently; that is one
// subject, and it is this function.  `warp_fade` is taken BY VALUE: the L7
// fake-cave seam turns it on for this decision only.
void classify_screen_transition(Loaded& g, const PrevFrame& pf,
                                TransitionState& trans, FramePresenter& fp,
                                WidescreenPresenter& wsp,
                                const FrameBuffer& fb, bool enhanced,
                                bool warp_fade, bool now_inside) {
    const bool l7_fake_cave =
        std::abs(pf.screen - g.state.current_screen) == 1 &&
        systems::seam_kind(g.state.current_level, pf.screen,
                           g.state.current_screen) ==
            systems::SeamKind::FakeCaveInstant;
    if (l7_fake_cave && enhanced) warp_fade = true;
    if (!now_inside && !pf.inside && !warp_fade) {
        trans.kind = 1;   // surface pan-scroll
        const int ddx = g.state.player.x - pf.px;
        const int ddy = g.state.player.y - pf.py;
        if (std::abs(ddx) >= std::abs(ddy)) {
            trans.dir = ddx < 0 ? 'R' : 'L';
        } else {
            trans.dir = ddy < 0 ? 'D' : 'U';
        }

        // Pan-scroll: re-compose the outgoing frame WITHOUT the
        // player — the old screen's assets and entity binding
        // are still live here (bind_screen runs below).  Both
        // slide surfaces carrying a player shows two of them
        // mid-pan; player-less old frame = the player "rides"
        // the incoming screen, as in the original.  (Reference
        // fix: renders old_surf with draw_player=False.)
        {
            auto rt = make_render_target(trans.old_frame, *fp.surface,
                                        *fp.hd_cache);
            compose_frame(rt, g.state, g.render,
                          /*draw_player=*/false);
        }
        fp.draw_hud_for(trans.old_frame);
    } else {
        // Detect enhanced-mode secret-entry / secret-exit slides.
        // Secret entry: was on surface (!pf.inside), now in secret
        //   (now_inside && g.state.secret_flag).
        // Secret exit:  was in secret (pf.secret), now on surface
        //   (!now_inside).
        const bool is_secret_entry =
            enhanced && !pf.inside && now_inside &&
            g.state.secret_flag;
        const bool is_secret_exit =
            enhanced && pf.secret && !now_inside;
        if (is_secret_entry) {
            // kind 3: 12-frame downward slide (surface→secret).
            // Old frame: current surface, player-less so the player
            // "rides" into the new screen.
            trans.kind = 3;
            {
                auto rt = make_render_target(trans.old_frame, *fp.surface,
                                        *fp.hd_cache);
                compose_frame(rt, g.state, g.render,
                              /*draw_player=*/false);
            }
            fp.draw_hud_for(trans.old_frame);
            if (wsp.active()) {
                // Wide OLD frame for the slide: a native-320 sibling of
                // trans.old_frame (same state → same content), wrapped with
                // the OLD surface cache (peek), still live before the
                // rebind.  Built here because the kind 1/2 wide block below
                // only handles those kinds.
                FrameBuffer oc{};
                RenderTarget rt{oc.px.data(), 320, 200, 1, nullptr,
                                nullptr};
                rt.advance_state = false;
                compose_frame(rt, g.state, g.render, /*draw_player=*/false);
                wsp.wrap_wide(oc, trans.slide_old_wide);
                trans.slide_old_wide_ok = true;
            }
        } else if (is_secret_exit) {
            // kind 4: 30-frame upward slide (secret→surface) with
            // player-arc overlay.  Both surfaces rendered player-less;
            // the overlay draws the jump sprite traversing from
            // secret-exit-x to return-x.
            trans.kind = 4;

            // Save arc parameters before the state is mutated by
            // bind_screen / clear_per_screen_state below.
            trans.slide_secret_exit_x = g.state.secret_exit_x;
            trans.slide_end_x = g.state.player.x;
            trans.slide_end_y = g.state.player.y;

            // Old (secret) frame: re-render player-less at the exit
            // position.  Move player to exit pos for render, then
            // restore.
            const int saved_rx = g.state.player.x;
            const int saved_ry = g.state.player.y;
            g.state.player.x = trans.slide_secret_exit_x;
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
                auto rt = make_render_target(trans.old_frame, *fp.surface,
                                        *fp.hd_cache);
                compose_frame(rt, g.state, g.render,
                              /*draw_player=*/false, old_bubble_hook);
            }
            fp.draw_hud_for(trans.old_frame);
            if (wsp.active()) {
                // Wide OLD frame for the slide, built WHILE the transient
                // secret state is live (secret_flag=1, player at exit,
                // old_bubble_hook) — a native-320 sibling of trans.old_frame
                // wrapped with the secret-room cache (no neighbours, null
                // backdrop → self-tile margins).  Must precede the restore.
                FrameBuffer oc{};
                RenderTarget rt{oc.px.data(), 320, 200, 1, nullptr,
                                nullptr};
                rt.advance_state = false;
                compose_frame(rt, g.state, g.render, /*draw_player=*/false,
                              old_bubble_hook);
                wsp.wrap_wide(oc, trans.slide_old_wide);
                trans.slide_old_wide_ok = true;
            }
            g.state.secret_flag = 0;   // restore
            g.state.player.x = saved_rx;
            g.state.player.y = saved_ry;
        } else {
            trans.kind = 2;   // cave/secret/warp fade pair
            // Fades keep the player on the old frame (the blend to
            // black hides it) — snapshot last frame as displayed.
            trans.old_frame = fb;
        }
    }
}

// What the wide pass below decided, for the post-rebind wide/not-wide call
// that needs the NEW screen's status too.
struct WideOutgoing {
    bool old_wide;       // the OLD side is a ws_present_path screen
    bool eligible_kind;  // this transition kind can go wide at all
};

// The same transition composed WIDE, while the peek cache still reflects the
// OLD screen.  Runs after the classification above because it reads the kind
// that one decided.
WideOutgoing compose_outgoing_wide(Loaded& g, const PrevFrame& pf,
                                   TransitionState& trans,
                                   WidescreenPresenter& wsp) {
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
    trans.wide = false;
    trans.old_wide.clear();
    bool ws_old = false;
    const bool ws_eligible_kind =
        (trans.kind == 1 || trans.kind == 2 ||
         trans.kind == 3 || trans.kind == 4);
    if (wsp.active() &&
        (trans.kind == 1 || trans.kind == 2)) {
        // The peek CACHE is still the old screen's here, but the
        // STATE is not: enter_cave/exit_cave have already flipped
        // cave_flag + current_screen to the NEW side (see the restore
        // block below, which exists for exactly that reason).
        // present_path()'s surface_selffill() case reads those two
        // fields, so on a cave EXIT it mis-reads the outgoing CAVE as a
        // no-neighbour SURFACE screen and wraps it with composed
        // margins — the cave fade-OUT grew widescreen content the cave
        // fade-IN correctly lacks.  pf.cave/pf.secret describe the
        // OUTGOING side, so gate on them.
        ws_old = !pf.cave && !pf.secret && wsp.present_path();

        // Native-320 outgoing center matching the kind's content, composed
        // from g.state (still the OLD screen — assets not yet rebound):
        //   kind 1 (pan): player-LESS (the player rides the incoming
        //                 screen, mirroring the HD trans.old_frame compose).
        //   kind 2 (fade): player-INCLUDED (the fade-to-black hides it; the
        //                  320 path snapshots `fb` with the player on it).
        FrameBuffer old_center{};   // 320x200
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
            if (trans.kind == 2) {
                g.state.player.x = pf.px;
                g.state.player.y = pf.py;
                g.state.current_screen = pf.screen;
                g.state.cave_flag = pf.cave ? 1 : 0;

                // Restore the outgoing cave's index too (the exit set it
                // to -1) so the STOP-sign render fires in the fade frame.
                g.state.cave_index = pf.cave ? pf.cave_index : -1;
                g.state.secret_flag = pf.secret ? 1 : 0;

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
                g.state.player.sprite = pf.psprite;
                g.state.player.dx = pf.pdx;
                g.state.player.dy = pf.pdy;
                g.state.player.facing_left = pf.pfacing;
                g.state.player.club_flag = pf.pclub;
                g.state.cave_emerge_frames = pf.emerge;
            }
            RenderTarget rt{old_center.px.data(), 320, 200, 1, nullptr,
                            nullptr};
            rt.advance_state = false;
            compose_frame(rt, g.state, g.render,
                          /*draw_player=*/trans.kind == 2);
            if (trans.kind == 2) {
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
        if (ws_old && trans.kind == 2) {
            const int cur_screen = g.state.current_screen;
            g.state.current_screen = pf.screen;
            wsp.wrap_wide_static(old_center, trans.old_wide);
            g.state.current_screen = cur_screen;
        } else {
            wsp.wrap_wide_for(old_center, ws_old, trans.old_wide);
        }
    } else if (wsp.active() &&
               (trans.kind == 3 || trans.kind == 4)) {
        // kind 3/4: the wide OLD buffer was already built + wrapped inside
        // the classification block (trans.slide_old_wide), while the secret
        // state was live; ws_old just records that the old side is wide.
        ws_old = trans.slide_old_wide_ok;
    }
    return {ws_old, ws_eligible_kind};
}

}  // namespace


// ── Step 9's screen-change body, sans the trunk-descent dispatch ──────────
// Extracted verbatim from run_platform_level (§3.7 D slice).  The probe put
// this seam at 14 free names before the cut; six of those were the descent
// branch's DescentCtx wiring (surface / lsz / frame_ms / upload_and_show_fn
// / smoke-tail), which stays in the driver where the branch is.  What
// crosses are the block's subject (trans), the state it reads (g, pf), the
// two present owners it composes through (fp, fb) and the one flag it
// branches on (enhanced) — subjects and owners, not a republished closure.
// The WidescreenPresenter is NOT a parameter: FramePresenter already owns it,
// so it is derived below rather than passed.
void step9_screen_change(Loaded& g, const PrevFrame& pf,
                         TransitionState& trans, FramePresenter& fp,
                         FrameBuffer& fb, bool enhanced,
                         bool warp_fade) {
    // The present apparatus rides on FramePresenter: it already owns the
    // LevelSurface and the WidescreenPresenter (§3.7 cluster 1), so this
    // signature carries owners, not their members.
    WidescreenPresenter& wsp = *fp.wsp;
    const bool now_inside = g.state.cave_flag || g.state.secret_flag;
    trans.slide_old_wide_ok = false;   // rebuilt per kind 3/4 classification
    classify_screen_transition(g, pf, trans, fp, wsp, fb, enhanced, warp_fade,
                               now_inside);
    const WideOutgoing wide = compose_outgoing_wide(g, pf, trans, wsp);
    // The L3 trunk-descent branch (dispatched in run_platform_level)
    // already called clear_per_screen_state, bind_screen, and cleared
    // screen_change — this helper is only reached on every OTHER
    // transition, so the common path runs unconditionally here.
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
    if (wsp.active() && wide.eligible_kind) {
        const bool ws_new = wsp.present_path();
        trans.wide = wide.old_wide || ws_new;
    }

    // Note: the L3 trunk-descent path keeps trans.wide false — its
    // descent animation is its own (non-peek) flow, left unchanged.
}

// The F5 report of a platform level: the live SystemsState, the widescreen
// presenter's display state, and — when the present path transforms the
// frame (HD or widescreen) — the frame as the player saw it.
void write_platform_report(
    Loaded& g, WidescreenPresenter& wsp, SDL_Renderer* ren, SDL_Window* win,
    const FrameBuffer& shot, const BugAnnotations& ann, int display_level,
    int internal, int hd_scale, bool hd,
    const std::function<void(FrameBuffer&, bool, bool)>& upload_and_show) {
    // Read the present path's live state at the moment of capture — the
    // report is otherwise silent about exactly the thing a visual bug is
    // about (bug_capture.hpp's DisplayInfo note).
    DisplayInfo di = read_display_info(ren, win);
    di.aspect = wsp.aspect();
    di.hd = wsp.hd();
    di.hd_scale = wsp.hd_scale();
    di.ws_active = wsp.active();
    di.ws_margin = wsp.margin();
    di.ws_native_w = wsp.native_w();
    const bool want_presented = hd || wsp.present_path();
    const std::string dir = write_bug_report(
        g.state, shot, g.render.entity_sprites, display_level, internal,
        hd_scale, ann, want_presented, di);
    // screenshot_presented.png — what the player actually saw: the scene run
    // through the live present (HD upscale + widescreen margins), which the
    // native shot skips.  Re-render the frozen scene WITHOUT presenting so
    // RenderReadPixels sees the backbuffer (a post-present read is black on
    // Metal).  Classic 1x is pixel-equal to the native shot, so it is skipped
    // there.  Empty bubble hook: the L1-secret cosmetic bubbles are
    // immaterial to a bug shot.
    if (!dir.empty() && want_presented) {
        if (wsp.present_path()) {
            wsp.present(std::function<void(RenderTarget&)>{},
                        /*do_present=*/false);
        } else {
            FrameBuffer copy = shot;
            upload_and_show(copy, /*with_hud=*/true, /*do_present=*/false);
        }
        capture_renderer_output(ren, dir + "/screenshot_presented.png");
    }
}

LevelOutcome run_platform_level(GameOptions& opts, int display_level,
                                int internal, CarriedState& carry,
                                SdlAudio& audio, const ScaledWindow& sw,
                                const std::optional<SaveState>& restore_in,
                                std::optional<SaveState>& out_load,
                                std::optional<PendingReinit>& out_reinit,
                                int& out_warp_display) {
    RunCapture capture;
    capture.open(opts.replay, opts.trace, opts.record_inputs);
    InputReplay& replay = capture.replay;
    TraceWriter& trace = capture.trace;
    InputRecorder& input_rec = capture.input_rec;

    // HD upscaling + the enhanced (vector) HUD/menu are one mode: they require
    // --enhanced.  An hd_profile alone (e.g. a stray play.json key) no longer
    // forces HD — that left the bitmap HUD suppressed but the vector HUD off
    // (no HUD).  No --enhanced ⇒ classic 320x200, bitmap HUD + bitmap menu.
    // Computed BEFORE load_level so bind-time decisions (extend_top_backdrop
    // below) can key on the FULL vector-HUD gate, not a wider approximation.
    const bool hd = hd_active(opts.enhanced, opts.hd_profile);
    const int hd_scale =
        hd_scale_for(opts.enhanced, opts.hd_profile, opts.render_scale);
    // The level's presentation surface — texture, vector font, overlay, logical
    // size — owned in one place (§3.7 cluster 1).  The aliases below keep the
    // body reading as it did; they are the same objects.
    LevelSurface surface(sw.win, sw.ren, hd, hd_scale, opts.hd_font,
                         opts.hd_profile, LogicalDims{0, 0});
    enhance::HdText& hd_text = surface.hd_text();

    // The vector-HUD + vector-text subsystem (BG-label erase, in-buffer HUD,
    // output-res text overlay) is a single coupled unit, active whenever the
    // HD substrate is and the font actually loaded.
    const bool use_hd_text = hd && hd_text.ok();
    Loaded g;

    // Enhanced vector HUD: continue the level backdrop up through the top
    // HUD-strip band (no "black bar" behind Score/Lives/Time).  Set BEFORE
    // load_level so the initial bind_screen already extends the backdrop tiling
    // (L7 lavarock adds a row at y=-54 in bind_screen; PC1 levels use the
    // compose-time mirror).  MUST be gated on use_hd_text — the same gate as
    // the rows-0-8 label erase / hud_strip clear / vector HUD below — not on
    // enhanced+HD alone: with hd-text off (now reachable only by the font file
    // missing) the baked HUD labels still render in rows 0-8, and extending the
    // backdrop would overwrite (PC1 mirror) or cover (L7 y=-54 tile) them with
    // no replacement.  Classic keeps the EXE black strip.
    g.render.extend_top_backdrop = use_hd_text;
    if (!load_level(opts.game_dir, g, internal, opts.start_screen)) {
        std::fprintf(stderr, "game: could not load level data from %s\n",
                     opts.game_dir.string().c_str());
        return LevelOutcome::kQuit;
    }
    // The level the user asked for (--level) is the DISPLAY level, while the
    // rules keying on the level number use the INTERNAL one, and slots 3 and 5
    // swap (kGameLevelOrder).  Say both once at load so a hand-run capture
    // names the level it actually photographed (BACKLOG §6, 2026-09-21).
    std::fprintf(stderr, "game: level %d (internal %d)\n", display_level,
                 internal);

    // Enhanced icy-glider sea-level normalisation (level_setup.hpp): flatten the
    // decorative water to one continuous body during the glider (L5, enhanced).
    setup_enhanced_glider_water(g, opts.enhanced, internal);
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
    // OLDUVAI_FORCE_FOOD=<n>: start the level with n food (debug/test hook,
    // like the other OLDUVAI_FORCE_*).  --god cannot do it under --replay,
    // and tests/food_gate_transition.sh needs the gate screen both ways.
    if (const int food = env_int("OLDUVAI_FORCE_FOOD", -1); food >= 0)
        g.state.food_count = food;

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
    // Disk persistence is OPT-IN, and off by default.
    //
    // It exists because upscaling used to be expensive — a Python-port-era
    // problem that does not survive into this engine.  Measured on a cold
    // (empty) cache with the most expensive profile, omniscale, at
    // widescreen: 600 frames, ZERO overruns, worst frame 25 ms against a
    // 55 ms budget.  Upscaling every asset on first visit does not come close
    // to costing a frame, and the in-memory map_ already makes it once-per-
    // session.  A warm run measured no faster.
    //
    // What it did cost: (asset x profile x scale) written forever with no cap,
    // no eviction and no TTL — one machine reached 2680 files and 411 MB
    // simply by trying the six HD profiles.
    //
    // Kept, not deleted, because the trade reverses on memory-constrained
    // platforms (handhelds), where holding every upscaled asset in RAM is the
    // expensive side.  No CLI flag reaches it any more — HdAssetCache::
    // enable_disk() is called only by test_upscale.cpp.  Re-measure both sides
    // on the target hardware before wiring it back up.
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
    TextOverlay& text_overlay = surface.overlay();

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
    LogicalSize& lsz = surface.lsz();
    SDL_Texture* const tex = surface.tex();
    WidescreenShellCtx wsctx;
    wsctx.surface = &surface;   // was six members, wired one at a time
    wsctx.aspect = &opts.aspect;          // Tier-1 live changes tracked
    wsctx.state = &g.state;
    wsctx.render = &g.render;
    wsctx.hd_cache = &g.hd_cache;
    wsctx.internal_level_id = g.config.internal_id;
    wsctx.surface_screen_count = static_cast<int>(g.tiles.screens.size());
    wsctx.level_visual_background = g.config.visual_background;
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
    // set() writes SDL and the mirror together.  Previously SDL was written
    // only when wsp.active() while the mirror was written unconditionally —
    // the asymmetry §3.13 is about.  When inactive both now land on
    // _fallback_ld, which is what SDL should already hold.
    lsz.set(_ld.w, _ld.h);

    // --cheats interactive power-up picker (F7 opens; UP/DOWN select;
    // ENTER/1-6 grant; ESC closes).  Pauses the world while open.
    CheatPicker cheats;
    auto cheat_row_label = [&](int i) { return cheats.row_label(i); };

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

    // Classic path: CHARSET1 bitmap font into the native 320x200 buffer.  A
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
            row(76 + i * 12, cheat_row_label(i), i == cheats.sel() ? 14 : 7);
        row(176, "1-6  UP/DOWN  ENTER  ESC", 7);
    };

    // In HD mode gameplay buffers are sized at the target resolution so every
    // compose goes directly through the per-asset cache path (no whole-frame
    // upscale).  Classic buffers stay 320x200.  The default FrameBuffer ctor
    // produces 320x200 (used by loading/tally/PC1 screens via present()).
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
    const int fb_w = 320 * hd_scale;
    const int fb_h = 200 * hd_scale;
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

    // Enhanced: the L1 balloon bunch floats away when the ride lands on
    // screen 12 (render/rising_balloons.hpp — shared with the boss fly-in).
    // Stepped once per tick before the compose; `fx_alpha` interpolates its
    // rise on the smooth-motion sub-frames (1 everywhere else).
    RisingBalloons rising_balloons;
    float fx_alpha = 1.0f;
    auto draw_rising_balloons = [&](RenderTarget& t) {
        rising_balloons.draw(t, g.render.entity_sprites, g.render.palette,
                             fx_alpha);
    };

    // The widescreen present draws the smoke tail over its wide foreground
    // (same site the in-loop wsp.present lambda called it from).
    wsp.set_draw_overlay_tail([&](RenderTarget& t) {
        draw_l3_smoke_tail(t);
        draw_teleport_fx(t);
        draw_rising_balloons(t);
    });
    bool running = true;

    // ESC / window-close → abort the run to the title via the game-over path.
    // Kept SEPARATE from g.state.game_over because --god resets game_over every
    // frame to suppress death; routing the abort through its own flag means ESC
    // still works under --god.  Consumed at the game-over block below.
    bool abort_to_title = false;

    // ── In-game Pause menu (ESC) ──────────────────────────────────────────
    // Model generated from data/menus.json at build time, drawn by draw_menu,
    // freezes the
    // sim like the --cheats picker.  v1: dark backdrop behind the slab, god
    // toggle is live; other Options are navigable placeholders (live-apply is
    // the next slice).  Spec: 2026-06-19-re-game-menus-design.md.
    bool want_reinit = false;         // Pause → Settings change needing re-init
    PendingReinit reinit_req;
    std::optional<MenuModel> menu_model_opt = load_menu_model();
    MenuModel pause_model = menu_model_opt.value_or(MenuModel{});
    const bool menu_ok = menu_model_opt.has_value();
    // Unreachable since the model became build-time generated (it cannot be
    // missing or malformed any more).  The branch stays because every call
    // site is written against optional<>, and rewiring them all to prove a
    // now-impossible case cannot happen is not worth the churn.
    if (!menu_ok) {
        std::fprintf(stderr, "menu: no menu model - ESC falls back to "
                     "quit-to-title\n");
    }

    // State + orchestration (bindings, staging session, confirm dialog,
    // menu, SettingsFlow, exit intents) live in PauseService (CC3 seam 2);
    // want_reinit/reinit_req stay locals — the REINIT_TEST hook and the
    // kReinitDisplay outcome mapping below use them directly.  The Cheats →
    // Spawn bonus actions ride make_pause_actions unchanged (see
    // pause_flow.cpp).
    PauseService pause(pause_model, menu_ok,
                       {&g, &replay, &opts, &audio, &sw, &god_active,
                        &abort_to_title, &out_load, &want_reinit, &reinit_req,
                        &lsz, hd_scale, display_level,
                        // A live Aspect edit changes no output size, so the
                        // widescreen presenter would otherwise not notice
                        // until the next Alt+Enter.
                        [&wsp] { wsp.aspect_changed(); }});

    // ── F5 bug-report form ─────────────────────────────────────────────────
    // F5 freezes the sim and opens an in-engine form (tag/repro choice rows +
    // a multi-line description edited in a full-canvas overlay).  Leaving the
    // form opens a Save/Discard confirm (the Options-Apply pattern): Save
    // writes the report WITH the annotations, Discard drops the whole capture.
    // State + orchestration live in ReportFormService (CC3 seam 1); the
    // pause MenuModel carries the form's "bug_report" screen.
    ReportFormService report_form(pause_model);

    // Debug: OLDUVAI_PAUSE_SHOT force-opens the Pause overlay on frame 1 and
    // dumps it to a PNG (see the pause block below) — headless render check.
    if (std::getenv("OLDUVAI_PAUSE_SHOT") && menu_ok) {
        // OLDUVAI_PAUSE_SCREEN picks the screen to capture (default "pause")
        // — lets the headless render check reach submenus (Cheats, Options).
        const char* ps = std::getenv("OLDUVAI_PAUSE_SCREEN");
        pause.force_open_screen(ps != nullptr ? ps : "pause");
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

    LevelDiag diag;
    diag.stats.begin_run();

    // (The RAII present timer that used to live here moved onto FramePresenter
    // with the `upload_and_show` alias it existed to wrap — §3.7 cluster 3.
    // diag.stats.perf_ms / .enabled below are still read: they are what `fp`
    // is wired with.)
    const Uint32 frame_ms = 1000 / 18;   // 18 Hz logic (aux pacing sites)
    DosTicker dos_ticker;                // drift-free 18.2065 Hz main pacing
    bool vga_scan_ok = true;   // cleared when the driver clearly refused vsync

    // Smooth-motion sub-frame count.  The logic stays 18 Hz; the renderer draws
    // smooth_N interpolated frames per tick.  Scaled to the display refresh so
    // the present cadence tracks the panel (finer per-sub-frame motion = slow
    // sprites like the fluid bubbles step in smaller increments).  Clamped
    // [4,5]: 4 is already finer than the legacy 3 on a 60 Hz panel; 5 is the
    // perf ceiling — the worst-case WS-omniscale compose measured ~9.6 ms, so
    // 5 x 9.6 = 48 ms fits the 55 ms tick budget (6 overshoots → ~17.4 Hz).
    // Logic is untouched so this is cosmetic-only.  Override for tuning:
    // OLDUVAI_SMOOTH_SUBFRAMES=<n> (e.g. on a light profile a 120 Hz panel can
    // afford 6-7).
    // Refresh-adaptive sub-frame count — the SAME helper boss_app uses
    // (smooth_present.hpp), so the two render loops can't drift.
    const int smooth_N = smooth_subframe_count(win);
    // The vsync fill honours smooth_N only when it was ASKED FOR, never from
    // the refresh-derived default.  Capping the default is a REGRESSION, and it
    // was measured as one on a 144 Hz panel: five presents then a 20 ms timer
    // wait is burst-then-stall, where filling the tick at vblank rate is even.
    //   uncapped  fps 126.33  1%low 56.40  jitter 1.45ms  eff_hz 15.14
    //   capped    fps  86.77  1%low 29.31  jitter 6.00ms  eff_hz 15.54
    // Four times the jitter for 2.6% of game speed.  The refresh-derived value
    // is a HINT for the discrete path; only an explicit request is a ceiling.
    const bool smooth_sub_explicit = smooth_subframes_explicit();

    // Enhanced smooth-motion presents via vsync-locked render interpolation:
    // logic stays a fixed 18 Hz, but the tick's wall-time is filled with
    // interpolated frames paced by the panel's vsync at a CONTINUOUS alpha
    // (elapsed / tick) — so motion is smooth at any refresh (60/120/144/VRR)
    // with no fixed-sub-frame quantisation and no 54-vs-60 beat.  This is the
    // general "fix-your-timestep + render interpolation" technique.  vsync is
    // requested at runtime (SDL>=2.0.18); if the driver refuses, vsync_active
    // stays false and the loop falls back to the discrete smooth_N pacing.
    // OLDUVAI_NO_VSYNC=1 forces the discrete fallback (for A/B testing).
    bool vsync_active = smooth_try_enable_vsync(ren, opts.enhance.smooth_motion);

    // Carryover (ms) of render-fill overshoot beyond one tick — subtracted from
    // the next tick's render budget so the long-term logic cadence stays 18 Hz
    // even though an integer number of vsync frames rarely divides the 55 ms
    // tick exactly (e.g. 3.3 refreshes/tick at 60 Hz).
    Uint32 smooth_carryover = 0;

    // Helper: build a RenderTarget over a gameplay FrameBuffer.  In HD the
    // target carries the cache and profile so blit_sprite uses the per-asset
    // upscale path; in classic it is a plain scale-1 wrapper.
    auto make_rt = [&](FrameBuffer& b) {
        return make_render_target(b, surface, g.hd_cache);
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
    // HUD compose lives on FramePresenter (draw_hud_for) — §3.7 cluster 3
    // slice 2.  The state-mutation semantics and the classic/HD split are
    // documented at the method; the wiring is with the rest of fp's below.

    // Debug aid: OLDUVAI_DRAW_LOG=<file> logs every RENDERED frame's
    // draw positions (player + entities, sub-frames included) as JSONL.
    // an offline analysis tool flags
    // single-frame outliers — transient glitches no eyeball catches.
    FILE* draw_log = nullptr;
    if (const char* dl = std::getenv("OLDUVAI_DRAW_LOG")) {
        draw_log = std::fopen(dl, "w");
    }
    auto log_draw = [&](int sub) {
        write_draw_log(draw_log, frame, sub, g.state);
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
    // frame (fp.present), and the wide transitions (wsp->present_transition)
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
    // Animated enhanced banner substitutes (GET READY / NOT ENOUGH FOOD):
    // BannerPresenter owns the fly-away latch and the wall-clock animation
    // (banners.hpp); the shell keeps the enhanced-only gating at the call
    // sites and the once-per-logic-tick arm_tick() placement below.
    BannerPresenter banners(hd_text, g.state, opts.banner_fx,
                            opts.frames > 0 || !opts.screenshot.empty());
    // Every banner draw — both presenters, the smooth path — comes through
    // here, so this is the one place that keeps banners out from under an
    // open menu: pause, the F5 form, the F7 picker (owner report, 2026-09-17:
    // NOT ENOUGH FOOD stayed on top of the pause menu).
    auto draw_enhanced_banners = [&](std::vector<std::uint8_t>& b,
                                     int ow, int oh) {
        if (pause.open() || report_form.open() || cheats.open()) return;
        banners.draw(b, ow, oh);
    };

    // Banner substitutes are shell-owned (state-driven); the wide HUD-text
    // mapping itself lives in the presenter (wsp.draw_wide_hud_text — CC2d).
    wsp.set_draw_banners([&](std::vector<std::uint8_t>& b, int ow, int oh) {
        draw_enhanced_banners(b, ow, oh);
    });
    // Opt this site into the overlay skip.  BannerPresenter::key() returns 0
    // whenever it would draw anything, so a visible banner forces a redraw.
    wsp.set_banners_key([&]() { return banners.key(); });

    // OLDUVAI_MENU_SCRIPT: drive the menus headlessly with synthetic SDL key
    // events (same SDL_PushEvent path the gamepad uses), one token per frame.
    // Tokens: esc up down left right enter space 1..6 | wait | shot | quit.
    // `shot` dumps the composed frame to OLDUVAI_MENU_SCRIPT_DIR/NNN.png; `quit`
    // exits cleanly (kQuitProgram). Turns interactive menu paths into automatable
    // regression tests — see tests/diag.menu.script.sh.
    // CAVEAT: these are locals of run_platform_level, so an Apply that
    // triggers a display reinit re-enters the level and REPLAYS the script
    // from the first token (shots restart at 000.png and overwrite).  Walks
    // that apply a reinit-class change must account for the second pass
    // (or use OLDUVAI_REINIT_TEST, which reinit_smoke drives instead).
    // Parsing + key injection live in menu_script_util.hpp (shared with the
    // title-menu walk); the type:/chord tokens below stay local to this loop.
    diag.menu.load_from_env();

    // The per-frame upload/composite/present pipeline now lives in
    // FramePresenter (frame_presenter.cpp); wire it to the live run-loop state.
    // It brackets its own present time now (present_ms/perf_ms/stats_on below).
    FramePresenter fp;
    fp.surface = &surface;   // was nine members, wired one at a time
    fp.render = &g.render;
    fp.charset = &g.charset;
    fp.hd_cache = &g.hd_cache;
    fp.wsp = &wsp;
    fp.pause = &pause;
    fp.state = &g.state;
    fp.cheats = &cheats;
    fp.menu_shot_path = &diag.menu.shot_path;
    fp.draw_cheat_rows_native = draw_cheat_rows_native;
    fp.draw_cheat_rows = draw_cheat_rows;
    fp.draw_enhanced_banners = draw_enhanced_banners;
    // §3.7 cluster 3: `upload_and_show` is GONE.  It was a lambda whose whole
    // body was an RAII timer plus `fp.present(...)`, with a signature and
    // defaults already identical to present()'s — a wrapper whose only content
    // belonged to the presenter.  The timer is a FramePresenter member now, so
    // the loop calls the pipeline directly and one prologue name and one
    // std::function adapter leave the driver.
    fp.present_ms = &diag.stats.present_ms;
    fp.swap_ms = &diag.stats.swap_ms;
    fp.upload_ms = &diag.stats.upload_ms;
    fp.present_calls = &diag.stats.present_calls;
    // The HUD overlay is owned by the LevelSurface and reached by both
    // presenters, so its sinks are set once here rather than per presenter.
    wire_overlay_stats(diag.stats, surface.overlay());
    fp.perf_ms = diag.stats.perf_ms;
    fp.stats_on = diag.stats.enabled;
    // The widescreen presenter accumulates into the SAME counter: a frame goes
    // through exactly one of them, so summing is right and it keeps the stat
    // meaning "time spent presenting this frame" in either aspect.
    wsp.present_ms = &diag.stats.present_ms;
    wsp.swap_ms = &diag.stats.swap_ms;
    wsp.upload_ms = &diag.stats.upload_ms;
    wsp.present_calls = &diag.stats.present_calls;
    wsp.fg_ms = &diag.stats.fg_ms;
    wsp.tick_paused = &diag.stats.tick_paused;
    wsp.present_iv = &diag.stats.present_iv_ms;
    wsp.last_present_pc = &diag.stats.last_present_pc;
    fp.present_iv = &diag.stats.present_iv_ms;
    fp.last_present_pc = &diag.stats.last_present_pc;
    wsp.bg_copy_ms = &diag.stats.bg_copy_ms;
    wsp.scene_ms = &diag.stats.scene_ms;
    wsp.glyph_ms = &diag.stats.glyph_ms;
    wsp.perf_ms = diag.stats.perf_ms;
    wsp.stats_on = diag.stats.enabled;

    // std::function view of the present for the services extracted out of this
    // frame loop (ReportFormService, DescentCtx, CC3).  A call adapter, not a
    // copy — and now over `fp` itself rather than over a lambda over `fp`.
    const std::function<void(FrameBuffer&, bool, bool)> upload_and_show_fn =
        [&fp](FrameBuffer& f, bool with_hud, bool do_present) {
            fp.present(f, with_hud, do_present);
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
    // The non-gameplay screen presenter — loading card, fades, tally, the L3
    // descent (§3.7 cluster 3).  boss_app builds the same type with its own
    // compose step; see sequence/screen_presenter.hpp.
    ScreenPresenter screen(surface,
                           [&](const FrameBuffer& f, bool do_present) {
                               FrameBuffer copy = f;   // upload may mutate
                               fp.present(copy, /*with_hud=*/false,
                                          do_present);
                           },
                           frame_ms);
    const PresentFn present = screen.fn();

    // Enhanced score tally: route text through the cartoon vector font at HD
    // resolution.  present_hd uploads a ready HD buffer; the
    // tally builds it (upscaled black base + cartoon rows).  Classic → null.
    // Built once, used by both text screens this level presents (the loading
    // card below, the tally on level-complete).
    const TextScreenDeps text_screen_deps{ren,      win,
                                          tex,      &hd_text,
                                          &text_overlay, &lsz,
                                          hd_scale, &opts.hd_profile,
                                          frame_ms};
    LevelOutcome outcome = LevelOutcome::kQuit;

    // The loading card and the tally both route their text rows through the
    // cartoon vector font at HD res (the reference records the loading lines
    // into a TextLayer); classic passes a null hd_text and keeps the
    // byte-identical bitmap path.  Both handles are built by
    // ScreenPresenter::text_screen at the screen itself — they differ only in
    // which gate dump they answer to, which is the argument for one wiring.

    // Warm the HD sprite cache BEFORE the loading screen, not after: the
    // upscales then happen while "Please Wait" is already on the display,
    // which is a moment the player expects to wait.  Lazily, they landed in
    // the frame that first drew each sprite — measured on a Cortex-A53 at
    // 1137.81 ms for 56 sprites on entering the L1 secret room, the worst
    // remaining hitch in the handheld port.  See hd_warm.hpp.
    if (hd_scale > 1) {
        const auto t0 = SDL_GetPerformanceCounter();
        const std::size_t n =
            warm_hd_sprite_cache(g.hd_cache, g.render.tile_sprites,
                                 g.render.palette, hd_scale, opts.hd_profile) +
            warm_hd_sprite_cache(g.hd_cache, g.render.entity_sprites,
                                 g.render.palette, hd_scale, opts.hd_profile);
        if (std::getenv("OLDUVAI_FRAME_STATS") != nullptr) {
            const double ms = 1000.0 *
                static_cast<double>(SDL_GetPerformanceCounter() - t0) /
                static_cast<double>(SDL_GetPerformanceFrequency());
            std::fprintf(stderr,
                         "hd-warm: %zu upscales in %.1f ms (cache now %zu)\n",
                         n, ms, g.hd_cache.size());
        }
    }

    // Level-entry loading screen.
    if (!screen.text_screen(text_screen_deps, use_hd_text,
                            "OLDUVAI_DUMP_LOADING", "loading",
                            [&](const TextScreenHd& hd) {
                                return show_loading_screen(
                                    nullptr, display_level, g.charset,
                                    g.render.palette, present, hd);
                            })) {
        running = false;
    }

    // Level music starts AFTER the loading screen, with the level itself —
    // the reference plays it post-setup (_show_loading_screen →
    // _setup_level → play_level_music); starting it earlier had the track
    // running over the "Please Wait" text.
    if (running) {
        if (const char* mname = level_music_name(internal))
            play_game_music(&audio, opts.game_dir, mname);
    }

    // Screen-change transition bookkeeping: the old screen's last frame
    // plus the classified effect, played back after the new screen's
    // first compose.  HD-sized to match the gameplay fb.
    TransitionState trans(fb_w, fb_h);

    // --debug-perf: rolling FPS / frame-time over ~30 frames.  `diag.perf.last_t`
    // is the wall clock at the previous frame top (inter-frame interval =
    // wall FPS incl. the 18 Hz throttle); `diag.perf.ms_accum` sums per-frame
    // compute time.  Updated at frame top; smoothed value drawn into fb.
    const bool any_debug_overlay =
        opts.debug_collision || opts.debug_entities || opts.debug_perf;
    diag.perf.last_t = SDL_GetTicks();

    // Draw the requested dev overlays into a gameplay FrameBuffer just before
    // it is presented.  Gated entirely on the --debug-* flags (no-op when
    // none set) so default rendering is byte-identical.
    auto apply_debug_overlays = [&](FrameBuffer& target) {
        if (opts.debug_collision)
            draw_debug_collision(target, g.state, hd_scale);
        if (opts.debug_entities)
            draw_debug_entities(target, g.state, g.render.entity_sprites,
                                hd_scale);
        if (opts.debug_perf)
            draw_debug_perf(target, g.charset, g.render.palette, diag.perf.fps,
                            diag.perf.frame_ms, hd_scale);
    };

    // OLDUVAI_REINIT_TEST headless integration hook (reinit_test_hook.hpp),
    // env-gated (a total no-op in normal play).  On the post-reinit re-entry it
    // writes the round-trip result file and ends the loop.
    ReinitTestHook reinit_hook(std::getenv("OLDUVAI_REINIT_TEST"));
    reinit_hook.maybe_write_result(g.state, win, opts, running);

    // The SettingsFlow (OL-B1) + close-without-apply detection live in
    // PauseService (CC3 seam 2); begin_frame() runs the dirty-session
    // Discard at the TOP of the loop so it fires on the first iteration
    // after pause closes, before any input that could reopen pause.
    // The player position of the last PRESENTED frame, for the level-end
    // fade (set by the 8b intercept on its way out of the loop).
    int end_px = 0;
    int end_py = 0;
    while (running) {
        cursor_autohide_frame();   // keyboard game: park the OS arrow
        pause.begin_frame();

        diag.stats.begin_tick();
        const Uint32 t0 = SDL_GetTicks();
        sample_debug_perf(diag, any_debug_overlay);

        // OLDUVAI_REINIT_TEST: frame-5 pre-reinit trigger (reinit_test_hook.hpp).
        reinit_hook.maybe_trigger(g.state, opts, frame, menu_ok, reinit_req,
                                  want_reinit, pause);
        // Pre-frame snapshot for transition classification: the player
        // position before this frame's movement/teleport (direction
        // inference) and the inside-ness before cave/secret entry.
        const PrevFrame pf(g.state);

        // OLDUVAI_MENU_SCRIPT: consume one token before the poll so the synthetic
        // key is processed by this frame's event loop (drives pause/menus exactly
        // like a human — open via ESC, navigate, activate, cheats).
        if (diag.menu.active()) {
            if (drive_menu_script(diag.menu, &report_form)) {
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
            if (report_form.open()) {
                report_form.handle_event(ev);   // consumes every event
                continue;
            }
            if (ev.type == SDL_KEYDOWN) {
                const auto sym = ev.key.keysym.sym;

                // In-game Pause menu owns input while open; swallow gameplay keys.
                if (pause.open()) {
                    pause.handle_keydown(sym);
                    continue;
                }

                // --cheats interactive power-up picker (non-EXE-faithful test
                // aid; off during replay so traces stay deterministic).
                if (cheats.handle_key(sym, [&](int bt) {
                        systems::dispatch_bonus_activate(g.state, bt);
                        std::printf("cheat: granted %s\n",
                                    CheatPicker::name(bt));
                    })) {
                    continue;   // swallowed while the picker is up
                }
                if (sym == SDLK_ESCAPE) {
                    // ESC opens the Pause overlay (or falls back to a direct
                    // title-abort — see PauseService::esc_pressed).
                    pause.esc_pressed();
                }
                else if (sym == SDLK_F5) {
                    // F5 opens the in-engine bug-report form (seed + freeze;
                    // see ReportFormService::open_form).
                    report_form.open_form();
                }
                else if (opts.cheats && !replay.active() && sym == SDLK_F7) {
                    cheats.open_picker();
                }
            }
        }

        // ── Options-exit detection (§8.6 step 2): after input handling,
        // SettingsFlow checks whether the menu just transitioned from inside
        // the Options subtree back to the pause root, and opens the confirm
        // dialog if changes are staged (PauseService::track_options_exit). ──
        pause.track_options_exit();

        // ── Pause overlay: handle exit actions, else freeze the sim and draw
        // the menu (or confirm dialog) over a dark backdrop.  The `continue`
        // skips frame-counter, run_frame, post-frame logic and gameplay render
        // — a full freeze.  Quit to Title routes through the normal
        // game-over→title path; Exit Game / Restart use new outcomes. ──
        // ── F5 report form: freeze + draw over the frozen scene (before the
        // pause block; the two are mutually exclusive since F5 only fires
        // outside pause).  Save writes the report from the stashed frame;
        // the service owns the whole frame when open (ReportFormService,
        // CC3 seam 1). ──
        if (report_form.open() && report_form.service_freeze(
                {[&](FrameBuffer& out) {
                     g.state.god_mode = god_active;
                     RenderTarget prt{out.px.data(), out.w, out.h, 1,
                                      nullptr, nullptr};
                     prt.advance_state = false;   // see FreezeDeps::compose
                     compose_frame(prt, g.state, g.render,
                                   /*draw_player=*/true);
                 },
                 [&](FrameBuffer& f) {
                     upload_and_show_fn(f, /*with_hud=*/false,
                                        /*do_present=*/true);
                 },
                 [&](const FrameBuffer& shot, const BugAnnotations& ann) {
                     write_platform_report(g, wsp, ren, win, shot, ann,
                                           display_level, internal, hd_scale,
                                           hd, upload_and_show_fn);
                 },
                 g.charset,
                 g.render.entity_sprites.size() > 33
                     ? &g.render.entity_sprites[33]
                     : nullptr,
                 &g.render.palette, frame_ms}))
            continue;
        {
            // Freeze + draw live in PauseService (CC3 seam 2); the intent →
            // LevelOutcome mapping stays here (LevelOutcome is file-local),
            // in the exact order of the old inline block.
            const PauseService::FreezeResult pfr = pause.service_freeze(
                {g, god_active, use_hd_text, frame_ms, wsp,
                 upload_and_show_fn});
            if (pfr == PauseService::FreezeResult::kFroze) continue;
            if (pfr != PauseService::FreezeResult::kNone) {
                switch (pfr) {
                    case PauseService::FreezeResult::kQuitProgram:
                        outcome = LevelOutcome::kQuitProgram; break;
                    case PauseService::FreezeResult::kRestartLevel:
                        outcome = LevelOutcome::kRestartLevel; break;
                    case PauseService::FreezeResult::kLoadCheckpoint:
                        outcome = LevelOutcome::kLoadCheckpoint; break;
                    case PauseService::FreezeResult::kWarpLevel:
                        out_warp_display = pause.want_warp();
                        outcome = LevelOutcome::kWarpLevel; break;
                    case PauseService::FreezeResult::kReinitDisplay:
                        reinit_req.state = capture_save(g, display_level);
                        out_reinit = reinit_req;
                        outcome = LevelOutcome::kReinitDisplay; break;
                    case PauseService::FreezeResult::kAbortGameOver:
                        outcome = LevelOutcome::kGameOver; break;
                    case PauseService::FreezeResult::kShotQuit:
                        // kQuitProgram is the "Exit Game" outcome
                        // run_game exits on (pause_shot must not advance the
                        // sequencer).
                        outcome = LevelOutcome::kQuitProgram;
                        running = false; break;
                    default: break;
                }
                break;
            }
        }

        // Frame-counter wrap drives the timer (1 Hz-ish) and food-out
        // death.  The original resets when the PRE-increment value
        // exceeded 0x3C — i.e. after fc=61 has been used — giving a
        // 62-value cycle (1..61, then 0..61).  // DS:0x985a
        if (!cheats.open() && g.state.frame_counter > 0x3D) {
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

        // Resolve this frame's inputs — replay, or live keyboard + gamepad +
        // autofire (frame_input.hpp).
        systems::FrameInputs in = gather_frame_inputs(
            replay, frame, opts.autofire, autofire, g.state.player, running);
        // Record the RESOLVED inputs (live or replay-injected) at the frame
        // the reader will resolve them — the loop reads replay.at(frame+1),
        // so emit at frame+1 to round-trip back to this same game frame.
        if (input_rec.active()) input_rec.record(frame + 1, in);

        // Previous-tick snapshot for the smooth-motion lerp — every field the
        // reference interpolates, before the sim tick (lerp_snapshot.hpp).
        save_prev_positions(g.state);

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
        // snapshot above still holds the cave state (pf.inside,
        // pf.cave_index, pf.screen), so the transition classifier below
        // sees the cave->surface edge and plays the cave fade pair.
        // Completing it in the end-of-tick block (where the countdown is
        // decremented) mutated the state AFTER the classifier, and the next
        // tick misclassified the change as a surface pan-scroll.
        if (!cheats.open()) systems::try_complete_sign_teleport(g.state);
        g.state.skip_player_update = systems::tick_cave_descent(g.state);
        if (!cheats.open()) systems::run_frame(g.state, in);

        // Tier-1 living margins: cycle the peek monsters' walk sprites IN
        // PLACE, once per logic tick.  No translation, no RNG, no collision —
        // pure sprite animation on cloned lists (the anchor at the spawn post
        // is what keeps entry pop-free).  L3A alternates ride the live global
        // phase counter so margin cadence matches the centre screen.
        if (!cheats.open() && wsp.active()) wsp.tick_margin_monsters();

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
        // 6b-8a: death halo, glider, clamp/death-by-fall, exits,
        // transitions, cave-warp — all pure SystemsState, so they live in
        // systems/ now.  docs/FRAME_LOOP.md governs their order.
        systems::run_post_frame_steps(g.state);
        // OLDUVAI_FORCE_LEVEL_COMPLETE=<frame>: seed the intercept below so it
        // fires headlessly.  Without it this whole block is reachable only by
        // finishing a level in a real playthrough, which is why it had no gate
        // of any kind before the §3.7 slice-2 extraction needed one.
        if (frame == env_int("OLDUVAI_FORCE_LEVEL_COMPLETE", -1))
            g.state.level_complete = true;

        // 8b. Level-complete intercept: leave the loop, as the reference
        // does (`if state.level_complete: break`) and the EXE does (FUN_21f3_006f
        // +0x01a1 jumps to its exit block) — the pseudo-exit screen never
        // binds or renders, and this frame is neither composed, presented nor
        // traced.  The fade and the tally run after the loop, like boss_app's.
        if (g.state.level_complete) {
            g.state.screen_change = false;
            end_px = pf.px;
            end_py = pf.py;
            break;
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

        // 9. Screen change - per-screen state clear, store rebind, and the
        //    one-frame walk/gravity skip for the next frame.
        if (g.state.screen_change) {
            // Classify the visual effect + capture the old screen's frame
            // before the rebind (playback happens after the new screen's
            // first compose, below).  The body lives in step9_screen_change;
            // only the trunk-descent dispatch stays here — its six wiring
            // names belong to the driver, not the classifier.
            bool warp_fade = g.state.player.cave_warp_freeze == 0x3E8 ||
                             g.state.player.cave_warp_pending;
            g.state.player.cave_warp_pending = false;   // per-frame transient
            const bool l3_trunk_descent =
                pf.screen == 17 && g.state.current_screen == 18 &&
                systems::seam_kind(g.state.current_level, 17, 18) ==
                    systems::SeamKind::TrunkDescent;
            // L7 fake cave (12↔13): EXE entry is an instant warp (capstone
            // 25b2:07df jumps over the wipe).  The reference pans in
            // classic mode (documented simplification) and fades under
            // --enhanced (intentional-divergence-cosmetic, matching the
            // L3 cave-system convention) — mirror both.
            if (l3_trunk_descent) {
                // The whole screen-17→18 trunk-descent cinematic (widescreen
                // margins + present callbacks + Phase 1 / enhanced pan / Phase 2
                // + screen-18 overlay stamping) lives in l3_end_level.cpp.  Bind
                // the live run-loop context and run it; it drives `running`
                // false on ESC / window-close and arms l3_smoke_tail, exactly as
                // the inline block did.  trans.kind stays 0 (no extra
                // transition) afterwards.
                DescentCtx dc;
                dc.surface = &surface;   // was seven members
                dc.wsp = &wsp;
                dc.g = &g;
                dc.opts = &opts;
                dc.running = &running;
                dc.l3_smoke_tail = &l3_smoke_tail;
                dc.frame_ms = frame_ms;
                dc.prev_screen = pf.screen;
                dc.logical_w = lsz.w();
                dc.logical_h = lsz.h();
                dc.l3_smoke_tail_ticks = kL3SmokeTailTicks;
                dc.upload_and_show = upload_and_show_fn;
                run_l3_trunk_descent_sequence(dc);
                trans.kind = 0;   // no additional transition needed
            } else {
                step9_screen_change(g, pf, trans, fp, fb, opts.enhanced,
                                    warp_fade);
            }
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

        // 8c. Secret-room bubble scatter - exactly one 627-draw LCG pass per
        //     gameplay frame.  Native runs it HERE in the render gate; the
        //     reference runs it as logic step 8c - same per-frame consumption.
        if (g.state.secret_flag) {
            const bool fluid = opts.enhanced &&
                               g.fluid_bubbles_initialized;
            refresh_secret_tiles(g, /*draw_scatter=*/!fluid);
            // Enhanced-mode: tick fluid bubbles AFTER the LCG pass so the
            // global LCG sequence is unaffected (cosmetic PRNG is separate).
            if (opts.enhanced && g.fluid_bubbles_initialized) {
                g.fluid_bubbles.tick();
            }
        }

        // Enhanced-mode secret-room bubble hook: draw 60 persistent rising
        // bubbles BEFORE the tile placements so the floor line at y=168
        // covers low-y bubbles (they emerge from behind the floor).
        // The hook is null in classic mode (LCG scatter draws normally).
        std::function<void(RenderTarget&)> bubble_hook;
        if (opts.enhanced && g.state.secret_flag &&
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
        // The balloons are held while the L1 ride is on (drawn at the same
        // origin as the death halo); a death sends up the game's own halo.
        rising_balloons.step(g.state.enhanced_active,
                             g.state.current_level == 1 &&
                                 g.state.glider_active,
                             g.state.player.death_counter == 0,
                             g.state.player.x, g.state.player.y - 30,
                             g.state.current_screen);
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
            draw_rising_balloons(rt);
        }

        advance_once_per_tick(g, fp, fb, banners, l3_smoke_tail);

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
        if (trans.kind != 0) {
            // No banner over a moving screen (BannerPresenter::set_suppressed).
            banners.set_suppressed(true);
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
            tctx.wsp = &wsp;   // borrowed; was four copied accessors
            tctx.hd_cache = &g.hd_cache;
            tctx.state = &g.state;
            tctx.render = &g.render;
            tctx.screen_count = static_cast<int>(g.tiles.screens.size());
            tctx.slide_secret_exit_x = trans.slide_secret_exit_x;
            tctx.slide_end_x = trans.slide_end_x;
            tctx.slide_end_y = trans.slide_end_y;

            tctx.upload_and_show = [&fp](FrameBuffer& f) { fp.present(f); };
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

            if (trans.wide && (trans.kind == 1 || trans.kind == 2)) {
                // Widescreen path: build the INCOMING wide buffer from the NEW
                // screen's native-320 center (cache now reflects the new screen),
                // then slide/fade the wide buffers.  Player-included for both
                // kinds: kind 1's player rides the incoming surface; kind 2's fade
                // hides it.  ws_new chooses peek-vs-bezel for the new side.
                const bool ws_new = wsp.present_path();
                FrameBuffer new_center{};   // 320x200
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
                    trans.kind == 1 && !g.state.cave_flag &&
                    !g.state.secret_flag && g.state.current_screen < 100 &&
                    pf.screen < 100 &&
                    std::abs(g.state.current_screen - pf.screen) == 1 &&
                    (trans.dir == 'R' || trans.dir == 'L');
                if (std::getenv("OLDUVAI_WS_DEBUG") != nullptr)
                    std::fprintf(stderr,
                                 "[WS-TRANS] kind=%d dir=%c %d->%d cave=%d "
                                 "secret=%d => %s\n",
                                 trans.kind, trans.dir, pf.screen,
                                 g.state.current_screen, g.state.cave_flag,
                                 g.state.secret_flag,
                                 panorama_ok ? "panorama" : "legacy");

                if (panorama_ok) {
                    play_panorama_wide(tctx, pf.screen,
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
                    play_transition_wide(tctx, trans.old_wide, new_wide,
                                         trans.kind, trans.dir);
                }
                trans.kind = 0;
            } else if (trans.kind == 3 || trans.kind == 4) {
                if (trans.wide && trans.slide_old_wide_ok) {
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
                    play_transition_wide(tctx, trans.slide_old_wide, new_wide,
                                         trans.kind, trans.dir);
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
                    fp.draw_hud_for(fb_noplayer);
                    play_transition(tctx, trans.old_frame, fb_noplayer,
                                    trans.kind, trans.dir);
                }
            } else {
                play_transition(tctx, trans.old_frame, fb, trans.kind,
                                trans.dir);
            }
            trans.kind = 0;
            banners.set_suppressed(false);
        }

        // Debug/test hook: OLDUVAI_AUTO_FULLSCREEN=<frame> programmatically
        // toggles desktop-fullscreen at that gameplay frame (simulates Alt+Enter)
        // so the surface widescreen recompute path can be captured headlessly
        // (pair with --screenshot/--screenshot-frame).  Mirrors the boss hook.
        // -1 fallback — see boss_app: unset and mistyped both mean "never",
        // where atoi turned a typo into "frame 0".
        if (win != nullptr && frame == env_int("OLDUVAI_AUTO_FULLSCREEN", -1)) {
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

            // A screen or cave/secret-mode change this tick is a teleport by
            // definition — the discontinuity signal the 16-px distance guard
            // cannot see when the warp lands nearby (the L3 S4 cave entry is
            // a (9,11)px hop; bug report 2026-07-17_165051_L3_S4 — the
            // sprite swept from the hole onto the cave background for one
            // tick).  Snap every lerp for this tick.
            const bool warp_snap =
                g.state.current_screen != pf.screen ||
                (g.state.cave_flag != 0 || g.state.secret_flag != 0) !=
                    pf.inside;

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
                fx_alpha = alpha;   // this sub-frame's balloon rise

                // ONE guarded decision per field, writing BOTH shadows.
                //
                // These were four lambdas — pair_lerp / one_lerp and their _f
                // twins — and every call site invoked a pair and its float
                // twin back to back with identical arguments.  That is the
                // same condition evaluated twice per field, ~20 times, and it
                // is the "did you remember the float half?" bug in waiting:
                // the L6 victory drop shipped for exactly that reason.  Now
                // the int and float shadows come out of one call and cannot
                // be written apart.
                //
                // `warp_snap` is this loop's own snap SIGNAL (screen / cave
                // mode change).  It is the reason the shared helper takes a
                // `force` argument at all: the L3 screen-4 cave entry moves
                // the player only (9,11) px — under the distance threshold —
                // so nothing but the caller can know it teleported.  Gated by
                // `cave_lerp`.
                auto lerp2 = [&](int& x, int& y, float& fx, float& fy,
                                 int prevx, int prevy, int curx, int cury) {
                    const auto r = snap_lerp_pair(prevx, prevy, curx, cury,
                                                  alpha, warp_snap);
                    x = r.x;
                    y = r.y;
                    fx = r.fx;
                    fy = r.fy;
                };
                // Scalar twin, for the one-axis fields (current_y, draw_dy).
                auto lerp1 = [&](int& v, float& fv, int prevv, int curv) {
                    v = snap_lerp_i(prevv, curv, alpha, warp_snap);
                    fv = snap_lerp_f(prevv, curv, alpha, warp_snap);
                };
                // Integer-only, for the gait offsets: dx/dy have no float
                // shadow (they are added to an already-float base).
                auto lerp2_i = [&](int& x, int& y, int prevx, int prevy,
                                   int curx, int cury) {
                    const auto r = snap_lerp_pair(prevx, prevy, curx, cury,
                                                  alpha, warp_snap);
                    x = r.x;
                    y = r.y;
                };

                lerp2(g.state.player.x, g.state.player.y,
                      smooth_player_fx, smooth_player_fy,
                      saved_p.prev_x, saved_p.prev_y, saved_p.x, saved_p.y);
                // dx/dy carry the gait offset — interpolating them
                // staggers the walk; the reference lerps them only
                // during the ghost rise (smooth death float).
                if (g.state.player.ghost_rise != 0) {
                    lerp2_i(g.state.player.dx, g.state.player.dy,
                            saved_p.prev_dx, saved_p.prev_dy, saved_p.dx,
                            saved_p.dy);
                }

                for (std::size_t ei = 0; ei < g.state.entities.size();
                     ++ei) {
                    auto& e = g.state.entities[ei];
                    const auto& sv = saved_e[ei];
                    lerp2(e.x, e.y, e.fx, e.fy, e.prev_x, e.prev_y,
                          sv.x, sv.y);
                    lerp1(e.current_y, e.f_current_y, e.prev_current_y, sv.cy);
                    lerp2(e.throw_x, e.throw_y, e.f_throw_x, e.f_throw_y,
                          e.prev_throw_x, e.prev_throw_y, sv.tx, sv.ty);
                    lerp1(e.draw_dy, e.f_draw_dy, e.prev_draw_dy, sv.ddy);
                }

                if (g.state.fireball_flag != 0) {
                    lerp2(g.state.fireball_x, g.state.fireball_y,
                          g.state.fireball_fx, g.state.fireball_fy,
                          g.state.prev_fireball_x, g.state.prev_fireball_y,
                          sv_fb_x, sv_fb_y);
                }
                lerp2(g.state.glider_x, g.state.glider_y,
                      g.state.glider_fx, g.state.glider_fy,
                      g.state.prev_glider_x, g.state.prev_glider_y,
                      sv_gl_x, sv_gl_y);
                if (g.state.death_halo_active) {
                    lerp2(g.state.death_halo_x, g.state.death_halo_y,
                          g.state.death_halo_fx, g.state.death_halo_fy,
                          g.state.prev_death_halo_x, g.state.prev_death_halo_y,
                          sv_dh_x, sv_dh_y);
                }
                if (g.state.stone_state != 0) {
                    lerp2(g.state.stone_x, g.state.stone_y,
                          g.state.stone_fx, g.state.stone_fy,
                          g.state.prev_stone_x, g.state.prev_stone_y,
                          sv_stone_x, sv_stone_y);
                }
                for (std::size_t bi = 0;
                     bi < g.state.score_bonuses.size(); ++bi) {
                    auto& b = g.state.score_bonuses[bi];
                    if (b.counter > 0) {
                        lerp2(b.x, b.y, b.fx, b.fy, b.prev_x, b.prev_y,
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
                if (opts.enhanced && g.state.secret_flag &&
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
                    draw_rising_balloons(rt);
                }

                fp.draw_hud_for(fb);

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
                    maybe_dump_steady(fb.px.data(), fb.w, fb.h);
                    fp.present(fb);
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
                    // smooth_N is an UPPER BOUND here too, not just in the
                    // discrete branch below.  It was not, so
                    // OLDUVAI_SMOOTH_SUBFRAMES silently did nothing whenever
                    // runtime vsync was accepted -- which is the common case,
                    // since SDL_RenderSetVSync succeeds even when the renderer
                    // was created without PRESENTVSYNC.
                    //
                    // WHY IT MATTERS.  Filling to render_budget alone commits
                    // to as many sub-frames as nominally fit, and on a weak
                    // part each one costs real logic rate.  Measured on a
                    // TrimUI Smart Pro: 2.29 sub-frames -> 16.90 Hz,
                    // 3.29 -> 14.40, 4.29 -> 11.32.  A straight ~2.5-3 Hz per
                    // sub-frame, with no cliff -- so the only way to buy the
                    // logic clock back is to ask for fewer, and before this
                    // there was no way to ask without also disabling vsync and
                    // taking tearing instead.
                    //
                    // Default smooth_N is 4-5, so a machine that was already
                    // fitting 4 or fewer is unaffected.
                    const Uint32 el = SDL_GetTicks() - tick_t0;
                    // Only the BUDGET path may claim the tick was paced.
                    // pace_end_of_tick skips its sleep entirely when
                    // smooth_vsync_ran is set, because the vsync fill is
                    // assumed to have consumed the whole tick via the panel.
                    // Breaking early on the smooth_N cap does NOT consume it,
                    // so claiming otherwise ends the tick short with no sleep
                    // and the game runs FAST -- measured at 20.52 Hz against a
                    // target of 18.2, which eff_hz caught immediately and no
                    // other counter would have.  Fall through to timer pacing.
                    const bool budget_met = el >= render_budget;
                    if (budget_met || (smooth_sub_explicit && sub >= smooth_N) ||
                        sub >= 64) {
                        smooth_carryover = el > frame_ms
                                               ? std::min(el - frame_ms, frame_ms)
                                               : 0;
                        smooth_vsync_ran = budget_met;
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
            fx_alpha = 1.0f;

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
                maybe_dump_steady(fb.px.data(), fb.w, fb.h);
                fp.present(fb);
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
            if (wsp.active() || hd) {
                // HD / widescreen: the vector HUD text lives in the
                // output-resolution overlay, not in fb, so capture the FINAL
                // output — re-rendered through the SAME present the live
                // frame used (peek frames via wsp.present's wide composite;
                // everything else via fp.present, which also places the text
                // for the picture's aspect), WITHOUT presenting: on Metal a
                // RenderReadPixels after present returns black.  fp.present
                // redraws the opaque HUD bars into fb; drawing them twice
                // leaves the same pixels.  (The HD branch here used to
                // re-draw scene + HUD text by hand — a copy of fp.present
                // that missed every change to it, §3.23 included.)
                if (wsp.present_path())
                    wsp.present(bubble_hook, /*do_present=*/false);
                else
                    fp.present(fb, /*with_hud=*/true, /*do_present=*/false);
                capture_renderer_output(ren, opts.screenshot);
            } else {
                // Classic: the bitmap HUD is in the 320x200 buffer — save it.
                save_rgba_image(fb.px.data(), fb.w, fb.h, opts.screenshot);
            }
            running = false;
        }
        if (opts.frames > 0 && frame >= opts.frames) running = false;

        // Debug/test hook: OLDUVAI_SMOOTH_FRAMES=<n> caps the run at n frames
        // WITHOUT going through opts.frames — which would force smooth=false
        // (line ~3693) and the classic transition path.  This lets the smooth
        // 54Hz sub-frame loop (and OLDUVAI_BUBBLE_TRACE) run headlessly for a
        // bounded number of frames, the one combination --play-frames can't give.
        // INT_MAX fallback — see boss_app's OLDUVAI_FORCE_WIN: with atoi a
        // typo parsed to 0 and stopped the run on its first frame.
        if (frame >= env_int("OLDUVAI_SMOOTH_FRAMES", INT_MAX)) running = false;

        const Uint32 spent = SDL_GetTicks() - t0;
        if (any_debug_overlay) diag.perf.ms_accum += spent;
        diag.stats.end_tick();

        pace_end_of_tick(ren, tex, dos_ticker, smooth_vsync_ran,
                         opts.vga_scan, hd, vga_scan_ok, &diag.vga.fill_presents,
                         &diag.vga.fill_ticks);
    }

    // ── Level end: fade to black, then the tally — after the loop.  The 8b
    // break skipped the loop's game-over check, so it is made here, in the
    // reference's order: a game over on the completing frame wins.
    if (g.state.level_complete && (g.state.game_over || abort_to_title)) {
        outcome = LevelOutcome::kGameOver;
    } else if (g.state.level_complete) {
        // Fade from the LAST PRESENTED gameplay frame (still in fb).  Do NOT
        // re-compose g.state: level_complete has already moved the player
        // (the EXE pseudo-exit), so a re-compose would show a frame the
        // player never saw.
        bool fade_ok = true;
        if (wsp.active()) {
            bool still_running = true;
            fade_wide_to_black(g, wsp, win, end_px, end_py, frame_ms,
                               still_running);
            fade_ok = still_running;
        } else {
            fade_ok = fade_to_black(fb, present, [](const FrameBuffer& f) {
                dump_level_fade(f.px, f.w, f.h);
            });
        }

        // Fade-dump mode stops the PROGRAM here: the gate has every frame it
        // measures, and playing the tally and the next level only made the
        // harness wait for its timeout (six minutes, fifty seconds of work).
        if (!fade_ok || std::getenv("OLDUVAI_DUMP_LEVEL_FADE") != nullptr) {
            outcome = LevelOutcome::kQuitProgram;
        } else {
            // false = the tally was quit (window close) — and the dump hook's
            // way of ending the run once it has its frames.
            play_tally_music(&audio, opts.game_dir);
            const bool tally_done = screen.text_screen(
                text_screen_deps, use_hd_text, "OLDUVAI_DUMP_TALLY", "tally",
                [&](const TextScreenHd& hd) {
                    return show_score_tally(
                        g.state.player.lives, g.state.score, display_level,
                        500, g.charset, g.render.palette, present, hd,
                        TallyAudio{&audio, opts.enhanced});
                });
            outcome = tally_done ? LevelOutcome::kComplete
                                 : LevelOutcome::kQuitProgram;
        }
    }

    if (draw_log != nullptr) std::fclose(draw_log);
    if (diag.vga.fill_ticks > 0 && std::getenv("OLDUVAI_PACE_TRACE"))
        std::fprintf(stderr,
                     "[PACE] vga-scan: %.2f presents/tick over %lu ticks\n",
                     static_cast<double>(diag.vga.fill_presents) /
                         static_cast<double>(diag.vga.fill_ticks),
                     diag.vga.fill_ticks);
    diag.stats.report(display_level);
    audio.stop_music();
    carry.lives = g.state.player.lives;
    carry.score = g.state.score;
    return outcome;
}

int run_game(const GameOptions& opts) {
    // Publish the smooth-present tuning before any frame loop reads it.
    smooth_present_config() = {opts.smooth_subframes, opts.smooth_vsync_off};
    // Runtime-mutable copy — Options edits (hd_profile, render_scale, audio
    // device, etc.) apply this session through rt; launch-fixed fields
    // (game_dir, frames, screenshot, replay, trace, level) stay on opts.
    GameOptions rt = opts;
    static const int kOrder[7] = {1, 2, 5, 4, 3, 6, 7};
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "game: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Gamepad: subsystem + hotplug watch + button mapping from play.json pad_*
    // keys (gamepad.hpp two-prong design).
    gamepad::init_from_options(opts);

    // Gameplay tables + AdLib SFX voice patches live in the user's own
    // executable — read and install them up front so the audio backend can
    // pre-render the OPL SFX (content policy: the engine ships no game
    // data).  Missing/corrupt files surface later via the level loader.
    try {
        install_exe_game_data(prepare::load_game_executable(opts.game_dir));
    } catch (const std::exception& e) {
        // Non-fatal BY DESIGN (the level loader reports missing/corrupt game
        // files with better context), but not silent: swallowing this without a
        // word meant a corrupt executable surfaced later as a confusing
        // loader error with no hint that table installation was the cause.
        std::fprintf(stderr, "game data: EXE tables not installed (%s)\n",
                     e.what());
    }
    std::optional<SdlAudio> audio_opt;
    audio_opt.emplace(rt.music_device, rt.rom_dir, rt.soundfont,
                      rt.sfx_backend, rt.audio_rate, rt.audio_buffer,
                      rt.midi_port, /*offline=*/false,
                      rt.mt32_model.empty() ? "auto" : rt.mt32_model);
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
            const prepare::GameArchives archives(opts.game_dir);
            load_sfx_bank(a, [&](const std::string& n)
                                 -> const std::vector<std::uint8_t>* {
                return archives.entry(n);
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
    bool hd = hd_active(rt.enhanced, rt.hd_profile);
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

        // IN FULLSCREEN, DO NOT REBUILD.  The window only exists at
        // 320*scale x 200*scale to size a WINDOWED canvas; in fullscreen it is
        // display-sized and that number means nothing.  What actually depends
        // on the scale is the renderer's logical size and the integer-scale
        // flag — both settable in place.
        //
        // Destroying and recreating instead cost the user seconds of black
        // screen on every enhanced <-> classic switch (hd_scale 2-or-4 <-> 1,
        // so it fires every time): on macOS, one Spaces animation out as the
        // old window dies, a window creation, then another animation back in
        // as the new one is promoted to fullscreen.  The reinit LOGIC was
        // never the cost — a headless Style Apply measures 0.25 s.
        //
        // Safe because the level owns its textures and has already destroyed
        // them: run_platform_level ends with SDL_DestroyTexture(tex) and
        // RETURNS, and run_game applies the reinit afterwards.  Nothing holds
        // a texture across this call, so keeping the renderer alive leaks
        // nothing — and it is strictly safer than destroying one that
        // something might still reference.
        if (fs && sw.win != nullptr && sw.ren != nullptr) {
            const LogicalDims ld = aspect_logical(new_scale, rt.aspect);
            SDL_RenderSetLogicalSize(sw.ren, ld.w, ld.h);

            // Unlike create_scaled_window, this renderer is REUSED: it may
            // still carry integer scaling from a previous classic pass, so the
            // flag has to be cleared explicitly, not just set.  ld.w == 0 is
            // "stretch" (logical size disabled), where it is meaningless.
            SDL_RenderSetIntegerScale(
                sw.ren, (new_scale == 1 && ld.w > 0) ? SDL_TRUE : SDL_FALSE);
            return true;
        }

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
                be.sound_avail = probe_sound_cards(rt.rom_dir, rt.soundfont);
                be.profile_family = rt.profile_family;
                be.persist = rt.persist;

                // --god on a boss fight.  god is seeded per LEVEL ENTRY inside
                // run_platform_level (energy 999 / lives 99 / full belly), but a
                // boss arena is its own loop that never runs that code — so
                // jumping straight to a boss (`--play --level 2 --god`) fought
                // it with the default 3 lives and looked like --god was
                // ignored.  Carry the lives boost in; damage and energy are
                // deliberately left alone, so the fight is still a real fight,
                // just endlessly retryable.  Suppressed under --replay for the
                // same reason the surface path suppresses it (a scripted replay
                // must not get cheat state).
                //
                // rt.god, NOT opts.god: `opts` is run_game's CONST parameter —
                // the frozen CLI snapshot — while `rt` is the runtime-mutable
                // session copy that run_platform_level actually receives, and
                // that the pause Cheats toggle writes through
                // (configure_pause_bind: bind.god_session = &opts->god, where
                // that opts IS rt).  Reading the const snapshot here meant god
                // enabled from the pause menu never reached a boss, and god
                // disabled there still seeded 99 lives.  Immutable CLI fields
                // (replay, game_dir, frames…) correctly stay on `opts`.
                if (rt.god && opts.replay.empty())
                    carry.lives = 99;   // EXE cap, same value as the surface seed
                const auto r = run_boss_level(opts.game_dir, internal,
                                              carry.lives, carry.score, sw,
                                              opts.frames, opts.screenshot,
                                              opts.screenshot_frame, &*audio_opt,
                                              be, opts.replay, opts.trace,
                                              opts.record_inputs);
                carry.lives = r.lives;
                carry.score = r.score;
                if (r.reinit) {
                    // After-fight apply: the boss staged reinit-class display/
                    // audio Options it could not honor mid-fight.  Apply them to
                    // rt + rebuild the pipeline now so the next level (or a
                    // Restart Fight) uses them — the surface pause's
                    // kReinitDisplay path, minus the state snapshot (the fight
                    // is already over, so there is nothing to restore).
                    const BossReinit& nr = *r.reinit;
                    rt.enhanced = nr.enhanced;
                    // smooth_motion is derived from `enhanced` (it stopped
                    // being a user toggle when the per-effect flags went), so
                    // it has to be re-derived wherever `enhanced` is adopted.
                    // Missing this left it stuck at its startup value: a
                    // session started --profile hd kept smooth motion after
                    // switching to Classic — and, because the present-path
                    // chain tests `smooth` BEFORE `vga_scan`, silently
                    // suppressed the classic VGA hold-frame scanout too.
                    rt.enhance.smooth_motion =
                        rt.enhanced && rt.transitions != "classic";
                    const int new_scale =
                        hd_scale_for(rt.enhanced, nr.hd_profile, nr.render_scale);
                    rt.render_scale = nr.render_scale;
                    rt.hd_profile   = nr.hd_profile;
                    const bool aspect_changed = nr.aspect != rt.aspect;
                    rt.aspect       = nr.aspect;
                    if (nr.music_device != rt.music_device ||
                        nr.sfx_backend != rt.sfx_backend) {
                        rt.music_device = nr.music_device;
                        rt.sfx_backend  = nr.sfx_backend;
                        audio_opt.reset();   // tear down device + synth handles
                        audio_opt.emplace(rt.music_device, rt.rom_dir,
                                          rt.soundfont, rt.sfx_backend,
                                          rt.audio_rate, rt.audio_buffer,
                                          rt.midi_port);
                        load_all_sfx(*audio_opt);
                    }

                    // Rebuild when the HD scale changes OR aspect changed
                    // (create_scaled_window re-derives the layout from
                    // rt.aspect).  Same primitive the surface reinit uses.
                    if (new_scale != hd_scale || aspect_changed) {
                        hd = hd_active(rt.enhanced, rt.hd_profile);
                        hd_scale = new_scale;
                        if (!rebuild_window(hd_scale)) {
                            std::fprintf(stderr, "settings: aborting after "
                                                 "failed window rebuild\n");
                            quit_requested = true;
                            break;
                        }
                    }
                }
                if (r.quit_program) {          // boss Pause → Exit Game
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
                    // Range-checked in deserialize: fits a 32-bit long.
                    carry.score = static_cast<long>(load_request->hdr.score);
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
                    // Derived — see the boss re-init above.
                    rt.enhance.smooth_motion =
                        rt.enhanced && rt.transitions != "classic";
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
                        hd = hd_active(rt.enhanced, rt.hd_profile);
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
                    quit_requested = true;   // Pause → Exit Game
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
    // Both end sequences below, and the attract loop after them, are 320-wide
    // NATIVE presenters — they upload a 320x200 frame and RenderCopy it to the
    // whole output.  run_platform_level leaves the renderer on the WIDE logical
    // canvas (320+2M), so without this restore that 320 texture is STRETCHED
    // across the wide canvas instead of pillarboxed inside it, and because
    // nothing else sets a logical size until the main menu does, the stretch
    // survives the ending AND the whole intro that follows it.
    //
    // This is the same restore run_boss_level already does before its own
    // 320-wide fade + tally (`boss_app.cpp`, "Restore the pillarbox logical
    // size before the fade + tally") — the boss driver grew it when the wide
    // work landed and the shell's two sequences never did.  aspect_logical()
    // maps "widescreen" to the keep/pillarbox fallback for exactly this use.
    //
    // Unconditional: it is a no-op when the level never went wide, and doing
    // it here rather than inside each sequence keeps the invariant with the
    // shell that owns the renderer.
    const LogicalDims _end_ld = aspect_logical(hd_scale, rt.aspect);
    SDL_RenderSetLogicalSize(sw.ren, _end_ld.w, _end_ld.h);

    if (!quit_requested && game_over && !single) {
        show_game_over_screen(opts.game_dir, *audio_opt, sw, hd_scale,
                              rt.hd_profile);
    }

    // ── Ending: the win picture + music after the last level. ──
    // Reached by finishing L7 (the level loop leaves display == 8) or
    // directly via --level 8 (sequence position 8 = FUN_2bd7_04be's
    // Game_WinSequence slot) — great for testing the ending.  Afterwards the
    // normal post-win flow applies: loop back to the attract at position 0.
    if (!quit_requested && display > 7 && rc == 0 && !single) {
        show_win_ending(opts.game_dir, *audio_opt, sw, hd_scale, rt.hd_profile,
                        rt.enhance.smooth_motion, quit_requested);
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
