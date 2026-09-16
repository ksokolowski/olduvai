// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/hd_warm.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "enhance/parallel_rows.hpp"
#include "presentation/render/game_render.hpp"   // sprite_to_rgba

namespace olduvai::presentation {

namespace {

// The RGBA the cache keys on is produced by sprite_to_rgba() — the SAME
// function blit_sprite's HD path calls, not a copy of it.  It has to be the
// same body: a warm that hashes even slightly different bytes writes entries
// under keys no blit ever asks for, which is 100% wasted work that ALSO leaves
// the hitch exactly where it was, with nothing visibly wrong to see it by.
// A copy could only be kept in step by a comment saying so, which is the shape
// this tree's most expensive failures have all had (BACKLOG.md §1).

// Run body(i) over [0, n) across `threads` participants, the caller being one
// of them.  A private fan-out rather than enhance::parallel_rows for two
// reasons: that one is ROW-shaped, and its pool holds exactly ONE task's state
// (body_/h_/pending_), so entering it from several threads at once corrupts it.
//
// The index is an atomic counter rather than a fixed stride because sprite
// areas vary by an order of magnitude and a static split leaves threads idle on
// the tail.  That makes COMPLETION ORDER non-deterministic, which is fine here
// in a way it would not be for a scaler band split: every job writes only its
// own slot, and the slots are consumed in fixed index order afterwards, so
// nothing observable depends on who finished first.
void parallel_indices(std::size_t n, int threads,
                      const std::function<void(std::size_t)>& body) {
    if (n == 0) return;
    if (threads <= 1 || n == 1) {
        for (std::size_t i = 0; i < n; ++i) body(i);
        return;
    }
    std::atomic<std::size_t> next{0};
    const auto drain = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) return;
            body(i);
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads) - 1);
    for (int t = 1; t < threads; ++t) workers.emplace_back(drain);
    drain();                                   // the caller is participant 0
    for (auto& t : workers) t.join();
}

// The inner scalers call enhance::parallel_rows, whose pool is not reentrant.
// Today every sprite is below kMinRowsToSplit and takes the early return before
// any locking, so nesting would not actually fire — but that is luck, not a
// design, and one 32-row sprite would turn it into a corruption bug that only
// reproduces under load.  Turn the row split off for the duration instead.
// Nothing is lost: 16-32 px art was never eligible for it, and per-sprite
// parallelism is the better axis for this work anyway.
//
// Safe to touch a global here because the warm runs on the loading screen, with
// no other thread rendering.
struct SerialRowsDuringWarm {
    const bool prev = enhance::parallel_rows_enabled();
    SerialRowsDuringWarm() { enhance::set_parallel_rows_enabled(false); }
    ~SerialRowsDuringWarm() { enhance::set_parallel_rows_enabled(prev); }
    SerialRowsDuringWarm(const SerialRowsDuringWarm&) = delete;
    SerialRowsDuringWarm& operator=(const SerialRowsDuringWarm&) = delete;
};

struct Source {
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    std::uint64_t key = 0;
};

}  // namespace

std::size_t warm_hd_sprite_cache(enhance::HdAssetCache& cache,
                                 const std::vector<formats::Sprite>& sprites,
                                 const std::vector<formats::Rgb>& pal,
                                 int scale, const std::string& profile) {
    if (scale <= 1) return 0;   // classic: the cache is not on this path at all

    // Both orientations, because blit_sprite applies flip_h BEFORE hashing, so
    // a left-facing and a right-facing sprite are two distinct cache entries.
    std::vector<std::pair<std::size_t, bool>> requests;
    requests.reserve(sprites.size() * 2);
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        if (sprites[i].width <= 0 || sprites[i].height <= 0) continue;
        requests.emplace_back(i, false);
        requests.emplace_back(i, true);
    }
    if (requests.empty()) return 0;

    // Read BEFORE the guard below flips it.  Reusing that flag rather than
    // adding a second knob: it already means "no threading anywhere in the HD
    // path", and it is what test_hd_warm flips to compute the cache both ways
    // in one process — the same trick test_upscale_threading uses, and the only
    // way to gate "threaded == serial" as a byte comparison rather than a hope.
    const int threads =
        enhance::parallel_rows_enabled() ? enhance::parallel_row_threads() : 1;
    const SerialRowsDuringWarm serial_rows;

    // Phase 1 (parallel): decode + palette-map + hash.  Pure per request.
    std::vector<Source> sources(requests.size());
    parallel_indices(requests.size(), threads, [&](std::size_t i) {
        const formats::Sprite& s = sprites[requests[i].first];
        Source& out = sources[i];
        out.rgba = sprite_to_rgba(s, pal, requests[i].second);
        out.w = s.width;
        out.h = s.height;
        out.key = enhance::HdAssetCache::key_for(out.rgba, out.w, out.h, scale,
                                                 profile);
    });

    // Phase 2 (serial, cheap): drop duplicates and anything already cached.
    // Duplicates are common — a symmetric sprite hashes identically both ways,
    // and sheets repeat frames — and upscaling one twice would be pure waste.
    std::vector<std::size_t> todo;
    std::unordered_set<std::uint64_t> seen;
    todo.reserve(sources.size());
    seen.reserve(sources.size() * 2);
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (cache.contains(sources[i].key)) continue;
        if (!seen.insert(sources[i].key).second) continue;
        todo.push_back(i);
    }

    // Phase 3 (parallel): the expensive one.  build() never touches the map.
    std::vector<enhance::HdAsset> built(todo.size());
    parallel_indices(todo.size(), threads, [&](std::size_t j) {
        const Source& s = sources[todo[j]];
        built[j] = cache.build(s.rgba, s.w, s.h, scale, profile);
    });

    // Phase 4 (serial): the map is mutated here and nowhere else, which is what
    // keeps the per-frame blit path lock-free.
    const std::size_t before = cache.size();
    for (std::size_t j = 0; j < todo.size(); ++j)
        cache.insert(sources[todo[j]].key, std::move(built[j]));
    return cache.size() - before;
}

}  // namespace olduvai::presentation
