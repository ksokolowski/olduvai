// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Confirm dialog state, in two modes sharing one slab and one renderer:
//  * Apply / Discard — driven by the Options menu when the user leaves with
//    staged (uncommitted) changes; the slab lists the pending changes.
//  * a yes/no QUESTION (ask) — the quit confirm.  Opens on No, so a double
//    press of Accept never quits by accident; Yes runs the stored callback.
// The left button (Apply / No) is the one selected on open.  Pure: no SDL.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "presentation/menu/settings_session.hpp"  // StagedChange

namespace olduvai::presentation {

class ConfirmDialog {
public:
    // Open the dialog with the given title, a list of staged changes, and an
    // optional explanatory note.  Resets the cursor to "Apply".
    void open(const std::string& title, const std::vector<StagedChange>& changes,
              const std::string& note = "");

    // Open a yes/no question.  The cursor starts on No; answer_yes() runs
    // `on_yes` only if Yes is selected.
    void ask(const std::string& title, std::function<void()> on_yes);

    // Close the dialog without committing (a question closes as No).
    void close() { open_ = false; on_yes_ = nullptr; }

    bool is_open() const { return open_; }

    // Move the button cursor.  Any non-zero dx flips between Apply/Discard.
    void move(int dx);

    // True iff the LEFT button (Apply, or No for a question) is selected.
    bool apply_selected() const { return apply_sel_; }

    bool is_question() const { return question_; }
    // No brackets: the game's bitmap charset has no '[' / ']' (they drew
    // as '?', and "? Discard ?" overflowed its highlight); the highlight
    // cell already marks the selected button.
    const char* left_label()  const { return question_ ? "No"  : "Apply"; }
    const char* right_label() const { return question_ ? "Yes" : "Discard"; }

    // Resolve a question: close, and run the Yes callback if Yes is
    // selected.  Returns whether it ran.  No-op (false) for Apply/Discard.
    bool answer_yes();

    const std::string& title()   const { return title_; }
    const std::string& note()    const { return note_; }
    const std::vector<StagedChange>& changes() const { return changes_; }

private:
    bool open_       = false;
    bool apply_sel_  = true;
    bool question_   = false;
    std::function<void()> on_yes_;
    std::string title_, note_;
    std::vector<StagedChange> changes_;
};

}  // namespace olduvai::presentation
