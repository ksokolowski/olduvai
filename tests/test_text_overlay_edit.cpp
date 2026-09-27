// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "presentation/menu/text_overlay_edit.hpp"

#include <cstdio>

#include <SDL.h>

using namespace olduvai::presentation;

static SDL_Event key(SDL_Keycode k, Uint16 mod = 0) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    e.key.keysym.mod = mod;
    return e;
}

static SDL_Event text(const char* s) {
    SDL_Event e{};
    e.type = SDL_TEXTINPUT;
    std::snprintf(e.text.text, sizeof(e.text.text), "%s", s);
    return e;
}

TEST_CASE("typing goes to the editor; tab moves focus off the text area") {
    EditOverlayState st;
    CHECK(edit_handle_event(st, text("hi")) == EditResult::kNone);
    CHECK(st.editor.text() == "hi");
    edit_handle_event(st, key(SDLK_TAB));
    CHECK(st.focus == EditFocus::kSave);
    edit_handle_event(st, text("x"));           // ignored while a button is focused
    CHECK(st.editor.text() == "hi");
    CHECK(edit_handle_event(st, key(SDLK_RETURN)) == EditResult::kSave);
}

TEST_CASE("shift+tab cycles the other way") {
    EditOverlayState st;
    edit_handle_event(st, key(SDLK_TAB, KMOD_LSHIFT));
    CHECK(st.focus == EditFocus::kCancel);
    edit_handle_event(st, key(SDLK_TAB, KMOD_LSHIFT));
    CHECK(st.focus == EditFocus::kSave);
}

TEST_CASE("esc cancels from any focus; ctrl+enter saves from the text area") {
    EditOverlayState st;
    CHECK(edit_handle_event(st, key(SDLK_ESCAPE)) == EditResult::kCancel);
    EditOverlayState st2;
    CHECK(edit_handle_event(st2, key(SDLK_RETURN, KMOD_LCTRL)) == EditResult::kSave);
}

TEST_CASE("enter in the text area is a newline, not a commit") {
    EditOverlayState st;
    edit_handle_event(st, text("a"));
    CHECK(edit_handle_event(st, key(SDLK_RETURN)) == EditResult::kNone);
    CHECK(st.editor.text() == "a\n");
}

TEST_CASE("cancel button commits a cancel") {
    EditOverlayState st;
    edit_handle_event(st, key(SDLK_TAB));       // -> Save
    edit_handle_event(st, key(SDLK_TAB));       // -> Cancel
    CHECK(st.focus == EditFocus::kCancel);
    CHECK(edit_handle_event(st, key(SDLK_RETURN)) == EditResult::kCancel);
}

TEST_CASE("wrap_lines soft-wraps at word boundaries and preserves logical rows") {
    // A logical line longer than cols wraps into multiple visual rows; the
    // stored text is NOT modified (wrap is visual only).
    std::vector<std::string> lines = {"the quick brown fox jumps", "short"};
    auto rows = wrap_lines(lines, 10);
    // "the quick " -> "the quick" (break at space), "brown fox " -> ...
    CHECK(rows.size() >= 4);
    CHECK(rows.front().lrow == 0);
    CHECK(rows.front().start == 0);
    // Every wrapped chunk of line 0 fits within cols.
    for (const auto& r : rows)
        if (r.lrow == 0) CHECK(r.text.size() <= 10);
    // "short" is a single row.
    bool short_ok = false;
    for (const auto& r : rows)
        if (r.lrow == 1 && r.text == "short") short_ok = true;
    CHECK(short_ok);
}

TEST_CASE("wrap_lines hard-breaks a word longer than cols") {
    std::vector<std::string> lines = {"aaaaaaaaaaaaaaaaaaaa"};   // 20, no spaces
    auto rows = wrap_lines(lines, 8);
    CHECK(rows.size() == 3);          // 8 + 8 + 4
    CHECK(rows[0].text.size() == 8);
    CHECK(rows[2].text == "aaaa");
}

TEST_CASE("empty and blank logical lines each keep one visual row") {
    std::vector<std::string> lines = {"", "x", ""};
    auto rows = wrap_lines(lines, 10);
    CHECK(rows.size() == 3);
    CHECK(rows[0].text.empty());
    CHECK(rows[1].text == "x");
    CHECK(rows[2].text.empty());
}

TEST_CASE("cursor keys edit in place; the keypad enter is enter") {
    EditOverlayState st;
    edit_handle_event(st, text("abc"));
    edit_handle_event(st, key(SDLK_LEFT));
    edit_handle_event(st, key(SDLK_BACKSPACE));
    CHECK(st.editor.text() == "ac");
    edit_handle_event(st, key(SDLK_HOME));
    edit_handle_event(st, key(SDLK_DELETE));
    CHECK(st.editor.text() == "c");
    edit_handle_event(st, key(SDLK_END));
    edit_handle_event(st, text("d"));
    CHECK(st.editor.text() == "cd");
    CHECK(edit_handle_event(st, key(SDLK_KP_ENTER)) == EditResult::kNone);
    CHECK(st.editor.text() == "cd\n");
    edit_handle_event(st, key(SDLK_UP));
    edit_handle_event(st, text("x"));
    CHECK(st.editor.text() == "xcd\n");
}

TEST_CASE("buttons ignore editing keys; other events do nothing") {
    EditOverlayState st;
    edit_handle_event(st, text("a"));
    edit_handle_event(st, key(SDLK_TAB));       // -> Save
    CHECK(edit_handle_event(st, key(SDLK_BACKSPACE)) == EditResult::kNone);
    CHECK(st.editor.text() == "a");
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    CHECK(edit_handle_event(st, motion) == EditResult::kNone);
    CHECK(st.focus == EditFocus::kSave);
}
