// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>
#include "test_pid.hpp"

#include "enhance/hd_asset_cache.hpp"


#include <cstdint>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
// A small opaque RGBA block with varied content (so the upscale is non-trivial
// and the round-trip is a real comparison, not all-one-colour).
std::vector<std::uint8_t> sample(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i + 0] = static_cast<std::uint8_t>(x * 23 + y * 7);
            px[i + 1] = static_cast<std::uint8_t>(y * 31 + 11);
            px[i + 2] = static_cast<std::uint8_t>((x ^ y) * 17);
            px[i + 3] = 255;
        }
    return px;
}
fs::path scratch() {
    return fs::temp_directory_path() /
           ("olduvai_hddisk_" + std::to_string(olduvai_test::pid()));
}
}  // namespace

TEST_CASE("HD disk cache: warm load is byte-identical to a fresh upscale") {
    const fs::path dir = scratch();
    std::error_code ec; fs::remove_all(dir, ec);
    const auto src = sample(8, 6);

    // Cold cache (memory only): the reference output.
    olduvai::enhance::HdAssetCache cold;
    const auto& ref = cold.get(src, 8, 6, 4, "omniscale");
    const std::vector<std::uint8_t> ref_px = ref.px;
    const int ref_w = ref.w, ref_h = ref.h;

    // First disk-enabled cache: miss → upscale → persist.
    olduvai::enhance::HdAssetCache c1;
    c1.enable_disk(dir);
    const auto& a = c1.get(src, 8, 6, 4, "omniscale");
    CHECK(a.px == ref_px);          // same as memory-only output
    CHECK(c1.disk_loads() == 0);
    CHECK(c1.disk_stores() == 1);   // wrote one block
    // The block file exists under the cache dir.
    bool found_bin = false;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".bin") found_bin = true;
    CHECK(found_bin);

    // Second, FRESH cache instance over the SAME dir: must LOAD from disk and
    // reproduce the exact bytes — never recompute, never diverge.
    olduvai::enhance::HdAssetCache c2;
    c2.enable_disk(dir);
    const auto& b = c2.get(src, 8, 6, 4, "omniscale");
    CHECK(c2.disk_loads() == 1);
    CHECK(c2.disk_stores() == 0);
    CHECK(b.w == ref_w);
    CHECK(b.h == ref_h);
    CHECK(b.px == ref_px);          // PIXEL-IDENTICAL to the fresh upscale

    fs::remove_all(dir, ec);
}

TEST_CASE("HD disk cache: disabled by default (no files written)") {
    const fs::path dir = scratch();
    std::error_code ec; fs::remove_all(dir, ec);
    const auto src = sample(4, 4);
    olduvai::enhance::HdAssetCache c;   // no enable_disk
    c.get(src, 4, 4, 4, "omniscale");
    CHECK(c.disk_stores() == 0);
    CHECK_FALSE(fs::exists(dir));        // nothing touched the cache dir
}

TEST_CASE("HD disk cache: scale 1 identity is not persisted") {
    const fs::path dir = scratch();
    std::error_code ec; fs::remove_all(dir, ec);
    const auto src = sample(4, 4);
    olduvai::enhance::HdAssetCache c;
    c.enable_disk(dir);
    c.get(src, 4, 4, 1, "omniscale");   // identity
    CHECK(c.disk_stores() == 0);

    fs::remove_all(dir, ec);
}

TEST_CASE("HD disk cache: a corrupt block file is a silent miss") {
    const fs::path dir = scratch();
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto src = sample(4, 4);

    // Pre-seed the exact key file with garbage so load_block rejects it.  We
    // don't know the key, so write garbage to ALL candidate names by running
    // a store first, then truncating the file.
    {
        olduvai::enhance::HdAssetCache seed;
        seed.enable_disk(dir);
        seed.get(src, 4, 4, 4, "omniscale");
    }
    for (const auto& e : fs::directory_iterator(dir)) {
        std::ofstream(e.path(), std::ios::binary | std::ios::trunc) << "junk";
    }

    olduvai::enhance::HdAssetCache c;
    c.enable_disk(dir);
    const auto& a = c.get(src, 4, 4, 4, "omniscale");
    // Rejected the corrupt file, re-upscaled, and re-stored a valid one.
    CHECK(a.w == 16);
    CHECK(a.h == 16);
    CHECK(c.disk_loads() == 0);
    CHECK(c.disk_stores() == 1);

    fs::remove_all(dir, ec);
}
