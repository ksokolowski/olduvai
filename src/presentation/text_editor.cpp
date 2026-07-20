// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/text_editor.hpp"

#include <algorithm>

namespace olduvai::presentation {

std::size_t TextEditor::total_chars() const {
    std::size_t n = 0;
    for (const auto& l : lines_) n += l.size();
    n += lines_.empty() ? 0 : lines_.size() - 1;   // the '\n' joiners
    return n;
}

void TextEditor::set_text(const std::string& s) {
    lines_.clear();
    std::string cur;
    for (char c : s) {
        if (c == '\n') { lines_.push_back(cur); cur.clear(); }
        else cur += c;
    }
    lines_.push_back(cur);
    if (lines_.size() > kMaxLines) lines_.resize(kMaxLines);
    row_ = static_cast<int>(lines_.size()) - 1;
    col_ = static_cast<int>(lines_[static_cast<std::size_t>(row_)].size());
}

std::string TextEditor::text() const {
    std::string out;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) out += '\n';
        out += lines_[i];
    }
    return out;
}

void TextEditor::insert(const std::string& utf8) {
    if (utf8.empty()) return;
    // Whole-reject if the insert would cross either cap.
    std::size_t added_lines = 0;
    for (char c : utf8) if (c == '\n') ++added_lines;
    if (total_chars() + utf8.size() > kMaxChars) return;
    if (lines_.size() + added_lines > kMaxLines) return;
    for (char c : utf8) {
        if (c == '\n') { newline(); continue; }
        lines_[static_cast<std::size_t>(row_)]
            .insert(static_cast<std::size_t>(col_), 1, c);
        ++col_;
    }
}

void TextEditor::newline() {
    if (lines_.size() + 1 > kMaxLines) return;
    if (total_chars() + 1 > kMaxChars) return;
    auto& line = lines_[static_cast<std::size_t>(row_)];
    const std::string tail = line.substr(static_cast<std::size_t>(col_));
    line.erase(static_cast<std::size_t>(col_));
    lines_.insert(lines_.begin() + row_ + 1, tail);
    ++row_;
    col_ = 0;
}

void TextEditor::backspace() {
    if (col_ > 0) {
        lines_[static_cast<std::size_t>(row_)]
            .erase(static_cast<std::size_t>(col_ - 1), 1);
        --col_;
    } else if (row_ > 0) {
        col_ = static_cast<int>(lines_[static_cast<std::size_t>(row_ - 1)].size());
        lines_[static_cast<std::size_t>(row_ - 1)] +=
            lines_[static_cast<std::size_t>(row_)];
        lines_.erase(lines_.begin() + row_);
        --row_;
    }
}

void TextEditor::del() {
    auto& line = lines_[static_cast<std::size_t>(row_)];
    if (col_ < static_cast<int>(line.size())) {
        line.erase(static_cast<std::size_t>(col_), 1);
    } else if (row_ + 1 < static_cast<int>(lines_.size())) {
        line += lines_[static_cast<std::size_t>(row_ + 1)];
        lines_.erase(lines_.begin() + row_ + 1);
    }
}

void TextEditor::move(Move m) {
    const int last =
        static_cast<int>(lines_[static_cast<std::size_t>(row_)].size());
    switch (m) {
        case Move::kLeft:
            if (col_ > 0) --col_;
            else if (row_ > 0) {
                --row_;
                col_ = static_cast<int>(
                    lines_[static_cast<std::size_t>(row_)].size());
            }
            break;
        case Move::kRight:
            if (col_ < last) ++col_;
            else if (row_ + 1 < static_cast<int>(lines_.size())) {
                ++row_;
                col_ = 0;
            }
            break;
        case Move::kUp:
            if (row_ > 0) {
                --row_;
                col_ = std::min(col_, static_cast<int>(
                    lines_[static_cast<std::size_t>(row_)].size()));
            }
            break;
        case Move::kDown:
            if (row_ + 1 < static_cast<int>(lines_.size())) {
                ++row_;
                col_ = std::min(col_, static_cast<int>(
                    lines_[static_cast<std::size_t>(row_)].size()));
            }
            break;
        case Move::kHome: col_ = 0; break;
        case Move::kEnd:  col_ = last; break;
    }
}

}  // namespace olduvai::presentation
