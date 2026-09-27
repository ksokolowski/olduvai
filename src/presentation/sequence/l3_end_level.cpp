// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L3 trunk-descent end-level sequence: FUN_2276_0282 (Phase 1) and
// FUN_2276_03d9 (Phase 2).

#include "presentation/sequence/l3_end_level.hpp"

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "enhance/enhanced_hud.hpp"           // compute/draw_enhanced_hud_*
#include "enhance/hd_text.hpp"                // enhance::HdText
#include "enhance/upscale.hpp"                // upscale_rgba
#include "presentation/diag/bug_capture.hpp"       // bug_report_root
#include "presentation/game_app.hpp"          // GameOptions
#include "presentation/render/game_render.hpp"
#include "presentation/image_out.hpp"   // capture_renderer_output/save_rgba_image
#include "presentation/level/level_setup.hpp"       // Loaded, bind_screen
#include "presentation/render/shift_blit.hpp"
#include "presentation/sequence/screens.hpp"   // PresentFn
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/text_overlay.hpp"      // TextOverlay
#include "presentation/render/widescreen_presenter.hpp"  // WidescreenPresenter
#include "presentation/window_util.hpp"       // poll_screen_events
#include "systems/transitions.hpp"            // roll_l3_descent_smoke_jitter, clear_per_screen_state

