// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/confirm_dialog.hpp"

namespace olduvai::presentation {

void ConfirmDialog::open(const std::string& title,
                         const std::vector<StagedChange>& changes,
                         const std::string& note) {
    title_     = title;
    changes_   = changes;
    note_      = note;
    open_      = true;
    apply_sel_ = true;
}

void ConfirmDialog::move(int dx) {
    if (dx != 0) apply_sel_ = !apply_sel_;
}

}  // namespace olduvai::presentation
