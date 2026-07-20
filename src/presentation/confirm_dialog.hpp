// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Confirm/Discard dialog state — driven by the Options menu when the user
// leaves with staged (uncommitted) changes.  Presents a slab listing the
// pending changes and two buttons (Apply / Discard).  Pure: no SDL.

#pragma once

#include <string>
#include <vector>

#include "presentation/settings_session.hpp"  // StagedChange

namespace olduvai::presentation {

class ConfirmDialog {
public:
    // Open the dialog with the given title, a list of staged changes, and an
    // optional explanatory note.  Resets the cursor to "Apply".
    void open(const std::string& title, const std::vector<StagedChange>& changes,
              const std::string& note = "");

    // Close the dialog without committing.
    void close() { open_ = false; }

    bool is_open() const { return open_; }

    // Move the button cursor.  Any non-zero dx flips between Apply/Discard.
    void move(int dx);

    // True iff the Apply button is currently selected.
    bool apply_selected() const { return apply_sel_; }

    const std::string& title()   const { return title_; }
    const std::string& note()    const { return note_; }
    const std::vector<StagedChange>& changes() const { return changes_; }

private:
    bool open_       = false;
    bool apply_sel_  = true;
    std::string title_, note_;
    std::vector<StagedChange> changes_;
};

}  // namespace olduvai::presentation
