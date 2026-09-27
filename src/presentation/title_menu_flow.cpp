// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The intro/title sequence and the interactive main menu.  Coverage caveat:
// title_menu_flow.hpp.

#include "presentation/title_menu_flow.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "enhance/hd_text.hpp"
#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "prepare/game_files.hpp"
#include "presentation/audio/game_music.hpp"
#include "presentation/diag/menu_script_util.hpp"
#include "presentation/image_out.hpp"
#include "presentation/input/gamepad.hpp"
#include "presentation/menu/about_info.hpp"
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/dialog_key_map.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/menu/menu_render.hpp"
#include "presentation/menu/pause_flow.hpp"         // load_menu_model
#include "presentation/menu/settings_apply.hpp"
#include "presentation/menu/settings_flow.hpp"
#include "presentation/menu/settings_seed.hpp"
#include "presentation/menu/settings_session.hpp"
#include "presentation/menu/staging_bindings.hpp"
#include "presentation/render/boss_widescreen.hpp"  // boss_ws_margin
#include "presentation/render/game_render.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {
namespace {

constexpr Uint32 kFrameMs = 1000 / 18;

// A title frame: 320x200 upscaled into the title texture, then the 18 Hz
// wait.  A window close quits; ESC skips the intro to the menu.
struct TitlePresenter {
    TitleMenuCtx& ctx;
    SDL_Texture* tex = nullptr;
    bool to_menu = false;

    bool present(const FrameBuffer& f) {
        cursor_autohide_frame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, ctx.pipe.sw.win)) continue;
            if (ev.type == SDL_QUIT) {
                ctx.quit_requested = true;
                return false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                to_menu = true;
                return false;
            }
        }
        upload_native_frame(tex, f, ctx.hd_scale, ctx.rt.hd_profile);
        show_texture(ctx.pipe.sw.ren, tex);
        present_output(ctx.pipe.sw.ren);
        SDL_Delay(kFrameMs);
        return true;
    }
    bool stopped() const { return ctx.quit_requested || to_menu; }
};

bool fire_held() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    return k[SDL_SCANCODE_SPACE] != 0 ||
           (k[SDL_SCANCODE_RETURN] != 0 && enter_skip_allowed()) ||
           k[SDL_SCANCODE_LCTRL] != 0 || gamepad::fire_held();
}

// The publisher logo, the title cards and the BULLE dream hold.  The hold
// waits for fire (edge-gated: the key that skipped TITRE2 must be released
// first) with one fade-in and no fade-out, since the menu draws over the same
// backdrop.  `headless` skips every hold; the intro music still starts.
void play_intro(TitlePresenter& p, const formats::CurArchive& vga,
                SdlAudio* audio, const std::vector<std::uint8_t>* intro_mdi,
                bool headless) {
    const PresentFn present = [&p](const FrameBuffer& f) {
        return p.present(f);
    };
    const auto show = [&](const char* name, int hold_frames) {
        if (p.stopped() || headless || !vga.contains(name)) return;
        show_pc1_screen(formats::parse_pc1(vga.get(name).data), hold_frames,
                        present, fire_held);
    };
    show("TITUS.PC1", 3 * 18);
    play_mdi(audio, intro_mdi, "INTRO.MDI");
    show("TITRE1.PC1", 20 * 18);
    show("TITRE2.PC1", 10 * 18);
    if (p.stopped() || headless || !vga.contains("BULLE.PC1")) return;
    const formats::Pc1Image bulle = formats::parse_pc1(vga.get("BULLE.PC1").data);
    const SkipFn never = [] { return false; };
    show_pc1_screen(bulle, 1, present, never, /*fade_in=*/true,
                    /*fade_out=*/false);
    while (!p.stopped() && fire_held())
        show_pc1_screen(bulle, 1, present, never, false, false);
    if (!p.stopped())
        show_pc1_screen(bulle, 1 << 28, present, fire_held, false, false);
}

// The menu's art: the BULLE backdrop, the charset, and the score bone
// (L1SPR[33], 32x13) as the pointer, in FOND1's palette.
struct MenuArt {
    FrameBuffer bg;
    std::vector<formats::Sprite> charset;
    std::vector<formats::Sprite> bone_atlas;
    std::vector<formats::Rgb> bone_palette;

    const formats::Sprite* bone() const {
        return bone_atlas.size() > 33 ? &bone_atlas[33] : nullptr;
    }
};

