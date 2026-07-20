// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/cache_paths.hpp"

#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace olduvai::prepare {

namespace {

// Read an environment variable, returning empty when unset or blank.
std::string env(const char* name) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string();
}

}  // namespace

fs::path cache_root() {
    // 1. Explicit override (tests, power users) wins everywhere.
    if (std::string ov = env("OLDUVAI_CACHE_DIR"); !ov.empty()) {
        return fs::path(ov);
    }

#if defined(_WIN32)
    // %LOCALAPPDATA%\olduvai\cache (fall back to a relative path if unset).
    if (std::string local = env("LOCALAPPDATA"); !local.empty()) {
        return fs::path(local) / "olduvai" / "cache";
    }
    return fs::path("olduvai") / "cache";
#elif defined(__APPLE__)
    if (std::string home = env("HOME"); !home.empty()) {
        return fs::path(home) / "Library" / "Caches" / "olduvai";
    }
    return fs::path(".cache") / "olduvai";
#else
    // Linux / other Unix: XDG_CACHE_HOME, else ~/.cache.
    if (std::string xdg = env("XDG_CACHE_HOME"); !xdg.empty()) {
        return fs::path(xdg) / "olduvai";
    }
    if (std::string home = env("HOME"); !home.empty()) {
        return fs::path(home) / ".cache" / "olduvai";
    }
    return fs::path(".cache") / "olduvai";
#endif
}

fs::path prepare_bucket(const std::string& key) {
    return cache_root() / key;
}

fs::path hd_dir() {
    return cache_root() / "hd";
}

bool ensure_cache_dir(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir, ec)) return true;
    fs::create_directories(dir, ec);
    return !ec;
}

bool purge_cache() {
    const fs::path root = cache_root();
    std::error_code ec;
    if (!fs::exists(root, ec)) return true;   // nothing to remove
    fs::remove_all(root, ec);
    return !ec;
}

}  // namespace olduvai::prepare
