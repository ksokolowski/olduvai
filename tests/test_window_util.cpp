// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Display helpers: Alt+Enter fullscreen toggle filtering + integer
// desktop-fit scaling.  Runs against SDL's dummy video driver so the
// suite stays headless; real-compositor behaviour (the window actually
// going fullscreen) is verified in playtests.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <SDL.h>

#include "presentation/window_util.hpp"

using olduvai::presentation::desktop_integer_scale;
using olduvai::presentation::handle_fullscreen_toggle;
using olduvai::presentation::aspect_logical;

namespace {

SDL_Event key_event(SDL_Keycode sym, Uint16 mod) {
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = sym;
    ev.key.keysym.mod = mod;
    return ev;
}

struct DummySdl {
    DummySdl() {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        ok = SDL_Init(SDL_INIT_VIDEO) == 0;
    }
    ~DummySdl() { SDL_Quit(); }
    bool ok = false;
};

}  // namespace

TEST_CASE("alt+enter (and keypad enter) toggle; other keys pass through") {
    DummySdl sdl;
    REQUIRE(sdl.ok);
    SDL_Window* win =
        SDL_CreateWindow("t", 0, 0, 320, 200, SDL_WINDOW_HIDDEN);
    REQUIRE(win != nullptr);

    // Not consumed: bare Enter (menus advance on it), Alt+other key,
    // non-key events, null window.
    CHECK(!handle_fullscreen_toggle(key_event(SDLK_RETURN, 0), win));
    CHECK(!handle_fullscreen_toggle(key_event(SDLK_SPACE, KMOD_LALT), win));
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    CHECK(!handle_fullscreen_toggle(motion, win));
    CHECK(!handle_fullscreen_toggle(key_event(SDLK_RETURN, KMOD_LALT),
                                    nullptr));
    CHECK((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0U);

    // Consumed: Alt+Enter flips to desktop fullscreen and back, either
    // Alt, main or keypad Enter.
    CHECK(handle_fullscreen_toggle(key_event(SDLK_RETURN, KMOD_LALT), win));
    CHECK((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
          SDL_WINDOW_FULLSCREEN_DESKTOP);
    CHECK(handle_fullscreen_toggle(key_event(SDLK_KP_ENTER, KMOD_RALT), win));
    CHECK((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0U);

    SDL_DestroyWindow(win);
}

TEST_CASE("desktop_integer_scale never drops below 1") {
    DummySdl sdl;
    REQUIRE(sdl.ok);
    CHECK(desktop_integer_scale(320, 200) >= 1);
    CHECK(desktop_integer_scale(100000, 100000) == 1);
}

#include "presentation/image_out.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

TEST_CASE("save_surface_image writes real PNG for .png paths") {
    DummySdl sdl;
    REQUIRE(sdl.ok);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
        0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(s != nullptr);
    SDL_FillRect(s, nullptr, SDL_MapRGB(s->format, 200, 100, 50));

    const auto tmp_dir = std::filesystem::temp_directory_path();
    const std::string png_path = (tmp_dir / "olduvai_test_shot.png").string();
    const std::string bmp_path = (tmp_dir / "olduvai_test_shot.bmp").string();
    CHECK(olduvai::presentation::save_surface_image(s, png_path));
    CHECK(olduvai::presentation::save_surface_image(s, bmp_path));

    unsigned char magic[4] = {0, 0, 0, 0};
    {   // scope the read handles closed: on Windows an open ifstream blocks
        // a subsequent writer of the same file (sharing semantics), and
        // "shot.PNG" below aliases "shot.png" on case-insensitive systems.
        std::ifstream png(png_path, std::ios::binary);
        png.read(reinterpret_cast<char*>(magic), 4);
        CHECK(magic[0] == 0x89);  // PNG signature, not 'BM'
        CHECK(magic[1] == 'P');

        std::ifstream bmp(bmp_path, std::ios::binary);
        bmp.read(reinterpret_cast<char*>(magic), 2);
        CHECK(magic[0] == 'B');
        CHECK(magic[1] == 'M');
    }

    // Distinct basename: on case-insensitive filesystems (Windows, macOS)
    // "olduvai_test_shot.PNG" would be the SAME file as the .png above.
    const std::string upper_path =
        (tmp_dir / "olduvai_test_shot_upper.PNG").string();
    CHECK(olduvai::presentation::save_surface_image(s, upper_path));
    {
        std::ifstream upper(upper_path, std::ios::binary);
        upper.read(reinterpret_cast<char*>(magic), 2);
        CHECK(magic[0] == 0x89);
        CHECK(magic[1] == 'P');
    }
    std::remove(upper_path.c_str());

    SDL_FreeSurface(s);
    std::remove(png_path.c_str());
    std::remove(bmp_path.c_str());
}

// ── aspect_logical ──────────────────────────────────────────────────────────
// The function that decides the logical size of EVERY present, and it had no
// test at all: a 2026-08-18 sweep of what the corpus actually exercises found
// "4:3" and "stretch" appearing nowhere outside settings_apply's
// classify_change cases — i.e. the tree checked whether changing the key needs
// a reinit, and never once rendered in either mode.  Two of the four branches
// had never executed under test.
//
// Needs no SDL state (pure arithmetic on the mode string), but it lives in an
// SDL-including TU, so it rides in the sdl_unit binary with the rest of
// window_util.
TEST_CASE("aspect_logical: each mode's logical size") {
    // stretch disables logical scaling entirely — the renderer fills the
    // window and SDL does no letterboxing.  0x0 is the sentinel for that.
    CHECK(aspect_logical(1, "stretch").w == 0);
    CHECK(aspect_logical(1, "stretch").h == 0);
    CHECK(aspect_logical(4, "stretch").w == 0);   // scale is ignored
    CHECK(aspect_logical(4, "stretch").h == 0);

    // 4:3 is the CRT-like vertical stretch: 240 logical rows, not 200, so the
    // 200-row framebuffer is scaled up vertically.  This is the branch whose
    // height differs from every other mode.
    CHECK(aspect_logical(1, "4:3").w == 320);
    CHECK(aspect_logical(1, "4:3").h == 240);
    CHECK(aspect_logical(2, "4:3").w == 640);
    CHECK(aspect_logical(2, "4:3").h == 480);

    // keep is the default and the fallback: square-pixel 320x200 scaled.
    CHECK(aspect_logical(1, "keep").w == 320);
    CHECK(aspect_logical(1, "keep").h == 200);
    CHECK(aspect_logical(3, "keep").w == 960);
    CHECK(aspect_logical(3, "keep").h == 600);

    // "widescreen" falls back to keep HERE on purpose — an active wide present
    // computes its own logical size from the wide framebuffer (320 + 2*margin),
    // so this function only ever sees the classic / no-coherent-neighbour case
    // and must pillarbox rather than stretch.
    CHECK(aspect_logical(2, "widescreen").w == aspect_logical(2, "keep").w);
    CHECK(aspect_logical(2, "widescreen").h == aspect_logical(2, "keep").h);

    // An unknown mode must degrade to keep, not to the 0x0 stretch sentinel —
    // a typo in play.json must not silently disable logical scaling.
    CHECK(aspect_logical(2, "").w == 640);
    CHECK(aspect_logical(2, "").h == 400);
    CHECK(aspect_logical(2, "16:9").w == 640);
    CHECK(aspect_logical(2, "16:9").h == 400);
}
