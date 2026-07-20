// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// First-run table readers: level data that lives inside the game
// executable's data segment rather than the asset archives.
//
// File-offset mapping for a data-segment address:
//   file = 0x2000 + (0x2D99 - 0x1000) * 16 + ds_offset
// (8192-byte MZ header; load-base paragraph 0x1000; DGROUP paragraph
// 0x2D99 — verified against the relocation table.)
//
// Tile placement tables (levels 1, 3, 5, 7): per level, a counts array
// (one u16le per screen, stored as count-1) followed by flat records of
// {i16 x_raw, i16 y, u16 sprite_1based}.  x_raw's low nibble is the
// collision layer; the pixel x is the high bits re-aligned to 16.  The
// sprite index converts to 0-based.  19 playable screens (a 20th counts
// slot, where present, is overread garbage).
//
// Object spawn tables (levels 1/3/5/7 + global secret/cave): variable-
// stride records, first word = object type; type 0x0000 separates
// screens, 0x00FF terminates the table.  Strides confirmed from the
// reset/change-screen jump-table groupings.

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace olduvai::prepare {

class ExeTableError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ── executable build variants ───────────────────────────────────────────
// All table addresses in this module are data-segment offsets of the
// canonical (floppy) build.  Re-linked distributions of the same program
// exist: the CD-era build (shipped SQZ-packed by GOG and the 1995 Titus
// compilation) has a 481-paragraph MZ header instead of 512 and 30 extra
// bytes early in DGROUP, shifting every data offset by +30.  Known builds
// are recognised by the FNV-1a/64 digest of the (decompressed) image and
// mapped to a layout; unknown builds get the canonical layout, preserving
// the old behaviour.  Every reader below detects the layout itself, so
// callers pass exe bytes exactly as before.
struct ExeLayout {
    std::size_t header_size = 0x2000;  // MZ header bytes (e_cparhdr × 16)
    std::int32_t ds_delta = 0;         // shift applied to canonical offsets
    const char* variant = "canonical"; // for prepare-time logging
};

ExeLayout detect_exe_layout(const std::vector<std::uint8_t>& exe);

std::size_t ds_offset_to_file(std::uint32_t ds_offset);
std::size_t ds_offset_to_file(std::uint32_t ds_offset,
                              const ExeLayout& layout);

// ── tile placement ──────────────────────────────────────────────────────
struct TilePlacement {
    int sprite_idx = 0;  // 0-based
    int x = 0;           // pixel x (16-aligned)
    int y = 0;
    int layer = 0;       // DUR collision layer nibble
};

struct TileScreen {
    int screen = 0;
    std::vector<TilePlacement> tiles;
};

struct LevelTiles {
    int level = 0;
    std::vector<TileScreen> screens;  // 19 playable screens
};

// `level` ∈ {1, 3, 5, 7} (the DUR-bearing platform levels).
LevelTiles read_tile_table(const std::vector<std::uint8_t>& exe, int level);

// ── object spawn tables ─────────────────────────────────────────────────
struct ObjectRecord {
    std::uint16_t type = 0;
    std::vector<std::int16_t> words;  // record body after the type word
};

using ObjectScreens = std::vector<std::vector<ObjectRecord>>;

struct ObjectTableInfo {
    const char* name;
    std::uint32_t ds_offset;
};

// level1 / level3 / level5 / level7 / secret / cave.
const std::vector<ObjectTableInfo>& object_tables();

ObjectScreens read_object_table(const std::vector<std::uint8_t>& exe,
                                std::uint32_t ds_offset);

// Record stride (total bytes incl. type word) for an object type;
// throws ExeTableError for unknown types.
std::size_t object_record_size(std::uint16_t type);

// ── monster data tables (48-byte records in the data segment) ───────────
struct MonsterTable {
    int dat00 = 0;       // collision X offset (arrow position)
    int dat02 = 0;       // club reach, player facing right
    int di = 0;          // forward-probe X offset
    int si = 0;          // foot-probe Y offset
    int var18 = 0;       // detection width
    int var16 = 0;       // club reach, player facing left
    int score = 0;
    int ko_spr = 0;      // sprite bases converted 1-based → 0-based
    int init_spr = 0;
    int move_spr = 0;
    int away_spr = 0;
    int energy = 0;      // hits to KO
    int direction_flag = 0;
    std::array<int, 8> walk_offsets{};
};

MonsterTable read_monster_table(const std::vector<std::uint8_t>& exe,
                                std::uint32_t ds_offset);

struct MonsterTableRef {
    std::uint16_t type;        // object type id
    std::uint32_t ds_offset;
    std::uint32_t alt_ds_offset;  // 0 = none (L3A animation-phase table)
};

// The ten shared-state-machine monster types and their table locations.
const std::vector<MonsterTableRef>& monster_table_refs();

// The dark-woods cave layouts: two blocks of 32 records x 3 i16 words at
// the cave-table address (odd caves use block 0, even caves block +192).
// Records carry the nibble-packed layer in x; the sprite passes through
// the same alias chain as the surface tile renderer (operating on the
// raw 1-based index: 30->31, 29->30, 20->32, 5->29).
struct CaveTileRecord {
    int x = 0;        // 16-aligned
    int y = 0;
    int layer = 0;    // low nibble of raw x
    int final_si = 0; // post-alias 1-based sprite index
};

struct L3CaveLayouts {
    std::vector<CaveTileRecord> odd;    // caves 23, 25
    std::vector<CaveTileRecord> even;   // caves 22, 24
};

L3CaveLayouts read_l3_cave_tables(const std::vector<std::uint8_t>& exe);

// ── cave-width table ────────────────────────────────────────────────────
// 53 interior widths, one per cave index, composed from the three u16le
// runs in the data segment: L1 caves 0-21 (22 entries @ DS:0x7CE8), L5
// caves 26-38 (13 @ DS:0x7D14), L7 caves 39-52 (14 @ DS:0x7D2E).  The L3
// range 22-25 has no width run — the L3 cave exit edge is hardcoded, so
// those slots get an inert filler the runtime never reads.
std::array<int, 53> read_cave_size_table(const std::vector<std::uint8_t>& exe);

// ── secret-food score table (10 u16le @ DS:0x8094, by sprite number) ────
std::array<int, 10> read_secret_score_table(
    const std::vector<std::uint8_t>& exe);

// ── AdLib SFX voice records ─────────────────────────────────────────────
// One record per SFX: 28 u16le — modulator patch [13], carrier patch [13],
// modulator waveform, carrier waveform — at DS:0x80CC (generic), 0x810A
// (jump apex), 0x8148 (hit).  // FUN_1fe0_018b voice-install walk
struct AdlibSfxVoice {
    std::array<int, 13> mod{};
    std::array<int, 13> car{};
    int mod_wf = 0;
    int car_wf = 0;
};
struct AdlibSfxVoices {
    AdlibSfxVoice generic, jump_apex, hit;
};
AdlibSfxVoices read_adlib_sfx_voices(const std::vector<std::uint8_t>& exe);

}  // namespace olduvai::prepare
