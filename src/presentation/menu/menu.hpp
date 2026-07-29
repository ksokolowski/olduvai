// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Headless menu navigation core — the C++ mirror of the reference
// implementation's menu controller.  Pure logic: no SDL, no rendering.
// Drives the declarative model from data/menus.json (loaded elsewhere into a
// MenuModel).
//
// Bound values are exchanged as STRINGS (toggle = "0"/"1", choice = the value
// token, slider = a numeric string); the controller interprets per item type,
// exactly as the Python version does with dynamic typing.

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace olduvai::presentation {

struct MenuItem {
    std::string id;
    std::string type;   // submenu|action|toggle|choice|slider|back|readout
    std::string label;
    std::string hint;
    std::string target; // submenu
    std::string action; // action
    std::string key;    // toggle/choice/slider config key
    std::vector<std::string> values;        // choice (stored/persisted tokens)
    double min = 0.0, max = 0.0, step = 0.0;  // slider
    bool restart = false;
    std::vector<std::string> value_labels;  // choice display names (optional)
};

struct MenuScreen {
    std::string header;
    std::vector<MenuItem> items;
};

struct MenuModel {
    std::map<std::string, MenuScreen> screens;
};

// Config-value access for bound items. Values are strings (see file header).
struct MenuBindings {
    virtual ~MenuBindings() = default;
    virtual std::string get(const std::string& key) = 0;
    virtual void set(const std::string& key, const std::string& value) = 0;
};

struct MenuRow {
    std::string label;
    std::optional<std::string> value;
    bool selectable = true;
};

using MenuActionTable = std::map<std::string, std::function<void()>>;

class Menu {
public:
    Menu(const MenuModel& model, MenuBindings& bindings, MenuActionTable actions = {})
        : model_(model), bind_(bindings), actions_(std::move(actions)) {}

    void open(const std::string& screen_id);
    void close() { stack_.clear(); }
    bool is_open() const { return !stack_.empty(); }

    const std::string& current_screen() const { return stack_.back().first; }
    int cursor_index() const { return stack_.back().second; }
    const std::string& header() const { return screen().header; }
    std::vector<MenuRow> rows() const;

    void move(int dy);
    void adjust(int dx);
    // Enter on the selected item. Returns the invoked action id, if any.
    std::string activate();
    void back();

private:
    const MenuModel& model_;
    MenuBindings& bind_;
    MenuActionTable actions_;
    std::vector<std::pair<std::string, int>> stack_;  // (screen_id, cursor)

    const MenuScreen& screen() const { return model_.screens.at(current_screen()); }
    const std::vector<MenuItem>& items() const { return screen().items; }
    const MenuItem& selected() const { return items()[cursor_index()]; }
    std::optional<std::string> value_str(const MenuItem& it) const;
    void snap_to_selectable(int step);
    static bool selectable(const MenuItem& it) { return it.type != "readout"; }
};

}  // namespace olduvai::presentation
