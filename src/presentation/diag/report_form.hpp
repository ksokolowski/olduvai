// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// F5 in-game bug-report form — shared by the platform level and the boss
// arena (BACKLOG §3.30).  Owns the form's state (fields menu, confirm dialog,
// description editor, the stashed pre-form frame) and its input handling;
// the freeze-frame servicing runs through service_freeze(), with the frozen
// scene, the present and the report's contents supplied by the driver as
// callbacks.  The controllers themselves (Menu, ConfirmDialog, EditOverlay,
// report_templates, write_bug_report) live where they always did — this is
// orchestration only.  Ordering is part of the frame-loop contract: the
// report_form / menu_script golden gates pin the platform side byte-exact.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/text_overlay_edit.hpp"
#include "presentation/diag/bug_capture.hpp"

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

    // What a level driver supplies to the freeze-frame service.  The form
    // itself is driver-agnostic; the frozen scene, the present and the
    // report's CONTENTS are the driver's (a platform level and a boss arena
    // differ in all three).
    struct FreezeDeps {
        // Draw the frozen scene into a native 320x200 frame WITHOUT advancing
        // any per-frame state — the caller `continue`s before its tick, so an
        // advancing draw here would be the only advance of a frozen frame
        // (open F5 mid-swing and the club drained away).
        std::function<void(FrameBuffer&)> compose;
        // Present a native form frame.
        std::function<void(FrameBuffer&)> show;
        // Write the report: the clean frozen scene (the first compose after
        // open) and the form's fields.
        std::function<void(const FrameBuffer&, const BugAnnotations&)> write;
        // The form's bitmap font and bone cursor (cursor may be null).
        const std::vector<formats::Sprite>& charset;
        const formats::Sprite* cursor;
        const std::vector<formats::Rgb>* cursor_palette;
        std::uint32_t frame_ms;
    };

    // Freeze + draw over the frozen scene; Save writes the report from the
    // stashed frame.  Returns true when the form owned this frame (the
    // caller must `continue` — full freeze); false when the form is closed
    // and the frame proceeds.
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
