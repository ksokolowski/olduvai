// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SettingsFlow: one controller for the Options stage / confirm / apply flow,
// used by the in-game Pause, the boss pause and the main menu.
//   * Options-subtree membership, derived from the MenuModel;
//   * staging and subtree-exit detection (leaving Options with staged changes
//     opens the confirm dialog);
//   * the ConfirmDialog lifecycle and keys;
//   * Apply drains the session through the tier classifier; Discard reverts.
// Environment effects are hooks (pause: a reinit target; main menu:
// rt.* and an in-place rebuild).  No SDL.

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/settings_apply.hpp"
#include "presentation/menu/settings_session.hpp"

namespace olduvai::presentation {

// Screens in the Options subtree: `root` plus every screen reachable through
// `submenu` targets, so menus.json stays the source of truth ("dev" and
// "cheats" are not reachable).  A target missing from the model is included
// but not recursed into.
std::set<std::string> options_subtree_screens(const MenuModel& model,
                                              const std::string& root = "options");

// Staged changes as display rows (labels from the MenuModel), for the confirm
// dialog.  With `value_of`, a Sound card pick (music_device and/or
// sfx_backend) shows as one row ("Sound card: Auto -> Sound Blaster"); a pair
// no card names keeps the raw rows.
std::vector<StagedChange> build_display_changes(
    const SettingsSession& sess, const MenuModel& model,
    const std::function<std::string(const std::string&)>& value_of = {});

class SettingsFlow {
public:
    // Dialog keys (call sites map SDL keys); kNone = any other key, consumed.
    enum class Key { kNone, kPrev, kNext, kAccept, kCancel };
    // kAnswered: a yes/no question (ConfirmDialog::ask) resolved.  Accept runs
    // Yes if selected; Cancel answers No.  The session is untouched.
    enum class KeyOutcome { kIgnored, kConsumed, kApplied, kDiscarded, kCancelled,
                            kAnswered };

    struct Hooks {
        // Persist one applied change (config write; play.json).
        std::function<void(const std::string& key, const std::string& value)>
            persist;
        // Tier of one staged change against the environment's live baseline.
        std::function<ApplyTier(const std::string& key,
                                const std::string& value)> classify;
        // Optional, once at the start of Apply (pause seeds its reinit target).
        std::function<void()> apply_begin;
        // Effect of one applied change (pause: the reinit target / live hd_profile;
        // main menu: rt.*).
        std::function<void(const StagedChange&, ApplyTier)> apply_change;
        // Once after the session is drained and cleared; needs_reinit = any
        // change was Reinit-tier.
        std::function<void(bool needs_reinit)> apply_done;
        // Revert one change's live preview (Discard, close-without-apply).
        std::function<void(const StagedChange&)> revert_change;
        // Cancel in the dialog: reopen Options to keep editing.
        std::function<void()> reopen_options;
        // The note under the change list.  any_persist = some change takes
        // effect only on the next launch (the note must say so, or Apply looks
        // like a no-op).
        std::function<std::string(bool any_reinit, bool any_persist)>
            confirm_note;
        // Optional: a key's current value, to name a Sound card pick as one
        // row.
        std::function<std::string(const std::string&)> value_of;
    };

    SettingsFlow(const MenuModel& model, SettingsSession& session,
                 ConfirmDialog& dialog, Hooks hooks);

    bool in_options_subtree(const std::string& screen_id) const {
        return subtree_.count(screen_id) != 0;
    }

    // Per-frame subtree-exit detection, after input handling, with the menu's
    // current screen (only while the menu is open and the dialog closed; also
    // guarded here).  Leaving the subtree with staged changes opens the dialog.
    void track_screen(const std::string& menu_screen);

    // A key while the dialog is open; kIgnored if it is closed.
    KeyOutcome handle_key(Key k);

    // Revert every staged change, clear the session, close the dialog.  Also
    // the close-without-apply path (Resume / Start Game / Quit with a dirty
    // session).
    void discard();

    bool confirm_open() const { return dialog_.is_open(); }

private:
    void apply_();
    bool any_reinit_staged_() const;

    const MenuModel& model_;
    SettingsSession& session_;
    ConfirmDialog& dialog_;
    Hooks hooks_;
    std::set<std::string> subtree_;
    bool was_in_options_ = false;  // state from the previous frame
};

}  // namespace olduvai::presentation
