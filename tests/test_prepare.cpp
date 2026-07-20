// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>
#include "test_pid.hpp"

#include "prepare/cache_paths.hpp"
#include "prepare/game_files.hpp"
#include "prepare/prepare.hpp"

#include "_env_helpers.hpp"

#ifndef _WIN32
#else
#include <process.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace pr = olduvai::prepare;

namespace {
struct CacheDirEnv {
    std::string prev; bool had = false;
    explicit CacheDirEnv(const std::string& v) {
        if (const char* p = std::getenv("OLDUVAI_CACHE_DIR")) { prev = p; had = true; }
        olduvai::test::set_env("OLDUVAI_CACHE_DIR", v.c_str());
    }
    ~CacheDirEnv() {
        if (had) olduvai::test::set_env("OLDUVAI_CACHE_DIR", prev.c_str());
        else olduvai::test::unset_env("OLDUVAI_CACHE_DIR");
    }
};
void write_file(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void populate(const fs::path& dir, const std::string& exe = "EXE-A") {
    write_file(dir / "FILESA.CUR", "AAAA");
    write_file(dir / "FILESB.CUR", "BBBB");
    write_file(dir / "FILESA.VGA", "CCCC");
    write_file(dir / "FILESB.VGA", "DDDD");
    write_file(dir / "HISTORIK.EXE", exe);
}
}  // namespace

TEST_CASE("prepare: missing → prepare → valid lifecycle") {
    const fs::path base = fs::temp_directory_path() /
        ("olduvai_prep_" + std::to_string(olduvai_test::pid()));
    std::error_code ec; fs::remove_all(base, ec);
    const fs::path game = base / "game";
    const fs::path cache = base / "cache";
    CacheDirEnv env(cache.string());
    populate(game);

    const pr::GameFiles gf = pr::detect_game_files(game);
    REQUIRE(gf.complete());

    // Missing first.
    CHECK(pr::inspect_cache(gf).state == pr::CacheState::kMissing);

    // Build it.
    CHECK(pr::run_prepare(gf, /*verbose=*/false));

    // Now valid, and the bucket + manifest live UNDER the cache root.
    const pr::CacheStatus st = pr::inspect_cache(gf);
    CHECK(st.state == pr::CacheState::kValid);
    CHECK(fs::exists(st.bucket / "manifest.txt"));
    // Bucket path is inside the cache dir, never elsewhere.
    CHECK(st.bucket.string().rfind(cache.string(), 0) == 0);

    fs::remove_all(base, ec);
}

TEST_CASE("prepare: a changed game file makes the old cache stale") {
    const fs::path base = fs::temp_directory_path() /
        ("olduvai_prep2_" + std::to_string(olduvai_test::pid()));
    std::error_code ec; fs::remove_all(base, ec);
    const fs::path game = base / "game";
    const fs::path cache = base / "cache";
    CacheDirEnv env(cache.string());

    populate(game, "EXE-A");
    pr::GameFiles gf = pr::detect_game_files(game);
    CHECK(pr::run_prepare(gf, false));
    CHECK(pr::inspect_cache(gf).state == pr::CacheState::kValid);

    // Change a game file → the live key differs → old bucket is stale.
    write_file(game / "HISTORIK.EXE", "EXE-B-different");
    pr::GameFiles gf2 = pr::detect_game_files(game);
    CHECK(pr::inspect_cache(gf2).state == pr::CacheState::kStale);

    // ensure_prepared rebuilds for the new key → valid again.
    CHECK(pr::ensure_prepared(gf2));
    CHECK(pr::inspect_cache(gf2).state == pr::CacheState::kValid);

    fs::remove_all(base, ec);
}

TEST_CASE("prepare: incomplete fileset reports kNoFiles, never builds") {
    const fs::path base = fs::temp_directory_path() /
        ("olduvai_prep3_" + std::to_string(olduvai_test::pid()));
    std::error_code ec; fs::remove_all(base, ec);
    const fs::path game = base / "game";
    const fs::path cache = base / "cache";
    CacheDirEnv env(cache.string());
    populate(game);
    fs::remove(game / "FILESA.VGA", ec);

    const pr::GameFiles gf = pr::detect_game_files(game);
    CHECK(pr::inspect_cache(gf).state == pr::CacheState::kNoFiles);
    CHECK_FALSE(pr::run_prepare(gf, false));
    CHECK_FALSE(pr::ensure_prepared(gf));

    fs::remove_all(base, ec);
}
