// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-state save: binary round-trip + entity snapshot/overlay + header
// capture/apply.  Spec: the save-state design spec (full-state, revised).

#include <cstring>
#include <filesystem>

#include "doctest/doctest.h"
#include "core/types.hpp"
#include "presentation/save_state.hpp"
#include "systems/player.hpp"

using namespace olduvai::presentation;

TEST_CASE("SaveState binary serialize/deserialize round-trips") {
    SaveState s;
    s.hdr.level = 5;
    s.hdr.rng = 0xDEADBEEFu;
    s.hdr.current_screen = 118;     // a secret/cave (>=100) screen
    s.hdr.secret_flag = 1;
    s.hdr.secret_index = 18;
    s.hdr.player.x = 152;
    s.hdr.player.y = 88;
    s.hdr.player.lives = 4;
    s.hdr.score = 87000;
    s.hdr.food_count = 6;

    s.entities = {EntitySnapshot{}, EntitySnapshot{}};
    s.entities[0].obj_type = 3;
    s.entities[0].x = 200;
    s.entities[0].active = 0;
    s.store = {ScreenEntities{2, {EntitySnapshot{}}}};
    s.store[0].entities[0].y = 77;
    s.bound_key = 118;

    const auto bytes = serialize(s);
    const auto back = deserialize(bytes);
    REQUIRE(back.has_value());
    CHECK(back->hdr.level == 5);
    CHECK(back->hdr.rng == 0xDEADBEEFu);
    CHECK(back->hdr.current_screen == 118);
    CHECK(back->hdr.secret_flag == 1);
    CHECK(back->hdr.player.x == 152);
    CHECK(back->hdr.player.lives == 4);
    CHECK(back->hdr.score == 87000);
    CHECK(back->entities.size() == 2);
    CHECK(back->entities[0].x == 200);
    CHECK(back->entities[0].active == 0);
    CHECK(back->store.size() == 1);
    CHECK(back->store[0].key == 2);
    CHECK(back->store[0].entities[0].y == 77);
    CHECK(back->bound_key == 118);

    // Re-serialize must be byte-identical (catches a dropped field).
    CHECK(serialize(*back) == bytes);
}

TEST_CASE("deserialize rejects bad magic / version / truncation") {
    CHECK_FALSE(deserialize({}).has_value());
    CHECK_FALSE(deserialize({'x', 'x', 'x', 'x'}).has_value());
    SaveState s;
    s.hdr.version = 99;
    CHECK_FALSE(deserialize(serialize(s)).has_value());  // unsupported version
}

TEST_CASE("entity snapshot/overlay preserves runtime fields, not derived data") {
    olduvai::core::Entity e;
    e.obj_type = olduvai::core::ObjType::RedDino;
    e.x = 120; e.y = 96; e.state = 4; e.active = false; e.direction = 1;
    e.ko_counter = 7; e.current_y = 88; e.walk_offsets = {1, 2, 3};  // derived

    const EntitySnapshot snap = snapshot_entity(e);

    olduvai::core::Entity fresh;                  // a freshly-spawned entity of same type
    fresh.obj_type = olduvai::core::ObjType::RedDino;
    fresh.walk_offsets = {1, 2, 3};      // derived data from the spawn tables
    overlay_entity(snap, fresh);
    CHECK(fresh.x == 120);
    CHECK(fresh.state == 4);
    CHECK(fresh.active == false);
    CHECK(fresh.direction == 1);
    CHECK(fresh.ko_counter == 7);
    CHECK(fresh.current_y == 88);
    CHECK(fresh.walk_offsets == std::vector<int>{1, 2, 3});  // untouched (re-spawn owns it)
}

TEST_CASE("header capture/apply round-trips the player + scalars") {
    olduvai::systems::SystemsState st;
    st.current_screen = 118;
    st.secret_flag = 1;
    st.secret_index = 18;
    st.player.x = 152; st.player.y = 88; st.player.lives = 4;
    st.score = 87000; st.food_count = 6; st.glider_active = true;

    const SaveHeader h = capture_header(st, /*display_level=*/5);
    olduvai::systems::SystemsState st2;
    apply_header(h, st2);
    CHECK(st2.current_screen == 118);
    CHECK(st2.secret_flag == 1);
    CHECK(st2.secret_index == 18);
    CHECK(st2.player.x == 152);
    CHECK(st2.player.lives == 4);
    CHECK(st2.score == 87000);
    CHECK(st2.glider_active == true);
    CHECK(h.level == 5);
}

