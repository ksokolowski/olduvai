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
