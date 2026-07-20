// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/gamepad.hpp"

#include <cstdio>

namespace olduvai::presentation::gamepad {

namespace {

Config g_cfg;
SDL_GameController* g_pad = nullptr;
bool g_inited = false;

void open_first_available() {
    if (g_pad != nullptr) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        g_pad = SDL_GameControllerOpen(i);
        if (g_pad != nullptr) {
            std::printf("gamepad: %s connected\n",
                        SDL_GameControllerName(g_pad));
            return;
        }
    }
}

void push_key(SDL_Keycode sym, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = sym;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
    e.key.repeat = 0;
    SDL_PushEvent(&e);
}

// Synthetic-key translation for the event-driven loops.  Deliberately
// minimal: dpad → arrows (menu navigation), confirm → RETURN, back and
// pause → ESCAPE.  jump/attack get NO synthetic key — gameplay reads the
// polled accessors, and stray SPACE events would double-trigger menus.
SDL_Keycode translate(Uint8 button) {
    if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) return SDLK_UP;
    if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) return SDLK_DOWN;
    if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) return SDLK_LEFT;
    if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) return SDLK_RIGHT;
    if (button == g_cfg.confirm) return SDLK_RETURN;
    if (button == g_cfg.back || button == g_cfg.pause) return SDLK_ESCAPE;
    return SDLK_UNKNOWN;
}

// NAMED watch function — SDL_DelEventWatch matches by function pointer
// (same lesson as the audio device watch: an anonymous lambda would be
// unremovable and dangle across re-inits).
int gamepad_event_watch(void* /*userdata*/, SDL_Event* ev) {
    switch (ev->type) {
        case SDL_CONTROLLERDEVICEADDED:
            open_first_available();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad != nullptr &&
                ev->cdevice.which ==
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(g_pad))) {
                SDL_GameControllerClose(g_pad);
                g_pad = nullptr;
                std::printf("gamepad: disconnected\n");
                open_first_available();  // fall back to another pad if any
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const SDL_Keycode sym = translate(ev->cbutton.button);
            if (sym != SDLK_UNKNOWN)
                push_key(sym, ev->type == SDL_CONTROLLERBUTTONDOWN);
            break;
        }
        default:
            break;
    }
    return 1;  // never filter the original event out
}

bool btn(SDL_GameControllerButton b) {
    return g_pad != nullptr && SDL_GameControllerGetButton(g_pad, b) != 0;
}

bool axis_past(SDL_GameControllerAxis a, int sign) {
    if (g_pad == nullptr) return false;
    const int v = SDL_GameControllerGetAxis(g_pad, a);
    return sign > 0 ? v > g_cfg.deadzone : v < -g_cfg.deadzone;
}

}  // namespace

void init(const Config& cfg) {
    g_cfg = cfg;
    if (g_inited) return;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "gamepad: init failed: %s (keyboard only)\n",
                     SDL_GetError());
        return;
    }
    SDL_AddEventWatch(gamepad_event_watch, nullptr);
    open_first_available();
    g_inited = true;
}

void shutdown() {
    if (!g_inited) return;
    SDL_DelEventWatch(gamepad_event_watch, nullptr);
    if (g_pad != nullptr) SDL_GameControllerClose(g_pad);
    g_pad = nullptr;
    g_inited = false;
}

bool connected() { return g_pad != nullptr; }

bool left() {
    return btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
           axis_past(SDL_CONTROLLER_AXIS_LEFTX, -1);
}
bool right() {
    return btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
           axis_past(SDL_CONTROLLER_AXIS_LEFTX, +1);
}
bool up() {
    return btn(SDL_CONTROLLER_BUTTON_DPAD_UP) ||
           axis_past(SDL_CONTROLLER_AXIS_LEFTY, -1) || btn(g_cfg.jump);
}
bool down() {
    return btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
           axis_past(SDL_CONTROLLER_AXIS_LEFTY, +1);
}
bool jump_held() { return btn(g_cfg.jump); }
bool attack_held() { return btn(g_cfg.attack); }
bool fire_held() { return btn(g_cfg.confirm) || btn(g_cfg.jump); }

SDL_GameControllerButton button_from_string(const std::string& name,
                                            SDL_GameControllerButton def) {
    if (name.empty()) return def;
    const SDL_GameControllerButton b =
        SDL_GameControllerGetButtonFromString(name.c_str());
    if (b == SDL_CONTROLLER_BUTTON_INVALID) {
        std::fprintf(stderr,
                     "gamepad: unknown button name '%s' (using default)\n",
                     name.c_str());
        return def;
    }
    return b;
}

}  // namespace olduvai::presentation::gamepad
