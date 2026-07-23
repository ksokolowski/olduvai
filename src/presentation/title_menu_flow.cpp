// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// run_title_menu — the intro/title sequence + interactive main menu, extracted
// VERBATIM from run_game (CC1, 2026-07-08). See title_menu_flow.hpp for the
// coverage caveat (compose is gated by mainmenu_shot; Options->apply->rebuild
// is playtest-only).

#include "presentation/title_menu_flow.hpp"

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "presentation/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/cache_paths.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_files.hpp"
#include "presentation/debug_overlay.hpp"
#include "presentation/game_render.hpp"
#include "presentation/tile_patterns.hpp"
#include "presentation/hud_render.hpp"
#include "presentation/dialog_key_map.hpp"
#include "presentation/l3_end_level.hpp"
#include "presentation/menu.hpp"
#include "presentation/menu_model.hpp"
#include "presentation/menu_nav.hpp"
#include "presentation/banner_fx.hpp"
#include "presentation/menu_render.hpp"
#include "presentation/save_state.hpp"
#include "presentation/replay.hpp"
#include "presentation/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/bug_capture.hpp"
#include "presentation/screen_tiles.hpp"
#include "presentation/screens.hpp"
#include "presentation/smooth_present.hpp"
#include "presentation/text_overlay.hpp"
#include "presentation/transition_players.hpp"
#include "presentation/widescreen_presenter.hpp"
#include "presentation/widescreen.hpp"
#include "presentation/window_util.hpp"
#include "systems/frame_runner.hpp"
#include "systems/screen_topology.hpp"
#include "systems/spawning.hpp"
#include "core/rng.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <functional>
#include <map>
#include <optional>

#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/confirm_dialog.hpp"
#include "presentation/enhance_flags.hpp"
#include "presentation/menu_script_util.hpp"
#include "presentation/settings_apply.hpp"
#include "presentation/settings_preview.hpp"
#include "presentation/settings_seed.hpp"
#include "presentation/settings_flow.hpp"
#include "presentation/settings_session.hpp"
#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/upscale.hpp"
#include "formats/mdi.hpp"
#include "formats/voc.hpp"
#include "systems/cave_logic.hpp"
#include "systems/collision_dispatch.hpp"
#include "systems/fluid_bubbles.hpp"
#include "systems/monster_ai.hpp"
#include "systems/secret.hpp"
#include "systems/transitions.hpp"

