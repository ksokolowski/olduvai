// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "first_run.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// MSVC spells the pipe-open functions with a leading underscore.
#if defined(_MSC_VER)
#define popen _popen
#define pclose _pclose
#endif

#include "config.hpp"
#include "enhance/hd_text.hpp"
#include "prepare/game_files.hpp"
#include "presentation/image_out.hpp"
#include "presentation/window_util.hpp"   // sdl_base_dir
#include "presentation/menu/profile_table.hpp"

namespace olduvai::app {

namespace {

constexpr const char* kGogUrl = "https://www.gog.com/game/prehistorik_12";

// Run a shell command and return its trimmed single-line stdout, or nullopt
// on failure/cancel (non-zero exit or empty output).
std::optional<std::string> capture_line(const char* cmd) {
    FILE* p = popen(cmd, "r");
    if (p == nullptr) return std::nullopt;
    char buf[4096] = {0};
    const bool got = std::fgets(buf, sizeof(buf), p) != nullptr;
    const int rc = pclose(p);
    if (!got || rc != 0) return std::nullopt;
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) return std::nullopt;
    return s;
}

// Native folder picker, no extra toolkit: AppleScript on macOS,
// zenity/kdialog on Linux, the Shell.Application COM browser on Windows.
// nullopt = cancelled or no picker available.
std::optional<std::filesystem::path> pick_folder() {
    if (const char* forced = std::getenv("OLDUVAI_FIRSTRUN_DIR")) {
        return std::filesystem::path(forced);
    }
#if defined(__APPLE__)
    return capture_line(
        "osascript -e 'POSIX path of (choose folder with prompt "
        "\"Select your Prehistorik game folder\")' 2>/dev/null");
#elif defined(_WIN32)
    return capture_line(
        "powershell -NoProfile -Command "
        "\"(New-Object -ComObject Shell.Application)."
        "BrowseForFolder(0,'Select your Prehistorik game folder',0)."
        "Self.Path\" 2>NUL");
#else
    if (auto z = capture_line(
            "zenity --file-selection --directory "
            "--title='Select your Prehistorik game folder' 2>/dev/null")) {
        return z;
    }
    return capture_line(
        "kdialog --getexistingdirectory ~ "
        "--title 'Select your Prehistorik game folder' 2>/dev/null");
#endif
}

enum Choice { kLocate = 0, kGog = 1, kQuit = 2, kNoBox = 3 };

int show_box(const std::string& text) {
    if (const char* forced = std::getenv("OLDUVAI_FIRSTRUN_ANSWER")) {
        if (std::strcmp(forced, "locate") == 0) return kLocate;
        return kQuit;
    }
    // returnkey → Locate (the productive default), escape → Quit.
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kQuit, "Quit"},
        {0, kGog, "Get the game (GOG)"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kLocate,
         "Locate game folder…"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION, nullptr, "Olduvai — game files needed",
        text.c_str(), SDL_arraysize(buttons), buttons, nullptr};
    int hit = kQuit;
    // No box could be shown — a handheld frontend (KNULLI: kmsdrm / mali, no
    // desktop to host it) or a headless run.  NOT the same as Quit: the
    // caller then puts the message on screen itself.
    if (SDL_ShowMessageBox(&box, &hit) != 0) return kNoBox;
    return hit;
}

// HdText walks BYTES (stb_truetype codepoint per char), so the screen below
// gets plain ASCII: the messages' em dashes and ellipses become their ASCII
// spellings, anything else outside ASCII is dropped rather than drawn as junk.
std::string ascii_only(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out += s[i]; ++i; continue; }
        if (s.compare(i, 3, "\xE2\x80\x94") == 0 ||
            s.compare(i, 3, "\xE2\x80\x93") == 0) { out += '-'; i += 3; continue; }
        if (s.compare(i, 3, "\xE2\x80\xA6") == 0) { out += "..."; i += 3; continue; }
        ++i;   // skip the rest of this UTF-8 sequence byte by byte
        while (i < s.size() &&
               (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return out;
}

// The missing-files message ON SCREEN, for when no message box can be shown.
// Without it a handheld launch from the Ports menu exited in under a second
// with the explanation only in a log file the player never sees (Powkiddy A12,
// KNULLI, 2026-09-13: rc=1, "ran 0s").  Drawn with the bundled vector font —
// there is no game data to borrow a font from, by definition.  Waits for any
// button, key or touch, or two minutes.  OLDUVAI_NOFILES_SHOT=<file> saves the
// composed screen and returns at once (tests/nofiles_screen.sh).  Returns false
// if it could not show anything; the log line has been printed either way.
// Paint the page: the first line in the title colour, the rest in body grey,
// the block centred.  The cap size fits the longest line to 90% of the width
// and the whole block to 90% of the height, never below a readable 8 px --
// the handhelds' panels and a desktop window are the same code path here.
std::vector<std::uint8_t> paint_text_page(enhance::HdText& text,
                                          const std::vector<std::string>& lines,
                                          int ow, int oh) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(ow) * oh * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = 18; px[i + 1] = 16; px[i + 2] = 24; px[i + 3] = 255;
    }
    text.set_cap_px(std::max(8, oh / 34));
    int widest = 1;
    for (const auto& l : lines) widest = std::max(widest, text.measure(l));
    if (widest > ow * 9 / 10)
        text.set_cap_px(std::max(8, text.cap_px() * (ow * 9 / 10) / widest));
    const int line_h = text.cap_px() * 2;
    int y = (oh - static_cast<int>(lines.size()) * line_h) / 2 +
            text.cap_px() * 3 / 2;
    for (std::size_t i = 0; i < lines.size(); ++i, y += line_h) {
        const int x = (ow - text.measure(lines[i])) / 2;
        if (i == 0)
            text.draw(px, ow, oh, x, y, lines[i], 255, 196, 64);
        else
            text.draw(px, ow, oh, x, y, lines[i], 220, 220, 228);
    }
    return px;
}