namespace olduvai::presentation {

// ── EXE-verified constants (Phase 1 — FUN_2276_0282) ─────────────────────────

// FUN_2276_0282:0x03ca — iteration bound 0x2c → 44 iters (0..43).
constexpr int kS17Iters = 44;
// y_offset starts at 0 (0x028a), increments by 4 each iter (0x03c3).
constexpr int kS17YStep = 4;
// 16 tile records per iter (same as Phase 2).
constexpr int kDescendTiles = 16;
// 4 BIOS ticks per iter (same call pattern as Phase 2).
constexpr int kTicksPerIter = 4;

// ── EXE-verified constants (Phase 2 — FUN_2276_03d9) ─────────────────────────

// FUN_2276_03d9:0x0668 — iteration bound 0x15 → 21 iters (0..20).
constexpr int kS18Iters = 21;
// y_offset starts at -80 (0xffb0 at 0x03e1).  The EXE steps 4 per outer iter
// (0x0661); the port steps 1 px per logical frame, like Phase 1 and the
// reference.
constexpr int kS18YOffsetInit = -80;
// constexpr int kS18YStep = 4;  // EXE value; port uses per-frame +1 instead

// Descent tile remap, FUN_2276_03d9:0x0496-0x04b5 +
// FUN_2276_0282:0x032a-0x0347. 28 -> 29 unconditionally, unlike
// resolve_sprite_idx (28 -> -1).
int descent_resolve_sprite_idx(int idx) {
    switch (idx) {
        case 29: return 30;
        case 28: return 29;   // descent-path only: no skip, goes to 29
        case 19: return 31;
        case  4: return 28;
        default: return idx;
    }
}

// FUN_2276_03d9:0x0415 forces the screen arg to 17: both phases draw screen
// 17's first 16 records.
constexpr int kScreen17 = 17;
constexpr int kScreen18 = 18;

// The GROT3 trunk (x=98; body y 167/128/89/50, cap y=23) is net-hidden on
// screens 17/18 by the EXE's draw -> black-fill -> pine order, so it is not
// drawn.

// ── Smoke constants (Phase 2, FUN_2276_03d9:0x053e-0x05c5) ───────────────────
// Smoke A: L3SPR[85+(iter&1)], x=49, y=175+jitter_a
constexpr int kSmokeASprBase = 85;
constexpr int kSmokeAX = 49;
constexpr int kSmokeAYBase = 175;
// Smoke B: L3SPR[86+(iter&1)], x=65, y=170+jitter_b
constexpr int kSmokeBSprBase = 86;
constexpr int kSmokeBX = 65;
constexpr int kSmokeBYBase = 170;
// Smoke C: L3SPR[85+(iter&1)], x=79, y=178 (fixed)
constexpr int kSmokeCSprBase = 85;
constexpr int kSmokeCX = 79;
constexpr int kSmokeCY = 178;

// ── Decoration tiles (Phase 2, FUN_2276_03d9:0x05c8-0x0638) ──────────────────
// 4 ELEML3 tiles at y=185 every iter except last.
constexpr int kDecY = 185;
struct DecTile { int x; int sprite_idx; };   // sprite_idx in eleml3
constexpr DecTile kDecorations[4] = {{16,0},{32,1},{80,1},{128,1}};

// The enhanced camera pan's sub-frames per 18 Hz step (enhanced only).
constexpr int kDescentPanSubsteps = 3;

// Pine silhouette: ELEML3B[3] = index 28 + 3 in the combined surface_tiles.
constexpr int kPineSprIdx = 31;

// Static background: black fill + pine silhouettes (the trunk is net-hidden on
// 17/18, see above).  tile_sprites must already have GROT3 appended.
static void build_l3_bg_base(FrameBuffer& bg,
                              const std::vector<formats::Sprite>& tile_sprites,
                              const std::vector<formats::Rgb>& palette,
                              const std::vector<formats::Sprite>& /*grot3*/) {
    clear_opaque(bg.px);

    // Pine silhouette at (0,9) and (160,9) — ELEML3B[3] = index 31.
    if (kPineSprIdx < static_cast<int>(tile_sprites.size())) {
        const auto& pine = tile_sprites[static_cast<std::size_t>(kPineSprIdx)];
        blit_sprite(bg, pine, palette, 0, 9);
        blit_sprite(bg, pine, palette, 160, 9);
    }
}

// Blit a screen's tile records [begin_idx..).  `extend_band` first continues
// the trunk columns up through the HUD strip (as the steady widescreen view
// does); classic keeps the strip black.
static void blit_screen_tiles(FrameBuffer& fb,
                              const std::vector<prepare::TilePlacement>& src,
                              int begin_idx,
                              const std::vector<formats::Sprite>& tile_sprites,
                              const std::vector<formats::Rgb>& palette,
                              bool extend_band) {
    std::vector<LevelRenderAssets::TileDraw> list;
    list.reserve(src.size());
    for (int i = begin_idx; i < static_cast<int>(src.size()); ++i) {
        const auto& tp = src[static_cast<std::size_t>(i)];
        const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
        if (idx >= 0 && idx < static_cast<int>(tile_sprites.size()))
            list.push_back({idx, tp.x, tp.y});
    }
    if (extend_band)
        tile_patterns::extend_columns_to_top(list, tile_sprites);
    for (const auto& t : list)
        blit_sprite(fb, tile_sprites[static_cast<std::size_t>(t.sprite_idx)],
                    palette, t.x, t.y);
}

// Enhanced dead-tail trim: the smallest Phase-1 y_offset at which the player
// and every descending record are below the screen.  The EXE's fixed 44-iter
// slide shows ~2.5 s of empty screen; enhanced ends Phase 1 here so the pan
// follows at once.
static constexpr int kGameH = 200;   // native game-surface height (GAME_H)

static int phase1_offscreen_offset(int locked_y,
                                   const std::vector<prepare::TilePlacement>& descent,
                                   int descent_count) {
    int min_y = locked_y;
    for (int i = 0; i < descent_count &&
                    i < static_cast<int>(descent.size()); ++i) {
        if (descent[static_cast<std::size_t>(i)].y < min_y)
            min_y = descent[static_cast<std::size_t>(i)].y;
    }
    return kGameH - min_y;
}

// Render a tile placement from the descent record set with the remap chain.
static void blit_descent_tile(FrameBuffer& fb,
                               const prepare::TilePlacement& tp,
                               const std::vector<formats::Sprite>& tile_sprites,
                               const std::vector<formats::Rgb>& palette,
                               int y_offset) {
    const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
    if (idx < 0 || idx >= static_cast<int>(tile_sprites.size())) return;
    blit_sprite(fb, tile_sprites[static_cast<std::size_t>(idx)], palette,
                tp.x, tp.y + y_offset);
}

// ── Phase 1 ───────────────────────────────────────────────────────────────────

bool run_l3_screen17_descent(const L3DescentPhase& p)
{
    // Guard: need screen 17's tile records.
    if (static_cast<int>(p.tile_data.screens.size()) <= kScreen17) return true;
    const auto& s17tiles = p.tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Descent records: first 16 of screen 17.
    const int descent_count = kDescendTiles;

    // Lock player position at trigger time (player_y == 0x44 at trigger).
    const int locked_x = p.state.player.x;
    const int locked_y = p.state.player.y;
    // EXE FUN_2276_0282:0x0390 uses [0x988a] (L3SPR base) → sprite 0.
    constexpr int kLockedSprite = 0;

    // Static background: screen 17 without records 0-15 (the platform, drawn
    // per frame with the offset).
    FrameBuffer bg_static;
    build_l3_bg_base(bg_static, p.tile_sprites, p.palette, p.grot3);
    // Draw screen-17 records AFTER index 15 at their normal positions.
    blit_screen_tiles(bg_static, s17tiles, kDescendTiles, p.tile_sprites,
                      p.palette, p.extend_band);

    // 44 iters x 4 ticks = 176 frames at 18 Hz (~9.7 s).  One frame per logical
    // frame: Phase 1 does not interpolate, and extra sub-frames would repeat
    // the same image at the cost of a full wide omniscale upscale each (25 ms,
    // more than an 18 ms sub-frame slice).  Only the camera pan keeps
    // sub-frames.
    const int substeps = 1;
    const int total_frames = kS17Iters * kTicksPerIter;   // 176 logical frames
    const int final_offset = (kS17Iters - 1) * kS17YStep;  // 172

    // Enhanced: end Phase 1 once the last element leaves the bottom.  Classic
    // keeps the full slide (trim_offset = final_offset).
    const int trim_offset =
        p.enhanced
            ? std::min(final_offset,
                       phase1_offscreen_offset(locked_y, s17tiles, descent_count))
            : final_offset;

    for (int frame = 0; frame < total_frames * substeps; ++frame) {
        // Everything has left the screen: stop (only when the trim is active).
        if (trim_offset < final_offset &&
            (frame / substeps) >= trim_offset) {
            break;
        }
        // 1 px per logical frame (4 per iter); final value 172.
        const int y_offset_raw = frame / substeps;   // logical-frame index
        const int y_offset = std::min(y_offset_raw, final_offset);

        // Drain SDL events (ESC is inert during the descent; window-close
        // aborts).
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { p.state.game_over = true; return false; }
            // ESC is inert (no menu here); only SDL_QUIT above stops the
            // descent.
            (void)0;
        }

        // Compose frame into p.fb.
        p.fb = bg_static;

        // 16 descent records with y_offset.
        for (int i = 0; i < descent_count; ++i) {
            blit_descent_tile(p.fb, s17tiles[static_cast<std::size_t>(i)],
                              p.tile_sprites, p.palette, y_offset);
        }

        // Player at (locked_x, locked_y + y_offset), sprite 0.  Drawn on every
        // iter, the last included.
        if (kLockedSprite < static_cast<int>(p.entity_sprites.size())) {
            blit_sprite(p.fb, p.entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                        p.palette, locked_x, locked_y + y_offset);
        }

        if (!p.present(p.fb)) { p.state.game_over = true; return false; }
    }
    return true;
}

