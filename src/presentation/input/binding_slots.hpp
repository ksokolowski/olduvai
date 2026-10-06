// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// An action's bindings as two slots, primary and alternate, stored as one
// comma-separated setting ("b,y", "Left,A").  A group is a set of actions
// that must not share a binding (the play actions of one device): putting a
// binding in one hands the slot's old binding to whoever held it, so every
// pick is reachable.
//
// Pure and header-only: no SDL.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace olduvai::presentation {

using BindingList = std::vector<std::string>;

inline constexpr int kBindingSlots = 2;

// "b, y" -> {"b", "y"}; empty entries dropped, at most two kept.
inline BindingList split_binding(const std::string& s) {
    BindingList out;
    std::size_t i = 0;
    while (i <= s.size() && out.size() < kBindingSlots) {
        std::size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        const auto b = part.find_first_not_of(' ');
        const auto e = part.find_last_not_of(' ');
        part = b == std::string::npos ? std::string{} : part.substr(b, e - b + 1);
        if (!part.empty()) out.push_back(part);
        i = j + 1;
    }
    return out;
}

inline std::string join_binding(const BindingList& l) {
    std::string out;
    for (const std::string& n : l) {
        if (!out.empty()) out += ',';
        out += n;
    }
    return out;
}

// split_binding, keeping only the names `valid` accepts.
template <class Valid>
BindingList valid_bindings(const std::string& s, Valid valid) {
    BindingList out = split_binding(s);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](const std::string& n) { return !valid(n); }),
              out.end());
    return out;
}

inline bool holds(const BindingList& l, const std::string& name) {
    return std::find(l.begin(), l.end(), name) != l.end();
}

// Whether no two lists share a binding and none is empty.
inline bool group_ok(const std::vector<const BindingList*>& group) {
    for (std::size_t i = 0; i < group.size(); ++i) {
        if (group[i]->empty()) return false;
        for (std::size_t j = i + 1; j < group.size(); ++j)
            for (const std::string& n : *group[i])
                if (holds(*group[j], n)) return false;
    }
    return true;
}

// Put `name` in `slot` of *group[target].  An alternate slot past the end
// appends.  A list in the group holding `name` takes the slot's old binding,
// or loses `name` when the slot was empty.  False, with nothing changed, when
// that would leave a list with no binding, or for a bad slot.
inline bool rebind_in_group(const std::vector<BindingList*>& group,
                            std::size_t target, int slot,
                            const std::string& name) {
    if (target >= group.size() || slot < 0 || slot >= kBindingSlots ||
        name.empty())
        return false;
    BindingList& t = *group[target];
    const auto s = static_cast<std::size_t>(slot);
    if (s > t.size()) return false;   // no alternate without a primary
    // Already this action's: the two slots trade places.
    const auto own = std::find(t.begin(), t.end(), name);
    if (own != t.end()) {
        if (s < t.size()) std::iter_swap(own, t.begin() + slot);
        return true;
    }
    const std::string old = s < t.size() ? t[s] : std::string{};
    for (std::size_t g = 0; g < group.size(); ++g) {
        if (g == target) continue;
        BindingList& other = *group[g];
        const auto it = std::find(other.begin(), other.end(), name);
        if (it == other.end()) continue;
        if (!old.empty()) {
            *it = old;
        } else {
            if (other.size() == 1) return false;
            other.erase(it);
        }
    }
    if (s < t.size())
        t[s] = name;
    else
        t.push_back(name);
    return true;
}

}  // namespace olduvai::presentation
