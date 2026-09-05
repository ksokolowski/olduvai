// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss arena as a presentation surface: its widescreen geometry, and the
// presenter that turns one fight frame into pixels on the renderer.
//
// BACKLOG §3.18, the arena group.  run_boss_level's present family was six
// mutually-calling lambdas over eighteen shared locals, which is why §3.18
// concluded it needed an owner rather than extractions: any one of them lifted
// to a free function just relocates the tangle behind a parameter list.
//
// WHY NOT boss_widescreen.hpp, which is the name you would look under:
// that header is deliberately SDL-free and `test_boss_widescreen` links
// without SDL to keep it honest.  BossWidescreen holds an SDL_Texture, so it
// would drag SDL into the one place that is provably free of it.  This header
// is the SDL side of the same subject.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SDL.h>

#include "presentation/render/game_render.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::enhance { class HdAssetCache; }

namespace olduvai::presentation {

class BossHud;

// ── Boss-arena widescreen owner (§3.5a) ─────────────────────────────────────
// One type for what were eight locals used 178 times across run_boss_level —
// the fight loop, both victory sequences and the fade/tally all need every one
// of them.  L2/L4/L6 share them by construction, since run_boss_level is one
// function with three branches, and that ubiquity is exactly why §3.5 could
// not decompose: every extraction candidate carried 14-37 free names, of which
// about twelve were these.
//
// NOT WidescreenPresenter, and the difference is not stylistic.  That type's
// context is built entirely around the surface-level model —
// surface_screen_count, compose_surface_screen_static, collect_monsters,
// neighbour peeking.  The boss arena has no screens and no neighbours: its
// widescreen is a MIRRORED arena with an edge gradient.  The two are different
// presentations, so the duplication §3.5a removes is within boss_app, not
// between it and game_app.
//
// Owns wtex, which run_boss_level previously destroyed by hand on the way out.
struct BossWidescreen {
    BossWidescreen(SDL_Renderer* ren, bool enabled, int hd_scale,
                   LogicalDims fallback, LogicalSize* lsz);
    ~BossWidescreen();
    BossWidescreen(const BossWidescreen&) = delete;
    BossWidescreen& operator=(const BossWidescreen&) = delete;

    // Recompute when the renderer output size changes (Alt+Enter / resize).
    // Cheap no-op when unchanged.  MUST update the caller's lsz.w()/h too:
    // the text-overlay flush restores SDL's logical size from those, so
    // updating only SDL's gets clobbered back to the stale dims on the next
    // HUD draw — squashing the wide buffer into the old canvas (the fullscreen
    // bug).  OLDUVAI_WS_FORCE_MARGIN pins the margin so this stays a no-op
    // under the test harness.
    void rebuild_if_resized();

    int M = 0;                     // margin, native px each side
    bool active = false;           // widescreen on AND margin > 0
    int w = 320;                   // 320 + 2*M
    SDL_Texture* wtex = nullptr;   // wide stream texture (null when inactive)
    // Last native frame sent through the wide present — reused as the source
    // for the post-victory wide fade (the HD `fb` holds the stale fight frame).
    FrameBuffer last_native;

private:
    SDL_Renderer* ren_;
    bool enabled_;                 // hd && aspect == "widescreen"
    int hd_scale_;
    LogicalDims fallback_;
    LogicalSize* lsz_;
    int ow0_ = 0, oh0_ = 0;        // last seen renderer output size
};

// Wrap a native 320x200 arena frame in the darkened mirror margins every wide
// boss compose uses: pure reflection of the edge strips (so the black arena
// walls stay black rather than being void-scanned into a smear) with the 0.10
// edge-darkening gradient.  Three sites spelled this same six-argument
// compose_widescreen call out by hand.
void compose_arena_wide(std::vector<std::uint8_t>& out, int M,
                        const FrameBuffer& src);

// ── The purely-visual compose target (§3.4) ─────────────────────────────────
// Every arena compose that is NOT the live fight frame goes through here,
// because they all must do the one thing that is easy to forget:
// `advance_state = false`.  A compose that advances sprite animation state is
// how a boss "pause defect" is born — the paused frame quietly steps the
// animation the fight is holding, and nothing fails until someone notices the
// wings moved while the game was stopped.  §3.4 names that invariant for
// exactly this reason, and six sites used to restate it by hand.
//
// origin_x is always the widescreen margin: sprites are drawn over the wide
// buffer at x+M so edge-crossers overflow into the margins instead of clipping.
RenderTarget boss_visual_target(std::uint8_t* px, int w, int h, int scale,
                                enhance::HdAssetCache* cache,
                                const std::string* profile, int origin_x);

// The smooth-motion triple that the live fight feeds a wide compose.  Split
// out so that a compose which does NOT want it is visibly declining it rather
// than silently omitting three lines — see the asymmetry noted at the call
// sites.  Inert when use_float is false (classic, or a landed hold).
void boss_smooth_pos(RenderTarget& rt, bool use_float, float pfx, float pfy);

// The per-frame present pipeline for a boss fight.
//
// WHAT IT DOES NOT HOLD.  §3.18's rule, established when the HUD got its owner:
// a presenter may hold a live POINTER to what it displays, but not the state
// that contains it.  So there is no `assets`, no `player`, no `l2`/`l4`/`l6`
// and no `internal_level` here.  The arena background arrives as a buffer
// pointer, and everything that needs to know WHICH boss this is arrives as a
// callback the driver binds once — the three sprite renderers differ only in
// which function is called, which makes them a seam rather than a branch.
class BossArenaPresenter {
public:
    BossArenaPresenter(LevelSurface& surface, BossWidescreen& ws, BossHud& hud,
                       FrameBuffer& fb, enhance::HdAssetCache& cache);

