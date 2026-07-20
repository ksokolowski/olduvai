// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/game_files.hpp"

#include "formats/unsqz.hpp"
#include "prepare/cache_paths.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace olduvai::prepare {

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// Case-insensitive compare of an on-disk filename against a canonical
// upper-case required name.
bool name_matches(const std::string& on_disk, const std::string& want) {
    if (on_disk.size() != want.size()) return false;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(on_disk[i])) != want[i]) {
            return false;
        }
    }
    return true;
}

// Resolve a required name to an actual path inside `dir`, honouring case.
fs::path resolve(const fs::path& dir, const std::string& want) {
    std::error_code ec;
    for (const auto& it : fs::directory_iterator(dir, ec)) {
        const std::string name = it.path().filename().string();
        if (name_matches(name, want)) return it.path();
    }
    return {};
}

// Case-insensitive subdirectory lookup (GOG installs mix cases: "data",
// "PREH").  Returns "" when absent.
fs::path resolve_subdir(const fs::path& dir, const std::string& want) {
    std::error_code ec;
    for (const auto& it : fs::directory_iterator(dir, ec)) {
        if (!it.is_directory(ec)) continue;
        const std::string name = it.path().filename().string();
        if (name_matches(name, want)) return it.path();
    }
    return {};
}

// The compressed-executable container name CD-era distributions use.
constexpr const char* kSqzName = "PREH.SQZ";

std::vector<std::uint8_t> slurp_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

// Fold a 64-bit value into an FNV-1a running hash, byte by byte (low-first),
// so the result is endianness-independent.
void fnv_mix_u64(std::uint64_t& h, std::uint64_t v) {
    for (int b = 0; b < 8; ++b) {
        h ^= static_cast<std::uint8_t>(v & 0xFF);
        h *= kFnvPrime;
        v >>= 8;
    }
}

}  // namespace

const std::vector<std::string>& required_game_files() {
    static const std::vector<std::string> files = {
        "FILESA.CUR", "FILESB.CUR", "FILESA.VGA", "FILESB.VGA", "HISTORIK.EXE",
    };
    return files;
}

std::uint64_t fnv1a64_file(const fs::path& path, bool& ok,
                           std::uintmax_t& size) {
    ok = false;
    size = 0;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    std::uint64_t h = kFnvOffset;
    std::array<char, 1 << 16> buf;
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<std::uint8_t>(buf[static_cast<std::size_t>(i)]);
            h *= kFnvPrime;
        }
        size += static_cast<std::uintmax_t>(n);
    }
    // A read error that is not plain EOF means we can't trust the digest.
    if (in.bad()) return 0;
    ok = true;
    return h;
}

fs::path resolve_game_dir(const fs::path& dir) {
    if (!resolve(dir, "FILESA.CUR").empty()) return dir;
    // GOG install root: game files live in <root>/data/PREH.
    // (resolve_subdir wants canonical UPPER-CASE names, like resolve.)
    if (const fs::path data = resolve_subdir(dir, "DATA"); !data.empty()) {
        if (const fs::path preh = resolve_subdir(data, "PREH");
            !preh.empty() && !resolve(preh, "FILESA.CUR").empty()) {
            return preh;
        }
    }
    if (const fs::path preh = resolve_subdir(dir, "PREH");
        !preh.empty() && !resolve(preh, "FILESA.CUR").empty()) {
        return preh;
    }
    return dir;
}

std::vector<fs::path> default_game_dir_candidates() {
    std::vector<fs::path> out;
#ifdef _WIN32
    // The GOG installer records the install path in the registry under the
    // game's product id — authoritative even for custom install locations.
    // 32-bit installer: the key lives under WOW6432Node in a 64-bit view.
    for (const char* key : {
             "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1430912792",
             "SOFTWARE\\GOG.com\\Games\\1430912792",
         }) {
        char buf[MAX_PATH];
        DWORD len = sizeof(buf);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "path", RRF_RT_REG_SZ,
                         nullptr, buf, &len) == ERROR_SUCCESS &&
            buf[0] != '\0') {
            out.emplace_back(buf);
        }
    }
    // Conventional locations: GOG Galaxy's library and the standalone
    // installer's default.
    for (const char* env : {"ProgramFiles(x86)", "ProgramFiles"}) {
        if (const char* p = std::getenv(env)) {
            out.push_back(fs::path(p) / "GOG Galaxy" / "Games" /
                          "Prehistorik");
        }
    }
    out.emplace_back("C:\\GOG Games\\Prehistorik");
#endif
    return out;
}

GameFiles detect_game_files(const fs::path& game_dir) {
    GameFiles gf;
    gf.dir = resolve_game_dir(game_dir);
    for (const auto& want : required_game_files()) {
        GameFileInfo info;
        info.name = want;
        info.path = resolve(gf.dir, want);
        // The executable also ships SQZ-compressed (GOG / CD releases):
        // accept PREH.SQZ under the canonical entry, checksummed as-is.
        if (info.path.empty() && want == "HISTORIK.EXE") {
            info.path = resolve(gf.dir, kSqzName);
            info.via_sqz = !info.path.empty();
        }
        if (info.path.empty()) {
            info.present = false;
        } else {
            info.present = true;
            bool ok = false;
            std::uintmax_t size = 0;
            info.checksum = fnv1a64_file(info.path, ok, size);
            info.size = size;
            info.zero_byte = ok && size == 0;
            // An unreadable-but-present file is treated as missing (no key).
            if (!ok) {
                info.present = false;
                info.checksum = 0;
            }
        }
        gf.files.push_back(std::move(info));
    }
    return gf;
}

bool GameFiles::complete() const {
    if (files.size() != required_game_files().size()) return false;
    for (const auto& f : files) {
        if (!f.present || f.zero_byte) return false;
    }
    return true;
}

std::string GameFiles::problems() const {
    std::string out;
    for (const auto& f : files) {
        if (!f.present) {
            out += "  " + f.name + " — missing or unreadable";
            if (f.name == "HISTORIK.EXE") out += " (PREH.SQZ also accepted)";
            out += "\n";
        } else if (f.zero_byte) {
            out += "  " + f.name + " — present but zero bytes\n";
        }
    }
    return out;
}

std::vector<std::uint8_t> load_game_executable(const fs::path& game_dir) {
    const fs::path dir = resolve_game_dir(game_dir);
    if (const fs::path exe = resolve(dir, "HISTORIK.EXE"); !exe.empty()) {
        return slurp_file(exe);
    }
    if (const fs::path sqz = resolve(dir, kSqzName); !sqz.empty()) {
        const auto raw = slurp_file(sqz);
        if (!formats::looks_like_sqz(raw)) return {};
        try {
            return formats::unsqz(raw);
        } catch (const formats::SqzError&) {
            return {};   // malformed container == executable unavailable
        }
    }
    return {};
}

std::string GameFiles::cache_key() const {
    if (!complete()) return {};
    std::uint64_t h = kFnvOffset;
    fnv_mix_u64(h, static_cast<std::uint64_t>(kPipelineVersion));
    for (const auto& f : files) {
        fnv_mix_u64(h, f.checksum);
        fnv_mix_u64(h, static_cast<std::uint64_t>(f.size));
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

}  // namespace olduvai::prepare
