// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Minimal JSON value + recursive-descent parser, shared by the menu-model and
// save-state loaders (olduvai's src/app/config.cpp parser is flat-only).  Not a
// general-purpose library — just enough for the small, well-formed JSON these
// features read.  Throws std::runtime_error on malformed input.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace olduvai::presentation {

struct JsonValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    // Convenience accessors (return defaults when absent / wrong type).
    bool has(const std::string& k) const { return obj.find(k) != obj.end(); }
    const JsonValue* find(const std::string& k) const {
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
};

// Parse `text` into a JsonValue. Throws std::runtime_error on malformed JSON.
JsonValue parse_json(const std::string& text);

}  // namespace olduvai::presentation
