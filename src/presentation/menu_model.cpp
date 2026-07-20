// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu_model.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "presentation/mini_json.hpp"

namespace olduvai::presentation {

namespace {

std::string num_token(double v) {
    if (std::floor(v) == v) return std::to_string(static_cast<long long>(v));
    return std::to_string(v);
}

const JsonValue& field(const JsonValue& o, const std::string& k) {
    auto it = o.obj.find(k);
    if (it == o.obj.end())
        throw std::runtime_error("menus.json: missing field '" + k + "'");
    return it->second;
}
std::string opt_str(const JsonValue& o, const std::string& k) {
    auto it = o.obj.find(k);
    return it == o.obj.end() ? std::string{} : it->second.str;
}

}  // namespace

MenuModel parse_menus_json(const std::string& text) {
    const JsonValue root = parse_json(text);
    if (root.type != JsonValue::Obj)
        throw std::runtime_error("menus.json: root must be an object");

    MenuModel model;
// GCC's -Wdangling-reference heuristic can't see through field(): both
// references bind into named locals (root / sval) that outlive every use.
// Clang has no such warning, and an unknown -W name in the pragma would
// itself warn there — hence the guard.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-reference"
#endif
    const JsonValue& screens = field(root, "screens");
    if (screens.type != JsonValue::Obj)
        throw std::runtime_error("menus.json: 'screens' must be an object");

    for (const auto& [sid, sval] : screens.obj) {
        MenuScreen screen;
        screen.header = field(sval, "header").str;
        const JsonValue& items = field(sval, "items");
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        if (items.type != JsonValue::Arr)
            throw std::runtime_error("menus.json: items must be an array");
        for (const auto& iv : items.arr) {
            MenuItem item;
            item.id = field(iv, "id").str;
            item.type = field(iv, "type").str;
            item.label = field(iv, "label").str;
            item.hint = opt_str(iv, "hint");
            item.target = opt_str(iv, "target");
            item.action = opt_str(iv, "action");
            item.key = opt_str(iv, "key");
            if (auto it = iv.obj.find("values"); it != iv.obj.end())
                for (const auto& ev : it->second.arr)
                    item.values.push_back(
                        ev.type == JsonValue::Num ? num_token(ev.num) : ev.str);
            if (auto it = iv.obj.find("value_labels"); it != iv.obj.end())
                for (const auto& ev : it->second.arr)
                    item.value_labels.push_back(ev.str);
            if (auto it = iv.obj.find("min"); it != iv.obj.end()) item.min = it->second.num;
            if (auto it = iv.obj.find("max"); it != iv.obj.end()) item.max = it->second.num;
            if (auto it = iv.obj.find("step"); it != iv.obj.end()) item.step = it->second.num;
            if (auto it = iv.obj.find("restart"); it != iv.obj.end()) item.restart = it->second.b;
            screen.items.push_back(std::move(item));
        }
        model.screens[sid] = std::move(screen);
    }
    return model;
}

std::optional<MenuModel> load_menus(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_menus_json(ss.str());
}

std::optional<MenuModel> load_menus_embedded() {
    // embedded_menus_json() is generated from data/menus.json at build
    // time; a parse failure here is a build defect, not a user error —
    // degrade like a missing file instead of crashing.
    try {
        return parse_menus_json(embedded_menus_json());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace olduvai::presentation
