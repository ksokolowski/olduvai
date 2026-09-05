// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Checked string→int for the two places that read numbers the user typed:
// play.json (options_resolve) and argv (cli_args).
//
// std::atoi cannot fail.  "abc" is 0, "" is 0, and "2x" is 2 — so a typo in a
// hand-edited config became a real setting, and render_scale 0 is not a scale.
// The two surfaces then differ on POLICY, which is why this returns a bool
// instead of a fallback value:
//   config  — ignore the key, keep what we had (a persistent file must not be
//             able to brick a session over one typo)
//   CLI     — fail loudly with exit 2 (an explicit argument that cannot be
//             honoured is an error, not a suggestion)
#pragma once

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace olduvai::app {

// True only if `text` is entirely one integer, ignoring surrounding
// whitespace.  Leaves `out` untouched on failure, so callers can write
// `if (!parse_int(v, field)) { /* field still holds the old value */ }`.
inline bool parse_int(const std::string& text, int& out) {
    const char* begin = text.c_str();
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(begin, &end, 10);
    if (end == begin) return false;              // no digits at all ("", "abc")
    while (*end != '\0' &&
           std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') return false;              // trailing garbage ("2x")
    if (errno == ERANGE || value < INT_MIN || value > INT_MAX) return false;
    out = static_cast<int>(value);
    return true;
}

// Same contract for the one fractional argument (--render-audio-secs).
inline bool parse_double(const std::string& text, double& out) {
    const char* begin = text.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    if (end == begin) return false;
    while (*end != '\0' &&
           std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0' || errno == ERANGE) return false;
    out = value;
    return true;
}

}  // namespace olduvai::app
