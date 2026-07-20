// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pause-menu action semantics.  Exit actions (warp/restart/quit/load) must
// LEAVE pause_open set: the run loop's pause block is the only consumer of
// their outcome flags, so an action that also closes the pause overlay
// strands its flag until the menu is next opened (the "Warp! does nothing
// until the next ESC" bug).  In-place actions (refill) close the overlay
// themselves because they carry no outcome flag.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "doctest/doctest.h"

#include "presentation/pause_flow.hpp"
#include "presentation/replay.hpp"
#include "presentation/save_state.hpp"

using namespace olduvai::presentation;

namespace {

struct Rig {
    InputReplay replay;          // default: inactive
    PauseBindings bind;
    GameOptions opts;
    std::optional<SaveState> out_load;
    bool pause_open = true;
    bool abort_to_title = false;
    bool want_quit_program = false;
    bool want_restart = false;
    bool want_load = false;
    bool god_active = false;
    int want_warp = 0;
    PauseActionsDeps deps;
    MenuActionTable actions;

    Rig() {
        deps = {/*g=*/nullptr,     &replay,       &bind,       &opts,
                &out_load,         &pause_open,   &abort_to_title,
                &want_quit_program, &want_restart, &want_load,
                &god_active,       &want_warp,    /*display_level=*/1};
        actions = make_pause_actions(&deps);
    }
};

}  // namespace

TEST_CASE("cheat_warp raises the flag and leaves pause_open for the consumer") {
    Rig r;
    r.bind.mem["cheat.start_level"] = "3";
    r.actions.at("cheat_warp")();
    CHECK(r.want_warp == 3);
    CHECK(r.pause_open);   // the pause block consumes want_warp; closing the
                           // overlay here would strand the flag (the bug)
}

TEST_CASE("cheat_warp ignores an out-of-range level") {
    Rig r;
    r.bind.mem["cheat.start_level"] = "9";
    r.actions.at("cheat_warp")();
    CHECK(r.want_warp == 0);
    CHECK(r.pause_open);
}

TEST_CASE("restart_level uses the same leave-open pattern") {
    Rig r;
    r.actions.at("restart_level")();
    CHECK(r.want_restart);
    CHECK(r.pause_open);
}

// ─── enhance.* staging (Style-preset Discard bug) ────────────────────────────
// The Style preset's enhance.* flags must ride the SAME staging session as
// every other editable key: nothing hits play.json before Apply, Discard
// reverts cleanly, and Apply both persists the granular list (without
// clobbering a staged master flag) and adopts the flags into the live
// GameOptions so they take effect in-session.

namespace {

const char* const kEnhanceKeys[] = {
    "enhance.smooth_motion", "enhance.hd_text",      "enhance.hud_overlay",
    "enhance.cinematic_cue", "enhance.fluid_bubbles",
    "enhance.secret_slide",  "enhance.descent_pan"};

struct PersistRecorder {
    std::vector<std::pair<std::string, std::string>> writes;
    PersistFn fn;
    PersistRecorder() {
        fn = [this](const std::string& k, const std::string& v) {
            writes.emplace_back(k, v);
        };
    }
    int count(const std::string& k) const {
        int n = 0;
        for (const auto& w : writes) n += w.first == k;
        return n;
    }
    bool has(const std::string& k, const std::string& v) const {
        for (const auto& w : writes)
            if (w.first == k && w.second == v) return true;
        return false;
    }
    // Index of the first write of `k` (=`v` if given), or -1.
    int index_of(const std::string& k, const char* v = nullptr) const {
        for (std::size_t i = 0; i < writes.size(); ++i)
            if (writes[i].first == k && (!v || writes[i].second == v))
                return static_cast<int>(i);
        return -1;
    }
};

// Classic-session baseline, seeded the way configure_pause_bind does.
void seed_classic(PauseBindings& bind) {
    bind.mem["enhanced"] = "false";
    for (const char* k : kEnhanceKeys) bind.mem[k] = "0";
    bind.mem["hd_profile"] = "native";
    bind.mem["render_scale"] = "2";
    bind.mem["aspect"] = "keep";
    bind.cur = {false, "native", 2, "auto", "auto"};
}

// Full-HD baseline (the "hd" preset shape).
void seed_hd(PauseBindings& bind) {
    bind.mem["enhanced"] = "true";
    for (const char* k : kEnhanceKeys) bind.mem[k] = "1";
    bind.mem["hd_profile"] = "omniscale";
    bind.mem["render_scale"] = "4";
    bind.mem["aspect"] = "widescreen";
    bind.cur = {true, "omniscale", 4, "auto", "auto"};
}

// mem iterates alphabetically, so the persisted granular list is the dashed
// tokens in key order.
const char* const kFullList =
    "cinematic-cue,descent-pan,fluid-bubbles,hd-text,hud-overlay,"
    "secret-slide,smooth-motion";

// Flow rig: PauseBindings + make_pause_flow over a minimal two-screen model
// ("options" subtree root + a "pause" screen outside it).
struct FlowRig {
    MenuModel model;
    PauseBindings bind;
    GameOptions opts;
    PersistRecorder rec;
    SettingsSession session;
    ConfirmDialog confirm;
    PendingReinit reinit_req;
    bool want_reinit = false;
    PauseFlowDeps deps;
    std::optional<SettingsFlow> flow;