namespace olduvai::presentation {
namespace {

// File-local copy of run_game's tiny private helper (parse_f/parse_i come
// from parse_util.hpp — the former local copies collided with it).
std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

}  // namespace

void run_title_menu(TitleMenuCtx& ctx) {
    // Alias ctx members so the moved body reads verbatim.
    auto& sw = ctx.sw;
    auto& rt = ctx.rt;
    auto& audio_opt = ctx.audio_opt;
    const auto& opts = ctx.opts;
    auto& hd = ctx.hd;
    auto& hd_scale = ctx.hd_scale;
    auto& display = ctx.display;
    auto& quit_requested = ctx.quit_requested;
    auto& menu_continue = ctx.menu_continue;
    const bool autoloaded = ctx.autoloaded;
    auto& rebuild_window = ctx.rebuild_window;
    auto& load_all_sfx = ctx.load_all_sfx;

        SDL_Texture* itex =
            create_stream_tex(sw.ren, 320 * hd_scale, 200 * hd_scale);
        // Owner UX 2026-07-05: ESC during the intro is the QUICK PATH to
        // the main menu (it used to quit).  Window-close still quits;
        // quitting now lives in the menu (Quit to Desktop).
        bool intro_to_menu = false;
        // Headless menu-shot hook: skip straight past the title cards to the
        // menu (they hold for ~30 s with no keyboard to skip them, and the
        // shot only verifies the menu compose — see tests/mainmenu_shot.sh).
        const bool shot_mode = std::getenv("OLDUVAI_MAINMENU_SHOT") != nullptr;
        // OLDUVAI_MENU_SCRIPT (title-menu walk): drive the MAIN menu headlessly
        // with synthetic key events, one token per frame — the title-screen
        // counterpart of run_platform_level's pause/menu walk.  Tokens: the
        // plain keys from menu_script_util plus wait | shot | quit (`shot`
        // dumps the composed frame to OLDUVAI_MENU_SCRIPT_DIR/NNN.png; the
        // walk auto-quits when the script runs out).  Script mode skips the
        // intro cards and the dream hold exactly like shot mode, and freezes
        // the pointer animation for reproducible shots.  NB: pause-menu walks
        // (tests/menu_script.sh) pass --level and never enter this loop; a
        // title walk that starts a game would replay the script in-level.
        const std::vector<std::string> menu_script =
            parse_menu_script(std::getenv("OLDUVAI_MENU_SCRIPT"));
        const bool script_mode = !menu_script.empty();
        std::size_t menu_script_idx = 0;
        int menu_shot_ctr = 0;
        std::string menu_script_dir = ".";
        if (const char* d = std::getenv("OLDUVAI_MENU_SCRIPT_DIR"))
            menu_script_dir = d;
        auto ipresent = [&](const FrameBuffer& f) -> bool {
            cursor_autohide_frame();   // title/menu frames too
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (handle_fullscreen_toggle(ev, sw.win)) continue;
                if (ev.type == SDL_QUIT) {
                    quit_requested = true;
                    return false;
                }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) {
                    intro_to_menu = true;
                    return false;
                }
            }
            if (hd_scale > 1) {
                const auto up = enhance::upscale_rgba(f.px, 320, 200,
                                                      hd_scale,
                                                      rt.hd_profile);
                SDL_UpdateTexture(itex, nullptr, up.data(),
                                  320 * hd_scale * 4);
            } else {
                SDL_UpdateTexture(itex, nullptr, f.px.data(), 320 * 4);
            }
            SDL_RenderClear(sw.ren);
            SDL_RenderCopy(sw.ren, itex, nullptr, nullptr);
            SDL_RenderPresent(sw.ren);
            SDL_Delay(1000 / 18);
            return true;
        };
        auto iskip = []() -> bool {
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            return k[SDL_SCANCODE_SPACE] != 0 ||
                   (k[SDL_SCANCODE_RETURN] != 0 && enter_skip_allowed()) ||
                   k[SDL_SCANCODE_LCTRL] != 0 || gamepad::fire_held();
        };
        formats::CurArchive iva(slurp(opts.game_dir / "FILESA.VGA"));
        formats::CurArchive ifa(slurp(opts.game_dir / "FILESA.CUR"));
        auto show = [&](const char* name, int hold_frames) {
            if (quit_requested || intro_to_menu || shot_mode || script_mode ||
                !iva.contains(name))
                return;
            show_pc1_screen(formats::parse_pc1(iva.get(name).data),
                            hold_frames, ipresent, iskip);
        };
        show("TITUS.PC1", 3 * 18);              // publisher logo
        if (audio_opt->music_available() && ifa.contains("INTRO.MDI")) {
            audio_opt->play_music(ifa.get("INTRO.MDI").data, formats::mdi_track_id("intro.mdi"));
        }
        show("TITRE1.PC1", 20 * 18);            // title card (skippable)
        show("TITRE2.PC1", 10 * 18);
        // ── BULLE "dreaming caveman" attract hold (owner UX 2026-07-05):
        // the original press-fire wait is RESTORED — the full dream screen
        // sits uncovered until fire (SPACE / LCTRL / RETURN) opens the
        // menu, or ESC (handled in ipresent) jumps there.  Edge-gated:
        // the keypress that skipped TITRE2 must be RELEASED first, so
        // skipping the cards lands on the dream screen instead of
        // falling straight through into the menu.
        // (OLDUVAI_MAINMENU_SHOT and script walks skip the hold — the
        // headless hooks have no keyboard to press fire with.)
        if (!quit_requested && !intro_to_menu && iva.contains("BULLE.PC1") &&
            !shot_mode && !script_mode) {
            const formats::Pc1Image bulle_hold =
                formats::parse_pc1(iva.get("BULLE.PC1").data);
            auto no_skip = []() -> bool { return false; };
            // One fade-in onto the dream screen, then raw frames only —
            // NO fade-out on fire: the menu draws straight over the same
            // BULLE backdrop (owner UX: no black dip between "dream
            // uncovered" and "dream + menu").
            show_pc1_screen(bulle_hold, 1, ipresent, no_skip,
                            /*fade_in=*/true, /*fade_out=*/false);
            while (!quit_requested && !intro_to_menu && iskip())
                show_pc1_screen(bulle_hold, 1, ipresent, no_skip,
                                /*fade_in=*/false, /*fade_out=*/false);
            if (!quit_requested && !intro_to_menu)
                show_pc1_screen(bulle_hold, 1 << 28, ipresent, iskip,
                                /*fade_in=*/false, /*fade_out=*/false);
        }
        // ── Main menu over the BULLE backdrop (Start / Continue / Options /
        // Quit) — reached only via fire/SPACE from the dream hold or ESC. ──
        const formats::CurArchive* cs_ar =
            ifa.contains("CHARSET1.MAT") ? &ifa
            : iva.contains("CHARSET1.MAT") ? &iva : nullptr;
        if (!quit_requested && iva.contains("BULLE.PC1") && cs_ar && !autoloaded) {
            const formats::Pc1Image bulle =
                formats::parse_pc1(iva.get("BULLE.PC1").data);
            FrameBuffer bg;
            for (std::size_t i = 0;
                 i < bulle.pixels.size() && i < 320u * 200u; ++i) {
                const std::uint8_t idx = bulle.pixels[i];
                const auto c = idx < bulle.palette.size() ? bulle.palette[idx]
                                                          : formats::Rgb{};
                bg.px[i * 4] = c.r; bg.px[i * 4 + 1] = c.g;
                bg.px[i * 4 + 2] = c.b; bg.px[i * 4 + 3] = 255;
            }
            const auto mcharset = formats::MatFile(
                cs_ar->get("CHARSET1.MAT").data, "CHARSET1.MAT").sprites();
            // Score-bone sprite (L1SPR[33], 32x13) for the white selection
            // pointer — same art on every level, so L1's atlas serves the
            // pre-level main menu.
            std::vector<formats::Sprite> menu_bone_atlas;
            const formats::Sprite* menu_bone = nullptr;
            std::vector<formats::Rgb> menu_bone_pal;
            for (const formats::CurArchive* ar : {&ifa, &iva})
                if (ar->contains("L1SPR.MAT")) {
                    menu_bone_atlas = formats::MatFile(
                        ar->get("L1SPR.MAT").data, "L1SPR.MAT").sprites();
                    if (menu_bone_atlas.size() > 33)
                        menu_bone = &menu_bone_atlas[33];
                    break;
                }
            for (const formats::CurArchive* ar : {&iva, &ifa})
                if (ar->contains("FOND1.PC1")) {
                    menu_bone_pal =
                        formats::parse_pc1(ar->get("FOND1.PC1").data).palette;
                    break;
                }
            std::optional<MenuModel> mm;
            {
                std::string base;
                if (char* p = SDL_GetBasePath()) { base = p; SDL_free(p); }
                for (const std::string& cand : {base + "data/menus.json",
                                                base + "../Resources/data/menus.json",
                                                std::string("data/menus.json")}) {
                    try { mm = load_menus(cand); } catch (...) { mm.reset(); }
                    if (mm) break;
                }
            }
            if (mm) {
                using PFn = std::function<void(const std::string&, const std::string&)>;
                // SettingsSession + ConfirmDialog for the main-menu Options batch
                // staging flow (§8.6).  Mirrors the in-game Pause wiring exactly.
                SettingsSession main_session;
                ConfirmDialog   main_confirm;

                struct MBind : MenuBindings {
                    SdlAudio* audio = nullptr;
                    SDL_Window* win = nullptr;
                    bool enhanced = false;
                    const PFn* persist = nullptr;
                    std::map<std::string, std::string> mem;
                    GameOptions* rt = nullptr;   // write back so Start Game uses the edited value this session
                    DisplaySettings cur;         // snapshot of rt at baseline (updated on APPLY)
                    // Tier-1 live Aspect: sets rt.aspect + SDL_RenderSetLogicalSize.
                    // The main-menu flush dims are recomputed per-frame from
                    // rt.aspect, so this just gives immediate effect.
                    std::function<void(const std::string&)> apply_aspect;
                    // Batched staging session: every editable key change goes here.
                    SettingsSession* session = nullptr;
                    std::string get(const std::string& k) override {
                        auto it = mem.find(k);
                        return it == mem.end() ? std::string{} : it->second;
                    }
                    void save(const std::string& k, const std::string& v) {
                        if (persist && *persist) (*persist)(k, v);
                    }
                    void set(const std::string& k, const std::string& v) override {
                        if (k == "preset") {
                            // One-click Classic/HD preset: fan the bundle out through
                            // this same set() so every key rides the normal machinery.
                            mem[k] = v;
                            apply_preset(*this, v);
                            return;
                        }
                        // cheat.*: session-only — no staging, no persist.
                        if (k.rfind("cheat.", 0) == 0) {
                            mem[k] = v;
                            return;
                        }

                        // All editable settings keys — enhance.* included —
                        // stage provisionally (play.json sees nothing until
                        // Apply; encode_enhance_persist writes the flags as
                        // the single "enhance" config list there).
                        const std::string old_val = mem.count(k) ? mem[k] : std::string{};
                        mem[k] = v;

                        // Live preview for cheap keys only — do NOT write rt or
                        // persist (shared: settings_preview.hpp).
                        if (preview_cheap_key(k, v, audio, win, enhanced)) {
                            // handled — still stages below
                        } else if (k == "hd_profile") {
                            // Same-scale hd_profile: live-swap the rt field the upscaler reads.
                            const ApplyTier tier = classify_change(k, v, cur);
                            if (tier == ApplyTier::Live && rt)
                                rt->hd_profile = v;
                        } else if (k == "aspect" && apply_aspect) {
                            apply_aspect(v);   // Tier-1 live: logical-size only
                        }
                        // Heavy keys (render_scale, music_device, sfx_backend, hd_profile Reinit)
                        // are staged only — no rt write, no persist, no rebuild here.

                        if (session) session->stage(k, k, old_val, v);
                    }
                } mbind;
                mbind.audio = &*audio_opt; mbind.win = sw.win;
                mbind.enhanced = rt.enhanced; mbind.persist = &opts.persist;
                mbind.rt = &rt;
                mbind.session = &main_session;
                SettingsSeed seed;
                seed.enhanced = rt.enhanced;
                seed.hd_profile = rt.hd_profile;
                seed.render_scale = rt.render_scale;
                seed.music_device = rt.music_device;
                seed.sfx_backend = rt.sfx_backend;
                seed.aspect = rt.aspect;
                seed.fullscreen =
                    (SDL_GetWindowFlags(sw.win) &
                     SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                seed.flags = rt.enhance;
                seed_settings_mem(mbind, seed);
                // Tier-1 live Aspect on the title: set rt.aspect (the per-frame
                // flush dims read it) + SDL_RenderSetLogicalSize for immediacy.
                mbind.apply_aspect = [&](const std::string& v) {
                    rt.aspect = v;
                    const LogicalDims ld = aspect_logical(hd_scale, v);
                    SDL_RenderSetLogicalSize(sw.ren, ld.w, ld.h);
                };

                // Start Game level select (left/right on the Start row) —
                // session-only, defaults to Level 1 every boot.
                mbind.mem["menu.start_level"] = "1";

                bool want_start = false, want_quit = false;
                MenuActionTable acts = {
                    {"start_game", [&] {
                        // Level 1 leaves `display` untouched so the classic
                        // title→game flow stays exactly as before; 2-7 jump
                        // straight into the chosen level (the CLI --level
                        // semantics), mirroring the Continue handler below.
                        const std::string lv = mbind.get("menu.start_level");
                        if (!lv.empty() && lv != "1")
                            display = std::atoi(lv.c_str());
                        want_start = true;
                    }},
                    {"continue", [&] {
                        if (!opts.save_path.empty())
                            if (auto s = load_from_file(opts.save_path)) {
                                menu_continue = s;
                                display = s->hdr.level;
                                want_start = true;
                            }
                    }},
                    {"quit_desktop", [&] { want_quit = true; }},
                };
                Menu menu(*mm, mbind, acts);
                menu.open("main");
                // Enhanced mode: render the menu text with the SAME cartoony
                // vector font the in-game Pause overlay + HUD use, so the title
                // menu matches.  Classic mode keeps the bitmap glyphs (drawn by
                // draw_menu).  Mirrors run_platform_level's use_hd_text gate.
                // fbase is hoisted out of the if(hd) block so the apply block
                // can re-call menu_font.load() without re-querying SDL.
                std::string fbase = ".";
                if (char* p = SDL_GetBasePath()) {
                    fbase = p; SDL_free(p);
                    if (!fbase.empty() && fbase.back() == '/') fbase.pop_back();
                }
                enhance::HdText menu_font;
                if (hd) {
                    menu_font.load(fbase, hd_scale, rt.hd_font);
                }
                TextOverlay menu_overlay;
                // SettingsFlow: the SAME controller the in-game Pause path
                // uses (OL-B1) — subtree membership from *mm, staging via
                // main_session, confirm lifecycle on main_confirm.  Only the
                // hooks differ: this environment writes rt.* and rebuilds
                // window/audio IN PLACE (no PendingReinit — there is no level
                // to snapshot on the title screen).
                SettingsFlow::Hooks main_hooks;
                main_hooks.persist = [&](const std::string& k,
                                         const std::string& v) {
                    // enhance.* keys persist as ONE "enhance" config list
                    // (plus the master companion when the master is not
                    // staged) — see encode_enhance_persist.
                    if (k.rfind("enhance.", 0) == 0) {
                        for (const auto& [pk, pv] : encode_enhance_persist(
                                 mbind.mem, main_session.changes(), k))
                            mbind.save(pk, pv);
                        return;
                    }
                    mbind.save(k, v);
                };
                main_hooks.classify = [&](const std::string& k,
                                          const std::string& v) {
                    // mbind.cur is the baseline snapshot — untouched while the
                    // apply loop runs (refreshed in apply_done below).
                    // Set-aware: the Style preset's keys only cross the
                    // classic<->HD boundary together.
                    std::vector<std::pair<std::string, std::string>> staged;
                    for (const auto& ch : main_session.changes())
                        staged.emplace_back(ch.key, ch.new_value);
                    return classify_change_in_set(k, v, mbind.cur, staged);
                };
                main_hooks.apply_change = [&](const StagedChange& ch,
                                              ApplyTier tier) {
                    // APPLY: write rt so Start Game uses the edited values.
                    (void)tier;   // Live hd_profile already previewed via rt
                    if (ch.key == "render_scale" &&
                        rt.render_scale != parse_i(ch.new_value,
                                                   rt.render_scale)) {
                        rt.render_scale = parse_i(ch.new_value,
                                                  rt.render_scale);
                    } else if (ch.key == "hd_profile") {
                        rt.hd_profile = ch.new_value;
                    } else if (ch.key == "music_device") {
                        rt.music_device = ch.new_value;
                    } else if (ch.key == "sfx_backend") {
                        rt.sfx_backend = ch.new_value;
                    } else if (ch.key == "enhanced") {
                        // Preset row: crossing the classic<->HD boundary.
                        rt.enhanced = ch.new_value == "true" ||
                                      ch.new_value == "1";
                    } else if (ch.key == "aspect") {
                        rt.aspect = ch.new_value;   // live-previewed; keep rt
                    } else if (ch.key.rfind("enhance.", 0) == 0) {
                        // Adopt into rt so Start Game (and the session) uses
                        // the applied flags.
                        set_enhance_flag(rt.enhance, ch.key.substr(8),
                                         ch.new_value == "1");
                    }
                    // volume/fullscreen already live-previewed; no rt field.
                };
                main_hooks.apply_done = [&](bool needs_reinit) {
                    // Baseline BEFORE the apply (mbind.cur is refreshed below;
                    // it was untouched during the loop, so this equals the
                    // pre-loop snapshot the old inline block captured).
                    const DisplaySettings base = mbind.cur;
                    // Update baseline snapshot for subsequent edits.
                    mbind.cur = {rt.enhanced,
                                 rt.hd_profile.empty() ? "native" : rt.hd_profile,
                                 rt.render_scale,
                                 rt.music_device,
                                 rt.sfx_backend};
                    // In-place rebuild (display + audio) if any Reinit-class
                    // key was applied.
                    if (!needs_reinit) return;
                    const int new_scale =
                        hd_scale_for(rt.enhanced, rt.hd_profile, rt.render_scale);
                    if (new_scale != hd_scale) {
                        hd = rt.enhanced && rt.hd_profile != "native";
                        hd_scale = new_scale;
                        if (!rebuild_window(hd_scale)) {
                            std::fprintf(stderr,
                                "settings: aborting after failed window rebuild\n");
                            quit_requested = true;
                            return;
                        }
                        SDL_DestroyTexture(itex);
                        itex = create_stream_tex(sw.ren, 320 * hd_scale,
                                                 200 * hd_scale);
                        if (!itex) {
                            std::fprintf(stderr,
                                "settings: title texture recreate failed: %s\n",
                                SDL_GetError());
                            quit_requested = true;
                            return;
                        }
                        if (hd && (rt.enhance.hd_text || rt.enhance.hud_overlay))
                            menu_font.load(fbase, hd_scale, rt.hd_font);
                    }
                    // Rebuild audio if device/backend changed.
                    const bool audio_changed =
                        (rt.music_device != base.music_device ||
                         rt.sfx_backend  != base.sfx_backend);
                    if (audio_changed) {
                        audio_opt.reset();
                        audio_opt.emplace(rt.music_device, rt.rom_dir, rt.soundfont,
                                          rt.sfx_backend, rt.audio_rate,
                                          rt.audio_buffer, rt.midi_port);
                        load_all_sfx(*audio_opt);
                        mbind.audio = &*audio_opt;
                        if (audio_opt->music_available() && ifa.contains("INTRO.MDI")) {
                            audio_opt->play_music(ifa.get("INTRO.MDI").data, formats::mdi_track_id("intro.mdi"));
                        }
                    }
                };
                // Discard: revert staged changes, undo live previews.
                main_hooks.revert_change = [&](const StagedChange& ch) {
                    mbind.mem[ch.key] = ch.old_value;
                    // Cheap live preview at baseline (shared:
                    // settings_preview.hpp); site-specific live keys follow.
                    if (preview_cheap_key(ch.key, ch.old_value, mbind.audio,
                                          mbind.win, mbind.enhanced)) {
                        // handled
                    } else if (ch.key == "hd_profile" && mbind.rt) {
                        mbind.rt->hd_profile = ch.old_value;
                    } else if (ch.key == "aspect" && mbind.apply_aspect) {
                        mbind.apply_aspect(ch.old_value);
                    }
                };
                main_hooks.reopen_options = [&]() { menu.open("options"); };
                main_hooks.confirm_note = [](bool any_reinit, bool any_persist) {
                    if (any_reinit || !any_persist)
                        return std::string("Apply settings now.");
                    return std::string(
                        "Saved - takes effect on next launch.");
                };
                SettingsFlow main_flow(*mm, main_session, main_confirm,
                                       std::move(main_hooks));
                const char* mainmenu_shot = std::getenv("OLDUVAI_MAINMENU_SHOT");
                std::string pending_shot;   // set for one frame by a `shot` token
                bool done = false;
                while (!done && !quit_requested) {
                    // OLDUVAI_MENU_SCRIPT: consume one token before the poll so
                    // the synthetic key is processed by this frame's event loop
                    // (mirrors run_platform_level's consumer).
                    if (script_mode) {
                        if (menu_script_idx >= menu_script.size()) {
                            want_quit = true;   // auto-exit at end of script
                        } else {
                            const std::string tok =
                                menu_script[menu_script_idx++];
                            if (tok == "quit") want_quit = true;
                            else if (tok == "wait") { /* idle one frame */ }
                            else if (tok == "shot") {
                                char nm[32];
                                std::snprintf(nm, sizeof nm, "%03d.png",
                                              menu_shot_ctr++);
                                pending_shot = menu_script_dir + "/" + nm;
                            } else {
                                const SDL_Keycode sym = menu_token_sym(tok);
                                if (sym != SDLK_UNKNOWN) push_menu_key(sym);
                            }
                        }
                    }
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (handle_fullscreen_toggle(ev, sw.win)) continue;
                        if (ev.type == SDL_QUIT) want_quit = true;
                        else if (ev.type == SDL_KEYDOWN) {
                            const auto sym = ev.key.keysym.sym;
                            // ── Confirm dialog intercepts all key input ──────
                            // SettingsFlow resolves move/apply/discard/cancel
                            // through the main-menu hooks above (OL-B1).
                            if (main_confirm.is_open()) {
                                main_flow.handle_key(flow_key_from_sym(sym));
                                // Failed window/texture rebuild during apply:
                                // stop draining events against a torn-down
                                // renderer (matches the old inline break).
                                if (quit_requested) break;
                                // Dialog consumed this event.
                            } else {
                                // ── Normal menu input ────────────────────────
                                if (sym == SDLK_ESCAPE) {
                                    menu.back();
                                    if (!menu.is_open()) menu.open("main");  // ESC at root stays
                                } else {
                                    menu_nav_keydown(menu, sym);
                                }
                            }
                        }
                    }
                    // ── Options-subtree exit detection (§8.6) ──────────────
                    // When the user backs out from Options to "main" and there
                    // are staged changes, open the Save & Apply / Discard dialog.
                    if (!main_confirm.is_open())
                        main_flow.track_screen(menu.current_screen());
                    // ── Close-without-apply revert ──────────────────────────
                    // If Start Game / Quit fires with unconfirmed staged changes,
                    // discard them so previews don't leak into the game.
                    if ((want_start || want_quit) && !main_session.empty())
                        main_flow.discard();
                    if (quit_requested) break;   // window/texture rebuild failed — don't render against a torn-down renderer
                    // ── Render ──────────────────────────────────────────────
                    // Vector-vs-bitmap glyph gate, recomputed EVERY frame
                    // (mirrors the boss pause): a Style preset Apply at the
                    // title is reinit-class (classify_change_in_set), and
                    // apply_done/apply_change rebuild hd + menu_font and adopt
                    // enhance.* into rt mid-loop — a pre-loop latch kept the
                    // classic bitmap glyphs after applying Enhanced HD (see
                    // tests/title_style_apply.sh).
                    const bool menu_use_vector =
                        hd && menu_font.ok() &&
                        (rt.enhance.hd_text || rt.enhance.hud_overlay);
                    FrameBuffer pf = bg;
                    // In enhanced mode the slab + accent bar come from draw_menu
                    // (native, upscaled) and the glyphs from the vector overlay;
                    // classic mode draws the bitmap glyphs here.  No full-frame
                    // dim here (unlike the in-game Pause overlay): the BULLE
                    // intro backdrop is already near-black, so the slab alone
                    // gives enough separation — dimming would just muddy it.
                    if (main_confirm.is_open()) {
                        draw_confirm(pf, main_confirm, mcharset, /*dim=*/false,
                                     /*draw_text=*/!menu_use_vector);
                    } else {
                        draw_menu(pf, menu, mcharset, /*dim=*/false,
                                  /*draw_text=*/!menu_use_vector, menu_bone,
                                  &menu_bone_pal);
                    }
                    if (hd_scale > 1) {
                        const auto up = enhance::upscale_rgba(pf.px, 320, 200,
                                                              hd_scale, rt.hd_profile);
                        SDL_UpdateTexture(itex, nullptr, up.data(), 320 * hd_scale * 4);
                    } else {
                        SDL_UpdateTexture(itex, nullptr, pf.px.data(), 320 * 4);
                    }
                    SDL_RenderClear(sw.ren);
                    // Widescreen: OWN the geometry — compute the wide margin
                    // from the window aspect (same math as the in-game path:
                    // boss_ws_margin), set the WIDE logical size, and
                    // PILLARBOX the 320 menu frame at its centre, mapping the
                    // glyph overlay into the same frame rect.  Before this
                    // the menu rendered under a stale 4:3 logical while the
                    // flush used other dims — the misaligned main menu.
                    // (aspect_logical returns the 4:3 fallback for
                    // "widescreen"; only the game loop derives wide dims.)
                    LogicalDims mld = aspect_logical(hd_scale, rt.aspect);
                    int menu_ws_m = 0;
                    // hd_scale > 1: classic mode has no wide framebuffer —
                    // aspect_logical's "widescreen" falls back to keep there,
                    // and the menu must match (no pillarbox, no wide logical).
                    if (rt.aspect == "widescreen" && hd_scale > 1) {
                        int ww = 0, wh = 0;
                        SDL_GetRendererOutputSize(sw.ren, &ww, &wh);
                        menu_ws_m = boss_ws_margin(
                            ww, wh, std::getenv("OLDUVAI_WS_FORCE_MARGIN"));
                    }
                    if (menu_ws_m > 0) {
                        mld.w = (320 + 2 * menu_ws_m) * hd_scale;
                        mld.h = 200 * hd_scale;
                    }
                    SDL_RenderSetLogicalSize(sw.ren, mld.w, mld.h);
                    const int mm_margin = menu_ws_m * hd_scale;
                    if (mm_margin > 0) {
                        SDL_SetRenderDrawColor(sw.ren, 0, 0, 0, 255);
                        SDL_RenderFillRect(sw.ren, nullptr);
                        SDL_Rect mdst{mm_margin, 0, 320 * hd_scale,
                                      200 * hd_scale};
                        SDL_RenderCopy(sw.ren, itex, nullptr, &mdst);
                    } else {
                        SDL_RenderCopy(sw.ren, itex, nullptr, nullptr);
                    }
                    if (menu_use_vector) {
                        int ow = 0, oh = 0;
                        if (menu_overlay.begin(sw.ren, menu_font, ow, oh)) {
                            int mfx = -1, mfy = -1, mfw = -1, mfh = -1;
                            if (mm_margin > 0) {
                                mfx = mm_margin * ow / mld.w;
                                mfw = 320 * hd_scale * ow / mld.w;
                                mfy = 0;
                                mfh = oh;
                            }
                            if (main_confirm.is_open())
                                draw_confirm_vector(menu_overlay.buffer(), ow, oh,
                                                    menu_font, main_confirm, mfx,
                                                    mfy, mfw, mfh);
                            else
                                draw_menu_vector(menu_overlay.buffer(), ow, oh,
                                                 menu_font, menu,
                                                 // Freeze the pointer animation
                                                 // for a reproducible shot.
                                                 (shot_mode || script_mode)
                                                     ? 0.0f
                                                     : SDL_GetTicks() / 1000.0f,
                                                 mfx, mfy, mfw, mfh);
                            menu_overlay.flush(sw.ren, mld.w, mld.h);
                        }
                    }
                    // Headless verify: read back the composited frame (slab +
                    // vector text) before present.
                    const auto dump_frame = [&](const std::string& path) {
                        capture_renderer_output(sw.ren, path);
                    };
                    if (!pending_shot.empty()) {
                        // `shot` token: dump and keep walking.
                        dump_frame(pending_shot);
                        pending_shot.clear();
                    } else if (mainmenu_shot &&
                               menu_script_idx >= menu_script.size()) {
                        // OLDUVAI_MAINMENU_SHOT: one frame, then exit the loop.
                        // No script → the first frame (the classic hook); with
                        // a walk, the shot waits until the script has run out.
                        dump_frame(mainmenu_shot);
                        want_quit = true;
                    }
                    SDL_RenderPresent(sw.ren);
                    SDL_Delay(1000 / 18);
                    if (want_start || want_quit) done = true;
                }
                if (want_quit) quit_requested = true;
            }
        }
        audio_opt->stop_music();
        SDL_DestroyTexture(itex);
}

}  // namespace olduvai::presentation
