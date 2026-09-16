// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Content-addressed per-asset HD upscale cache.  Each unique RGBA block
// (sprite or background, post-palette) is upscaled once and reused.  An
// optional DISK layer persists each upscaled block under the platform cache
// dir, so enhanced-mode startup is fast after the first prepare: a fresh run
// loads the baked block instead of re-running OmniScale.
//
// The disk layer is purely a cache of the in-memory pipeline's output — the
// same key (FNV of src bytes,w,h,scale,profile) names both the map entry and
// the file, and a disk hit reproduces exactly what an upscale would have
// produced.  It never changes rendered output; it only avoids recompute.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace olduvai::enhance {

struct HdAsset {
    std::vector<std::uint8_t> px;   // w*h*4 RGBA
    int w = 0;
    int h = 0;
};

class HdAssetCache {
 public:
    // src: native RGBA (w*h*4), alpha 0 = transparent.  profile "mmpx" uses
    // MMPX, anything else OmniScale.  scale 1 returns an identity copy.
    // Transparent-bordered sources are alpha-edge-extended before upscaling
    // so scaler kernels don't bleed undefined pixels into the edges; the
    // upscaled alpha mask is re-applied so the shape stays crisp.
    // bleed: alpha-edge-extend transparent borders before scaling (default).
    // Pass false when the source already carries a defined RGB under its
    // alpha-0 pixels (e.g. the fluid bubbles keep the water-blue bg colour) —
    // then OmniScale blends toward that colour for crisp edges instead of the
    // bled opaque colour fattening thin shapes.
    const HdAsset& get(const std::vector<std::uint8_t>& src, int w, int h,
                       int scale, const std::string& profile, bool bleed = true);
    void clear();
    std::size_t size() const { return map_.size(); }

    // ── Batch interface, for warming ────────────────────────────────────
    // get() fuses key -> lookup -> build -> insert.  That is right for the
    // per-frame path and useless for a warm pass: there is no way to compute
    // an entry without mutating the map, so a warm could not be threaded
    // without a lock, and the only place to put one is the path get() is on
    // — the per-frame blit.  Splitting the phases moves the lock out of the
    // problem entirely: a caller runs the EXPENSIVE phase (build) on many
    // threads and keeps the map strictly single-threaded.  get() below is
    // rewritten in terms of these and still takes no lock.
    static std::uint64_t key_for(const std::vector<std::uint8_t>& src, int w,
                                 int h, int scale, const std::string& profile,
                                 bool bleed = true);
    bool contains(std::uint64_t k) const { return map_.find(k) != map_.end(); }

    // Produce exactly what get() would have produced, without touching the
    // map.  THREAD-SAFE: reads only its arguments and the disk config, which
    // enable_disk() must not be racing; the only shared writes are the atomic
    // diagnostic counters.
    HdAsset build(const std::vector<std::uint8_t>& src, int w, int h, int scale,
                  const std::string& profile, bool bleed = true) const;

    // Adopt a prebuilt asset.  False if the key was already present — the
    // existing entry wins, so a late insert can never change what a blit that
    // already holds a reference sees.
    bool insert(std::uint64_t k, HdAsset a);

    // Enable the disk persistence layer, writing/reading baked blocks under
    // `dir` (created if absent).  When unset (the default) the cache is
    // in-memory only — gameplay-faithful by construction.  Stage-2 HD assets
    // are cosmetic, so a disk hit can never affect logic; it only skips an
    // upscale.  Pass an empty path to disable.
    void enable_disk(const std::filesystem::path& dir);

    // Test/diagnostic counters for the disk layer.
    std::size_t disk_loads() const { return disk_loads_.load(); }
    std::size_t disk_stores() const { return disk_stores_.load(); }

 private:
    // Shared key+build half of get(), so the per-frame path hashes `src` ONCE
    // rather than once for the lookup and again inside build().  That hash
    // walks every source byte, which is 256 kB for a full background.
    HdAsset build_with_key(std::uint64_t k, const std::vector<std::uint8_t>& src,
                           int w, int h, int scale, const std::string& profile,
                           bool bleed) const;

    std::unordered_map<std::uint64_t, HdAsset> map_;
    std::filesystem::path disk_dir_;   // empty = disk layer off
    bool disk_enabled_ = false;
    // Atomic only because build() is callable from several warm threads at
    // once.  Diagnostics, so relaxed ordering is all they need.
    mutable std::atomic<std::size_t> disk_loads_{0};
    mutable std::atomic<std::size_t> disk_stores_{0};
};

}  // namespace olduvai::enhance
