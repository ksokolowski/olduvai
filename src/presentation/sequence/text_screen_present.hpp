// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One HD presenter for every full-screen text screen, shared by both drivers
// (BACKLOG §3.14).
//
// A "text screen" is a full-screen upscaled buffer with cartoon vector rows
// drawn over it at OUTPUT resolution: the level-entry loading card and the
// score tally.  Four presenters used to do that — one per (screen x driver) —
// all the same thirty lines, differing only in the locals they closed over.
//
// That duplication was not cosmetic.  It is why a logical-size defect existed on
// one path and not the other (§3.13), why the mis-centring was visible in some
// modes and not others, and why `show_score_tally`'s return value was honoured
// by one driver and discarded by the other.  It is also why the two loading
// screens drew their rows through DIFFERENT functions — `game_app`'s went
// through `draw_tally_rows_overlay` (it reused its tally handle) while
// `boss_app`'s had its own `draw_centered_overlay_row` loop.  Same output, by
// arithmetic that nothing required to agree.  Two copies of a presenter drift;
// this is the shared one.
//
// §3.9's case, not §3.7's: the arguments below already WERE the boundary between
// the drivers and these screens, passed as captures instead of parameters, and
// several now have owners (`LogicalSize`, `TextOverlay`).  It does not touch §2,
// which forbids merging the frame LOOPS — a UI screen's presenter is not a loop.
//
// Proven equal before merging, both times, which is the only reason it was safe:
// the tally frames measure identically on both stacks (per-row offsets
// [0, 0, 0, -1, 12, -11] each), and every cell of `tests/hd_text_screens.sh`
// was goldened against the hand-written presenter it replaced.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <SDL.h>

#include "enhance/hd_text.hpp"
#include "enhance/upscale.hpp"
#include "presentation/render/level_surface.hpp"   // TextScreenDeps
#include "presentation/render/logical_size.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {


// Gate hook: photograph one presented frame of a text screen, if the named env
// var is set.  Returns false once it has enough frames, which every caller
// turns into "stop this screen" — the same thing a window close means to them.
//
// Renderer READBACK, before the present.  A logical-size defect is invisible to
// any native-buffer dump, and post-present readback is black on Metal.
//
// The logical size is cleared for the read and restored after, because
// SDL_RenderReadPixels(rect = nullptr) reads the current VIEWPORT, not the
// target.  With a logical size set, the pillarboxed image would land at the
// top-left of a full-size surface and measure as "drawn flush left" when
// nothing is wrong at all — which is exactly how it read the first time the
// classic path was photographed.
//
// `seq` belongs to the CALLER, one counter per screen, so the loading card and
// the tally cannot spend each other's frame budget.
inline bool capture_gate_frame(SDL_Renderer* ren, const LogicalSize& lsz,
                               const char* dump_env, const char* dump_tag,
                               int& seq) {
    const char* dir = dump_env != nullptr ? std::getenv(dump_env) : nullptr;
    if (dir == nullptr) return true;
    char path[512];
    std::snprintf(path, sizeof path, "%s/%s_%03d.png", dir, dump_tag, seq++);
    SDL_RenderSetLogicalSize(ren, 0, 0);
    capture_renderer_output(ren, path);
    SDL_RenderSetLogicalSize(ren, lsz.w(), lsz.h());
    return seq < 8;
}

// Build the HD presenter for one text screen.
//
// `dump_env`/`dump_tag` name the gate hook: the handle knows WHICH screen it
// presents, so the dump does not have to guess (see below).
inline TextScreenHd make_text_screen_hd(const TextScreenDeps& d,
                                        const char* dump_env,
                                        const char* dump_tag) {
    TextScreenHd h;
    h.hd_text = d.hd_text;
    h.scale = d.hd_scale;
    // `d` is captured BY VALUE — it is nine pointers and two scalars, so the
    // copy costs nothing and the handle stops depending on the caller keeping
    // its own `TextScreenDeps` alive.  What must still outlive the handle is
    // what `d` points AT, which the struct's own comment states.
    h.upscale = [d](const std::vector<std::uint8_t>& px) {
        return enhance::upscale_rgba(px, 320, 200, d.hd_scale, *d.hd_profile);
    };
    h.present_hd = [d, dump_env, dump_tag, dump_seq = 0](
                       const std::vector<std::uint8_t>& hd_px, int w, int h_px,
                       const std::vector<HdTextRow>& rows) mutable -> bool {
        SDL_Renderer* ren = d.ren;
        // ESC never reaches this screen's own code: the tally consumes it as a
        // skip (poll_tally_key, never an abort) and the loading card has no
        // menu to open.  Only a window close stops these screens, and that is
        // what `false` means to both of their callers.
        if (!poll_screen_events(d.win)) return false;
        SDL_UpdateTexture(d.tex, nullptr, hd_px.data(), w * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, d.tex, nullptr, nullptr);
        if (!rows.empty()) {
            int ow = 0, oh = 0;
            if (d.overlay->begin(ren, *d.hd_text, ow, oh)) {
                draw_tally_rows_overlay(d.overlay->buffer(), ow, oh,
                                        *d.hd_text, rows);
                d.overlay->flush(ren, d.lsz->w(), d.lsz->h());
            }
        }
        // The env var and the counter belong to THE HANDLE, not to the factory.
        // One driver builds a handle per screen, so each one dumps its own
        // screen and counts its own frames; the previous single-env,
        // function-static version could not tell the loading screen from the
        // tally and guessed from the row count — which is how the platform
        // golden came to photograph "Please Wait" instead of the tally.
        if (!capture_gate_frame(ren, *d.lsz, dump_env, dump_tag, dump_seq))
            return false;
        SDL_RenderPresent(ren);
        SDL_Delay(d.frame_ms);
        (void)h_px;
        return true;
    };
    return h;
}

}  // namespace olduvai::presentation
