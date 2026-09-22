// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The Sound card choice (BACKLOG §3.27): card <-> (music_device, sfx_backend),
// "custom" for a pair no card names, which cards a machine is offered, and
// that every pair a card writes is a value its menu row can hold.
#include "doctest/doctest.h"
#include "presentation/audio/sound_card.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/menu/settings_session.hpp"
#include "presentation/menu/staging_bindings.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace olduvai::presentation;

TEST_CASE("sound card: each card round-trips through its pair") {
    for (const SoundCard& c : kSoundCards) {
        CHECK(sound_card_for(c.music, c.sfx) == c.id);
        const SoundCard* f = find_sound_card(c.id);
        REQUIRE(f != nullptr);
        CHECK(std::string(f->music) == c.music);
        CHECK(std::string(f->sfx) == c.sfx);
    }
}

TEST_CASE("sound card: the default pair reads as Auto; a mix reads as Custom") {
    CHECK(sound_card_for("auto", "auto") == "auto");
    CHECK(sound_card_for("opl", "sb-dac") == "sb");       // the Sound Blaster
    CHECK(sound_card_for("mt32-builtin", "sb-dac") == kCustomSoundCard);
    CHECK(sound_card_for("auto", "opl") == kCustomSoundCard);
    CHECK(find_sound_card(kCustomSoundCard) == nullptr);
    CHECK(find_sound_card("soundblaster16") == nullptr);
}

TEST_CASE("sound card: only cards that will sound are offered") {
    const std::vector<std::string> bare = available_sound_cards({});
    CHECK(bare == std::vector<std::string>{"auto", "sb", "adlib", "off"});

    SoundCardAvail all;
    all.mt32 = all.gm = all.midi = true;
    CHECK(available_sound_cards(all) ==
          std::vector<std::string>{"auto", "sb", "adlib", "mt32", "gm",
                                   "midi", "off"});

    SoundCardAvail roms;
    roms.mt32 = true;
    CHECK(available_sound_cards(roms) ==
          std::vector<std::string>{"auto", "sb", "adlib", "mt32", "off"});
}

TEST_CASE("sound card: the menu offers every card, and the Advanced rows "
          "hold every value a card writes") {
    const MenuModel m = built_in_menu_model();
    const MenuItem* card = nullptr;
    const MenuItem* music = nullptr;
    const MenuItem* sfx = nullptr;
    for (const auto& [sid, scr] : m.screens)
        for (const auto& it : scr.items) {
            if (it.key == "sound_card") card = &it;
            if (it.key == "music_device") music = &it;
            if (it.key == "sfx_backend") sfx = &it;
        }
    REQUIRE(card != nullptr);
    REQUIRE(music != nullptr);
    REQUIRE(sfx != nullptr);
    CHECK(card->type == "choice");
    const auto has = [](const MenuItem* it, const std::string& v) {
        return std::find(it->values.begin(), it->values.end(), v) !=
               it->values.end();
    };
    for (const SoundCard& c : kSoundCards) {
        CHECK(has(card, c.id));
        CHECK(has(music, c.music));
        CHECK(has(sfx, c.sfx));
    }
}

// ── The menu row, through the staging skeleton all three menus share ────────

namespace {
struct Bind : StagingBindings {};
MenuItem card_row() {
    MenuItem it;
    it.id = it.key = "sound_card";
    it.type = "choice";
    for (const SoundCard& c : kSoundCards) it.values.emplace_back(c.id);
    it.values.emplace_back(kCustomSoundCard);
    return it;
}
}  // namespace

TEST_CASE("sound card row: reads the pair, stages both keys, nets out") {
    Bind b;
    SettingsSession s;
    b.session = &s;
    b.mem["music_device"] = "auto";
    b.mem["sfx_backend"] = "auto";
    CHECK(b.get("sound_card") == "auto");

    b.set("sound_card", "adlib");
    CHECK(b.mem["music_device"] == "opl");
    CHECK(b.mem["sfx_backend"] == "opl");
    CHECK(b.get("sound_card") == "adlib");
    REQUIRE(s.changes().size() == 2);   // two real keys, each reload-class

    b.set("sound_card", "auto");        // back where we started
    CHECK(s.empty());

    b.set("sound_card", "custom");      // not a pair: nothing to write
    b.set("sound_card", "nonsense");
    CHECK(s.empty());
    CHECK(b.get("sound_card") == "auto");

    b.mem["music_device"] = "mt32-builtin";   // a mix from Advanced
    b.mem["sfx_backend"] = "sb-dac";
    CHECK(b.get("sound_card") == kCustomSoundCard);
}

TEST_CASE("sound card row: offers only what will sound, never Custom") {
    Bind b;
    CHECK(b.allowed_values(card_row()) ==
          std::vector<std::string>{"auto", "sb", "adlib", "off"});
    b.sound_avail.mt32 = true;
    b.sound_avail.midi = true;
    CHECK(b.allowed_values(card_row()) ==
          std::vector<std::string>{"auto", "sb", "adlib", "mt32", "midi",
                                   "off"});
}
