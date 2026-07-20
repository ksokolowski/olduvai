// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu.hpp"

#include <algorithm>
#include <cmath>

namespace olduvai::presentation {

namespace {
// Format a slider value without trailing ".000000" noise (volumes are whole).
std::string num_to_token(double v) {
    if (std::floor(v) == v) return std::to_string(static_cast<long long>(v));
    return std::to_string(v);
}
}  // namespace

void Menu::open(const std::string& screen_id) {
    // Unknown screen OR one with no items (hand-edited menus.json): refuse to
    // open — selected()/activate() index items()[cursor] and an empty screen
    // would deref element 0 of an empty vector on the first keypress.
    const auto it = model_.screens.find(screen_id);
    if (it == model_.screens.end() || it->second.items.empty()) return;
    stack_.clear();
    stack_.emplace_back(screen_id, 0);
    snap_to_selectable(+1);
}

std::optional<std::string> Menu::value_str(const MenuItem& it) const {
    if (it.type == "toggle") return bind_.get(it.key) == "1" ? "ON" : "OFF";
    // An action row may carry a value selector (key + values): Enter fires
    // the action, left/right cycles the value — the main menu's Start Game
    // level select.  Displays like a choice; plain action rows keep the hint.
    if (it.type == "choice" ||
        (it.type == "action" && !it.key.empty() && !it.values.empty())) {
        const std::string cur = bind_.get(it.key);
        if (!it.value_labels.empty()) {  // friendly display name for the token
            auto p = std::find(it.values.begin(), it.values.end(), cur);
            if (p != it.values.end()) {
                const std::size_t i = static_cast<std::size_t>(p - it.values.begin());
                if (i < it.value_labels.size()) return it.value_labels[i];
            }
        }
        return cur;
    }
    if (it.type == "slider") return bind_.get(it.key);
    if (it.type == "submenu" || it.type == "action")
        return it.hint.empty() ? std::nullopt : std::optional<std::string>(it.hint);
    if (it.type == "text") {
        // Preview = first line of the bound value, truncated to fit the
        // narrow slab (the full text lives in the editor overlay).
        const std::string cur = bind_.get(it.key);
        std::string first = cur.substr(0, cur.find('\n'));
        if (first.size() > 12) first = first.substr(0, 11) + "...";
        return first;
    }
    return std::nullopt;
}

std::vector<MenuRow> Menu::rows() const {
    std::vector<MenuRow> out;
    for (const auto& it : items())
        out.push_back(MenuRow{it.label, value_str(it), selectable(it)});
    return out;
}

void Menu::snap_to_selectable(int step) {
    const auto& its = items();
    const int n = static_cast<int>(its.size());
    int idx = stack_.back().second;
    for (int i = 0; i < n; ++i) {
        if (selectable(its[idx])) { stack_.back().second = idx; return; }
        idx = (idx + step % n + n) % n;
    }
}

void Menu::move(int dy) {
    if (!is_open() || dy == 0) return;
    const int step = dy > 0 ? 1 : -1;
    const auto& its = items();
    const int n = static_cast<int>(its.size());
    int idx = cursor_index();
    for (int i = 0; i < n; ++i) {
        idx = (idx + step + n) % n;
        if (selectable(its[idx])) { stack_.back().second = idx; return; }
    }
}

void Menu::adjust(int dx) {
    if (!is_open() || dx == 0) return;
    const MenuItem& it = selected();
    if (it.type == "toggle") {
        bind_.set(it.key, bind_.get(it.key) == "1" ? "0" : "1");
    } else if (it.type == "choice" ||
               (it.type == "action" && !it.key.empty() && !it.values.empty())) {
        const auto& vals = it.values;
        if (vals.empty()) return;
        const std::string cur = bind_.get(it.key);
        auto pos = std::find(vals.begin(), vals.end(), cur);
        int i = pos == vals.end() ? 0 : static_cast<int>(pos - vals.begin());
        const int n = static_cast<int>(vals.size());
        i = (i + (dx > 0 ? 1 : -1) + n) % n;
        bind_.set(it.key, vals[i]);
    } else if (it.type == "slider") {
        double cur = 0.0;
        try { cur = std::stod(bind_.get(it.key)); } catch (...) { cur = it.min; }
        double nxt = cur + (dx > 0 ? it.step : -it.step);
        nxt = std::max(it.min, std::min(it.max, nxt));
        bind_.set(it.key, num_to_token(nxt));
    }
}

std::string Menu::activate() {
    if (!is_open()) return {};
    const MenuItem& it = selected();
    if (it.type == "submenu") {
        // Dangling target or empty screen (stale / hand-edited menus.json):
        // ignore the item instead of pushing a screen that can't render.
        const auto tgt = model_.screens.find(it.target);
        if (tgt == model_.screens.end() || tgt->second.items.empty()) return {};
        stack_.emplace_back(it.target, 0);
        snap_to_selectable(+1);
    } else if (it.type == "back") {
        back();
    } else if (it.type == "toggle" || it.type == "choice") {
        adjust(+1);
    } else if (it.type == "text") {
        // The call site opens the full-canvas text-editor overlay for this
        // key (the row itself cannot host multi-line editing).
        return "__edit_text:" + it.key;
    } else if (it.type == "action") {
        auto cb = actions_.find(it.action);
        if (cb != actions_.end() && cb->second) cb->second();
        return it.action;
    }
    return {};
}

void Menu::back() {
    if (!is_open()) return;
    if (stack_.size() > 1) stack_.pop_back();
    else close();
}

}  // namespace olduvai::presentation
