// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SettingsFlow controller tests (OL-B1): options-subtree membership derived
// from a synthetic MenuModel, confirm-open on subtree exit with staged
// changes, apply draining the session through the tier hooks in order,
// discard reverting through the revert hook, and cancel/close-without-apply.

#include "presentation/settings_flow.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace olduvai::presentation;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n",         \
                                    __LINE__, #cond); ++fails; }          \
    } while (0)

namespace {

MenuItem submenu(const std::string& id, const std::string& target) {
    MenuItem it;
    it.id = id;
    it.type = "submenu";
    it.label = id;
    it.target = target;
    return it;
}

// Synthetic model mirroring the menus.json shape: the options subtree is
// options → {audio, video, enhancements}, enhancements → cave_paintings.
// "cheats"/"dev" hang off "pause"/nothing and must stay OUT of the subtree.
MenuModel make_model() {
    MenuModel m;
    m.screens["main"].items = {submenu("options", "options")};
    m.screens["pause"].items = {submenu("options", "options"),
                                submenu("cheats", "cheats")};
    m.screens["options"].items = {submenu("audio", "audio"),
                                  submenu("video", "video"),
                                  submenu("enhancements", "enhancements")};
    m.screens["audio"].items = {};
    m.screens["video"].items = {};
    m.screens["enhancements"].items = {submenu("cave_paintings",
                                               "cave_paintings")};
    m.screens["cave_paintings"].items = {};
    m.screens["cheats"].items = {submenu("spawn_bonus", "cheat_bonus")};
    m.screens["cheat_bonus"].items = {};
    m.screens["dev"].items = {};
    // A labelled choice item so build_display_changes resolution is covered.
    MenuItem dev(  // music_device choice on the audio screen
        [] { MenuItem it; it.id = "music_device"; it.type = "choice";
             it.label = "Music device"; it.key = "music_device";
             it.values = {"auto", "gm-builtin"};
             it.value_labels = {"Auto", "GM (builtin)"}; return it; }());
    m.screens["audio"].items.push_back(dev);
    return m;
}

// Hook recorder: one line per callback invocation, order-preserving.
struct Recorder {
    std::vector<std::string> log;
    bool last_needs_reinit = false;
    int apply_done_calls = 0;

    SettingsFlow::Hooks hooks(const DisplaySettings& cur) {
        SettingsFlow::Hooks h;
        h.persist = [this](const std::string& k, const std::string& v) {
            log.push_back("persist:" + k + "=" + v);
        };
        h.classify = [cur](const std::string& k, const std::string& v) {
            return classify_change(k, v, cur);
        };
        h.apply_begin = [this]() { log.push_back("begin"); };
        h.apply_change = [this](const StagedChange& ch, ApplyTier tier) {
            const char* t = tier == ApplyTier::Live      ? "live"
                            : tier == ApplyTier::Reinit  ? "reinit"
                                                         : "persist-only";
            log.push_back("apply:" + ch.key + "=" + ch.new_value + ":" + t);
        };
        h.apply_done = [this](bool needs_reinit) {
            ++apply_done_calls;
            last_needs_reinit = needs_reinit;
            log.push_back(needs_reinit ? "done:reinit" : "done:no-reinit");
        };
        h.revert_change = [this](const StagedChange& ch) {
            log.push_back("revert:" + ch.key + "=" + ch.old_value);
        };
        h.reopen_options = [this]() { log.push_back("reopen"); };
        h.confirm_note = [](bool any_reinit, bool any_persist) {
            if (any_reinit) return std::string("reload note");
            if (any_persist) return std::string("next-launch note");
            return std::string{};
        };
        return h;
    }
};

}  // namespace

