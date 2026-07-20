// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SettingsFlow — ONE controller for the Options staging/confirm/apply flow
// (OL-B1).  Owns the pieces that were previously triplicated across
// game_app.cpp (in-game Pause path, main-menu path, discard helpers):
//
//   * options-subtree membership — derived from the MenuModel (screens
//     reachable from the "options" screen via submenu targets), replacing the
//     verbatim-duplicated hardcoded lambda;
//   * the SettingsSession staging handoff and subtree-exit detection
//     (leaving Options with staged changes opens the confirm dialog);
//   * ConfirmDialog lifecycle — open contents, Prev/Next/Accept/Cancel keys;
//   * apply/discard resolution — Apply drains the session through the
//     tier-classifier, Discard reverts every staged change.
//
// Environment-specific EFFECTS are injected as hooks: the pause call site
// routes reinit-class keys into a PendingReinit request (kReinitDisplay),
// the main-menu call site writes rt.* and rebuilds window/audio in place.
// Same policy, one encoding.  Pure logic: no SDL.
//
// Spec: OL-B1 (SettingsFlow controller extraction; internal design notes).

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "presentation/confirm_dialog.hpp"
#include "presentation/menu.hpp"
#include "presentation/settings_apply.hpp"
#include "presentation/settings_session.hpp"

namespace olduvai::presentation {

// Screens inside the Options subtree: `root` plus every screen reachable from
// it via `submenu` item targets.  Derived from the model so menus.json stays
// the single source of truth (the hardcoded set this replaces listed exactly
// "options","audio","video","enhancements","cave_paintings"; "dev" and
// "cheats" are NOT reachable from "options" and stay out).  A target id is
// included even if its screen is missing from the model (dangling targets
// are diagnosed elsewhere); traversal only recurses into existing screens.
std::set<std::string> options_subtree_screens(const MenuModel& model,
                                              const std::string& root = "options");

// Resolve staged session changes into display-friendly StagedChange rows,
// looking up labels and value-labels from the MenuModel.  Used for the
// confirm-dialog contents by both the Pause and main-menu flows.
std::vector<StagedChange> build_display_changes(const SettingsSession& sess,
                                                const MenuModel& model);

class SettingsFlow {
public:
    // Semantic dialog keys (call sites map SDL keycodes; kNone = any other
    // key — consumed while the dialog is open, no effect).
    enum class Key { kNone, kPrev, kNext, kAccept, kCancel };
    enum class KeyOutcome { kIgnored, kConsumed, kApplied, kDiscarded, kCancelled };

    struct Hooks {
        // Persist one applied change (config write; play.json).
        std::function<void(const std::string& key, const std::string& value)>
            persist;
        // Tier-classify one staged change against the environment's live
        // baseline (classify_change(key, value, cur)).
        std::function<ApplyTier(const std::string& key,
                                const std::string& value)> classify;
        // Optional: runs once at the start of Apply, before any change is
        // drained (the pause site seeds its PendingReinit from the live rt).
        std::function<void()> apply_begin;
        // Environment effect of one applied change (pause: PendingReinit
        // field / live rt_hd_profile; main menu: rt.* write).
        std::function<void(const StagedChange&, ApplyTier)> apply_change;
        // Runs once after the session is drained + cleared;
        // `needs_reinit` = any staged change classified ApplyTier::Reinit
        // (pause: raise want_reinit; main menu: in-place rebuild).
        std::function<void(bool needs_reinit)> apply_done;
        // Revert the live preview of one staged change to its baseline
        // (Discard, and close-without-apply).
        std::function<void(const StagedChange&)> revert_change;
        // Cancel (ESC in the dialog): re-open the Options screen to keep
        // editing.
        std::function<void()> reopen_options;
        // Note line under the confirm-dialog change list.  `any_reinit` =
        // some staged key classifies Reinit; `any_persist` = some staged key
        // classifies PersistOnly (i.e. takes effect only on the next launch
        // — the note should SAY so, or the Apply looks like a no-op).
        std::function<std::string(bool any_reinit, bool any_persist)>
            confirm_note;
    };

    SettingsFlow(const MenuModel& model, SettingsSession& session,
                 ConfirmDialog& dialog, Hooks hooks);

    bool in_options_subtree(const std::string& screen_id) const {
        return subtree_.count(screen_id) != 0;
    }

    // Per-frame subtree-exit detection.  Call after input handling, with the
    // menu's current screen, only while the menu is open and the confirm
    // dialog is closed (both call sites guard this; also guarded here).  On
    // an inside→outside transition with staged changes, opens the dialog.
    void track_screen(const std::string& menu_screen);

    // Handle a key while the confirm dialog is open.  Returns kIgnored if
    // the dialog is closed (the call site should not have routed the key).
    KeyOutcome handle_key(Key k);

    // Revert all staged changes through revert_change, clear the session,
    // close the dialog.  Also the close-without-apply path (Resume / Start
    // Game / Quit with a dirty session = implicit discard).
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