std::optional<MenuArt> load_menu_art(const formats::CurArchive& cur,
                                     const formats::CurArchive& vga) {
    const formats::CurArchive* cs = cur.contains("CHARSET1.MAT")   ? &cur
                                    : vga.contains("CHARSET1.MAT") ? &vga
                                                                   : nullptr;
    if (cs == nullptr || !vga.contains("BULLE.PC1")) return std::nullopt;
    MenuArt art;
    art.bg = pc1_frame(formats::parse_pc1(vga.get("BULLE.PC1").data));
    art.charset = formats::load_mat_sprites(&cs->get("CHARSET1.MAT").data,
                                            "CHARSET1.MAT");
    for (const formats::CurArchive* ar : {&cur, &vga})
        if (ar->contains("L1SPR.MAT")) {
            art.bone_atlas = formats::load_mat_sprites(
                &ar->get("L1SPR.MAT").data, "L1SPR.MAT");
            break;
        }
    for (const formats::CurArchive* ar : {&vga, &cur})
        if (ar->contains("FOND1.PC1")) {
            art.bone_palette = formats::parse_pc1(ar->get("FOND1.PC1").data).palette;
            break;
        }
    return art;
}

// OLDUVAI_MENU_SCRIPT: a headless walk, one token per frame (the
// menu_script_util keys plus wait | shot | quit; `shot` writes
// OLDUVAI_MENU_SCRIPT_DIR/NNN.png; the walk quits when the script ends).
struct TitleScript {
    std::vector<std::string> tokens =
        parse_menu_script(std::getenv("OLDUVAI_MENU_SCRIPT"));
    std::size_t next = 0;
    int shots = 0;
    std::string dir = std::getenv("OLDUVAI_MENU_SCRIPT_DIR") != nullptr
                          ? std::getenv("OLDUVAI_MENU_SCRIPT_DIR")
                          : ".";

    bool active() const { return !tokens.empty(); }
    bool done() const { return next >= tokens.size(); }

    // Consume one token before the poll, so this frame's events see it.
    void step(bool& want_quit, std::string& shot_path) {
        if (done()) {
            want_quit = true;
            return;
        }
        const std::string& tok = tokens[next++];
        if (tok == "quit") {
            want_quit = true;
        } else if (tok == "shot") {
            char name[32];
            std::snprintf(name, sizeof name, "%03d.png", shots++);
            shot_path = dir + "/" + name;
        } else if (tok != "wait") {
            const SDL_Keycode sym = menu_token_sym(tok);
            if (sym != SDLK_UNKNOWN) push_menu_key(sym);
        }
    }
};

// The main menu over the BULLE backdrop: Start / Continue / Options / Quit.
// Options apply in place: the pipeline through run_game's adopt_pipeline
// (window and audio rebuilt as needed), then the title's own texture, font
// and music follow.
class MainMenu {
public:
    MainMenu(TitleMenuCtx& ctx, TitlePresenter& p, MenuModel& model,
             const MenuArt& art, const std::vector<std::uint8_t>* intro_mdi,
             TitleScript& script, bool frozen_pointer)
        : ctx_(ctx), p_(p), model_(model), art_(art), intro_mdi_(intro_mdi),
          script_(script), frozen_pointer_(frozen_pointer),
          menu_(model_, bind_, actions()),
          flow_(model_, session_, confirm_, hooks()) {
        GameOptions& rt = ctx_.rt;
        bind_.attach(ctx_.pipe.audio.get(), ctx_.pipe.sw.win, &session_, rt);
        bind_.live_hd_profile = &rt.hd_profile;
        // Live Aspect: rt.aspect (the frame reads it) and the logical size.
        bind_.apply_aspect = [this](const std::string& v) {
            ctx_.rt.aspect = v;
            set_aspect_logical(ctx_.pipe.sw.ren, ctx_.hd_scale, v);
        };
        // Start Game's level select: session-only, Level 1 every boot.
        bind_.mem["menu.start_level"] = "1";
        SDL_version sv;
        SDL_GetVersion(&sv);
        const std::string sdl = "SDL " + std::to_string(sv.major) + "." +
                                std::to_string(sv.minor) + "." +
                                std::to_string(sv.patch);
        fill_about_screen(model_, about_lines(about_build(sdl), kAboutChars));
        menu_.open("main");
        if (ctx_.hd) font_.load(fbase_, ctx_.hd_scale, rt.hd_font);
    }
    ~MainMenu() = default;
    MainMenu(const MainMenu&) = delete;
    MainMenu& operator=(const MainMenu&) = delete;

