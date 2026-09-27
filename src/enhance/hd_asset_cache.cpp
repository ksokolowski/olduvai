// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/hd_asset_cache.hpp"

#ifdef OLDUVAI_HAVE_ZLIB
#include <zlib.h>
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#include "enhance/upscale.hpp"

namespace olduvai::enhance {

namespace {
// FNV-1a 64 over the source bytes, then w/h/scale/profile/bleed mixed in.
// `bleed` must be in the key: it changes the pixels (the fluid bubbles use
// bleed=false), and the pre-warm would otherwise answer them with the wrong
// bytes.
std::uint64_t key_of(const std::vector<std::uint8_t>& src, int w, int h,
                     int scale, const std::string& profile, bool bleed) {
    // Not formats::Hash64: the scalars use a shift-and-add combine, and
    // existing cache entries are keyed on it (hash64.hpp notes the exception).
    std::uint64_t k = 1469598103934665603ull;
    for (std::uint8_t b : src) { k ^= b; k *= 1099511628211ull; }
    auto mix = [&](std::uint64_t v) {
        k ^= v + 0x9e3779b97f4a7c15ull + (k << 6) + (k >> 2);
    };
    mix(static_cast<std::uint64_t>(w));
    mix(static_cast<std::uint64_t>(h));
    mix(static_cast<std::uint64_t>(scale));
    for (const char c : profile) mix(static_cast<std::uint64_t>(c));
    mix(bleed ? 1ull : 2ull);
    return k;
}

// Extend opaque colour one ring into transparent 4-neighbours (RGB only; alpha
// stays 0), so the scaler reads a defined colour at the edge.
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

// ---- disk block format ----
// 16-byte header then raw RGBA:
//   bytes 0..3   magic "OHD1"
//   bytes 4..7   int32-le  w
//   bytes 8..11  int32-le  h
//   bytes 12..15 uint32-le payload length (= w*h*4)
// The filename is the key (hex), so a hit reproduces exactly the upscale bytes.
constexpr char kMagic[4] = {'O', 'H', 'D', '1'};
// Compressed variant, with its own magic: a build without zlib fails the magic
// check, treats it as a miss and re-bakes (the cache is derived data).  Future
// codecs get their own magic (OHDZ = deflate).
constexpr char kMagicZ[4] = {'O', 'H', 'D', 'Z'};

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

// Load a baked block; true only for a fully valid file.  Anything malformed is
// a silent miss (re-upscale and re-write).
bool load_block(const std::filesystem::path& file, HdAsset& a) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::uint8_t hdr[16];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(hdr))) return false;
    const bool zipped = std::memcmp(hdr, kMagicZ, 4) == 0;
    if (!zipped && std::memcmp(hdr, kMagic, 4) != 0) return false;
    const std::int32_t w = static_cast<std::int32_t>(get_le32(hdr + 4));
    const std::int32_t h = static_cast<std::int32_t>(get_le32(hdr + 8));
    const std::uint32_t len = get_le32(hdr + 12);
    if (w <= 0 || h <= 0) return false;
    const std::uint64_t expect =
        static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4ull;
    std::vector<std::uint8_t> px;
    if (zipped) {
#ifdef OLDUVAI_HAVE_ZLIB
        // len = COMPRESSED size; the uncompressed size is implied by w*h*4.
        if (expect > 0xFFFFFFFFull) return false;
        std::vector<std::uint8_t> comp(len);
        in.read(reinterpret_cast<char*>(comp.data()),
                static_cast<std::streamsize>(len));
        if (in.gcount() != static_cast<std::streamsize>(len)) return false;
        px.resize(static_cast<std::size_t>(expect));
        uLongf out_len = static_cast<uLongf>(expect);
        if (::uncompress(px.data(), &out_len, comp.data(),
                         static_cast<uLong>(len)) != Z_OK ||
            out_len != static_cast<uLongf>(expect)) {
            return false;   // corrupt entry -> miss -> re-bake
        }
#else
        return false;       // built without zlib: treat as absent, re-bake raw
#endif
    } else {
        if (len != expect) return false;
        px.resize(len);
        in.read(reinterpret_cast<char*>(px.data()),
                static_cast<std::streamsize>(len));
        if (in.gcount() != static_cast<std::streamsize>(len)) return false;
    }
    a.w = w;
    a.h = h;
    a.px = std::move(px);
    return true;
}

