// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure multi-line text-editing model — no SDL, no rendering.  Holds the text
// as a vector of UTF-8 lines and a {row, col} cursor (col = BYTE offset on a
// UTF-8 boundary).  All F5-report / future text-field editing logic lives
// here so it is exhaustively unit-testable without a window.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace olduvai::presentation {

class TextEditor {
public:
    // Hard caps: input that would cross either is rejected WHOLE (silent) —
    // bounds the report file, the wrapping cost, and accidental giant pastes.
    static constexpr std::size_t kMaxChars = 2000;
    static constexpr std::size_t kMaxLines = 60;

    enum class Move { kLeft, kRight, kUp, kDown, kHome, kEnd };

    TextEditor() : lines_{""} {}

    void set_text(const std::string& s);
    std::string text() const;

    void insert(const std::string& utf8);   // at cursor; may contain '\n'
    void newline();                          // split current line at cursor
    void backspace();                        // delete before cursor (joins)
    void del();                              // delete at cursor (joins at EOL)
    void move(Move m);

    int row() const { return row_; }
    int col() const { return col_; }
    const std::vector<std::string>& lines() const { return lines_; }

private:
    std::size_t total_chars() const;         // line bytes + '\n' joiners
    std::vector<std::string> lines_;
    int row_ = 0;
    int col_ = 0;
};

}  // namespace olduvai::presentation