int main() {
    const MenuModel model = make_model();
    DisplaySettings cur;   // enhanced=false, hd=native, scale=2, auto/auto

    // ── Subtree membership derived from the model ──────────────────────────
    {
        const auto sub = options_subtree_screens(model);
        CHECK(sub.count("options") == 1);
        CHECK(sub.count("audio") == 1);
        CHECK(sub.count("video") == 1);
        CHECK(sub.count("enhancements") == 1);
        CHECK(sub.count("cave_paintings") == 1);
        CHECK(sub.size() == 5);              // exactly the hardcoded legacy set
        CHECK(sub.count("dev") == 0);        // required exclusions
        CHECK(sub.count("cheats") == 0);
        CHECK(sub.count("cheat_bonus") == 0);
        CHECK(sub.count("pause") == 0);
        CHECK(sub.count("main") == 0);

        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        CHECK(flow.in_options_subtree("options"));
        CHECK(flow.in_options_subtree("cave_paintings"));
        CHECK(!flow.in_options_subtree("dev"));
        CHECK(!flow.in_options_subtree("cheats"));
        CHECK(!flow.in_options_subtree("pause"));
        CHECK(!flow.in_options_subtree("no_such_screen"));
    }

    // ── Staged changes → confirm opens on subtree exit (and only then) ─────
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));

        // Clean exit: no staged changes → no dialog.
        flow.track_screen("options");
        flow.track_screen("pause");
        CHECK(!d.is_open());

        // Dirty exit: staged change + inside→outside transition → dialog.
        flow.track_screen("audio");
        s.stage("music_device", "music_device", "auto", "gm-builtin");
        flow.track_screen("audio");           // still inside: no dialog
        CHECK(!d.is_open());
        flow.track_screen("pause");           // exit → dialog opens
        CHECK(d.is_open());
        CHECK(d.title() == "Apply changes?");
        CHECK(d.changes().size() == 1);
        // Display rows resolved via the model (label + value labels).
        CHECK(d.changes()[0].label == "Music device");
        CHECK(d.changes()[0].old_value == "Auto");
        CHECK(d.changes()[0].new_value == "GM (builtin)");
        CHECK(d.note() == "reload note");     // music_device auto→gm = Reinit
        // While the dialog is open, tracking is inert (call sites guard too).
        flow.track_screen("pause");
        CHECK(d.is_open());
    }

    // Live-only staged change → note reflects any_reinit == false.
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        flow.track_screen("audio");
        s.stage("music_volume", "music_volume", "100", "60");
        flow.track_screen("pause");
        CHECK(d.is_open());
        CHECK(d.note().empty());
    }

    // ── Apply drains the session through the tier hooks in order ───────────
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        flow.track_screen("audio");
        s.stage("music_volume", "music_volume", "100", "60");
        s.stage("music_device", "music_device", "auto", "gm-builtin");
        flow.track_screen("pause");
        CHECK(d.is_open());
        CHECK(d.apply_selected());            // cursor resets to Apply
        const auto out = flow.handle_key(SettingsFlow::Key::kAccept);
        CHECK(out == SettingsFlow::KeyOutcome::kApplied);
        CHECK(!d.is_open());
        CHECK(s.empty());                     // session drained
        CHECK(rec.apply_done_calls == 1);
        CHECK(rec.last_needs_reinit);         // music_device change = Reinit
        const std::vector<std::string> want = {
            "begin",
            "persist:music_volume=60",  "apply:music_volume=60:live",
            "persist:music_device=gm-builtin",
            "apply:music_device=gm-builtin:reinit",
            "done:reinit",
        };
        CHECK(rec.log == want);               // staged order preserved
    }

    // Apply with only Live changes reports needs_reinit == false.
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        flow.track_screen("video");
        s.stage("fullscreen", "fullscreen", "0", "1");
        flow.track_screen("main");
        CHECK(d.is_open());
        flow.handle_key(SettingsFlow::Key::kAccept);
        CHECK(rec.apply_done_calls == 1);
        CHECK(!rec.last_needs_reinit);
    }

    // ── Discard reverts through the revert hook (no persist, no apply) ─────
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        flow.track_screen("audio");
        s.stage("music_volume", "music_volume", "100", "60");
        s.stage("sfx_volume", "sfx_volume", "100", "30");
        flow.track_screen("pause");
        CHECK(d.is_open());
        // kPrev/kNext move the cursor; unknown keys are consumed, no effect.
        CHECK(flow.handle_key(SettingsFlow::Key::kNext) ==
              SettingsFlow::KeyOutcome::kConsumed);
        CHECK(!d.apply_selected());           // moved to Discard
        CHECK(flow.handle_key(SettingsFlow::Key::kNone) ==
              SettingsFlow::KeyOutcome::kConsumed);
        const auto out = flow.handle_key(SettingsFlow::Key::kAccept);
        CHECK(out == SettingsFlow::KeyOutcome::kDiscarded);
        CHECK(!d.is_open());
        CHECK(s.empty());
        const std::vector<std::string> want = {
            "revert:music_volume=100", "revert:sfx_volume=100"};
        CHECK(rec.log == want);
        CHECK(rec.apply_done_calls == 0);     // nothing applied or persisted
    }

    // ── ESC cancels: dialog closes, session KEPT, options reopened ─────────
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        flow.track_screen("audio");
        s.stage("music_volume", "music_volume", "100", "60");
        flow.track_screen("pause");
        CHECK(d.is_open());
        const auto out = flow.handle_key(SettingsFlow::Key::kCancel);
        CHECK(out == SettingsFlow::KeyOutcome::kCancelled);
        CHECK(!d.is_open());
        CHECK(!s.empty());                    // changes stay staged
        CHECK(rec.log == std::vector<std::string>{"reopen"});
    }

    // ── Resume/close-without-apply: discard() reverts + clears + closes ────
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        s.stage("aspect", "aspect", "keep", "wide");
        flow.discard();
        CHECK(s.empty());
        CHECK(!d.is_open());
        CHECK(rec.log == std::vector<std::string>{"revert:aspect=keep"});
    }

    // Keys while the dialog is closed are ignored (call-site misroute guard).
    {
        SettingsSession s;
        ConfirmDialog d;
        Recorder rec;
        SettingsFlow flow(model, s, d, rec.hooks(cur));
        CHECK(flow.handle_key(SettingsFlow::Key::kAccept) ==
              SettingsFlow::KeyOutcome::kIgnored);
        CHECK(rec.log.empty());
    }

    if (fails == 0) std::puts("settings_flow: OK");
    return fails == 0 ? 0 : 1;
}
