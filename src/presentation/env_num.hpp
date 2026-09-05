// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Checked string→int for the presentation layer's debug hooks and replay data.
//
// std::atoi cannot fail: "abc" is 0, "" is 0, "2x" is 2.  That is harmless
// until the parsed value is compared against a frame counter, at which point
// `frame >= atoi("typo")` is `frame >= 0` — always true — and a mistyped
// OLDUVAI_FORCE_WIN wins the fight on frame one.  The quieter half of the same
// bug is `frame == atoi("typo")`, which fires on frame 0 instead of never.
//
// THE POLICY HERE IS "IGNORE GARBAGE, KEEP THE DEFAULT", which is a third one
// alongside the two in app/parse_num.hpp:
//
//   config (app)  — ignore the key, keep what we had
//   CLI    (app)  — fail loudly with exit 2
//   env    (here) — ignore the value, behave as if the hook were unset
//
// A debug hook that is set to nonsense should do nothing, not something
// surprising: the whole point of these hooks is to make a run reproducible,
// and a typo that silently changes the run defeats that more thoroughly than
// a typo that is ignored.
//
// WHY THIS DUPLICATES app/parse_num.hpp's strtol validation, about eight
// lines: `parse_num.hpp` lives in `app`, which is layer 6; this is layer 5,
// and `check_layers.sh` exists to stop a lower layer including from a higher
// one.  Sharing would mean moving the parser down to `core`, which today holds
// only game state, constants, RNG and the collision bitmap — a statement about
// what `core` is for, made to save eight lines.  Deliberate duplication, and
// if `core` ever does acquire a utilities home, both callers should move to it
// together.
#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace olduvai::presentation {

// True only if `text` is entirely one integer, ignoring surrounding
// whitespace.  Leaves `out` untouched on failure, so a caller can write
// `if (!parse_int(s, v)) continue;` and know `v` was never half-written.
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

// Read an integer debug hook.  Returns `fallback` when the variable is unset
// OR holds anything that is not exactly an integer — the two cases are
// deliberately not distinguished, because "set to nonsense" should behave like
// "not set" rather than like some third thing.
//
// Pick a `fallback` that cannot fire the hook: -1 for an `== frame` test,
// INT_MAX for a `>= frame` one.
inline int env_int(const char* name, int fallback) {
    const char* raw = std::getenv(name);
    int v = fallback;
    if (raw != nullptr && parse_int(raw, v)) return v;
    return fallback;
}

}  // namespace olduvai::presentation
