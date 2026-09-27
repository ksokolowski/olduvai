// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Content-addressed per-asset HD upscale cache: each unique RGBA block (sprite
// or background, post-palette) is upscaled once.  An optional disk layer
// stores blocks under the same key (FNV of src bytes, w, h, scale, profile);
// a hit reproduces the upscale exactly.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace olduvai::enhance {

struct HdAsset {
    std::vector<std::uint8_t> px;   // w*h*4 RGBA
    int w = 0;
    int h = 0;
};

class HdAssetCache {
 public:
    HdAssetCache() = default;
    // Movable, so a level's data can be replaced whole (the display reinit);
    // never while a warm pass runs.  Not copyable: a copy would double the
    // upscaled blocks.
    HdAssetCache(HdAssetCache&& o) noexcept { *this = std::move(o); }
    HdAssetCache& operator=(HdAssetCache&& o) noexcept {
        map_ = std::move(o.map_);
        source_keys_ = std::move(o.source_keys_);
        disk_dir_ = std::move(o.disk_dir_);
        disk_enabled_ = o.disk_enabled_;
        disk_loads_.store(o.disk_loads_.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
        disk_stores_.store(o.disk_stores_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
        return *this;
    }
    HdAssetCache(const HdAssetCache&) = delete;
    HdAssetCache& operator=(const HdAssetCache&) = delete;
    ~HdAssetCache() = default;

    // src: native RGBA (w*h*4), alpha 0 = transparent, upscaled with `profile`
    // (upscale_rgba); scale 1 returns a copy.  bleed (default): extend opaque
    // colour into transparent borders first so kernels do not blend undefined
    // pixels; pass false when the source already has a defined RGB under alpha
    // 0 (the fluid bubbles' water blue).
    const HdAsset& get(const std::vector<std::uint8_t>& src, int w, int h,
                       int scale, const std::string& profile, bool bleed = true);
    // get() for a source that is costly to build: `source_key` must cover
    // everything make_src() reads, and the scale, profile and bleed.  A hit
    // returns the asset without calling make_src() (no decode, no hash of
    // the pixels); a miss builds the source once and remembers its key.
    template <typename MakeSrc>
    const HdAsset& get_by_source(std::uint64_t source_key, MakeSrc&& make_src,
                                 int w, int h, int scale,
                                 const std::string& profile, bool bleed = true) {
        const auto known = source_keys_.find(source_key);
        if (known != source_keys_.end()) {
            const auto it = map_.find(known->second);
            if (it != map_.end()) return it->second;
        }
        const std::vector<std::uint8_t> src = make_src();
        const std::uint64_t k = key_for(src, w, h, scale, profile, bleed);
        source_keys_[source_key] = k;
        return get_keyed(k, src, w, h, scale, profile, bleed);
    }

    void clear();
    std::size_t size() const { return map_.size(); }

    // ---- Batch interface, for warming ----
    // key / build / insert separately: a warm pass runs build() on many threads
    // while the map stays single-threaded, so get() needs no lock.
    static std::uint64_t key_for(const std::vector<std::uint8_t>& src, int w,
                                 int h, int scale, const std::string& profile,
                                 bool bleed = true);
    bool contains(std::uint64_t k) const { return map_.find(k) != map_.end(); }

    // Exactly what get() would produce, without touching the map.  Thread-safe:
    // reads its arguments and the disk config (enable_disk() must not race).
    HdAsset build(const std::vector<std::uint8_t>& src, int w, int h, int scale,
                  const std::string& profile, bool bleed = true) const;

    // Adopt a prebuilt asset; false if the key exists (the existing entry wins,
    // so a blit holding a reference never sees it change).
    bool insert(std::uint64_t k, HdAsset a);

    // Disk persistence under `dir` (created if absent); empty disables (the
    // default, in-memory only).  Cosmetic data: a hit only skips an upscale.
    void enable_disk(const std::filesystem::path& dir);

    // Test/diagnostic counters for the disk layer.
    std::size_t disk_loads() const { return disk_loads_.load(); }
    std::size_t disk_stores() const { return disk_stores_.load(); }

 private:
    // The key + build half of get(), so the per-frame path hashes `src` once
    // (256 kB for a full background).
    HdAsset build_with_key(std::uint64_t k, const std::vector<std::uint8_t>& src,
                           int w, int h, int scale, const std::string& profile,
                           bool bleed) const;

    const HdAsset& get_keyed(std::uint64_t k,
                             const std::vector<std::uint8_t>& src, int w, int h,
                             int scale, const std::string& profile, bool bleed);

    std::unordered_map<std::uint64_t, HdAsset> map_;
    std::unordered_map<std::uint64_t, std::uint64_t> source_keys_;  // -> map_ key
    std::filesystem::path disk_dir_;   // empty = disk layer off
    bool disk_enabled_ = false;
    // Atomic only because build() is callable from several warm threads at
    // once.  Diagnostics, so relaxed ordering is all they need.
    mutable std::atomic<std::size_t> disk_loads_{0};
    mutable std::atomic<std::size_t> disk_stores_{0};
};

}  // namespace olduvai::enhance
