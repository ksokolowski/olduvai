// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Checked string -> int for debug env hooks and replay data.  std::atoi turns
// garbage into 0, so `frame >= atoi("typo")` fires on frame 0.  Policy: ignore
// garbage and behave as if unset (config ignores the key, the CLI exits 2;
// see app/parse_num.hpp).  Duplicates ~8 lines of app/parse_num.hpp because a
// lower layer may not include from app (check_layers.sh).
#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace olduvai::presentation {

// True only if `text` is one integer (surrounding whitespace allowed).  `out`
// is untouched on failure.
inline bool parse_int(const char* text, int& out) {
    if (text == nullptr || *text == '\0') return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || errno == ERANGE) return false;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (*end != '\0') return false;              // trailing garbage: "2x"
    if (v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

inline bool parse_int(const std::string& text, int& out) {
    return parse_int(text.c_str(), out);
}

// Integer debug hook: `fallback` when unset or not exactly an integer.  Choose
// a fallback that cannot fire the hook: -1 for `== frame`, INT_MAX for
// `>= frame`.
inline int env_int(const char* name, int fallback) {
    const char* raw = std::getenv(name);
    int v = fallback;
    if (raw != nullptr && parse_int(raw, v)) return v;
    return fallback;
}

}  // namespace olduvai::presentation
