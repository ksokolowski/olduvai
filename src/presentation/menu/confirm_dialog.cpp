// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/confirm_dialog.hpp"

#include <utility>

namespace olduvai::presentation {

void ConfirmDialog::open(const std::string& title,
                         const std::vector<StagedChange>& changes,
                         const std::string& note) {
    title_     = title;
    changes_   = changes;
    note_      = note;
    open_      = true;
    apply_sel_ = true;
    question_  = false;
    on_yes_    = nullptr;
}

void ConfirmDialog::ask(const std::string& title,
                        std::function<void()> on_yes) {
    open(title, {});
    question_ = true;
    on_yes_   = std::move(on_yes);
}

bool ConfirmDialog::answer_yes() {
    if (!open_ || !question_) return false;
    const bool yes = !apply_sel_;
    std::function<void()> cb = std::move(on_yes_);
    close();
    if (yes && cb) cb();
    return yes;
}

void ConfirmDialog::move(int dx) {
    if (dx != 0) apply_sel_ = !apply_sel_;
}

}  // namespace olduvai::presentation
