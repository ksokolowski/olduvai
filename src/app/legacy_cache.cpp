// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "legacy_cache.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace olduvai::app {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

// The locations pre-0.9.5 builds used, reproduced here verbatim.  This is the
// ONLY copy left; prepare/cache_paths.cpp is deleted.  OLDUVAI_CACHE_DIR is
// honoured because tests and power users pointed the old builds at it, and a
// migration that ignored the override would leave exactly those directories
// behind.
fs::path legacy_cache_root() {
    if (std::string ov = env("OLDUVAI_CACHE_DIR"); !ov.empty()) {
        return fs::path(ov);
    }
#if defined(_WIN32)
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
    if (std::string xdg = env("XDG_CACHE_HOME"); !xdg.empty()) {
        return fs::path(xdg) / "olduvai";
    }
    if (std::string home = env("HOME"); !home.empty()) {
        return fs::path(home) / ".cache" / "olduvai";
    }
    return fs::path(".cache") / "olduvai";
#endif
}

}  // namespace

void remove_legacy_cache_dir() {
    std::error_code ec;
    const fs::path root = legacy_cache_root();
    // exists() first so the common case (already gone) is one stat and no
    // recursive walk.
    if (!fs::exists(root, ec) || ec) return;
    // Only ever remove a directory, and only one whose name is ours — a
    // mistyped OLDUVAI_CACHE_DIR pointing at something real must not be
    // recursively deleted by a migration the user never asked for.
    if (!fs::is_directory(root, ec) || ec) return;
    const std::string leaf = root.filename().string();
    if (leaf != "olduvai" && leaf != "cache") return;
    fs::remove_all(root, ec);   // best-effort; a failure is not worth a message
}

}  // namespace olduvai::app