// ── Phase 2 ───────────────────────────────────────────────────────────────────

// The EXE's three smoke puffs, iters 0..19 only (0x0516-0x051c skips iter
// 20).  A and B take the pre-rolled jitter (roll_l3_descent_smoke_jitter).
static void draw_descent_smoke(const L3DescentPhase& p, int outer_iter) {
    if (outer_iter >= kS18Iters - 1) return;
    const auto& jitter = p.state.l3_descent_smoke_jitter;
    const int ji = std::min(outer_iter, static_cast<int>(jitter.size()) - 1);
    const int jit_a = (ji >= 0) ? jitter[static_cast<std::size_t>(ji)].first : 0;
    const int jit_b = (ji >= 0) ? jitter[static_cast<std::size_t>(ji)].second : 0;
    const auto puff = [&](int base, int x, int y) {
        const int idx = base + (outer_iter & 1);
        if (idx < static_cast<int>(p.entity_sprites.size()))
            blit_sprite(p.fb, p.entity_sprites[static_cast<std::size_t>(idx)],
                        p.palette, x, y);
    };
    puff(kSmokeASprBase, kSmokeAX, kSmokeAYBase + jit_a);   // L3SPR[85|86]
    puff(kSmokeBSprBase, kSmokeBX, kSmokeBYBase + jit_b);   // L3SPR[86|87]
    puff(kSmokeCSprBase, kSmokeCX, kSmokeCY);               // L3SPR[85|86]
}