// Hold the page on screen until the player presses something, or two minutes
// pass.  Every pad is opened for the wait: on a handheld there is no keyboard
// to fall back to, and the launcher hands us a device nobody has opened yet.
void wait_for_any_button(SDL_Renderer* ren, SDL_Texture* tex) {
    std::vector<SDL_Joystick*> pads;
    for (int j = 0; j < SDL_NumJoysticks(); ++j)
        if (SDL_Joystick* js = SDL_JoystickOpen(j)) pads.push_back(js);
    // The button that launched the port may still be bouncing:
    // ignore input for the first 700 ms.
    const Uint32 t0 = SDL_GetTicks();
    bool done = false;
    while (!done && SDL_GetTicks() - t0 < 120000) {
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Event e;
        while (SDL_WaitEventTimeout(&e, 100)) {
            const bool input =
                e.type == SDL_KEYDOWN ||
                e.type == SDL_JOYBUTTONDOWN ||
                e.type == SDL_CONTROLLERBUTTONDOWN ||
                e.type == SDL_MOUSEBUTTONDOWN ||
                e.type == SDL_FINGERDOWN;
            if (e.type == SDL_QUIT ||
                (input && SDL_GetTicks() - t0 > 700)) {
                done = true;
                break;
            }
        }
    }
    for (SDL_Joystick* js : pads) SDL_JoystickClose(js);
}

bool show_text_screen(const std::vector<std::string>& lines) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) return false;
    SDL_Window* win = SDL_CreateWindow(
        "Olduvai", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    // PRESENTVSYNC: on kmsdrm a non-vsynced present is an async page flip,
    // which the A12's driver rejects ("Could not queue pageflip: -22", seen
    // on this very screen) — and a static text screen has no use for more
    // frames than the panel shows.  Retry without it for a driver that
    // refuses the flag outright.
    SDL_Renderer* ren = nullptr;
    if (win != nullptr) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (ren == nullptr) ren = SDL_CreateRenderer(win, -1, 0);
    }
    bool shown = false;
    if (ren != nullptr) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const std::string exe_dir = olduvai::presentation::sdl_base_dir();
        enhance::HdText text;
        if (ow > 0 && oh > 0 && text.load(exe_dir, 1, "NotoSans-Regular.ttf")) {
            const std::vector<std::uint8_t> px =
                paint_text_page(text, lines, ow, oh);
            if (const char* shot = std::getenv("OLDUVAI_NOFILES_SHOT")) {
                shown = presentation::save_rgba_image(px.data(), ow, oh, shot);
            } else if (SDL_Texture* tex = SDL_CreateTexture(
                           ren, SDL_PIXELFORMAT_RGBA32,
                           SDL_TEXTUREACCESS_STATIC, ow, oh)) {
                SDL_UpdateTexture(tex, nullptr, px.data(), ow * 4);
                wait_for_any_button(ren, tex);
                SDL_DestroyTexture(tex);
                shown = true;
            }
        }
    }
    if (ren != nullptr) SDL_DestroyRenderer(ren);
    if (win != nullptr) SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    return shown;
}