    void run() {
        const char* mainmenu_shot = std::getenv("OLDUVAI_MAINMENU_SHOT");
        std::string shot_path;   // set for one frame by a `shot` token
        while (!ctx_.quit_requested) {
            if (script_.active()) script_.step(want_quit_, shot_path);
            poll();
            if (menu_.is_open() && !confirm_.is_open())
                flow_.track_screen(menu_.current_screen());
            // Start Game / Quit with unconfirmed changes discards them.
            if ((want_start_ || want_quit_) && !session_.empty())
                flow_.discard();
            if (ctx_.quit_requested) break;   // an apply's rebuild failed
            draw();
            if (!shot_path.empty()) {
                capture_renderer_output(ctx_.pipe.sw.ren, shot_path);
                shot_path.clear();
            } else if (mainmenu_shot != nullptr && script_.done()) {
                // OLDUVAI_MAINMENU_SHOT: one frame, then exit.
                capture_renderer_output(ctx_.pipe.sw.ren, mainmenu_shot);
                want_quit_ = true;
            }
            present_output(ctx_.pipe.sw.ren);
            SDL_Delay(kFrameMs);
            if (want_start_ || want_quit_) break;
        }
        if (want_quit_) ctx_.quit_requested = true;
    }

private:
    MenuActionTable actions() {
        return {
            {"start_game", [this] {
                // Level 1 leaves `display` alone (the normal title -> game
                // flow); 2-7 jump there, like --level.  atoi is safe:
                // menus.json declares a choice over [1..7].
                const std::string lv = bind_.get("menu.start_level");
                if (!lv.empty() && lv != "1")
                    // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
                    ctx_.display = std::atoi(lv.c_str());
                want_start_ = true;
            }},
            {"continue", [this] {
                if (ctx_.opts.save_path.empty()) return;
                if (auto s = load_from_file(ctx_.opts.save_path)) {
                    ctx_.menu_continue = s;
                    ctx_.display = s->hdr.level;
                    want_start_ = true;
                }
            }},
            {"quit_desktop", [this] {
                confirm_.ask("Exit game?", [this] { want_quit_ = true; });
            }},
        };
    }

    SettingsFlow::Hooks hooks() {
        SettingsFlow::Hooks h = staging_flow_hooks(bind_, session_, &menu_);
        h.apply_begin = [this] { target_ = display_settings_of(ctx_.rt); };
        // Volume and fullscreen were previewed live, as was aspect (rt).
        h.apply_change = [this](const StagedChange& ch, ApplyTier) {
            set_display_key(target_, ch.key, ch.new_value);
        };
        h.apply_done = [this](bool) { apply(); };
        h.confirm_note = [](bool any_reinit, bool any_persist) {
            if (any_reinit || !any_persist)
                return std::string("Apply settings now.");
            return std::string("Saved - takes effect on next launch.");
        };
        return h;
    }

    void apply() {
        const GameOptions& rt = ctx_.rt;
        const int old_scale = ctx_.hd_scale;
        const bool audio_changed = target_.music_device != rt.music_device ||
                                   target_.sfx_backend != rt.sfx_backend;
        if (!ctx_.pipe.adopt(target_, nullptr)) {
            ctx_.quit_requested = true;
            return;
        }
        bind_.rebind(ctx_.pipe.audio.get(), ctx_.pipe.sw.win,
                     display_settings_of(rt));
        if (ctx_.hd_scale != old_scale) {
            SDL_DestroyTexture(p_.tex);
            p_.tex = create_stream_tex(ctx_.pipe.sw.ren, 320 * ctx_.hd_scale,
                                       200 * ctx_.hd_scale);
            if (p_.tex == nullptr) {
                std::fprintf(stderr,
                             "settings: title texture recreate failed: %s\n",
                             SDL_GetError());
                ctx_.quit_requested = true;
                return;
            }
            if (ctx_.hd) font_.load(fbase_, ctx_.hd_scale, rt.hd_font);
        }
        if (audio_changed)
            play_mdi(ctx_.pipe.audio.get(), intro_mdi_, "INTRO.MDI");
    }

