// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/hd_asset_cache.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include "enhance/upscale.hpp"

namespace olduvai::enhance {

namespace {
// FNV-1a 64-bit over the source bytes, then mix in w/h/scale/profile.
std::uint64_t key_of(const std::vector<std::uint8_t>& src, int w, int h,
                     int scale, const std::string& profile) {
    std::uint64_t k = 1469598103934665603ull;
    for (std::uint8_t b : src) { k ^= b; k *= 1099511628211ull; }
    auto mix = [&](std::uint64_t v) {
        k ^= v + 0x9e3779b97f4a7c15ull + (k << 6) + (k >> 2);
    };
    mix(static_cast<std::uint64_t>(w));
    mix(static_cast<std::uint64_t>(h));
    mix(static_cast<std::uint64_t>(scale));
    for (char c : profile) mix(static_cast<std::uint64_t>(c));
    return k;
}

// Extend opaque colour one ring into transparent neighbours (4-neighbour),
// leaving alpha 0 — only RGB is filled so the scaler reads a defined colour
// at the edge.  Returns a copy with the bled RGB; alpha untouched.
std::vector<std::uint8_t> alpha_bleed(const std::vector<std::uint8_t>& src,
                                      int w, int h) {
    std::vector<std::uint8_t> out = src;
    auto idx = [&](int x, int y) {
        return (static_cast<std::size_t>(y) * w + x) * 4;
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t o = idx(x, y);
            if (src[o + 3] != 0) continue;          // already opaque
            const int dx[4] = {-1, 1, 0, 0};
            const int dy[4] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                const std::size_t no = idx(nx, ny);
                if (src[no + 3] == 0) continue;     // neighbour also transparent
                out[o] = src[no]; out[o + 1] = src[no + 1];
                out[o + 2] = src[no + 2];            // RGB only; alpha stays 0
                break;
            }
        }
    return out;
}

// ── disk block format ────────────────────────────────────────────────────
// A baked HD block on disk is: 16-byte header then raw RGBA.
//   bytes 0..3   magic "OHD1"
//   bytes 4..7   int32-le  w
//   bytes 8..11  int32-le  h
//   bytes 12..15 uint32-le payload length (must equal w*h*4)
// The key (hex) is the filename, so the file is content-addressed: a hit can
// only ever reproduce the exact bytes an upscale would have made.
constexpr char kMagic[4] = {'O', 'H', 'D', '1'};

void put_le32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}
std::uint32_t get_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string key_hex(std::uint64_t k) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(k));
    return std::string(buf);
}

// Load a baked block.  Returns true and fills `a` only on a fully-valid file
// (magic + dimensions + payload length all consistent).  Any malformation is
// a silent miss — the caller then re-upscales and re-writes.
bool load_block(const std::filesystem::path& file, HdAsset& a) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::uint8_t hdr[16];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(hdr))) return false;
    if (std::memcmp(hdr, kMagic, 4) != 0) return false;
    const std::int32_t w = static_cast<std::int32_t>(get_le32(hdr + 4));
    const std::int32_t h = static_cast<std::int32_t>(get_le32(hdr + 8));
    const std::uint32_t len = get_le32(hdr + 12);
    if (w <= 0 || h <= 0) return false;
    const std::uint64_t expect =
        static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4ull;
    if (len != expect) return false;
    std::vector<std::uint8_t> px(len);
    in.read(reinterpret_cast<char*>(px.data()),
            static_cast<std::streamsize>(len));
    if (in.gcount() != static_cast<std::streamsize>(len)) return false;
    a.w = w;
    a.h = h;
    a.px = std::move(px);
    return true;
}