// The missing-files screen's lines.  game_dir is made ABSOLUTE: a handheld
// launcher passes ./game, and "copy them into ./game" helps no one holding an
// SD card.
std::vector<std::string> missing_files_lines(
    const std::filesystem::path& game_dir, const std::string& problems) {
    std::error_code ec;
    const auto abs = std::filesystem::absolute(game_dir, ec);
    std::vector<std::string> l = {
        "Olduvai - game files needed",
        "",
        "Copy your own Prehistorik (1991) game files into:",
        ascii_only((ec ? game_dir : abs.lexically_normal()).string()),
        ""};
    std::istringstream in(problems);
    for (std::string line; std::getline(in, line);)
        if (!line.empty()) l.push_back(ascii_only(line));
    l.push_back("");
    l.push_back("GOG's \"Prehistorik 1+2\" works; so do the original DOS files.");
    l.push_back("");
    l.push_back("Press any button to exit.");
    return l;
}

// One-time "how should it look?" ask, right after the game folder is
// accepted — the moment a fresh user actually cares.  Returns the profile
// name.  OLDUVAI_FIRSTRUN_PRESET=dos|hd skips the box (tests/headless).
std::string ask_preset() {
    if (const char* forced = std::getenv("OLDUVAI_FIRSTRUN_PRESET")) {
        return std::strcmp(forced, "hd") == 0 ? "hd" : "dos";
    }
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Classic DOS"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Enhanced HD"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION, nullptr, "Olduvai — how should it look?",
        "Classic DOS: exactly the 1991 game, pixels and all.\n"
        "Enhanced HD: widescreen, upscaled sprites, smooth motion.\n\n"
        "You can change this any time in Options.",
        SDL_arraysize(buttons), buttons, nullptr};
    int hit = 0;
    // Box could not be shown (headless/dummy video): report NO choice —
    // persisting a default here would silently burn the one-time question.
    if (SDL_ShowMessageBox(&box, &hit) != 0) return "";
    return hit == 1 ? "hd" : "dos";
}

}  // namespace

bool launched_from_gui() {
    if (const char* env = std::getenv("OLDUVAI_NO_GUI");
        env != nullptr && env[0] == '1') {
        return false;
    }
    // Dev/testing: treat a terminal launch as a GUI one WITHOUT auto-
    // answering — the real dialogs appear and wait for clicks (the
    // FIRSTRUN_* hooks below force GUI mode too, but answer for you).
    if (const char* env = std::getenv("OLDUVAI_FORCE_GUI");
        env != nullptr && env[0] == '1') {
        return true;
    }
    if (std::getenv("OLDUVAI_FIRSTRUN_ANSWER") != nullptr) return true;
#ifdef _WIN32
    // A console shared with nobody = Windows allocated it for a double-
    // clicked console-subsystem exe; a terminal-run exe shares its parent's.
    DWORD pids[2];
    return GetConsoleProcessList(pids, 2) <= 1;
#else
    return isatty(STDIN_FILENO) == 0 && isatty(STDERR_FILENO) == 0;
#endif
}

std::string ask_preset_choice() { return ask_preset(); }

std::optional<std::filesystem::path> first_run_dialog(
    const std::filesystem::path& game_dir, const std::string& problems,
    std::string* chosen_preset, const std::string& family) {
    std::string msg =
        "Olduvai needs your original Prehistorik (1991) game files —\n"
        "the engine ships no game content.\n\n"
        "Nothing usable was found in:\n  " + game_dir.string() + "\n" +
        problems +
        "\nA GOG copy of \"Prehistorik 1+2\" works out of the box; the\n"
        "original floppy-era DOS files work too.";
    for (;;) {
        switch (show_box(msg)) {
            case kGog:
                SDL_OpenURL(kGogUrl);
                continue;   // back to the dialog — the user returns from
                            // the browser with files (or quits)
            case kLocate: {
                const auto picked = pick_folder();
                if (!picked) continue;              // cancelled → re-ask
                const auto gf = prepare::detect_game_files(*picked);
                if (!gf.complete()) {
                    msg = "That folder does not hold a complete set:\n  " +
                          picked->string() + "\n" + gf.problems() +
                          "\nSelect the folder that contains the game's "
                          "data files.";
                    continue;
                }
                // Remember the choice so the next double-click just plays,
                // and ask the one-time presentation question while we have
                // the user's attention (changeable later in Options).
                const std::string preset =
                    presentation::resolve_preset(family, ask_preset()).name;
                Config c = load_config_file();
                c["game_dir"] = picked->string();
                apply_profile(c, preset);
                save_config_file(c);
                if (chosen_preset != nullptr) *chosen_preset = preset;
                return picked;
            }
            case kNoBox:
                show_text_screen(missing_files_lines(game_dir, problems));
                return std::nullopt;
            case kQuit:
            default:
                return std::nullopt;
        }
    }
}

}  // namespace olduvai::app
