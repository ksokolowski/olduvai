// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/settings_session.hpp"

namespace olduvai::presentation {

void SettingsSession::stage(const std::string& key, const std::string& label,
                            const std::string& old_value,
                            const std::string& new_value) {
    for (auto it = changes_.begin(); it != changes_.end(); ++it) {
        if (it->key != key) continue;
        // Key already staged: keep its original baseline, move the new value.
        it->new_value = new_value;
        if (it->new_value == it->old_value) changes_.erase(it);  // net no-op
        return;
    }
    // First time this key is staged: only record a real change.
    if (old_value != new_value)
        changes_.push_back({key, label, old_value, new_value});
}

}  // namespace olduvai::presentation