TEST_CASE("save_to_file / load_from_file round-trip") {
    const auto dir = std::filesystem::temp_directory_path() / "olduvai_sav2";
    std::filesystem::remove_all(dir);
    const std::string path = (dir / "quicksave.sav").string();
    SaveState s;
    s.hdr.current_screen = 9;
    s.hdr.player.lives = 2;
    CHECK(save_to_file(s, path));
    const auto loaded = load_from_file(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->hdr.current_screen == 9);
    CHECK(loaded->hdr.player.lives == 2);
    std::filesystem::remove_all(dir);
}

TEST_CASE("deserialize enforces the v3 layout tag and semantic ranges") {
    SaveState s;                        // defaults: version 3, level 1
    const auto bytes = serialize(s);
    CHECK(deserialize(bytes).has_value());
    // Layout-tag mismatch = a save from a binary with a different
    // SaveHeader/PlayerState layout: must be rejected, not loaded shifted.
    auto bad = bytes;
    bad[sizeof(SaveHeader)] ^= 0xFF;    // first byte of the layout tag
    CHECK_FALSE(deserialize(bad).has_value());
    // v2 stream (untagged) is no longer a valid format: v3 is the sole format
    // since the first public release, so a v2 header must be rejected.
    SaveState v2 = s;
    v2.hdr.version = 2;
    auto v2bytes = serialize(v2);
    v2bytes.erase(v2bytes.begin() + sizeof(SaveHeader),
                  v2bytes.begin() + sizeof(SaveHeader) + 4);
    CHECK_FALSE(deserialize(v2bytes).has_value());
    // Semantic ranges: these index kOrder[]/screens[] downstream.
    for (auto mutate : {+[](SaveState& x) { x.hdr.level = 0; },
                        +[](SaveState& x) { x.hdr.level = 8; },
                        +[](SaveState& x) { x.hdr.current_level = 9; },
                        +[](SaveState& x) { x.hdr.current_screen = -1; },
                        +[](SaveState& x) { x.hdr.current_screen = 3000; }}) {
        SaveState r = s;
        mutate(r);
        CHECK_FALSE(deserialize(serialize(r)).has_value());
    }
}

TEST_CASE("EntitySnapshot canary: size tripwire + every-field round-trip") {
    // 25 int32 fields, no padding.  If this fires you added an Entity
    // runtime field: update snapshot_entity + overlay_entity + THIS constant
    // deliberately — a field the pair never learns about would round-trip
    // "successfully" while silently not persisting (review 2026-07-03 T4).
    CHECK(sizeof(EntitySnapshot) == 25 * sizeof(std::int32_t));
    // Fill every snapshotted Entity field with a distinct value, then
    // snapshot -> overlay onto a fresh entity -> snapshot again.  Any field
    // that is captured but not restored (or vice versa) breaks the equality.
    olduvai::core::Entity e;
    int v = 101;
    e.x = v++; e.y = v++; e.state = v++; e.sprite = v++;
    e.facing_left = true; e.active = true; e.visible = false;
    e.frame_counter = v++; e.respawns = v++; e.direction = v++;
    e.state_counter = v++; e.ko_counter = v++; e.current_y = v++;
    e.dy = v++; e.throw_flag = v++; e.throw_x = v++; e.throw_y = v++;
    e.fireball_request = v++; e.counter = v++; e.bird_height_idx = v++;
    e.bird_anim = v++; e.draw_dy = v++; e.direction_flag = v++;
    e.spider_di = v++;
    const EntitySnapshot a = snapshot_entity(e);
    olduvai::core::Entity fresh;
    overlay_entity(a, fresh);
    const EntitySnapshot b = snapshot_entity(fresh);
    CHECK(std::memcmp(&a, &b, sizeof(EntitySnapshot)) == 0);
}
