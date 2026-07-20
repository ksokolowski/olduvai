// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/settings_flow.hpp"

#include <utility>

namespace olduvai::presentation {

std::set<std::string> options_subtree_screens(const MenuModel& model,
                                              const std::string& root) {
    std::set<std::string> out;
    std::vector<std::string> stack;
    out.insert(root);
    stack.push_back(root);
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        const auto it = model.screens.find(id);
        if (it == model.screens.end()) continue;   // dangling target: keep, don't recurse
        for (const MenuItem& item : it->second.items) {
            if (item.type != "submenu" || item.target.empty()) continue;
            if (out.insert(item.target).second) stack.push_back(item.target);
        }
    }
    return out;
}

std::vector<StagedChange> build_display_changes(const SettingsSession& sess,
                                                const MenuModel& model) {
    auto find_item = [&](const std::string& key) -> const MenuItem* {
        for (const auto& [sname, scr] : model.screens)
            for (const auto& it : scr.items)
                if (it.key == key) return &it;
        return nullptr;
    };
    auto resolve_value = [](const MenuItem& it,
                            const std::string& raw) -> std::string {
        for (std::size_t i = 0; i < it.values.size(); ++i) {
            if (it.values[i] == raw) {
                if (i < it.value_labels.size()) return it.value_labels[i];
                break;
            }
        }
        return raw;
    };
    std::vector<StagedChange> out;
    for (const auto& ch : sess.changes()) {
        StagedChange disp = ch;
        if (const MenuItem* item = find_item(ch.key)) {
            disp.label     = item->label;
            disp.old_value = resolve_value(*item, ch.old_value);
            disp.new_value = resolve_value(*item, ch.new_value);
        }
        out.push_back(std::move(disp));
    }
    return out;
}

SettingsFlow::SettingsFlow(const MenuModel& model, SettingsSession& session,
                           ConfirmDialog& dialog, Hooks hooks)
    : model_(model), session_(session), dialog_(dialog),
      hooks_(std::move(hooks)),
      subtree_(options_subtree_screens(model)) {}

bool SettingsFlow::any_reinit_staged_() const {
    if (!hooks_.classify) return false;
    for (const auto& ch : session_.changes())
        if (hooks_.classify(ch.key, ch.new_value) == ApplyTier::Reinit)
            return true;
    return false;
}

void SettingsFlow::track_screen(const std::string& menu_screen) {
    if (dialog_.is_open()) return;
    const bool in_options = in_options_subtree(menu_screen);
    if (was_in_options_ && !in_options && !session_.empty()) {
        bool any_reinit = false, any_persist = false;
        if (hooks_.classify) {
            for (const auto& ch : session_.changes()) {
                const ApplyTier t = hooks_.classify(ch.key, ch.new_value);
                if (t == ApplyTier::Reinit) any_reinit = true;
                else if (t == ApplyTier::PersistOnly) any_persist = true;
            }
        }
        dialog_.open("Apply changes?",
                     build_display_changes(session_, model_),
                     hooks_.confirm_note
                         ? hooks_.confirm_note(any_reinit, any_persist)
                         : std::string{});
    }
    was_in_options_ = in_options;
}

SettingsFlow::KeyOutcome SettingsFlow::handle_key(Key k) {
    if (!dialog_.is_open()) return KeyOutcome::kIgnored;
    switch (k) {
        case Key::kPrev:
            dialog_.move(-1);          // left/up → towards Apply
            return KeyOutcome::kConsumed;
        case Key::kNext:
            dialog_.move(1);           // right/down → towards Discard
            return KeyOutcome::kConsumed;
        case Key::kAccept: {
            const bool do_apply = dialog_.apply_selected();
            dialog_.close();
            if (do_apply) {
                apply_();
                return KeyOutcome::kApplied;
            }
            discard();
            return KeyOutcome::kDiscarded;
        }
        case Key::kCancel:
            // Cancel: close dialog, re-open Options to keep editing.
            dialog_.close();
            if (hooks_.reopen_options) hooks_.reopen_options();
            return KeyOutcome::kCancelled;
        case Key::kNone:
        default:
            return KeyOutcome::kConsumed;  // dialog swallows all other keys
    }
}

void SettingsFlow::apply_() {
    // APPLY: persist every staged change, hand its environment effect to the
    // call site, and report once whether any key was reinit-class.  §8.6.
    if (hooks_.apply_begin) hooks_.apply_begin();
    bool needs_reinit = false;
    for (const auto& ch : session_.changes()) {
        if (hooks_.persist) hooks_.persist(ch.key, ch.new_value);
        const ApplyTier tier = hooks_.classify
                                   ? hooks_.classify(ch.key, ch.new_value)
                                   : ApplyTier::PersistOnly;
        if (tier == ApplyTier::Reinit) needs_reinit = true;
        if (hooks_.apply_change) hooks_.apply_change(ch, tier);
    }
    session_.clear();
    if (hooks_.apply_done) hooks_.apply_done(needs_reinit);
}

void SettingsFlow::discard() {
    // DISCARD: revert staged previews to baseline, clear, close.  §8.6.
    for (const auto& ch : session_.changes())
        if (hooks_.revert_change) hooks_.revert_change(ch);
    session_.clear();
    dialog_.close();
}

}  // namespace olduvai::presentation