// Enhanced dust: 3 extra puffs across the trunk base while it grinds down,
// then a 6-puff burst on the landing iter (the EXE's 3 stop there).  Jitter
// is an integer hash of iter and slot, never the game LCG.
static void draw_descent_dust(const L3DescentPhase& p, int outer_iter) {
    const auto hash_jit = [](int i, int k) -> int {
        std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u ^
                          (static_cast<std::uint32_t>(k) * 0x9E3779B9u);
        return static_cast<int>((h >> 16) & 7u);   // 0..7, EXE-like range
    };
    const auto puff = [&](int slot, int x, int y_base) {
        const int idx = kSmokeASprBase + ((outer_iter + slot) & 1);   // 85/86
        if (idx < static_cast<int>(p.entity_sprites.size()))
            blit_sprite(p.fb, p.entity_sprites[static_cast<std::size_t>(idx)],
                        p.palette, x, y_base + hash_jit(outer_iter, slot));
    };
    if (outer_iter < kS18Iters - 1) {   // the grind: widen the EXE's 49..79
        puff(3, 33, 176);
        puff(4, 95, 173);
        puff(5, 111, 177);
        return;
    }
    puff(0, 33, 175);   // the slam: across the whole base
    puff(1, 49, 172);
    puff(2, 65, 176);
    puff(3, 79, 173);
    puff(4, 95, 175);
    puff(5, 111, 177);
}

bool run_l3_trunk_descent(const L3DescentPhase& p)
{
    // Guard: need both screen 17 and screen 18 tile data.
    if (static_cast<int>(p.tile_data.screens.size()) <= kScreen18) return true;
    const auto& s17tiles = p.tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const auto& s18tiles = p.tile_data.screens[static_cast<std::size_t>(kScreen18)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Lock player position (same as Phase 1 — player_y == 0x44 at trigger).
    const int locked_x = p.state.player.x;
    const int locked_y = p.state.player.y;
    constexpr int kLockedSprite = 0;   // EXE FUN_2276_03d9:0x0527 uses L3SPR base

    // Screen-18 background, painted once before the loop (FUN_2276_000d,
    // 0x03e6).
    FrameBuffer bg_static;
    build_l3_bg_base(bg_static, p.tile_sprites, p.palette, p.grot3);
    // Screen-18 tile placements at normal positions.
    blit_screen_tiles(bg_static, s18tiles, 0, p.tile_sprites, p.palette,
                      p.extend_band);

    // 21 iters x 4 ticks = 84 frames at 18 Hz (~4.7 s).  One frame per logical
    // frame, as in Phase 1: the dust jitter is keyed on the iter, so nothing
    // changes between sub-frames either.
    const int substeps = 1;
    const int total_frames = kS18Iters * kTicksPerIter;   // 84

    for (int frame = 0; frame < total_frames * substeps; ++frame) {
        const int outer_iter = (frame / substeps) / kTicksPerIter;

        // y_offset: -80 + logical frame, clamped to 0 (1 px per frame, as Phase
        // 1).
        const int logical_frame = frame / substeps;
        const int y_offset = std::min(kS18YOffsetInit + logical_frame, 0);

        // Drain SDL events.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { p.state.game_over = true; return false; }
            // ESC is inert (no menu here); only SDL_QUIT above stops the
            // descent.
            (void)0;
        }

        p.fb = bg_static;

        // The 16 descent records from screen 17 (FUN_2276_03d9:0x0415).
        for (int i = 0; i < kDescendTiles; ++i) {
            blit_descent_tile(p.fb, s17tiles[static_cast<std::size_t>(i)],
                              p.tile_sprites, p.palette, y_offset);
        }

        // The EXE skips the player on the last iter (outer_iter 20); like the
        // reference, always draw it (avoids a ~220 ms blink).
        if (kLockedSprite < static_cast<int>(p.entity_sprites.size())) {
            blit_sprite(p.fb,
                        p.entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                        p.palette, locked_x, locked_y + y_offset);
        }

        draw_descent_smoke(p, outer_iter);
        if (p.enhanced) draw_descent_dust(p, outer_iter);

        // 4 decoration tiles at y=185 on every iter, the last included (the
        // 0x0516 last-iter jump skips only player + smoke).  ELEML3[0..1] =
        // p.tile_sprites[0..1].
        if (outer_iter < kS18Iters) {
            for (const auto& d : kDecorations) {
                if (d.sprite_idx < static_cast<int>(p.tile_sprites.size()))
                    blit_sprite(p.fb,
                                p.tile_sprites[static_cast<std::size_t>(d.sprite_idx)],
                                p.palette, d.x, kDecY);
            }
        }

        if (!p.present(p.fb)) { p.state.game_over = true; return false; }
    }
    return true;
}