    // ── what to draw (bound once by the driver) ──────────────────────────────
    // The HUD-clean arena background, RGBA 320x200.  Static for the whole
    // fight; the wide compose caches its upscaled form off it.
    const std::vector<std::uint8_t>* arena_bg = nullptr;
    // Draw this frame's fight sprites into the target (render_l2/l4/l6_sprites).
    std::function<void(RenderTarget&)> draw_fight_sprites;
    // True while the L4 ride-off victory should take the wide sprite-overflow
    // path instead of the mirrored-native one.  See present_any.
    std::function<bool()> wide_victory;
    // The ride-off, drawn natively (fade source) and as overflowing sprites.
    std::function<void(RenderTarget&)> draw_victory_native;
    std::function<void(RenderTarget&)> draw_victory_sprites;

    // ── the HUD's SDL side ───────────────────────────────────────────────────
    // Paint the vector HUD into the renderer's output-resolution overlay and
    // flush it over the scene.  §3.18 said not to lift these three as free
    // functions — 22 branchless lines that move the metric by ~0.  As methods
    // they cost nothing: this owner already holds every name they need.
    void hud_overlay(bool draw_lives);        // centre 320 domain
    void hud_overlay_wide(bool draw_lives);   // wide domain (origin M, width w)

    // ── the present family ───────────────────────────────────────────────────
    // The unchanged 320-wide present: upload `fb`, pillarbox it if a wide
    // canvas is active, lay the HUD over it.
    void present_frame(bool draw_lives = true, bool do_present = true);
    // Build one wide upscaled fight frame: cached static wide background, then
    // this frame's sprites at origin_x = M so edge-crossers overflow into the
    // margins.  Shared by present_wide and the wide screenshot branch, which
    // is why it returns the buffer instead of presenting it.
    std::vector<std::uint8_t> build_wide_up();
    // Show an already-upscaled wide buffer.  present_wide and the L4-victory
    // branch differ only in how `up` is built; this is the shared frame around
    // them, a duplicate shape the clone detector cannot see.
    void show_wide_up(const std::vector<std::uint8_t>& up, bool draw_lives,
                      bool do_present);
    void present_wide(bool draw_lives = true, bool do_present = true);
    // Present a NATIVE 320x200 frame (a victory-sequence frame) at the wide
    // width, mirrored like the fight.  Falls back to a plain 320 upscale if a
    // resize dropped widescreen mid-sequence.
    void present_wide_native(const FrameBuffer& nat, bool draw_lives = true,
                             bool do_present = true, bool draw_hud = true);
    // Route a FIGHT present: wide when active, the L4 ride-off through the
    // overflow compose, else the 320 path.
    void present_any(bool draw_lives = true, bool do_present = true);

    // The smooth-motion triple the live fight feeds the wide compose, bound as
    // live cells rather than pushed in by a setter — same rule as the HUD's
    // lives/health (§3.18): the driver owns these and rewrites them from five
    // places in the frame loop, so a snapshot here would be a staleness bug
    // waiting for whichever path forgot to re-push.  Inert while *use_float is
    // false (classic, or a landed hold).
    const bool* smooth_use_float = nullptr;
    const float* smooth_fx = nullptr;
    const float* smooth_fy = nullptr;

private:
    LevelSurface& surface_;
    BossWidescreen& ws_;
    BossHud& hud_;
    FrameBuffer& fb_;
    enhance::HdAssetCache& cache_;

    // Phase-2 perf (task #61): the arena background (RING.PC1, a single STATIC
    // screen) never changes during the fight, so its composed+upscaled wide
    // form is built once and copied per frame — no per-frame whole-frame
    // upscale (omniscale was the budget-buster).  Rebuilt only when the margin
    // (Alt+Enter) or the HD profile changes.
    std::vector<std::uint8_t> bg_hd_;
    int bg_hd_M_ = -1;
    std::string bg_hd_profile_;
};

}  // namespace olduvai::presentation
