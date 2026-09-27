// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// F5 bug-report form, shared by the platform level and the boss arena: form
// state (fields menu, confirm dialog, description editor, the stashed
// pre-form frame) and input handling.  The driver supplies the frozen scene,
// the present and the report contents.  Ordering is part of the frame-loop
// contract (the report_form / menu_script goldens pin it).

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

    // F5: freeze and open the form with fresh fields; it owns input until Save
    // or Discard.  The screenshot is the pre-form frame service_freeze()
    // stashed.
    void open_form();

    // Form input while open (call only when open()); consumes every event.
    void handle_event(const SDL_Event& ev);

    // OLDUVAI_MENU_SCRIPT "type:" tokens, sent straight to the editor, not
    // through SDL_PushEvent: sdl2-compat rejects app-pushed TEXTINPUT (and
    // SDL3_PushEvent then segfaults).  Text input only inserts.
    void inject_text(const std::string& txt);

    // What the driver supplies: the frozen scene, the present and the report
    // contents.
    struct FreezeDeps {
        // Draw the frozen scene into a native 320x200 frame without advancing
        // any state (the caller skips its tick, so an advance here would drain
        // the club swing).
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

    // Freeze and draw the form; Save writes the report from the stashed frame.
    // True when the form owned this frame (the caller skips the rest).
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
    void on_edit_event_(const SDL_Event& ev);
    void on_confirm_key_(SDL_Keycode sym);
    void on_form_key_(SDL_Keycode sym);

    Bind bind_;
    Menu menu_;
    ConfirmDialog confirm_;
    bool open_ = false, edit_open_ = false;
    bool save_pending_ = false, frame_ready_ = false;
    FrameBuffer frame_{320, 200};
    EditOverlayState edit_;
};

}  // namespace olduvai::presentation