// Write a baked block.  Best-effort: writes to a temp file then renames so a
// reader never sees a half-written file (no torn cache entries on crash).
bool store_block(const std::filesystem::path& file, const HdAsset& a) {
    const std::uint64_t expect =
        static_cast<std::uint64_t>(a.w) * static_cast<std::uint64_t>(a.h) * 4ull;
    if (a.w <= 0 || a.h <= 0 || a.px.size() != expect) return false;
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::uint8_t hdr[16];
        std::memcpy(hdr, kMagic, 4);
        put_le32(hdr + 4, static_cast<std::uint32_t>(a.w));
        put_le32(hdr + 8, static_cast<std::uint32_t>(a.h));
        put_le32(hdr + 12, static_cast<std::uint32_t>(a.px.size()));
        out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
        out.write(reinterpret_cast<const char*>(a.px.data()),
                  static_cast<std::streamsize>(a.px.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}
}  // namespace

const HdAsset& HdAssetCache::get(const std::vector<std::uint8_t>& src, int w,
                                 int h, int scale, const std::string& profile,
                                 bool bleed) {
    const std::uint64_t k = key_of(src, w, h, scale, profile);
    auto it = map_.find(k);
    if (it != map_.end()) return it->second;

    // Disk layer: a baked block from a prior run reproduces the exact upscale
    // output, so load it instead of recomputing.  scale<=1 is identity (never
    // worth a disk round-trip) so it's excluded.
    if (disk_enabled_ && scale > 1) {
        HdAsset loaded;
        const std::filesystem::path file = disk_dir_ / (key_hex(k) + ".bin");
        if (load_block(file, loaded)) {
            ++disk_loads_;
            return map_.emplace(k, std::move(loaded)).first->second;
        }
    }

    HdAsset a;
    if (scale <= 1) {
        a.px = src;
        a.w = w;
        a.h = h;
    } else {
        // Does the source carry any transparency?
        bool has_alpha = false;
        for (std::size_t i = 3; i < src.size(); i += 4)
            if (src[i] == 0) { has_alpha = true; break; }

        const std::vector<std::uint8_t>& to_scale =
            (has_alpha && bleed) ? alpha_bleed(src, w, h) : src;
        a.px = upscale_rgba(to_scale, w, h, scale, profile);
        a.w = w * scale;
        a.h = h * scale;

        // Re-apply the alpha as a NEAREST upscale of the source mask ONLY for
        // palette/binary-alpha-preserving scalers (mmpx, retro/native nearest,
        // smooth's scale2x/3x, eagle).  Those copy whole source pixels and so
        // never invent a partial-alpha edge; re-stamping the source mask keeps
        // transparent borders crisp and prevents stray colour bleeding from
        // the clamped-edge neighbourhood into the transparent halo.  Blending
        // scalers (omniscale, xbr) already produce an anti-aliased alpha edge
        // from the binary input mask — keeping it gives smooth silhouettes
        // (matches Python, which leaves the scaler's blended alpha untouched).
        // Overwriting it with nearest would re-introduce the blocky staircase.
        const bool palette_preserving =
            (profile == "mmpx" || profile == "retro" || profile == "native" ||
             profile == "smooth" || profile == "eagle");
        if (has_alpha && palette_preserving) {
            for (int y = 0; y < a.h; ++y)
                for (int x = 0; x < a.w; ++x) {
                    const std::size_t so =
                        (static_cast<std::size_t>(y / scale) * w + (x / scale))
                            * 4 + 3;
                    const std::size_t ho =
                        (static_cast<std::size_t>(y) * a.w + x) * 4 + 3;
                    a.px[ho] = src[so];
                }
        }
    }

    // Persist the freshly-upscaled block so the next run loads it.  Only
    // scale>1 (identity isn't worth a file).  Best-effort: a write failure
    // leaves the in-memory result untouched.
    if (disk_enabled_ && scale > 1) {
        const std::filesystem::path file = disk_dir_ / (key_hex(k) + ".bin");
        if (store_block(file, a)) ++disk_stores_;
    }
    return map_.emplace(k, std::move(a)).first->second;
}

void HdAssetCache::enable_disk(const std::filesystem::path& dir) {
    if (dir.empty()) {
        disk_enabled_ = false;
        disk_dir_.clear();
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        // Can't create the cache dir — fall back to memory-only, silently.
        disk_enabled_ = false;
        disk_dir_.clear();
        return;
    }
    disk_dir_ = dir;
    disk_enabled_ = true;
}

void HdAssetCache::clear() { map_.clear(); }

}  // namespace olduvai::enhance