// Enhanced descent camera pan (not in the EXE, which hard-swaps two identical
// backdrops): screen 17 scrolls up while 18 enters from below.  The platform
// and player are glued onto screen 18 at Phase 2's start offset, so the pan's
// last frame equals Phase 2's first.  Same geometry as the kind-3 'D' slide;
// `present` draws the HUD on each frame.
bool run_l3_descent_pan(const L3DescentPhase& p)
{
    if (!p.enhanced) return true;   // pan is descent-pan-only; hard swap stands
    // Guard: need both screen 17 and screen 18 tile data.
    if (static_cast<int>(p.tile_data.screens.size()) <= kScreen18) return true;
    const auto& s17tiles = p.tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const auto& s18tiles = p.tile_data.screens[static_cast<std::size_t>(kScreen18)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Screen 17 without records 0-15: Phase 1 already slid the platform off,
    // and redrawing it would double-image the descending one.
    FrameBuffer s17{};   // native 320x200
    build_l3_bg_base(s17, p.tile_sprites, p.palette, p.grot3);
    blit_screen_tiles(s17, s17tiles, kDescendTiles, p.tile_sprites, p.palette,
                      p.extend_band);
    FrameBuffer s18{};   // native 320x200
    build_l3_bg_base(s18, p.tile_sprites, p.palette, p.grot3);
    blit_screen_tiles(s18, s18tiles, 0, p.tile_sprites, p.palette, p.extend_band);

    // Glue the platform + player onto screen 18 at y_offset -80, where Phase
    // 2's first frame draws them.
    for (int i = 0; i < kDescendTiles; ++i) {
        blit_descent_tile(s18, s17tiles[static_cast<std::size_t>(i)],
                          p.tile_sprites, p.palette, kS18YOffsetInit);
    }
    constexpr int kLockedSprite = 0;   // L3SPR[0] standing (matches Phase 2)
    if (kLockedSprite < static_cast<int>(p.entity_sprites.size()))
        blit_sprite(s18, p.entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                    p.palette, p.state.player.x,
                    p.state.player.y + kS18YOffsetInit);

    // 'down' pan: old (17) ody = -t*H, new (18) ndy = H + ody.  12 frames x
    // kDescentPanSubsteps (the caller paces the sub-frames).
    const int n_frames = 12 * kDescentPanSubsteps;
    for (int f = 1; f <= n_frames; ++f) {
        const double t = static_cast<double>(f) / static_cast<double>(n_frames);
        const int ody = -static_cast<int>(t * kGameH);
        p.fb = FrameBuffer{};   // native 320x200
        clear_opaque(p.fb.px);
        blit_shifted(p.fb.px, s17.px, 320, kGameH, 0, ody);
        blit_shifted(p.fb.px, s18.px, 320, kGameH, 0, kGameH + ody);
        if (!p.present(p.fb)) { p.state.game_over = true; return false; }
    }
    return true;
}

std::vector<prepare::TilePlacement> l3_descent_overlay_tiles(
    const prepare::LevelTiles& tile_data)
{
    if (static_cast<int>(tile_data.screens.size()) <= kScreen17) return {};
    const auto& tiles = tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const int n = std::min(kDescendTiles, static_cast<int>(tiles.size()));
    return std::vector<prepare::TilePlacement>(tiles.begin(),
                                               tiles.begin() + n);
}


namespace {

// The descent's present.  Widescreen: the native frame composited into the
// bound screen's static wide margins (the steady view's no-neighbour fill,
// same peeks, so nothing pops at either end), HUD bars, upscale, vector HUD
// text.  Otherwise pillarboxed through the level's present.  Paced to
// step_ms.  ESC is inert; a window close stops the descent.
class DescentPresenter {
public:
    explicit DescentPresenter(const DescentCtx& c)
        : step_ms(c.frame_ms), c_(c),
          wide_(c.wsp->active() && c.surface->hd() &&
                c.surface->hd_scale() > 1 && c.wsp->wide_tex() != nullptr) {}

    Uint32 step_ms;
    std::vector<std::uint8_t> margins;   // native wsp->native_w() x 200

    // The bound screen's static wide margins.
    void build_margins() {
        if (wide_) c_.wsp->compose_static_wide_bg(margins);
    }

    // The camera pan scrolls the margins with the centre: `from` recedes
    // up, `to` enters below.
    void scroll_margins(const std::vector<std::uint8_t>& from,
                        const std::vector<std::uint8_t>& to, double t) {
        if (!wide_ || from.size() != margins.size() ||
            to.size() != margins.size())
            return;
        const int w = c_.wsp->native_w();
        const int ody = -static_cast<int>(t * 200);
        std::fill(margins.begin(), margins.end(), 0);
        blit_shifted(margins, from, w, 200, 0, ody);
        blit_shifted(margins, to, w, 200, 0, 200 + ody);
    }

    bool present(const FrameBuffer& f, bool with_hud) {
        const Uint32 t0 = SDL_GetTicks();
        if (!poll_screen_events(c_.surface->win())) return false;
        latch_f5();
        const std::size_t need =
            static_cast<std::size_t>(c_.wsp->native_w()) * 200 * 4;
        if (!wide_ || margins.size() != need) {
            dump_frame(f.px.data(), 320);
            FrameBuffer copy = f;
            c_.upload_and_show(copy, with_hud, /*do_present=*/true);
        } else {
            present_wide(f, with_hud);
        }
        const Uint32 spent = SDL_GetTicks() - t0;
        if (spent < step_ms) SDL_Delay(step_ms - spent);
        return true;
    }

private:
    // OLDUVAI_DUMP_DESCENT=<dir>: each presented frame, w x 200, pre-upscale
    // and before the HUD (the wide composite, or the 320 frame), as PNG.
    static void dump_frame(const std::uint8_t* px, int w) {
        const char* dir = std::getenv("OLDUVAI_DUMP_DESCENT");
        if (dir == nullptr) return;
        static int seq = 0;
        char path[512];
        std::snprintf(path, sizeof path, "%s/descent_%04d.png", dir, seq++);
        save_rgba_image(px, w, 200, path);
    }

    // The descent's own poll consumes F5, so read the key state, rising
    // edge only (one press, one capture).
    void latch_f5() {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        const bool f5 = ks != nullptr && ks[SDL_SCANCODE_F5] != 0;
        if (f5 && !f5_prev_) shot_ = true;
        f5_prev_ = f5;
    }

    // The wide composite (the margins, scrolled with the camera pan, and the
    // centre) through the widescreen presenter's transition present: the
    // steady frame's HD HUD and output path.
    void present_wide(const FrameBuffer& f, bool with_hud) {
        WidescreenPresenter& wsp = *c_.wsp;
        const int w = wsp.native_w();
        std::vector<std::uint8_t> wide = margins;
        for (int y = 0; y < 200; ++y)
            std::memcpy(&wide[(static_cast<std::size_t>(y) * w + wsp.margin()) * 4],
                        &f.px[static_cast<std::size_t>(y) * 320 * 4], 320 * 4);
        // Seam overhangs the 320 copy covered (none for Phase 2 or the pan).
        wsp.reapply_seam_bands(wide);
        dump_frame(wide.data(), w);
        wsp.present_transition(wide, with_hud, /*pre_upscaled=*/false,
                               /*do_present=*/!shot_);
        if (shot_) {
            save_shot();
            wsp.flip();
        }
    }

    // F5 mid-descent: the blocking loop never reaches the frame-service
    // capture, so save the wide composite into the report root.
    void save_shot() {
        shot_ = false;
        const std::filesystem::path root = bug_report_root();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        char name[64];
        std::snprintf(name, sizeof name, "descent_ws_%03d.png", shot_seq_++);
        const std::string path = (root / name).string();
        if (capture_renderer_output(c_.surface->ren(), path))
            std::fprintf(stderr, "[WS-SHOT] saved %s\n", path.c_str());
    }

    const DescentCtx& c_;
    const bool wide_;
    bool f5_prev_ = false;
    bool shot_ = false;
    int shot_seq_ = 0;
};

// After Phase 2: stamp the descent overlay onto screen 18's collision and
// render list (FUN_2276_03d9 Collision_StampDur on iter 20), before screen
// 18's own tiles (after the backdrop), so the ground row covers the trunk
// bottom as during the descent.
void stamp_descent_overlay(Loaded& g) {
    std::vector<LevelRenderAssets::TileDraw> overlay;
    for (const auto& tp : l3_descent_overlay_tiles(g.tiles)) {
        const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
        if (idx >= 0 && idx < static_cast<int>(g.dur.tiles.size()))
            g.state.collision.stamp_tile(
                g.dur.tiles[static_cast<std::size_t>(idx)].segments, tp.x,
                tp.y);
        overlay.push_back({idx, tp.x, tp.y});
    }
    const auto at = g.render.tiles.begin() +
                    std::min<std::ptrdiff_t>(
                        g.render.backdrop_tile_count,
                        static_cast<std::ptrdiff_t>(g.render.tiles.size()));
    g.render.tiles.insert(at, overlay.begin(), overlay.end());
}

}  // namespace

// Phase 1 on screen 17, the smoke jitter roll, bind screen 18, the enhanced
// camera pan, Phase 2.  The descent composes native 320x200 and the present
// upscales.  No LCG draws besides the jitter roll (which runs in every mode),
// so traces match the classic hard swap.
void run_l3_trunk_descent_sequence(const DescentCtx& c) {
    Loaded& g = *c.g;
    const GameOptions& opts = *c.opts;
    bool& running = *c.running;
    DescentPresenter dp(c);
    const PresentFn present = [&dp](const FrameBuffer& f) {
        return dp.present(f, /*with_hud=*/false);
    };

    // Phase 1 (FUN_2276_0282) runs before bind_screen: screen 17's platform
    // slides against screen 17's backdrop.  current_screen is already 18: set
    // it back for the margins so the per-screen dead-end rules fire.
    const int bound = g.state.current_screen;
    g.state.current_screen = c.prev_screen;
    dp.build_margins();
    const std::vector<std::uint8_t> m17 = dp.margins;
    g.state.current_screen = bound;
    {
        FrameBuffer fb{};
        if (!run_l3_screen17_descent(
                L3DescentPhase{g.state, g.render.tile_sprites,
                               g.render.entity_sprites, g.render.palette,
                               g.tiles, g.grot3, fb, opts.enhanced,
                               g.render.extend_top_backdrop, present}))
            running = false;
    }
    // 40 LCG draws in EXE order, in every mode: FUN_2276_03d9:0x0554 (smoke
    // A) + 0x0586 (smoke B), iters 0..19.
    systems::roll_l3_descent_smoke_jitter(g.state);

    // Bind screen 18: Phase 2's records descend onto its backdrop.
    systems::clear_per_screen_state(g.state);
    bind_screen(g, g.state.current_screen);
    c.wsp->update_cache();
    dp.build_margins();
    const std::vector<std::uint8_t> m18 = dp.margins;
    g.state.screen_change = false;
    g.state.transition_skip = true;

    // Enhanced camera pan (the EXE hard-swaps the backdrop), HUD on; its
    // sub-frames run at the sub-frame budget.
    if (running && opts.enhanced) {
        const int pan_n = 12 * kDescentPanSubsteps;
        int pan_i = 0;
        const PresentFn present_pan = [&](const FrameBuffer& f) {
            dp.scroll_margins(m17, m18, static_cast<double>(++pan_i) / pan_n);
            return dp.present(f, /*with_hud=*/true);
        };
        dp.step_ms = c.frame_ms / kDescentPanSubsteps;
        FrameBuffer fb{};
        if (!run_l3_descent_pan(
                L3DescentPhase{g.state, g.render.tile_sprites,
                               g.render.entity_sprites, g.render.palette,
                               g.tiles, g.grot3, fb, opts.enhanced,
                               g.render.extend_top_backdrop, present_pan}))
            running = false;
        dp.step_ms = c.frame_ms;
        dp.margins = m18;
    }

    // Phase 2 (FUN_2276_03d9): 21 iters, y_offset -80 -> 0.
    if (!running) return;
    FrameBuffer fb{};
    if (!run_l3_trunk_descent(
            L3DescentPhase{g.state, g.render.tile_sprites,
                           g.render.entity_sprites, g.render.palette, g.tiles,
                           g.grot3, fb, opts.enhanced,
                           g.render.extend_top_backdrop, present})) {
        running = false;
        return;
    }
    stamp_descent_overlay(g);
    // Enhanced: the settling-dust tail on the steady screen.
    if (opts.enhanced) *c.l3_smoke_tail = c.l3_smoke_tail_ticks;
}
}  // namespace olduvai::presentation
