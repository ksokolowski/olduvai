// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "first_run.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
#include "prepare/game_files.hpp"

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

enum Choice { kLocate = 0, kGog = 1, kQuit = 2 };

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
    if (SDL_ShowMessageBox(&box, &hit) != 0) return kQuit;  // headless etc.
    return hit;
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
    std::string* chosen_preset) {
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
                const std::string preset = ask_preset();
                Config c = load_config_file();
                c["game_dir"] = picked->string();
                apply_profile(c, preset);
                save_config_file(c);
                if (chosen_preset != nullptr) *chosen_preset = preset;
                return picked;
            }
            case kQuit:
            default:
                return std::nullopt;
        }
    }
}

}  // namespace olduvai::app
