// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One HD presenter for the full-screen text screens (loading card, score
// tally) in both drivers: an upscaled buffer with vector rows drawn over it at
// output resolution.  Verified equal to the per-driver presenters it replaced
// (tests/hd_text_screens.sh goldens).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <SDL.h>

#include "enhance/hd_text.hpp"
#include "enhance/upscale.hpp"
#include "presentation/image_out.hpp"   // present_output
#include "presentation/render/level_surface.hpp"   // TextScreenDeps
#include "presentation/render/logical_size.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {


// Gate hook: if the named env var is set, capture one presented frame (renderer
// readback before the present: a logical-size defect is invisible in a native
// dump, and post-present readback is black on Metal).  Returns false once it
// has enough frames (callers stop the screen).  `seq` is per screen, so the
// two screens do not share a budget.
inline bool capture_gate_frame(SDL_Renderer* ren,
                               const char* dump_env, const char* dump_tag,
                               int& seq) {
    const char* dir = dump_env != nullptr ? std::getenv(dump_env) : nullptr;
    if (dir == nullptr) return true;
    char path[512];
    std::snprintf(path, sizeof path, "%s/%s_%03d.png", dir, dump_tag, seq++);
    capture_renderer_output(ren, path);
    return seq < 8;
}

// Build the HD presenter for one text screen; `dump_env`/`dump_tag` name its
// gate hook.
inline TextScreenHd make_text_screen_hd(const TextScreenDeps& d,
                                        const char* dump_env,
                                        const char* dump_tag) {
    TextScreenHd h;
    h.hd_text = d.hd_text;
    h.scale = d.hd_scale;
    // `d` by value (a few pointers and scalars); what it points at must outlive
    // the handle.
    h.upscale = [d](const std::vector<std::uint8_t>& px) {
        return enhance::upscale_rgba(px, 320, 200, d.hd_scale, *d.hd_profile);
    };
    h.present_hd = [d, dump_env, dump_tag, dump_seq = 0](
                       const std::vector<std::uint8_t>& hd_px, int w, int h_px,
                       const std::vector<HdTextRow>& rows) mutable -> bool {
        SDL_Renderer* ren = d.ren;
        // ESC never reaches here (the tally skips on it, the loading card
        // ignores it); only a window close stops these screens.
        if (!poll_screen_events(d.win)) return false;
        // A black scene (the tally) skips the upload (~4 MB at 1280x800 per
        // counting step).
        if (!hd_px.empty())
            SDL_UpdateTexture(d.tex, nullptr, hd_px.data(), w * 4);
        show_texture(ren, hd_px.empty() ? nullptr : d.tex);
        if (!rows.empty())
            d.overlay->pass(ren, *d.hd_text, d.lsz->w(), d.lsz->h(),
                            [&](const enhance::Canvas& cv) {
                                draw_tally_rows_overlay(cv, *d.hd_text, rows);
                            });
        // The env var and frame counter belong to this handle, so each screen
        // dumps and counts its own frames.
        if (!capture_gate_frame(ren, dump_env, dump_tag, dump_seq))
            return false;
        present_output(ren);
        SDL_Delay(d.frame_ms);
        (void)h_px;
        return true;
    };
    return h;
}

}  // namespace olduvai::presentation
