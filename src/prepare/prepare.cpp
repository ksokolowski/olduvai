// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/prepare.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

#include "prepare/cache_paths.hpp"

namespace fs = std::filesystem;

namespace olduvai::prepare {

namespace {

constexpr const char* kManifestName = "manifest.txt";

// Manifest format (flat, human-readable, line-oriented):
//   olduvai-prepare 1            <- magic + pipeline version
//   key <hexkey>
//   file FILESA.CUR <hexsum> <size>
//   ...
// A bucket is "valid" when its manifest's key equals the live key.  We store
// the per-file digests too so a stale bucket can be explained.
std::string manifest_path(const fs::path& bucket) {
    return (bucket / kManifestName).string();
}

bool write_manifest(const fs::path& bucket, const GameFiles& gf) {
    std::ofstream out(manifest_path(bucket), std::ios::trunc);
    if (!out) return false;
    out << "olduvai-prepare " << kPipelineVersion << "\n";
    out << "key " << gf.cache_key() << "\n";
    for (const auto& f : gf.files) {
        char line[256];
        std::snprintf(line, sizeof(line), "file %s %016llx %llu\n",
                      f.name.c_str(),
                      static_cast<unsigned long long>(f.checksum),
                      static_cast<unsigned long long>(f.size));
        out << line;
    }
    return static_cast<bool>(out);
}

// Read just the "key <…>" line from a bucket's manifest.  Empty when the
// manifest is absent or malformed.
std::string read_manifest_key(const fs::path& bucket) {
    std::ifstream in(manifest_path(bucket));
    if (!in) return {};
    std::string tag;
    while (in >> tag) {
        if (tag == "key") {
            std::string key;
            in >> key;
            return key;
        }
        // Skip the rest of the line.
        std::string rest;
        std::getline(in, rest);
    }
    return {};
}

}  // namespace

CacheStatus inspect_cache(const GameFiles& gf) {
    CacheStatus st;
    if (!gf.complete()) {
        st.state = CacheState::kNoFiles;
        st.message = "game files incomplete — cannot form a cache key";
        return st;
    }
    st.key = gf.cache_key();
    st.bucket = prepare_bucket(st.key);

    std::error_code ec;
    const bool bucket_exists = fs::exists(st.bucket, ec) &&
                               fs::exists(st.bucket / kManifestName, ec);
    if (!bucket_exists) {
        // Is there ANY bucket from a different fileset/version? → stale.
        const fs::path root = cache_root();
        bool any_other = false;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_directory(ec)) continue;
            if (e.path().filename() == "hd") continue;
            if (fs::exists(e.path() / kManifestName, ec)) {
                any_other = true;
                break;
            }
        }
        st.state = any_other ? CacheState::kStale : CacheState::kMissing;
        st.message = any_other
            ? "no bucket for the current game files (a stale one exists)"
            : "no prepared cache for these game files";
        return st;
    }

    if (read_manifest_key(st.bucket) == st.key) {
        st.state = CacheState::kValid;
        st.message = "prepared cache is valid";
    } else {
        st.state = CacheState::kStale;
        st.message = "prepared cache key mismatch (pipeline/version drift)";
    }
    return st;
}

bool run_prepare(const GameFiles& gf, bool verbose) {
    if (!gf.complete()) {
        if (verbose) {
            std::fprintf(stderr, "prepare: game files incomplete:\n%s",
                         gf.problems().c_str());
        }
        return false;
    }
    const std::string key = gf.cache_key();
    const fs::path bucket = prepare_bucket(key);
    if (!ensure_cache_dir(bucket)) {
        if (verbose) {
            std::fprintf(stderr, "prepare: cannot create cache dir %s\n",
                         bucket.string().c_str());
        }
        return false;
    }
    if (verbose) {
        std::printf("Preparing game data…\n");
        std::printf("  key:    %s\n", key.c_str());
        std::printf("  bucket: %s\n", bucket.string().c_str());
        for (const auto& f : gf.files) {
            std::printf("  + %-13s %016llx (%llu bytes)\n", f.name.c_str(),
                        static_cast<unsigned long long>(f.checksum),
                        static_cast<unsigned long long>(f.size));
        }
    }

    // STAGE 1 — decoded-asset cache: DEFERRED.  A persisted decode cache must
    // be byte-identical to a fresh decode or it would silently diverge
    // gameplay/render output.  That round-trip is not proven yet and the
    // runtime decode is cheap, so we intentionally do not persist decoded
    // tables/pixels here.  The bucket + manifest below reserve the key so the
    // decode layer can drop in later without changing the key scheme.
    // TODO(prepare-stage1): write decoded LZSS/MAT/PC1 native pixels into the
    // bucket once a verified round-trip (cache-load == fresh-decode) lands.

    // Also make sure the HD (stage-2) directory exists so the first enhanced
    // run can persist into it without a race.
    ensure_cache_dir(hd_dir());

    if (!write_manifest(bucket, gf)) {
        if (verbose) {
            std::fprintf(stderr, "prepare: failed to write manifest in %s\n",
                         bucket.string().c_str());
        }
        return false;
    }
    if (verbose) std::printf("Prepared cache for these game files.\n");
    return true;
}

bool ensure_prepared(const GameFiles& gf) {
    const CacheStatus st = inspect_cache(gf);
    if (st.state == CacheState::kValid) return true;
    if (st.state == CacheState::kNoFiles) return false;  // caller reports
    // Missing or stale → (re)build silently-ish (one progress line).
    return run_prepare(gf, /*verbose=*/true);
}

}  // namespace olduvai::prepare
