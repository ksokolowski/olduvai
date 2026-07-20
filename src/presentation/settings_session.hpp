// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Provisional Options edits as a net diff vs a per-key baseline.
//
// The Options menus stage every change here instead of committing it: safe
// changes still preview live, but nothing is persisted or re-initialized until
// the user confirms.  On leaving Options, a non-empty session drives the
// "Save & Apply? / Discard" dialog; Apply commits + reinits, Discard reverts.
//
// Pure (no SDL).
#pragma once

#include <string>
#include <vector>

namespace olduvai::presentation {

struct StagedChange {
    std::string key;         // config/menu key
    std::string label;       // friendly row label for the dialog
    std::string old_value;   // display token at the FIRST stage of this key
    std::string new_value;   // current staged display token
};

// Tracks the NET change per key vs the value first seen for that key.  An edit
// that returns a key to its original value drops out of the diff.  changes()
// preserves first-staged order.
class SettingsSession {
public:
    void clear() { changes_.clear(); }
    bool empty() const { return changes_.empty(); }

    // Record an edit: `old_value` is the value BEFORE this edit, `new_value`
    // after.  The first stage of a key fixes its baseline `old_value`; later
    // stages only move `new_value`.  If the net (baseline → new) is a no-op the
    // key is removed from the diff.
    void stage(const std::string& key, const std::string& label,
               const std::string& old_value, const std::string& new_value);

    const std::vector<StagedChange>& changes() const { return changes_; }

private:
    std::vector<StagedChange> changes_;
};

}  // namespace olduvai::presentation