// Write a baked block: temp file then rename, so no reader sees a torn file.
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
        put_le32(hdr + 4, static_cast<std::uint32_t>(a.w));
        put_le32(hdr + 8, static_cast<std::uint32_t>(a.h));
#ifdef OLDUVAI_HAVE_ZLIB
        // Upscaled palette art compresses ~40x (a 4x full screen: ~17 RGBA
        // values in 4 MB).  Level 6: within a few percent of 9, much faster.
        uLongf cap = ::compressBound(static_cast<uLong>(a.px.size()));
        std::vector<std::uint8_t> comp(cap);
        if (::compress2(comp.data(), &cap, a.px.data(),
                        static_cast<uLong>(a.px.size()), 6) == Z_OK &&
            cap < a.px.size()) {
            std::memcpy(hdr, kMagicZ, 4);
            put_le32(hdr + 12, static_cast<std::uint32_t>(cap));
            out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
            out.write(reinterpret_cast<const char*>(comp.data()),
                      static_cast<std::streamsize>(cap));
            if (!out) return false;
        } else
#endif
        {   // no zlib, or compression did not help: store raw
            std::memcpy(hdr, kMagic, 4);
            put_le32(hdr + 12, static_cast<std::uint32_t>(a.px.size()));
            out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
            out.write(reinterpret_cast<const char*>(a.px.data()),
                      static_cast<std::streamsize>(a.px.size()));
            if (!out) return false;
        }
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

std::uint64_t HdAssetCache::key_for(const std::vector<std::uint8_t>& src,
                                    int w, int h, int scale,
                                    const std::string& profile, bool bleed) {
    return key_of(src, w, h, scale, profile, bleed);
}

bool HdAssetCache::insert(std::uint64_t k, HdAsset a) {
    return map_.emplace(k, std::move(a)).second;
}

const HdAsset& HdAssetCache::get(const std::vector<std::uint8_t>& src, int w,
                                 int h, int scale, const std::string& profile,
                                 bool bleed) {
    return get_keyed(key_of(src, w, h, scale, profile, bleed), src, w, h,
                     scale, profile, bleed);
}

const HdAsset& HdAssetCache::get_keyed(std::uint64_t k,
                                       const std::vector<std::uint8_t>& src,
                                       int w, int h, int scale,
                                       const std::string& profile, bool bleed) {
    auto it = map_.find(k);
    if (it != map_.end()) return it->second;
    return map_
        .emplace(k, build_with_key(k, src, w, h, scale, profile, bleed))
        .first->second;
}

HdAsset HdAssetCache::build(const std::vector<std::uint8_t>& src, int w, int h,
                            int scale, const std::string& profile,
                            bool bleed) const {
    return build_with_key(key_of(src, w, h, scale, profile, bleed), src, w, h,
                          scale, profile, bleed);
}

HdAsset HdAssetCache::build_with_key(std::uint64_t k,
                                     const std::vector<std::uint8_t>& src,
                                     int w, int h, int scale,
                                     const std::string& profile,
                                     bool bleed) const {
    // Disk layer: a baked block reproduces the upscale exactly.  Not for scale
    // 1.
    if (disk_enabled_ && scale > 1) {
        HdAsset loaded;
        const std::filesystem::path file = disk_dir_ / (key_hex(k) + ".bin");
        if (load_block(file, loaded)) {
            disk_loads_.fetch_add(1, std::memory_order_relaxed);
            return loaded;
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

        // Palette-preserving scalers (profile_preserves_palette): re-stamp
        // alpha as a nearest upscale of the source mask, keeping transparent
        // borders crisp. Blending scalers keep their anti-aliased alpha edge
        // (as the reference).
        const bool palette_preserving = profile_preserves_palette(profile);
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

    // Persist the new block (scale > 1); a write failure is ignored.
    if (disk_enabled_ && scale > 1) {
        const std::filesystem::path file = disk_dir_ / (key_hex(k) + ".bin");
        if (store_block(file, a))
            disk_stores_.fetch_add(1, std::memory_order_relaxed);
    }
    return a;
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

void HdAssetCache::clear() {
    map_.clear();
    source_keys_.clear();
}

}  // namespace olduvai::enhance
