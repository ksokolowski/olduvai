// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "presentation/menu/profile_table.hpp"

namespace olduvai::app {

namespace {

// A cursor over the settings text.
class FlatJsonReader {
public:
    explicit FlatJsonReader(const std::string& text) : text_(text) {}

    bool at(char c) const { return i_ < text_.size() && text_[i_] == c; }
    bool done() const { return i_ >= text_.size(); }
    void advance() { ++i_; }

    void skip_ws() {
        while (i_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[i_])))
            ++i_;
    }

    // A quoted string, the cursor on its opening quote.  The escapes the
    // writer and a hand edit can produce are decoded; any other escaped
    // character stands for itself (\" \\ \/).
    std::string read_string() {
        std::string s;
        ++i_;
        while (i_ < text_.size() && text_[i_] != '"') {
            if (text_[i_] == '\\' && i_ + 1 < text_.size()) {
                s += unescape(text_[i_ + 1]);
                i_ += 2;
            } else {
                s += text_[i_++];
            }
        }
        ++i_;
        return s;
    }

    // A number, bool or null: up to the next ',' or '}', whitespace dropped.
    std::string read_bare() {
        std::string s;
        for (; i_ < text_.size() && text_[i_] != ',' && text_[i_] != '}'; ++i_)
            if (!std::isspace(static_cast<unsigned char>(text_[i_])))
                s += text_[i_];
        return s;
    }

private:
    static char unescape(char e) {
        switch (e) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            default:  return e;
        }
    }

    const std::string& text_;
    std::size_t i_ = 0;
};

// A quote or backslash inside a value (a game_dir) keeps the file valid.
std::string json_escape(const std::string& v) {
    std::string o;
    for (const char ch : v) {
        if (ch == '"' || ch == '\\') o += '\\';
        o += ch;
    }
    return o;
}

// Written unquoted: a bool or an integer.  Anything else ("-", "4:3") is a
// string, or the file stops being JSON.
bool is_bare_json(const std::string& v) {
    if (v == "true" || v == "false") return true;
    const std::size_t digits = !v.empty() && v[0] == '-' ? 1 : 0;
    return v.size() > digits &&
           std::all_of(v.begin() + static_cast<std::ptrdiff_t>(digits),
                       v.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Minimal tolerant reader for a flat JSON object of string/number/bool
// values — exactly the shape the settings file uses.  It stops at the first
// malformed token and keeps what it read.
Config parse_flat_json(const std::string& text) {
    Config out;
    FlatJsonReader r(text);
    r.skip_ws();
    if (!r.at('{')) return out;
    r.advance();
    while (!r.done()) {
        r.skip_ws();
        if (!r.at('"')) break;   // '}' ends the object
        const std::string key = r.read_string();
        r.skip_ws();
        if (!r.at(':')) break;
        r.advance();
        r.skip_ws();
        out[key] = r.at('"') ? r.read_string() : r.read_bare();
        r.skip_ws();
        if (r.at(',')) r.advance();
    }
    return out;
}

}  // namespace

std::string config_path() {
#if defined(_WIN32)
    // Plain Windows launches (Explorer / cmd) set neither XDG_CONFIG_HOME
    // nor HOME — the POSIX fallback degraded to a CWD-relative ./.config
    // that never round-tripped.
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
    if (xdg != nullptr && *xdg != '\0')
        return std::string(xdg) + "/olduvai/play.json";
    // getenv ONCE, into a local.  The previous form called it twice — once to
    // null-test and once to construct from — and nothing makes the second call
    // return what the first did, so std::string(nullptr) was reachable as
    // written.  Same early-return shape as the Windows branch above.
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") +
           "/.config/olduvai/play.json";
#endif
}

Config builtin_profile(const std::string& name) {
    // The pins live in presentation/menu/profile_table.hpp — the one
    // definition the CLI, the first-run path and the menu all read.  `hd-43`
    // was a third profile differing from hd only by aspect, a display
    // setting with its own Video row; `--profile hd-43` stays an alias
    // (cli_args.cpp).
    Config c;
    if (const auto* p = presentation::find_profile(name)) {
        for (std::size_t i = 0; i < p->pin_count; ++i)
            c[p->pins[i].key] = p->pins[i].value;
    }
    return c;
}

void apply_profile(Config& cfg, const std::string& name) {
    for (const auto& [k, v] : builtin_profile(name)) cfg[k] = v;
}

Config load_config_file() {
    const std::ifstream in(config_path());
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
    out << "{\n";
    bool first = true;
    for (const auto& [k, v] : c) {
        if (!first) out << ",\n";
        first = false;
        out << "  \"" << json_escape(k) << "\": ";
        if (is_bare_json(v)) out << v;
        else out << '"' << json_escape(v) << '"';
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
