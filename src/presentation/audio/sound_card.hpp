// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The "Sound card" choice (BACKLOG §3.27): the sound setup a 1991 player
// would recognise, as one choice instead of two.  It is a VIEW of the two
// settings the audio code has always read — music_device + sfx_backend — not
// a third stored key: picking a card writes the pair, and the pair on disk
// reads back as the card whose pair it is, or "custom" when none matches
// (a mix made under Audio -> Advanced, or in play.json).  So play.json, the
// precedence rules and the audio code need nothing new.
//
// Pure and header-only: no SDL, no filesystem, and usable from the app layer
// (--sound-card), which also builds without the SDL presentation library.
// What this machine can actually play is probed elsewhere
// (probe_sound_cards in audio.hpp) and handed in.
#pragma once

#include <string>
#include <vector>

namespace olduvai::presentation {

struct SoundCard {
    const char* id;      // the menu value and the --sound-card name
    const char* music;   // music_device
    const char* sfx;     // sfx_backend
};

// In menu order.  "auto" keeps today's default: the best synth available
// (MT-32, then GM, then the Sound Blaster's OPL) with effects paired to it.
inline constexpr SoundCard kSoundCards[] = {
    {"auto",  "auto",         "auto"},
    {"sb",    "opl",          "sb-dac"},     // FM music + digital effects
    {"adlib", "opl",          "opl"},        // FM music + FM effects
    {"mt32",  "mt32-builtin", "mt32-sfx"},   // needs the user's ROMs
    {"gm",    "gm-builtin",   "gm-sfx"},     // needs a SoundFont
    {"midi",  "gm-host",      "midi"},       // an external MIDI device
    {"off",   "none",         "none"},
};

inline constexpr const char* kCustomSoundCard = "custom";

// The card whose pair is (music, sfx), else "custom".
inline std::string sound_card_for(const std::string& music,
                                  const std::string& sfx) {
    for (const SoundCard& c : kSoundCards)
        if (music == c.music && sfx == c.sfx) return c.id;
    return kCustomSoundCard;
}

// The pair for a card id; nullptr for an unknown id (including "custom").
inline const SoundCard* find_sound_card(const std::string& id) {
    for (const SoundCard& c : kSoundCards)
        if (id == c.id) return &c;
    return nullptr;
}

// What this machine can play beyond the always-present cards.
struct SoundCardAvail {
    bool mt32 = false;   // libmt32emu + a ROM pair
    bool gm = false;     // FluidSynth + a SoundFont
    bool midi = false;   // host MIDI built in + at least one output port
};

// The cards worth offering, in menu order: auto / sb / adlib / off always,
// the others only when `avail` says they will sound.
inline std::vector<std::string> available_sound_cards(
    const SoundCardAvail& avail) {
    std::vector<std::string> out;
    for (const SoundCard& c : kSoundCards) {
        const std::string id = c.id;
        if (id == "mt32" && !avail.mt32) continue;
        if (id == "gm" && !avail.gm) continue;
        if (id == "midi" && !avail.midi) continue;
        out.push_back(id);
    }
    return out;
}

}  // namespace olduvai::presentation