    FlowRig() {
        model.screens["options"];
        model.screens["pause"];
        bind.persist = &rec.fn;
        bind.session = &session;
        deps = {/*menu=*/nullptr, &opts, &bind, &reinit_req, &want_reinit};
        flow.emplace(make_pause_flow(model, session, confirm, &deps));
    }
    // Stage `edit` inside the Options subtree, then leave it → confirm opens.
    template <typename Edit>
    void stage_and_exit(Edit edit) {
        flow->track_screen("options");
        edit();
        flow->track_screen("pause");
    }
};

}  // namespace

TEST_CASE("enhance toggle stages instead of persisting immediately") {
    PauseBindings bind;
    SettingsSession session;
    PersistRecorder rec;
    bind.persist = &rec.fn;
    bind.session = &session;
    bind.mem["enhance.hd_text"] = "0";

    bind.set("enhance.hd_text", "1");

    CHECK(rec.writes.empty());   // nothing reaches play.json before Apply
    REQUIRE(session.changes().size() == 1);
    CHECK(session.changes()[0].key == "enhance.hd_text");
    CHECK(session.changes()[0].old_value == "0");
    CHECK(session.changes()[0].new_value == "1");
    CHECK(bind.get("enhance.hd_text") == "1");   // menu row shows the edit
}

TEST_CASE("Style preset fan-out stages everything, writes nothing") {
    PauseBindings bind;
    SettingsSession session;
    PersistRecorder rec;
    bind.persist = &rec.fn;
    bind.session = &session;
    seed_classic(bind);

    bind.set("preset", "hd");

    CHECK(rec.writes.empty());
    // enhanced first (sessions drain in stage order), then the 7 flags,
    // then hd_profile / render_scale / aspect.
    REQUIRE(session.changes().size() == 11);
    CHECK(session.changes().front().key == "enhanced");
    CHECK(session.changes().front().new_value == "true");
    for (const char* k : kEnhanceKeys) {
        bool staged = false;
        for (const auto& ch : session.changes())
            staged |= ch.key == k && ch.new_value == "1";
        CHECK_MESSAGE(staged, k);
    }
}

TEST_CASE("re-clicking the current style nets out to an empty session") {
    PauseBindings bind;
    SettingsSession session;
    PersistRecorder rec;
    bind.persist = &rec.fn;
    bind.session = &session;
    seed_hd(bind);

    bind.set("preset", "hd");

    CHECK(rec.writes.empty());
    CHECK(session.empty());   // no-op preset click → no confirm dialog
}

