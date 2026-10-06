// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/diag/report_form.hpp"

#include <cstdio>
#include <vector>

#include "presentation/diag/bug_capture.hpp"
#include "presentation/input/actions.hpp"   // key_is
#include "presentation/menu/menu_render.hpp"
#include "presentation/diag/report_templates.hpp"
#include "presentation/menu/settings_session.hpp"

namespace olduvai::presentation {

void ReportFormService::seed_() {
    bind_.mem["report.tag"] = "collision";
    bind_.mem["report.repro"] = "unknown";
    bind_.mem["report.description"] = report_template("collision");
}

void ReportFormService::retemplate_if_untouched_() {
    // Re-fill the description with the new tag's skeleton ONLY while it is
    // still an unedited template — once the user types, it is theirs.
    if (is_report_template(bind_.get("report.description")))
        bind_.set("report.description",
                  report_template(bind_.get("report.tag")));
}

void ReportFormService::open_confirm_() {
    const std::string desc = bind_.get("report.description");
    int nlines = 1;
    for (const char c : desc) if (c == '\n') ++nlines;
    const std::vector<StagedChange> rows = {
        {"report.tag", "Tag", "", bind_.get("report.tag")},
        {"report.repro", "Reproducibility", "", bind_.get("report.repro")},
        {"report.description", "Description", "",
         std::to_string(nlines) + (nlines == 1 ? " line" : " lines")},
    };
    confirm_.open("SAVE BUG REPORT?", rows, "");
}

void ReportFormService::open_form() {
    seed_();
    menu_.open("bug_report");
    open_ = menu_.is_open();
    edit_open_ = false;
    frame_ready_ = false;
}

void ReportFormService::inject_text(const std::string& txt) {
    SDL_Event te{};
    te.type = SDL_TEXTINPUT;
    std::snprintf(te.text.text, sizeof te.text.text, "%s", txt.c_str());
    edit_handle_event(edit_, te);
}

void ReportFormService::on_edit_event_(const SDL_Event& ev) {
    const EditResult er = edit_handle_event(edit_, ev);
    if (er == EditResult::kSave)
        bind_.set("report.description", edit_.editor.text());
    if (er == EditResult::kSave || er == EditResult::kCancel) {
        SDL_StopTextInput();
        edit_open_ = false;
    }
}

void ReportFormService::on_confirm_key_(SDL_Keycode sym) {
    if (key_is(sym, Action::kLeft) || key_is(sym, Action::kRight) ||
        key_is(sym, Action::kUp) || key_is(sym, Action::kDown)) {
        confirm_.move(1);
    } else if (key_is(sym, Action::kBack)) {
        confirm_.close();                        // back to the form
    } else if (key_is(sym, Action::kConfirm)) {
        if (confirm_.apply_selected())
            save_pending_ = true;                // freeze block writes
        else
            open_ = false;                       // Discard
        confirm_.close();
    }
}

void ReportFormService::on_form_key_(SDL_Keycode sym) {
    if (key_is(sym, Action::kUp)) {
        menu_.move(-1);
    } else if (key_is(sym, Action::kDown)) {
        menu_.move(+1);
    } else if (key_is(sym, Action::kLeft)) {
        menu_.adjust(-1);
    } else if (key_is(sym, Action::kRight)) {
        menu_.adjust(+1);
    } else if (key_is(sym, Action::kConfirm)) {
        if (menu_.activate().rfind("__edit_text:", 0) == 0) {
            edit_.editor.set_text(bind_.get("report.description"));
            edit_.title = "Description";
            edit_.focus = EditFocus::kText;
            SDL_StartTextInput();
            edit_open_ = true;
        } else if (!menu_.is_open()) {
            open_confirm_();                     // 'Back' left the form
        }
    } else if (key_is(sym, Action::kBack)) {
        open_confirm_();
    }
}

void ReportFormService::handle_event(const SDL_Event& ev) {
    if (edit_open_) {
        on_edit_event_(ev);
        return;
    }
    if (ev.type != SDL_KEYDOWN) return;
    const SDL_Keycode sym = ev.key.keysym.sym;
    if (confirm_.is_open()) {
        on_confirm_key_(sym);
        return;
    }
    // A tag changed by any key re-fills an untouched description.
    const std::string tag = bind_.get("report.tag");
    on_form_key_(sym);
    if (bind_.get("report.tag") != tag) retemplate_if_untouched_();
}

bool ReportFormService::service_freeze(const FreezeDeps& d) {
    if (!open_) return false;
    FrameBuffer pf{320, 200};
    d.compose(pf);
    if (!frame_ready_) {   // clean scene = the screenshot source
        frame_ = pf;
        frame_ready_ = true;
    }
    if (save_pending_) {
        save_pending_ = false;
        open_ = false;
        d.write(frame_, BugAnnotations{bind_.get("report.tag"),
                                       bind_.get("report.repro"),
                                       bind_.get("report.description")});
        return true;   // owned the frame; no delay on the save path
    }
    if (edit_open_) {
        draw_edit_overlay(pf, d.charset, edit_);
    } else if (confirm_.is_open()) {
        draw_confirm(pf, confirm_, d.charset, /*dim=*/true,
                     /*draw_text=*/true);
    } else {
        draw_menu(pf, menu_, d.charset, /*dim=*/true, /*draw_text=*/true,
                  d.cursor, d.cursor_palette);
    }
    d.show(pf);
    SDL_Delay(d.frame_ms);
    return true;
}

}  // namespace olduvai::presentation
