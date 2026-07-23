// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/report_form.hpp"

#include <cstdio>
#include <vector>

#include "presentation/bug_capture.hpp"
#include "presentation/image_out.hpp"
#include "presentation/menu_render.hpp"
#include "presentation/report_templates.hpp"
#include "presentation/settings_session.hpp"

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
    for (char c : desc) if (c == '\n') ++nlines;
    std::vector<StagedChange> rows = {
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

void ReportFormService::handle_event(const SDL_Event& ev) {
    if (edit_open_) {
        const EditResult er = edit_handle_event(edit_, ev);
        if (er == EditResult::kSave) {
            bind_.set("report.description", edit_.editor.text());
            SDL_StopTextInput();
            edit_open_ = false;
        } else if (er == EditResult::kCancel) {
            SDL_StopTextInput();
            edit_open_ = false;
        }
        return;
    }
    if (ev.type != SDL_KEYDOWN) return;
    const auto rsym = ev.key.keysym.sym;
    if (confirm_.is_open()) {
        if (rsym == SDLK_LEFT || rsym == SDLK_RIGHT ||
            rsym == SDLK_UP || rsym == SDLK_DOWN ||
            rsym == SDLK_a || rsym == SDLK_d)
            confirm_.move(1);
        else if (rsym == SDLK_ESCAPE)
            confirm_.close();                    // back to the form
        else if (rsym == SDLK_RETURN || rsym == SDLK_SPACE) {
            if (confirm_.apply_selected())
                save_pending_ = true;            // freeze block writes
            else
                open_ = false;                   // Discard
            confirm_.close();
        }
        return;
    }
    if (rsym == SDLK_UP || rsym == SDLK_w) menu_.move(-1);
    else if (rsym == SDLK_DOWN || rsym == SDLK_s) menu_.move(+1);
    else if (rsym == SDLK_LEFT || rsym == SDLK_a) {
        const std::string tb = bind_.get("report.tag");
        menu_.adjust(-1);
        if (bind_.get("report.tag") != tb) retemplate_if_untouched_();
    } else if (rsym == SDLK_RIGHT || rsym == SDLK_d) {
        const std::string tb = bind_.get("report.tag");
        menu_.adjust(+1);
        if (bind_.get("report.tag") != tb) retemplate_if_untouched_();
    } else if (rsym == SDLK_RETURN || rsym == SDLK_SPACE) {
        const std::string tb = bind_.get("report.tag");
        const std::string a = menu_.activate();
        if (a.rfind("__edit_text:", 0) == 0) {
            edit_.editor.set_text(bind_.get("report.description"));
            edit_.title = "Description";
            edit_.focus = EditFocus::kText;
            SDL_StartTextInput();
            edit_open_ = true;
        } else if (!menu_.is_open()) {
            open_confirm_();                     // 'Back' left the form
        } else if (bind_.get("report.tag") != tb) {
            retemplate_if_untouched_();
        }
    } else if (rsym == SDLK_ESCAPE) {
        open_confirm_();
    }
}

bool ReportFormService::service_freeze(const FreezeDeps& d) {
    if (!open_) return false;
    d.g.state.god_mode = d.god_active;
    FrameBuffer pf{320, 200};
    compose_frame(pf, d.g.state, d.g.render, /*draw_player=*/true);
    if (!frame_ready_) {   // clean scene = the screenshot source
        frame_ = pf;
        frame_ready_ = true;
    }
    if (save_pending_) {
        save_pending_ = false;
        open_ = false;
        const BugAnnotations ann{bind_.get("report.tag"),
                                 bind_.get("report.repro"),
                                 bind_.get("report.description")};
        const std::string dir = write_bug_report(
            d.g.state, frame_, d.g.render.entity_sprites,
            d.display_level, d.internal_level, d.overlay_scale, ann,
            /*has_presented=*/d.want_presented);
        // screenshot_presented.png — what the player actually saw: the
        // scene run through the live present (HD upscale + widescreen
        // margins), which the native frame_ skips.  Re-render the (frozen)
        // scene WITHOUT presenting so RenderReadPixels sees the backbuffer
        // (a post-present read is black on Metal), then read the
        // output-resolution pixels.  Only meaningful when the present path
        // transforms the frame (HD or widescreen); classic 1× is
        // pixel-equal to the native shot.  Empty bubble hook: the L1-secret
        // cosmetic bubbles are immaterial to a bug shot.
        if (!dir.empty() && d.want_presented) {
            if (d.wsp.present_path())
                d.wsp.present(std::function<void(RenderTarget&)>{},
                              /*do_present=*/false);
            else
                d.upload_and_show(frame_, /*with_hud=*/true,
                                  /*do_present=*/false);
            capture_renderer_output(d.ren, dir + "/screenshot_presented.png");
        }
        return true;   // owned the frame; no delay on the save path
    }
    if (edit_open_) {
        draw_edit_overlay(pf, d.g.charset, edit_);
    } else if (confirm_.is_open()) {
        draw_confirm(pf, confirm_, d.g.charset, /*dim=*/true,
                     /*draw_text=*/true);
    } else {
        draw_menu(pf, menu_, d.g.charset, /*dim=*/true,
                  /*draw_text=*/true,
                  d.g.render.entity_sprites.size() > 33
                      ? &d.g.render.entity_sprites[33]
                      : nullptr,
                  &d.g.render.palette);
    }
    d.upload_and_show(pf, /*with_hud=*/false, /*do_present=*/true);
    SDL_Delay(d.frame_ms);
    return true;
}

}  // namespace olduvai::presentation
