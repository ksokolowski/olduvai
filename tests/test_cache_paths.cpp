// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>
#include "test_pid.hpp"

#include "prepare/cache_paths.hpp"

#include "_env_helpers.hpp"

#ifndef _WIN32
#else
#include <process.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <string>

namespace fs = std::filesystem;
namespace pr = olduvai::prepare;

namespace {
// RAII override of OLDUVAI_CACHE_DIR for a test, restored on scope exit.
struct CacheDirEnv {
    std::string prev;
    bool had = false;
    explicit CacheDirEnv(const std::string& v) {
        if (const char* p = std::getenv("OLDUVAI_CACHE_DIR")) {
            prev = p;
            had = true;
        }
        olduvai::test::set_env("OLDUVAI_CACHE_DIR", v.c_str());
    }
    ~CacheDirEnv() {
        if (had) olduvai::test::set_env("OLDUVAI_CACHE_DIR", prev.c_str());
        else olduvai::test::unset_env("OLDUVAI_CACHE_DIR");
    }
};

fs::path temp_root() {
    return fs::temp_directory_path() /
           ("olduvai_cachepaths_" + std::to_string(olduvai_test::pid()));
}
}  // namespace

TEST_CASE("cache_paths: OLDUVAI_CACHE_DIR override wins") {
    const fs::path root = temp_root();
    CacheDirEnv env(root.string());
    CHECK(pr::cache_root() == root);
    CHECK(pr::prepare_bucket("deadbeef") == root / "deadbeef");
    CHECK(pr::hd_dir() == root / "hd");
}

TEST_CASE("cache_paths: ensure + purge round-trip stays under the root") {
    const fs::path root = temp_root();
    CacheDirEnv env(root.string());
    std::error_code ec;
    fs::remove_all(root, ec);

    CHECK(pr::ensure_cache_dir(pr::hd_dir()));
    CHECK(fs::exists(pr::hd_dir()));
    // A file written into the cache root must be removed by purge.
    {
        std::error_code w;
        fs::create_directories(pr::prepare_bucket("ff00"), w);
    }
    CHECK(fs::exists(pr::prepare_bucket("ff00")));
    CHECK(pr::purge_cache());
    CHECK_FALSE(fs::exists(root));
    // Purging an already-absent root is success.
    CHECK(pr::purge_cache());
}

TEST_CASE("cache_paths: pipeline version is a positive constant") {
    CHECK(pr::kPipelineVersion >= 1);
}
