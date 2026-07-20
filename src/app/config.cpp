// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace olduvai::app {

namespace {

// Minimal tolerant reader for a flat JSON object of string/number/bool
// values — exactly the shape the settings file uses.
Config parse_flat_json(const std::string& text) {
    Config out;
    std::size_t i = 0;
    auto skip_ws = [&] {
        while (i < text.size() && std::isspace(
            static_cast<unsigned char>(text[i]))) ++i;
    };
    auto read_string = [&]() -> std::string {
        std::string s;
        ++i;   // opening quote
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) {
                // Decode the JSON escapes the writer (and hand-edits) can
                // produce — the old "skip the backslash, keep the next char
                // literally" turned \n into 'n' and \t into 't'.
                const char e = text[i + 1];
                i += 2;
                switch (e) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    default:  s += e;    break;   // \" \\ \/ and unknown
                }
                continue;
            }
            s += text[i++];
        }
        ++i;   // closing quote
        return s;
    };
    skip_ws();
    if (i >= text.size() || text[i] != '{') return out;
    ++i;
    while (i < text.size()) {
        skip_ws();
        if (i < text.size() && text[i] == '}') break;
        if (i >= text.size() || text[i] != '"') break;
        const std::string key = read_string();
        skip_ws();
        if (i >= text.size() || text[i] != ':') break;
        ++i;
        skip_ws();
        std::string value;
        if (i < text.size() && text[i] == '"') {
            value = read_string();
        } else {
            while (i < text.size() && text[i] != ',' && text[i] != '}') {
                if (!std::isspace(static_cast<unsigned char>(text[i]))) {
                    value += text[i];
                }
                ++i;
            }
        }
        out[key] = value;
        skip_ws();
        if (i < text.size() && text[i] == ',') ++i;
    }
    return out;
}

}  // namespace

std::string config_path() {
#if defined(_WIN32)
    // Plain Windows launches (Explorer / cmd) set neither XDG_CONFIG_HOME
    // nor HOME — the POSIX fallback degraded to a CWD-relative ./.config
    // that never round-tripped (first Windows field test, 2026-07-19).
    // %APPDATA% is the platform config root; XDG still wins when set so
    // MSYS-shell users keep one config with their POSIX tools.
    const char* xdg_w = std::getenv("XDG_CONFIG_HOME");
    if (xdg_w != nullptr && *xdg_w != '\0')
        return std::string(xdg_w) + "/olduvai/play.json";
    const char* appdata = std::getenv("APPDATA");
    if (appdata != nullptr && *appdata != '\0')
        return std::string(appdata) + "\\olduvai\\play.json";
    const char* prof = std::getenv("USERPROFILE");
    if (prof != nullptr && *prof != '\0')
        return std::string(prof) + "\\olduvai\\play.json";
    return "olduvai-play.json";   // last resort: beside the exe's cwd
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdg != nullptr && *xdg != '\0'
                           ? xdg
                           : std::string(std::getenv("HOME") != nullptr
                                             ? std::getenv("HOME") : ".") +
                                 "/.config";
    return base + "/olduvai/play.json";
#endif
}

Config builtin_profile(const std::string& name) {
    // Three profiles ↔ three ways to play (owner 2026-07-04: hd and hd-43
    // target BEST UX by default; dos stays byte-faithful — main.cpp also
    // clears enhanced-side saved keys for dos).  Audio deliberately NOT
    // pinned in any profile: the auto-pick chain (MT-32 ROMs → soundfont →
    // OPL) chooses the best available backend per machine.
    if (name == "dos") return {};   // byte-faithful defaults (+vga-scan default)
    if (name == "hd") {             // full enhanced + widescreen peeks
        return {{"enhanced", "true"},
                {"hd_profile", "omniscale"},
                {"render_scale", "4"},
                {"aspect", "widescreen"}};
    }
    if (name == "hd-43") {          // full enhanced at the CRT 4:3 look
        return {{"enhanced", "true"},
                {"hd_profile", "omniscale"},
                {"render_scale", "4"},
                {"aspect", "4:3"}};
    }
    return {};
}

void apply_profile(Config& cfg, const std::string& name) {
    for (const auto& [k, v] : builtin_profile(name)) cfg[k] = v;
    if (name == "dos") {
        // dos = byte-faithful: the empty profile map can't clear enhanced-
        // side keys a saved config may carry — do it explicitly.
        cfg["enhanced"] = "false";
        cfg["enhance"] = "";
        cfg["hd_profile"] = "native";
        cfg["aspect"] = "keep";
    }
}

Config load_config_file() {
    std::ifstream in(config_path());
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_flat_json(ss.str());
}

bool save_config_file(const Config& c) {
    const std::string path = config_path();
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    // Write-to-temp + rename: a crash/full-disk mid-write must not leave a
    // truncated play.json that the tolerant reader silently half-parses.
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "config: could not open %s for writing\n",
                     tmp.c_str());
        return false;
    }
    // Escape backslashes and quotes so a value containing either round-trips
    // as valid JSON (a game_dir with a quote used to corrupt the file).
    const auto esc = [](const std::string& v) {
        std::string o;
        for (const char ch : v) {
            if (ch == '"' || ch == '\\') o += '\\';
            o += ch;
        }
        return o;
    };
    // Bare token only for real numbers/bools — the old "made of -0123456789"
    // test emitted values like "-" or "--" as bare tokens = invalid JSON.
    const auto is_number = [](const std::string& v) {
        if (v.empty()) return false;
        std::size_t i = (v[0] == '-') ? 1 : 0;
        if (i >= v.size()) return false;
        for (; i < v.size(); ++i)
            if (v[i] < '0' || v[i] > '9') return false;
        return true;
    };
    out << "{\n";
    bool first = true;
    for (const auto& [k, v] : c) {
        if (!first) out << ",\n";
        first = false;
        out << "  \"" << esc(k) << "\": ";
        if (v == "true" || v == "false" || is_number(v)) out << v;
        else out << '"' << esc(v) << '"';
    }
    out << "\n}\n";
    out.flush();
    if (!out) {
        std::fprintf(stderr, "config: write to %s failed\n", tmp.c_str());
        return false;
    }
    out.close();
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::fprintf(stderr, "config: could not save %s (%s)\n",
                     path.c_str(), ec.message().c_str());
    }
    return !ec;
}

}  // namespace olduvai::app
