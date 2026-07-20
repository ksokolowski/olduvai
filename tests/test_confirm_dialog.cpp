// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/confirm_dialog.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace olduvai::presentation;

static int fails = 0;
#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n",          \
                                    __LINE__, #cond); ++fails; }            \
    } while (0)

int main() {
    // open() sets is_open() true.
    ConfirmDialog d;
    CHECK(!d.is_open());
    d.open("Save & Apply?", {});
    CHECK(d.is_open());

    // open() sets default apply_selected() == true.
    CHECK(d.apply_selected());

    // open() stores title.
    CHECK(d.title() == "Save & Apply?");

    // open() stores changes.
    std::vector<StagedChange> changes = {
        {"render_scale", "Render scale", "2", "4"},
        {"music_device", "Music device", "auto", "gm-builtin"},
    };
    d.open("Confirm", changes);
    CHECK(d.changes().size() == 2);
    CHECK(d.changes()[0].key == "render_scale");
    CHECK(d.changes()[1].key == "music_device");

    // move(1) toggles apply_selected from true → false.
    CHECK(d.apply_selected());
    d.move(1);
    CHECK(!d.apply_selected());

    // move(-1) toggles apply_selected from false → true.
    d.move(-1);
    CHECK(d.apply_selected());

    // move(0) is a no-op.
    d.move(0);
    CHECK(d.apply_selected());
    d.move(1);
    d.move(0);
    CHECK(!d.apply_selected());

    // close() clears is_open().
    d.close();
    CHECK(!d.is_open());

    // note is stored and defaults to empty.
    ConfirmDialog d2;
    d2.open("T", {});
    CHECK(d2.note().empty());
    d2.open("T", {}, "Requires restart");
    CHECK(d2.note() == "Requires restart");

    if (fails == 0) std::puts("confirm_dialog: OK");
    return fails == 0 ? 0 : 1;
}
