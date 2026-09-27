// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/tile_patterns.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace olduvai::presentation::tile_patterns {

namespace {

// (x, y) → one 64-bit key.  Coordinates are small signed ints (tile grid).
std::uint64_t key(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

int sprite_h(const std::vector<formats::Sprite>& sprites, int idx) {
    if (idx < 0 || idx >= static_cast<int>(sprites.size())) return 0;
    return sprites[static_cast<std::size_t>(idx)].height;
}

int sprite_w(const std::vector<formats::Sprite>& sprites, int idx) {
    if (idx < 0 || idx >= static_cast<int>(sprites.size())) return 0;
    return sprites[static_cast<std::size_t>(idx)].width;
}

// Chain-membership sets over the placement list:
//   starts: every tile's top-left (x, y)
//   ends:   every tile's bottom-left (x, y + h)
// A tile is in a vertical column iff it has a neighbour
//   below: some tile starts at (t.x, t.y + h(t))   -> in starts
//   above: some tile ends at (t.x, t.y)            -> in ends
// The above test uses the neighbour's height (mixed-height columns like the
// giant trunk's 42/41 pair).
struct ChainSets {
    std::unordered_set<std::uint64_t> starts, ends;

    ChainSets(const std::vector<TileDraw>& tiles,
              const std::vector<formats::Sprite>& sprites) {
        starts.reserve(tiles.size() * 2);
        ends.reserve(tiles.size() * 2);
        for (const auto& t : tiles) {
            const int h = sprite_h(sprites, t.sprite_idx);
            if (h <= 0) continue;
            starts.insert(key(t.x, t.y));
            ends.insert(key(t.x, t.y + h));
        }
    }

};

}  // namespace

void extend_columns_to_top(std::vector<TileDraw>& tiles,
                           const std::vector<formats::Sprite>& sprites,
                           int top_band) {
    const ChainSets chain(tiles, sprites);
    // (x, y) start → tile index, for walking a column's chain downward.
    std::unordered_map<std::uint64_t, std::size_t> start_at;
    start_at.reserve(tiles.size() * 2);
    for (std::size_t i = 0; i < tiles.size(); ++i)
        start_at.emplace(key(tiles[i].x, tiles[i].y), i);
    std::vector<TileDraw> add;
    for (const auto& t : tiles) {
        // Only a column's TOP tile in the band qualifies: y in (0, top_band]
        // (y <= 0 already covers row 0) and no chain neighbour above.
        if (t.y <= 0 || t.y > top_band) continue;
        const int h = sprite_h(sprites, t.sprite_idx);
        if (h <= 0) continue;
        if (chain.ends.count(key(t.x, t.y)) != 0) continue;   // not the top
        // Read the column's sprite sequence downward and continue its period:
        // the giant trunk alternates 24/25, so upward tile k reuses chain[k mod
        // p] (p = 2 when the first two alternate, else 1).
        std::vector<std::size_t> down;   // tile indices, top → bottom
        {
            const TileDraw* cur = &t;
            while (true) {
                const int ch = sprite_h(sprites, cur->sprite_idx);
                auto it = start_at.find(key(cur->x, cur->y + ch));
                if (ch <= 0 || it == start_at.end()) break;
                down.push_back(it->second);
                cur = &tiles[it->second];
            }
        }
        if (down.empty()) continue;   // lone tile — bush/platform, leave it
        const int spr_below = tiles[down.front()].sprite_idx;
        // A repeating column's tiles share a width.  A coincidental adjacency
        // (the S15 door, 48 wide, ending where a 16-wide beam row starts) is
        // not a column.
        if (sprite_w(sprites, spr_below) != sprite_w(sprites, t.sprite_idx))
            continue;
        const int period = (spr_below == t.sprite_idx) ? 1 : 2;
        int y = t.y;
        for (int k = 1;; ++k) {
            const int spr = (period == 2 && (k % 2) == 1) ? spr_below
                                                          : t.sprite_idx;
            const int hs = sprite_h(sprites, spr);
            if (hs <= 0) break;
            y -= hs;
            if (y + hs <= 0) break;   // fully above the screen — done
            add.push_back({spr, t.x, y});
        }
    }
    tiles.insert(tiles.end(), add.begin(), add.end());
}

std::vector<TileDraw> seam_row_bridges(
    const std::vector<TileDraw>& a_tiles, int a_backdrop,
    const std::vector<TileDraw>& b_tiles, int b_backdrop,
    const std::vector<formats::Sprite>& sprites) {
    std::vector<TileDraw> out;
    // Level tiles only: backdrop rows neither form a bridgeable row nor fill a
    // hole (the backdrop showing through is the hole).
    const auto level = [](const std::vector<TileDraw>& v, int bdc) {
        std::vector<TileDraw> o;
        for (std::size_t i = static_cast<std::size_t>(std::max(0, bdc));
             i < v.size(); ++i)
            o.push_back(v[i]);
        return o;
    };
    const std::vector<TileDraw> a_lvl = level(a_tiles, a_backdrop);
    const std::vector<TileDraw> b_lvl = level(b_tiles, b_backdrop);
    // Continuous seam coordinates: A at its own x, B shifted +320.
    auto covered = [&](int gx0, int gx1, int y, int h) {
        auto covers = [&](const TileDraw& t, int shift) {
            const int w2 = sprite_w(sprites, t.sprite_idx);
            const int h2 = sprite_h(sprites, t.sprite_idx);
            return t.x + shift <= gx0 && gx1 <= t.x + shift + w2 &&
                   t.y <= y && y + h <= t.y + h2;
        };
        return std::any_of(a_lvl.begin(), a_lvl.end(),
                           [&](const TileDraw& t) { return covers(t, 0); }) ||
               std::any_of(b_lvl.begin(), b_lvl.end(),
                           [&](const TileDraw& t) { return covers(t, 320); });
    };
    auto run_of_two = [&](const std::vector<TileDraw>& tiles, int spr, int x,
                          int y, int w, int dir) {
        return std::any_of(tiles.begin(), tiles.end(), [&](const TileDraw& t) {
            return t.sprite_idx == spr && t.y == y && t.x == x + dir * w;
        });
    };
    for (const auto& t : a_lvl) {
        const int w = sprite_w(sprites, t.sprite_idx);
        const int h = sprite_h(sprites, t.sprite_idx);
        if (w <= 0 || h <= 0) continue;
        if (t.y + h > 150) continue;             // ground band — walkable
        const int end = t.x + w;
        if (end < 320 - 2 * w || end >= 320) continue;   // not seam-near
        // Only the run's LAST tile emits the bridge (an interior tile of the
        // same run would re-emit the same fill, duplicated).
        if (run_of_two(a_lvl, t.sprite_idx, t.x, t.y, w, +1)) continue;
        // Nearest same-row partner on B's side of the seam.
        int b_start = 1 << 28;
        for (const auto& u : b_lvl)
            if (u.sprite_idx == t.sprite_idx && u.y == t.y &&
                u.x + 320 > end)
                b_start = std::min(b_start, u.x + 320);
        if (b_start == 1 << 28 || b_start - end > 3 * w) continue;
        // Contiguity: this end tile continues a stride-w run on at least
        // one side (A leftward, or B rightward from the partner).
        if (!run_of_two(a_lvl, t.sprite_idx, t.x, t.y, w, -1) &&
            !run_of_two(b_lvl, t.sprite_idx, b_start - 320, t.y, w, +1))
            continue;
        for (int x = end; x + w <= b_start; x += w) {
            if (covered(x, x + w, t.y, h)) continue;
            out.push_back({t.sprite_idx, x, t.y});
        }
    }
    return out;
}

std::vector<TileDraw> seam_straddling_tiles(
    const std::vector<TileDraw>& tiles,
    const std::vector<formats::Sprite>& sprites, bool right_edge) {
    std::vector<TileDraw> out;
    for (const auto& t : tiles) {
        const int w = sprite_w(sprites, t.sprite_idx);
        if (w <= 0) continue;
        const bool straddle = right_edge ? (t.x < 320 && t.x + w > 320)
                                         : (t.x < 0 && t.x + w > 0);
        if (straddle) out.push_back(t);
    }
    return out;
}

}  // namespace olduvai::presentation::tile_patterns
