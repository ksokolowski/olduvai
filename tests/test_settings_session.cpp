// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/settings_session.hpp"

#include <cstdio>
#include <string>

using namespace olduvai::presentation;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n",         \
                                    __LINE__, #cond); ++fails; }          \
    } while (0)

int main() {
    SettingsSession s;
    CHECK(s.empty());

    // A single change shows up with the right old/new.
    s.stage("render_scale", "Render scale", "2", "4");
    CHECK(!s.empty());
    CHECK(s.changes().size() == 1);
    CHECK(s.changes()[0].key == "render_scale");
    CHECK(s.changes()[0].label == "Render scale");
    CHECK(s.changes()[0].old_value == "2");
    CHECK(s.changes()[0].new_value == "4");

    // Re-staging the same key keeps the ORIGINAL baseline, moves new only.
    s.stage("render_scale", "Render scale", "4", "2");  // old arg is ignored after first
    // 2 -> 4 -> 2 is a net no-op: the key drops out.
    CHECK(s.empty());

    // Re-stage and move twice; baseline stays the first old (2).
    s.stage("render_scale", "Render scale", "2", "4");
    s.stage("render_scale", "Render scale", "4", "2");  // back to baseline -> drops
    CHECK(s.empty());
    s.stage("render_scale", "Render scale", "2", "4");
    CHECK(s.changes().size() == 1);
    CHECK(s.changes()[0].old_value == "2" && s.changes()[0].new_value == "4");

    // A no-op stage (old == new) on a fresh key records nothing.
    SettingsSession s2;
    s2.stage("music_device", "Music device", "auto", "auto");
    CHECK(s2.empty());

    // Two distinct keys keep first-staged order.
    SettingsSession s3;
    s3.stage("music_device", "Music device", "auto", "gm-builtin");
    s3.stage("render_scale", "Render scale", "2", "4");
    CHECK(s3.changes().size() == 2);
    CHECK(s3.changes()[0].key == "music_device");
    CHECK(s3.changes()[1].key == "render_scale");

    // clear() empties.
    s3.clear();
    CHECK(s3.empty());

    if (fails == 0) std::puts("settings_session: OK");
    return fails == 0 ? 0 : 1;
}
