// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/exe_tables.hpp"

#include <array>

namespace olduvai::prepare {

namespace {

constexpr std::size_t kHeaderSize = 0x2000;
constexpr std::uint32_t kLoadBaseSeg = 0x1000;
constexpr std::uint32_t kDsSeg = 0x2D99;
constexpr int kPlayableScreens = 19;

std::uint16_t u16(const std::vector<std::uint8_t>& d, std::size_t p) {
    if (p + 2 > d.size()) throw ExeTableError("read past end of executable");
    return static_cast<std::uint16_t>(d[p] | (d[p + 1] << 8));
}

std::int16_t i16(const std::vector<std::uint8_t>& d, std::size_t p) {
    return static_cast<std::int16_t>(u16(d, p));
}

struct TileTableInfo {
    int level;
    std::uint32_t count_ds;
    std::uint32_t records_ds;
};

constexpr std::array<TileTableInfo, 4> kTileTables = {{
    {1, 0x2076, 0x209E},
    {3, 0x39AA, 0x39D2},
    {5, 0x4E2A, 0x4E50},
    {7, 0x5E36, 0x5E5E},
}};

// Record stride by object type (total bytes, incl. the type word) —
// format-structure metadata: the record sizes the original's table walker
// consumes, confirmed from the reset/change-screen jump-table groupings.
// // FUN_21f3_006f + per-level walkers (interoperability metadata, not a
// copied data block: without the strides the user's own tables cannot be
// parsed).
constexpr int kSizes[0x28] = {
    /*00*/ 2,  /*01*/ 8,  /*02*/ 6,  /*03*/ 8,  /*04*/ 8,  /*05*/ 12,
    /*06*/ 10, /*07*/ 10, /*08*/ 14, /*09*/ 10, /*0a*/ 26, /*0b*/ 26,
    /*0c*/ 26, /*0d*/ 26, /*0e*/ 14, /*0f*/ 18, /*10*/ 16, /*11*/ 8,
    /*12*/ 8,  /*13*/ 8,  /*14*/ 10, /*15*/ 8,  /*16*/ 8,  /*17*/ 12,
    /*18*/ 10, /*19*/ 8,  /*1a*/ 26, /*1b*/ 26, /*1c*/ 8,  /*1d*/ 8,
    /*1e*/ 12, /*1f*/ 12, /*20*/ 14, /*21*/ 26, /*22*/ 26, /*23*/ 18,
    /*24*/ 14, /*25*/ 6,  /*26*/ 26, /*27*/ 26,
};

constexpr std::uint16_t kSentinel = 0x00FF;
constexpr std::uint16_t kSeparator = 0x0000;

// FNV-1a/64 over an in-memory image (same digest family the prepare cache
// key uses for files).
std::uint64_t fnv1a64(const std::vector<std::uint8_t>& d) {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint8_t b : d) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

struct KnownBuild {
    std::uint64_t fnv;
    std::int32_t ds_delta;
    const char* variant;
};

// Recognised builds of the game executable.  Digests are identities, not
// content.  ds_delta values verified by byte-comparing every table this
// module reads across builds (all 18 read sites identical at the delta).
constexpr KnownBuild kKnownBuilds[] = {
    {0x37e6f8230ca02d6cull, 0, "floppy"},
    // CD re-link (GOG / 1995 compilation, decompressed from PREH.SQZ):
    // 30 bytes inserted early in DGROUP shift all data offsets.
    {0xb594796417028cdfull, 30, "cd"},
};

}  // namespace

ExeLayout detect_exe_layout(const std::vector<std::uint8_t>& exe) {
    ExeLayout layout;
    // Header size straight from the MZ header (e_cparhdr, paragraphs).
    if (exe.size() >= 10 && exe[0] == 'M' && exe[1] == 'Z') {
        const std::size_t paras =
            static_cast<std::size_t>(exe[8]) | (static_cast<std::size_t>(exe[9]) << 8);
        if (paras > 0) layout.header_size = paras * 16;
    }
    const std::uint64_t fnv = fnv1a64(exe);
    for (const auto& b : kKnownBuilds) {
        if (b.fnv == fnv) {
            layout.ds_delta = b.ds_delta;
            layout.variant = b.variant;
            break;
        }
    }
    return layout;
}

std::size_t ds_offset_to_file(std::uint32_t ds_offset) {
    return kHeaderSize + (kDsSeg - kLoadBaseSeg) * 16 + ds_offset;
}

std::size_t ds_offset_to_file(std::uint32_t ds_offset,
                              const ExeLayout& layout) {
    return layout.header_size + (kDsSeg - kLoadBaseSeg) * 16 +
           static_cast<std::size_t>(
               static_cast<std::int64_t>(ds_offset) + layout.ds_delta);
}

LevelTiles read_tile_table(const std::vector<std::uint8_t>& exe, int level) {
    const TileTableInfo* info = nullptr;
    for (const auto& t : kTileTables) {
        if (t.level == level) info = &t;
    }
    if (info == nullptr) throw ExeTableError("no tile table for this level");

    const ExeLayout layout = detect_exe_layout(exe);
    const std::size_t count_off = ds_offset_to_file(info->count_ds, layout);
    const std::size_t records_off =
        ds_offset_to_file(info->records_ds, layout);
    const std::size_t n_entries = (records_off - count_off) / 2;
    const std::size_t n_real =
        n_entries < kPlayableScreens ? n_entries : kPlayableScreens;

    LevelTiles out;
    out.level = level;
    std::size_t flat = 0;
    for (std::size_t s = 0; s < n_real; ++s) {
        TileScreen scr;
        scr.screen = static_cast<int>(s);
        const int count = u16(exe, count_off + s * 2) + 1;  // stored count-1
        for (int i = 0; i < count; ++i, ++flat) {
            const std::size_t rec = records_off + flat * 6;
            const std::int16_t x_raw = i16(exe, rec);
            TilePlacement tp;
            tp.y = i16(exe, rec + 2);
            tp.sprite_idx = u16(exe, rec + 4) - 1;  // 1-based on disk
            tp.layer = x_raw & 0x0F;
            // Clear the packed low nibble, keeping the sign (negative x =
            // off-screen tiles).  & ~0x0F, not (>>4)<<4 — bit-identical on
            // two's complement, but shifting a negative value left is UB
            // (caught by the OLDUVAI_SANITIZE lane; byte-identity of the
            // resulting tile lists is pinned by test_screen_tiles).
            tp.x = x_raw & ~0x0F;
            scr.tiles.push_back(tp);
        }
        out.screens.push_back(std::move(scr));
    }
    return out;
}

const std::vector<ObjectTableInfo>& object_tables() {
    static const std::vector<ObjectTableInfo> kTables = {
        {"level1", 0x33C2}, {"level3", 0x47EE}, {"level5", 0x5840},
        {"level7", 0x74FC}, {"secret", 0x2950}, {"cave", 0x29E2},
    };
    return kTables;
}

std::size_t object_record_size(std::uint16_t type) {
    if (type >= 0x28) throw ExeTableError("unknown object type");
    return static_cast<std::size_t>(kSizes[type]);
}

MonsterTable read_monster_table(const std::vector<std::uint8_t>& exe,
                                std::uint32_t ds_offset) {
    const std::size_t off = ds_offset_to_file(ds_offset, detect_exe_layout(exe));
    if (off + 48 > exe.size()) {
        throw ExeTableError("executable too short for monster table");
    }
    auto sw = [&](int i) {
        return static_cast<int>(i16(exe, off + static_cast<std::size_t>(i) * 2));
    };
    auto uw = [&](int i) {
        return static_cast<int>(u16(exe, off + static_cast<std::size_t>(i) * 2));
    };
    MonsterTable t;
    t.dat00 = sw(0);
    t.dat02 = sw(1);
    t.di = sw(3);
    t.si = sw(4);
    t.var18 = sw(5);
    t.var16 = sw(6);
    t.score = uw(7);
    // Sprite indices on disk are 1-based.
    t.ko_spr = uw(8) - 1;
    t.init_spr = uw(9) - 1;
    t.move_spr = uw(11) - 1;
    t.away_spr = uw(13) - 1;
    t.energy = uw(14);
    t.direction_flag = uw(15);
    for (int i = 0; i < 8; ++i) {
        t.walk_offsets[static_cast<std::size_t>(i)] = sw(16 + i);
    }
    return t;
}

const std::vector<MonsterTableRef>& monster_table_refs() {
    static const std::vector<MonsterTableRef> kRefs = {
        {0x0A, 0x2890, 0}, {0x0B, 0x28C0, 0}, {0x0C, 0x28F0, 0},
        {0x0D, 0x2920, 0}, {0x1A, 0x475E, 0x478E}, {0x1B, 0x47BE, 0},
        {0x21, 0x57E0, 0}, {0x22, 0x5810, 0}, {0x26, 0x749C, 0},
        {0x27, 0x74CC, 0},
    };
    return kRefs;
}

ObjectScreens read_object_table(const std::vector<std::uint8_t>& exe,
                                std::uint32_t ds_offset) {
    std::size_t pos = ds_offset_to_file(ds_offset, detect_exe_layout(exe));
    ObjectScreens screens;
    std::vector<ObjectRecord> current;

    while (pos + 1 < exe.size()) {
        const std::uint16_t type = u16(exe, pos);
        if (type == kSentinel) {
            screens.push_back(std::move(current));
            break;
        }
        if (type == kSeparator) {
            screens.push_back(std::move(current));
            current = {};
            pos += 2;
            continue;
        }
        const std::size_t size = object_record_size(type);
        ObjectRecord rec;
        rec.type = type;
        const std::size_t n_words = (size - 2) / 2;
        for (std::size_t i = 0; i < n_words; ++i) {
            rec.words.push_back(i16(exe, pos + 2 + i * 2));
        }
        current.push_back(std::move(rec));
        pos += size;
    }
    return screens;
}

L3CaveLayouts read_l3_cave_tables(const std::vector<std::uint8_t>& exe) {
    L3CaveLayouts out;
    const std::size_t base = ds_offset_to_file(0x7E6A, detect_exe_layout(exe));
    if (base + 384 > exe.size()) return out;
    auto decode = [&](std::size_t off) {
        std::vector<CaveTileRecord> recs;
        for (int i = 0; i < 32; ++i) {
            const std::size_t o = off + static_cast<std::size_t>(i) * 6;
            const auto x_raw = static_cast<std::int16_t>(
                exe[o] | (exe[o + 1] << 8));
            const auto y = static_cast<std::int16_t>(
                exe[o + 2] | (exe[o + 3] << 8));
            int si = exe[o + 4] | (exe[o + 5] << 8);
            if (si == 30) si = 31;
            if (si == 29) si = 30;
            if (si == 20) si = 32;
            if (si == 5) si = 29;
            CaveTileRecord r;
            r.x = (x_raw >> 4) << 4;
            r.y = y;
            r.layer = x_raw & 0xF;
            r.final_si = si;
            recs.push_back(r);
        }
        return recs;
    };
    out.odd = decode(base);
    out.even = decode(base + 192);
    return out;
}

std::array<int, 53> read_cave_size_table(const std::vector<std::uint8_t>& exe) {
    const ExeLayout layout = detect_exe_layout(exe);
    std::array<int, 53> out{};
    // The L3 slots (22-25) are never read at runtime (hardcoded exit edge);
    // fill with the mid-size width so the array has no surprising zeros.
    out.fill(224);
    auto run = [&](std::uint32_t ds, int first, int count) {
        const std::size_t base = ds_offset_to_file(ds, layout);
        for (int i = 0; i < count; ++i)
            out[static_cast<std::size_t>(first + i)] =
                u16(exe, base + static_cast<std::size_t>(i) * 2);
    };
    run(0x7CE8, 0, 22);    // L1 caves 0-21
    run(0x7D14, 26, 13);   // L5 caves 26-38
    run(0x7D2E, 39, 14);   // L7 caves 39-52
    return out;
}

std::array<int, 10> read_secret_score_table(
    const std::vector<std::uint8_t>& exe) {
    const ExeLayout layout = detect_exe_layout(exe);
    const std::size_t base = ds_offset_to_file(0x8094, layout);
    std::array<int, 10> out{};
    for (int i = 0; i < 10; ++i)
        out[static_cast<std::size_t>(i)] =
            u16(exe, base + static_cast<std::size_t>(i) * 2);
    return out;
}

AdlibSfxVoices read_adlib_sfx_voices(const std::vector<std::uint8_t>& exe) {
    const ExeLayout layout = detect_exe_layout(exe);
    auto voice = [&](std::uint32_t ds) {
        const std::size_t base = ds_offset_to_file(ds, layout);
        AdlibSfxVoice v;
        for (int i = 0; i < 13; ++i)
            v.mod[static_cast<std::size_t>(i)] =
                u16(exe, base + static_cast<std::size_t>(i) * 2);
        for (int i = 0; i < 13; ++i)
            v.car[static_cast<std::size_t>(i)] =
                u16(exe, base + 26 + static_cast<std::size_t>(i) * 2);
        v.mod_wf = u16(exe, base + 52);
        v.car_wf = u16(exe, base + 54);
        return v;
    };
    AdlibSfxVoices out;
    out.generic = voice(0x80CC);
    out.jump_apex = voice(0x810A);
    out.hit = voice(0x8148);
    return out;
}

}  // namespace olduvai::prepare
