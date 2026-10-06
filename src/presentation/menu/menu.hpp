// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Headless menu navigation (the reference's menu controller), pure logic.
// Drives the MenuModel built from data/menus.json.  Bound values are strings
// (toggle "0"/"1", choice = the value token, slider = a number), interpreted
// per item type; a binding row's value is up to two names, "b,y".

#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "presentation/input/binding_slots.hpp"

namespace olduvai::presentation {

struct MenuItem {
    std::string id;
    std::string type;   // submenu|action|toggle|choice|slider|back|readout|binding
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

    // Narrow a choice row's authored values, for a value that is inert in the
    // current configuration (Aspect "widescreen" without HD).  Labels still
    // come from the authored list.  Default: no narrowing.
    virtual std::vector<std::string> allowed_values(const MenuItem& it) {
        return it.values;
    }

    // A binding row's slot (0 primary, 1 alternate): put `name` there by the
    // rules of its key.  Default: the slot replaced, no rules.  False when
    // the pick is refused.
    virtual bool set_binding(const std::string& key, int slot,
                             const std::string& name) {
        BindingList l = split_binding(get(key));
        const auto s = static_cast<std::size_t>(slot);
        if (slot < 0 || slot >= kBindingSlots || s > l.size()) return false;
        if (s < l.size())
            l[s] = name;
        else
            l.push_back(name);
        set(key, join_binding(l));
        return true;
    }
    // Empty a binding row's alternate slot.
    virtual void clear_alternate(const std::string& key) {
        BindingList l = split_binding(get(key));
        if (l.size() < 2) return;
        l.pop_back();
        set(key, join_binding(l));
    }
    // An action the bindings run themselves (a Reset row); false: not one.
    virtual bool run_action(const std::string& /*action*/) { return false; }
};

struct MenuRow {
    std::string label;
    std::optional<std::string> value;
    bool selectable = true;
    std::optional<std::string> value2;   // a binding row's alternate slot
    int column = -1;                     // the selected binding row's slot
};

using MenuActionTable = std::map<std::string, std::function<void()>>;

class Menu {
public:
    Menu(const MenuModel& model, MenuBindings& bindings, MenuActionTable actions = {})
        : model_(model), bind_(bindings), actions_(std::move(actions)) {}

    void open(const std::string& screen_id);
    void close() { stack_.clear(); }
    bool is_open() const { return !stack_.empty(); }

    // Safe when closed: returns an empty id (stack_.back() on an empty stack is
    // undefined and once surfaced as "map::at: key not found").
    const std::string& current_screen() const {
        static const std::string kNone;
        return stack_.empty() ? kNone : stack_.back().first;
    }
    int cursor_index() const { return stack_.empty() ? 0 : stack_.back().second; }
    const std::string& header() const { return screen().header; }
    std::vector<MenuRow> rows() const;

    void move(int dy);
    void adjust(int dx);
    // Enter on the selected item. Returns the invoked action id, if any.
    std::string activate();
    void back();

    // Binding rows hold two slots: left / right pick one, Enter starts a
    // capture, which the site's key router ends (menu/binding_capture.hpp).
    // It lasts until the clock (set_clock; none: forever) passes the
    // deadline.
    bool capturing() const;
    const std::string& capture_key() const { return capture_key_; }
    int capture_slot() const { return column_; }
    void set_clock(std::function<std::uint32_t()> now) { clock_ = std::move(now); }
    void set_capture_deadline(std::uint32_t t) { capture_deadline_ = t; }
    std::uint32_t capture_deadline() const { return capture_deadline_; }
    // Put `name` in the captured slot; the capture ends either way.
    bool finish_capture(const std::string& name);
    void cancel_capture() { capture_key_.clear(); }
    // Empty the selected binding row's alternate slot.
    void clear_alternate();

private:
    const MenuModel& model_;
    MenuBindings& bind_;
    MenuActionTable actions_;
    std::vector<std::pair<std::string, int>> stack_;  // (screen_id, cursor)
    int column_ = 0;                   // binding rows: the selected slot
    std::string capture_key_;          // the binding row capturing, if any
    std::uint32_t capture_deadline_ = 0;
    std::function<std::uint32_t()> clock_;

    // A screen the model lacks renders as nothing and logs once (open() and
    // activate() validate, so this means a bug elsewhere).
    const MenuScreen& screen() const {
        static const MenuScreen kEmpty;
        const auto it = model_.screens.find(current_screen());
        if (it != model_.screens.end()) return it->second;
        static bool said = false;
        if (!said) {
            said = true;
            std::fprintf(stderr, "menu: no screen '%s' in the model%s\n",
                         current_screen().c_str(),
                         current_screen().empty() ? " (the menu is closed)"
                                                  : "");
        }
        return kEmpty;
    }
    const std::vector<MenuItem>& items() const { return screen().items; }
    const MenuItem& selected() const { return items()[cursor_index()]; }
    std::optional<std::string> value_str(const MenuItem& it) const;
    std::string slot_text(const MenuItem& it, int slot) const;
    void snap_to_selectable(int step);
    static bool selectable(const MenuItem& it) { return it.type != "readout"; }
};

}  // namespace olduvai::presentation
