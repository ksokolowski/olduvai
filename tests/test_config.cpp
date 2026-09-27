// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// play.json: the tolerant flat-JSON reader and the writer it must round-trip
// with, through the public load/save pair.  XDG_CONFIG_HOME points both at a
// per-process scratch directory (config_path honours it on every platform).

#include "doctest/doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/config.hpp"
#include "env_shim.hpp"
#include "test_pid.hpp"

namespace fs = std::filesystem;
using olduvai::app::Config;
using olduvai::app::load_config_file;
using olduvai::app::save_config_file;

namespace {

// Redirects config_path() for one test case and restores it after.
class ScratchConfig {
public:
    ScratchConfig()
        : dir_(fs::temp_directory_path() /
               ("olduvai_config_" + std::to_string(olduvai_test::pid()))) {
        const char* old = std::getenv("XDG_CONFIG_HOME");
        had_old_ = old != nullptr;
        if (had_old_) old_ = old;
        fs::remove_all(dir_);
        olduvai_test::set_env("XDG_CONFIG_HOME", dir_.string().c_str());
    }
    ~ScratchConfig() {
        if (had_old_) olduvai_test::set_env("XDG_CONFIG_HOME", old_.c_str());
        else olduvai_test::unset_env("XDG_CONFIG_HOME");
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    ScratchConfig(const ScratchConfig&) = delete;
    ScratchConfig& operator=(const ScratchConfig&) = delete;

    void write(const std::string& text) const {
        fs::create_directories(dir_ / "olduvai");
        std::ofstream(dir_ / "olduvai" / "play.json", std::ios::trunc) << text;
    }

private:
    fs::path dir_;
    std::string old_;
    bool had_old_ = false;
};

}  // namespace

TEST_CASE("config: an absent file reads as empty") {
    const ScratchConfig sc;
    CHECK(load_config_file().empty());
}

TEST_CASE("config: strings, numbers and bools round-trip") {
    const ScratchConfig sc;
    const Config c = {{"aspect", "4:3"},
                      {"audio_rate", "44100"},
                      {"offset", "-3"},
                      {"enhanced", "true"},
                      {"vga_scan", "false"},
                      {"dash", "-"},
                      {"game_dir", R"(C:\Games\a "quoted" dir)"}};
    REQUIRE(save_config_file(c));
    CHECK(load_config_file() == c);
}

TEST_CASE("config: escapes decode, other escaped characters stand for themselves") {
    const ScratchConfig sc;
    sc.write(R"({"a": "x\ny", "b": "t\tr\r", "c": "\/\q"})");
    const Config c = load_config_file();
    CHECK(c.at("a") == "x\ny");
    CHECK(c.at("b") == "t\tr\r");
    CHECK(c.at("c") == "/q");
}

TEST_CASE("config: bare values drop whitespace; layout is free") {
    const ScratchConfig sc;
    sc.write("\n  {\n\t\"n\" :  1 2 ,\"b\":true\n}\n");
    const Config c = load_config_file();
    CHECK(c.at("n") == "12");
    CHECK(c.at("b") == "true");
}

TEST_CASE("config: a malformed file keeps what was read before the fault") {
    const ScratchConfig sc;
    sc.write(R"({"a": "1", "b" "2", "c": "3"})");
    const Config c = load_config_file();
    CHECK(c.size() == 1);
    CHECK(c.at("a") == "1");

    sc.write("not json");
    CHECK(load_config_file().empty());

    sc.write(R"({"a": "unterminated)");
    CHECK(load_config_file().at("a") == "unterminated");
}
