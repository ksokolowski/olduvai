// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// F5 in-game bug-report form — the per-frame service glue, extracted
// verbatim from run_platform_level (CC3 phase 2, seam 1).  Owns the form's
// state (fields menu, confirm dialog, description editor, the stashed
// pre-form frame) and its input handling; the freeze-frame servicing runs
// through service_freeze() with the presentation plumbing passed narrowly.
// The controllers themselves (Menu, ConfirmDialog, EditOverlay,
// report_templates, write_bug_report) live where they always did — this is
// orchestration only.  Ordering is part of the frame-loop contract: the
// report_form / menu_script golden gates prove the extraction byte-exact.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "presentation/confirm_dialog.hpp"
#include "presentation/game_render.hpp"
#include "presentation/level_state.hpp"
#include "presentation/menu.hpp"
#include "presentation/text_overlay_edit.hpp"
#include "presentation/widescreen_presenter.hpp"

namespace olduvai::presentation {

class ReportFormService {
public:
    // The pause MenuModel carries the "bug_report" screen; the model must
    // outlive the service (both are locals of run_platform_level).
    explicit ReportFormService(MenuModel& model)
        : menu_(model, bind_) {}

    bool open() const { return open_; }
    bool edit_open() const { return edit_open_; }

    // F5: freeze the sim and open the form — seed fresh fields and let the
    // form/editor/confirm own input until Save (writes) or Discard.  The
    // screenshot is the pre-form frame stashed by service_freeze().
    void open_form();

    // Form input while open (call only when open()); consumes every event.
    void handle_event(const SDL_Event& ev);

    // OLDUVAI_MENU_SCRIPT "type:" tokens — dispatched STRAIGHT to the
    // editor's event handler, NOT via SDL_PushEvent: sdl2-compat refuses
    // app-pushed TEXTINPUT events (Event2to3 returns NULL and
    // SDL3_PushEvent(NULL) segfaults).  A TEXTINPUT can only insert
    // (kNone), so save/cancel handling is not needed here.
    void inject_text(const std::string& txt);

    // Per-call plumbing for the freeze-frame service — everything is a live
    // local of run_platform_level.
    struct FreezeDeps {
        Loaded& g;
        bool god_active;
        int display_level;
        int internal_level;
        int overlay_scale;
        bool want_presented;      // hd || wsp.present_path()
        WidescreenPresenter& wsp;
        SDL_Renderer* ren;
        std::uint32_t frame_ms;
        // run_platform_level's upload_and_show(frame, with_hud, do_present).
        const std::function<void(FrameBuffer&, bool, bool)>& upload_and_show;
    };

    // Freeze + draw over the frozen scene; Save writes the report from the
    // stashed frame.  Returns true when the form owned this frame (the
    // caller must `continue` — full freeze, exactly like the old inline
    // block); false when the form is closed and the frame proceeds.
    bool service_freeze(const FreezeDeps& d);

private:
    struct Bind : MenuBindings {
        std::map<std::string, std::string> mem;
        std::string get(const std::string& k) override {
            auto it = mem.find(k);
            return it == mem.end() ? std::string{} : it->second;
        }
        void set(const std::string& k, const std::string& v) override {
            mem[k] = v;
        }
    };

    void seed_();
    void retemplate_if_untouched_();
    void open_confirm_();

    Bind bind_;
    Menu menu_;
    ConfirmDialog confirm_;
    bool open_ = false, edit_open_ = false;
    bool save_pending_ = false, frame_ready_ = false;
    FrameBuffer frame_{320, 200};
    EditOverlayState edit_;
};

}  // namespace olduvai::presentation
