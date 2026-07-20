// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "presentation/text_editor.hpp"

using olduvai::presentation::TextEditor;

TEST_CASE("empty editor is one empty line, cursor at 0,0") {
    TextEditor e;
    CHECK(e.text().empty());
    CHECK(e.lines().size() == 1);
    CHECK(e.row() == 0);
    CHECK(e.col() == 0);
}

TEST_CASE("set_text splits on newline; text() round-trips") {
    TextEditor e;
    e.set_text("ab\ncd");
    CHECK(e.lines().size() == 2);
    CHECK(e.lines()[0] == "ab");
    CHECK(e.lines()[1] == "cd");
    CHECK(e.text() == "ab\ncd");
    CHECK(e.row() == 1);
    CHECK(e.col() == 2);
}

TEST_CASE("insert puts text at the cursor and advances it") {
    TextEditor e;
    e.insert("hi");
    CHECK(e.text() == "hi");
    CHECK(e.col() == 2);
    e.insert("!");
    CHECK(e.text() == "hi!");
}

TEST_CASE("insert containing a newline splits") {
    TextEditor e;
    e.insert("a\nb");
    CHECK(e.text() == "a\nb");
    CHECK(e.row() == 1);
    CHECK(e.col() == 1);
}

TEST_CASE("newline at end of text starts a fresh empty line") {
    TextEditor e;
    e.insert("abcd");
    e.newline();
    CHECK(e.text() == "abcd\n");
    CHECK(e.row() == 1);
    CHECK(e.col() == 0);
}

TEST_CASE("insert past the caps is rejected whole") {
    TextEditor e;
    std::string big(TextEditor::kMaxChars + 10, 'x');
    e.insert(big);
    CHECK(e.text().empty());
    e.insert("ok");
    CHECK(e.text() == "ok");
}

TEST_CASE("newline past the line cap is rejected") {
    TextEditor e;
    for (std::size_t i = 0; i + 1 < TextEditor::kMaxLines; ++i) e.newline();
    CHECK(e.lines().size() == TextEditor::kMaxLines);
    e.newline();   // at the cap: rejected
    CHECK(e.lines().size() == TextEditor::kMaxLines);
}

TEST_CASE("backspace deletes the char before the cursor") {
    TextEditor e;
    e.insert("abc");
    e.backspace();
    CHECK(e.text() == "ab");
    CHECK(e.col() == 2);
}

TEST_CASE("backspace at col 0 joins with the previous line") {
    TextEditor e;
    e.set_text("ab\ncd");
    e.move(TextEditor::Move::kHome);    // row 1, col 0
    e.backspace();
    CHECK(e.text() == "abcd");
    CHECK(e.row() == 0);
    CHECK(e.col() == 2);
}

TEST_CASE("del at end of line pulls the next line up") {
    TextEditor e;
    e.set_text("ab\ncd");
    e.move(TextEditor::Move::kUp);      // row 0, col clamped to 2
    e.move(TextEditor::Move::kEnd);
    e.del();
    CHECK(e.text() == "abcd");
    CHECK(e.row() == 0);
    CHECK(e.col() == 2);
}

TEST_CASE("up/down clamp the column to the shorter line") {
    TextEditor e;
    e.set_text("long line\nhi");        // row 1, col 2
    e.move(TextEditor::Move::kUp);      // row 0, col stays 2
    CHECK(e.row() == 0);
    CHECK(e.col() == 2);
    e.move(TextEditor::Move::kEnd);     // row 0, col 9
    e.move(TextEditor::Move::kDown);    // row 1, col clamped to 2
    CHECK(e.row() == 1);
    CHECK(e.col() == 2);
}

TEST_CASE("home/end and left/right at line edges") {
    TextEditor e;
    e.set_text("ab\ncd");               // row 1, col 2
    e.move(TextEditor::Move::kHome);
    CHECK(e.col() == 0);
    e.move(TextEditor::Move::kLeft);    // wraps to end of row 0
    CHECK(e.row() == 0);
    CHECK(e.col() == 2);
    e.move(TextEditor::Move::kRight);   // wraps to start of row 1
    CHECK(e.row() == 1);
    CHECK(e.col() == 0);
}

TEST_CASE("mid-line newline splits and mid-line insert lands at the cursor") {
    TextEditor e;
    e.set_text("abcd");
    e.move(TextEditor::Move::kLeft);
    e.move(TextEditor::Move::kLeft);    // col 2
    e.newline();
    CHECK(e.text() == "ab\ncd");
    e.insert("X");
    CHECK(e.text() == "ab\nXcd");
}