TEST_CASE("configure_pause_bind seeds the enhanced master baseline") {
    PauseBindings bind;
    SettingsSession session;
    GameOptions opts;
    opts.enhanced = true;
    ScaledWindow sw;   // null win/ren: SDL_GetWindowFlags(nullptr) is safe
    bool god = false, want_reinit = false;
    PendingReinit rr;
    int lw = 0, lh = 0;
    PauseBindWireDeps d{&god, /*audio=*/nullptr, &sw,  &opts, &session,
                        &want_reinit, &rr, &lw, &lh, /*hd_scale=*/2,
                        /*display_level=*/1};
    configure_pause_bind(bind, d);
    CHECK(bind.mem["enhanced"] == "true");
}

TEST_CASE("pause Apply persists the granular list once without master clobber") {
    FlowRig r;
    seed_classic(r.bind);

    r.stage_and_exit([&] { r.bind.set("preset", "hd"); });
    REQUIRE(r.flow->confirm_open());
    r.flow->handle_key(SettingsFlow::Key::kAccept);   // Apply is pre-selected

    // The staged master flag persists, and the granular list flushes exactly
    // once — with NO "enhanced=false" companion clobbering the master.
    CHECK(r.rec.has("enhanced", "true"));
    CHECK(!r.rec.has("enhanced", "false"));
    CHECK(r.rec.count("enhance") == 1);
    CHECK(r.rec.has("enhance", kFullList));
    // Master before list: stage order is the load-order contract.
    CHECK(r.rec.index_of("enhanced", "true") < r.rec.index_of("enhance"));
    // In-session adoption: the applied flags land in the live GameOptions.
    CHECK(r.opts.enhance.smooth_motion);
    CHECK(r.opts.enhance.hd_text);
    CHECK(r.opts.enhance.hud_overlay);
    CHECK(r.opts.enhance.cinematic_cue);
    CHECK(r.opts.enhance.fluid_bubbles);
    CHECK(r.opts.enhance.secret_slide);
    CHECK(r.opts.enhance.descent_pan);
    // Classic→HD crosses the scale boundary → reinit rides the master flag.
    CHECK(r.want_reinit);
    CHECK(r.reinit_req.enhanced);
}

TEST_CASE("pause Discard reverts the preset fan-out and writes nothing") {
    FlowRig r;
    seed_classic(r.bind);

    r.stage_and_exit([&] { r.bind.set("preset", "hd"); });
    REQUIRE(r.flow->confirm_open());
    r.flow->handle_key(SettingsFlow::Key::kNext);     // move to Discard
    r.flow->handle_key(SettingsFlow::Key::kAccept);

    CHECK(r.rec.writes.empty());   // play.json untouched — THE Discard bug
    CHECK(r.session.empty());
    CHECK(r.bind.get("enhanced") == "false");
    CHECK(r.bind.get("enhance.hd_text") == "0");
    CHECK(r.bind.get("hd_profile") == "native");
    CHECK(!r.opts.enhance.any());
    CHECK(!r.want_reinit);
}

TEST_CASE("granular toggle Apply keeps the enhanced=false companion") {
    FlowRig r;
    seed_hd(r.bind);
    r.opts.enhanced = true;
    r.opts.enhance = EnhanceFlags::all();

    r.stage_and_exit([&] { r.bind.set("enhance.hd_text", "0"); });
    REQUIRE(r.flow->confirm_open());
    r.flow->handle_key(SettingsFlow::Key::kAccept);

    // Master NOT staged → the granular write converts a bundle config into
    // the explicit list, so the companion "enhanced=false" is required.
    CHECK(r.rec.count("enhance") == 1);
    CHECK(r.rec.has("enhance",
                    "cinematic-cue,descent-pan,fluid-bubbles,hud-overlay,"
                    "secret-slide,smooth-motion"));
    CHECK(r.rec.has("enhanced", "false"));
    // In-session adoption: hd_text off, the rest untouched.
    CHECK(!r.opts.enhance.hd_text);
    CHECK(r.opts.enhance.smooth_motion);
    CHECK(!r.want_reinit);   // granular flags are not reinit-class
}