    void poll() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, ctx_.pipe.sw.win)) continue;
            if (ev.type == SDL_QUIT) {
                want_quit_ = true;
            } else if (ev.type == SDL_KEYDOWN) {
                // ESC at the root reopens "main": nothing is behind it.
                const bool dialog_took = menu_dialog_keydown(
                    ev.key.keysym.sym, confirm_, flow_, menu_,
                    [this] { menu_.open("main"); });
                if (dialog_took && ctx_.quit_requested) return;
            }
        }
    }

    // Widescreen: the game's margin (boss_ws_margin) around the 320 menu.
    // Classic has no wide framebuffer: "widescreen" means keep there.
    int wide_margin() const {
        if (ctx_.rt.aspect != "widescreen" || ctx_.hd_scale <= 1) return 0;
        int ww = 0, wh = 0;
        SDL_GetRendererOutputSize(ctx_.pipe.sw.ren, &ww, &wh);
        return boss_ws_margin(ww, wh, std::getenv("OLDUVAI_WS_FORCE_MARGIN"));
    }

    void draw() {
        SDL_Renderer* const ren = ctx_.pipe.sw.ren;
        const int s = ctx_.hd_scale;
        // Decided per frame: a Style apply rebuilds hd and the font mid-loop
        // (tests/title_style_apply.sh).
        const bool vector_text = ctx_.hd && font_.ok();
        // Enhanced: slab and accent bar here, glyphs from the vector overlay.
        // No dim: the backdrop is already dark.
        FrameBuffer pf = art_.bg;
        if (confirm_.is_open())
            draw_confirm(pf, confirm_, art_.charset, /*dim=*/false,
                         /*draw_text=*/!vector_text);
        else
            draw_menu(pf, menu_, art_.charset, /*dim=*/false,
                      /*draw_text=*/!vector_text, art_.bone(),
                      &art_.bone_palette);
        upload_native_frame(p_.tex, pf, s, ctx_.rt.hd_profile);

        const int margin = wide_margin() * s;
        const LogicalDims ld =
            margin > 0 ? LogicalDims{320 * s + 2 * margin, 200 * s}
                       : aspect_logical(s, ctx_.rt.aspect);
        SDL_RenderSetLogicalSize(ren, ld.w, ld.h);
        const SDL_Rect centre{margin, 0, 320 * s, 200 * s};
        show_texture(ren, p_.tex, margin > 0 ? &centre : nullptr);
        if (vector_text) draw_vector_text(ld, margin);
    }

    void draw_vector_text(const LogicalDims& ld, int margin) {
        overlay_.pass(ctx_.pipe.sw.ren, font_, ld.w, ld.h,
                      [&](const enhance::Canvas& cv) {
            const MenuFrame pic =
                margin > 0 ? MenuFrame::picture(cv.w, cv.h, ld.w, ld.h, margin,
                                                320 * ctx_.hd_scale)
                           : MenuFrame::picture(cv.w, cv.h, ld.w, ld.h);
            if (confirm_.is_open())
                draw_confirm_vector(cv, font_, confirm_, pic);
            else
                draw_menu_vector(cv, font_, menu_,
                                 // Frozen for a reproducible shot.
                                 frozen_pointer_ ? 0.0f
                                                 : SDL_GetTicks() / 1000.0f,
                                 pic);
        });
    }

    TitleMenuCtx& ctx_;
    TitlePresenter& p_;
    MenuModel& model_;
    const MenuArt& art_;
    const std::vector<std::uint8_t>* intro_mdi_;
    TitleScript& script_;
    bool frozen_pointer_;
    SettingsSession session_;
    ConfirmDialog confirm_;
    StagingBindings bind_;
    bool want_start_ = false;
    bool want_quit_ = false;
    Menu menu_;
    DisplaySettings target_;   // the pipeline an Apply builds
    SettingsFlow flow_;
    const std::string fbase_ = sdl_base_dir();
    enhance::HdText font_;
    TextOverlay overlay_;
};

}  // namespace

void run_title_menu(TitleMenuCtx& ctx) {
    TitlePresenter p{ctx, create_stream_tex(ctx.pipe.sw.ren, 320 * ctx.hd_scale,
                                            200 * ctx.hd_scale)};
    // OLDUVAI_MAINMENU_SHOT and OLDUVAI_MENU_SCRIPT skip the intro holds
    // (~30 s) and freeze the pointer animation (tests/mainmenu_shot.sh,
    // tests/menu_script.sh).
    TitleScript script;
    const bool headless =
        std::getenv("OLDUVAI_MAINMENU_SHOT") != nullptr || script.active();
    const formats::CurArchive vga(
        prepare::slurp_file(ctx.opts.game_dir / "FILESA.VGA"));
    const formats::CurArchive cur(
        prepare::slurp_file(ctx.opts.game_dir / "FILESA.CUR"));
    const std::vector<std::uint8_t>* const intro_mdi =
        cur.contains("INTRO.MDI") ? &cur.get("INTRO.MDI").data : nullptr;

    play_intro(p, vga, ctx.pipe.audio.get(), intro_mdi, headless);

    if (!ctx.quit_requested && !ctx.autoloaded) {
        std::optional<MenuModel> model = load_menu_model(ctx.rt.profile_family);
        const std::optional<MenuArt> art = load_menu_art(cur, vga);
        if (art && model) {
            MainMenu menu(ctx, p, *model, *art, intro_mdi, script, headless);
            menu.run();
        }
    }
    ctx.pipe.audio->stop_music();
    SDL_DestroyTexture(p.tex);
}

}  // namespace olduvai::presentation
