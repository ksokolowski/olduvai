// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>
#include "test_pid.hpp"

#include "prepare/game_files.hpp"


#include <cstdint>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace pr = olduvai::prepare;

namespace {
fs::path scratch() {
    return fs::temp_directory_path() /
           ("olduvai_gamefiles_" + std::to_string(olduvai_test::pid()));
}
void write_file(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
// Populate a directory with the five required files (lower-case on purpose,
// to exercise the case-insensitive resolver), each with distinct content.
void populate(const fs::path& dir) {
    write_file(dir / "filesa.cur", "AAAA-cur");
    write_file(dir / "filesb.cur", "BBBB-cur");
    write_file(dir / "filesa.vga", "AAAA-vga");
    write_file(dir / "filesb.vga", "BBBB-vga");
    write_file(dir / "historik.exe", "EXE-bytes-here");
}
}  // namespace

TEST_CASE("game_files: complete fileset detects cleanly") {
    const fs::path dir = scratch() / "complete";
    std::error_code ec; fs::remove_all(dir, ec);
    populate(dir);

    const pr::GameFiles gf = pr::detect_game_files(dir);
    CHECK(gf.complete());
    CHECK(gf.problems().empty());
    REQUIRE(gf.files.size() == pr::required_game_files().size());
    for (const auto& f : gf.files) {
        CHECK(f.present);
        CHECK_FALSE(f.zero_byte);
        CHECK(f.checksum != 0);
        CHECK(f.size > 0);
    }
}

TEST_CASE("game_files: missing file → incomplete, clear problem") {
    const fs::path dir = scratch() / "missing";
    std::error_code ec; fs::remove_all(dir, ec);
    populate(dir);
    fs::remove(dir / "filesb.vga", ec);

    const pr::GameFiles gf = pr::detect_game_files(dir);
    CHECK_FALSE(gf.complete());
    CHECK(gf.problems().find("FILESB.VGA") != std::string::npos);
}

TEST_CASE("game_files: zero-byte file is treated as incomplete") {
    const fs::path dir = scratch() / "zero";
    std::error_code ec; fs::remove_all(dir, ec);
    populate(dir);
    write_file(dir / "filesa.cur", "");   // truncate to empty

    const pr::GameFiles gf = pr::detect_game_files(dir);
    CHECK_FALSE(gf.complete());
    CHECK(gf.problems().find("FILESA.CUR") != std::string::npos);
    CHECK(gf.problems().find("zero") != std::string::npos);
}
