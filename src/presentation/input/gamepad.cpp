// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/input/gamepad.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "presentation/game_app.hpp"   // GameOptions

namespace olduvai::presentation::gamepad {

namespace {

Config g_cfg;
SDL_GameController* g_pad = nullptr;
bool g_inited = false;
Context g_context = Context::kMenu;
// A trigger counts as a button pressed past half its travel.
constexpr Sint16 kTriggerPressed = 16384;
bool g_trigger_down[2] = {false, false};   // left, right
bool g_capture_armed = false;
Uint32 g_capture_deadline = 0;
std::optional<std::string> g_captured;

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

bool capturing() {
    if (g_capture_armed && SDL_TICKS_PASSED(SDL_GetTicks(), g_capture_deadline))
        g_capture_armed = false;
    return g_capture_armed;
}

bool is_dpad(Uint8 b) {
    return b == SDL_CONTROLLER_BUTTON_DPAD_UP ||
           b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
           b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
           b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}

bool held(const BindingList& names);

// A press of `name` (a button, or a trigger crossing half) or its release.
void on_input(const std::string& name, bool down) {
    if (capturing()) {
        if (down) feed_capture(name);
        return;   // nothing acts while the menu listens
    }
    const SDL_Keycode sym =
        key_for_input(name, g_context, g_cfg, held(g_cfg.modifier));
    if (sym != SDLK_UNKNOWN) push_key(sym, down);
}

void on_button(Uint8 button, bool down) {
    const auto b = static_cast<SDL_GameControllerButton>(button);
    if (capturing() && (is_dpad(button) || b == SDL_CONTROLLER_BUTTON_GUIDE))
        return;
    const char* name = SDL_GameControllerGetStringForButton(b);
    if (name != nullptr) on_input(name, down);
}

void on_axis(Uint8 axis, Sint16 value) {
    const int t = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT    ? 0
                  : axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1
                                                             : -1;
    if (t < 0) return;
    const bool down = value > kTriggerPressed;
    if (down == g_trigger_down[t]) return;
    g_trigger_down[t] = down;
    on_input(t == 0 ? "lefttrigger" : "righttrigger", down);
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
        case SDL_CONTROLLERBUTTONUP:
            on_button(ev->cbutton.button,
                      ev->type == SDL_CONTROLLERBUTTONDOWN);
            break;
        case SDL_CONTROLLERAXISMOTION:
            on_axis(ev->caxis.axis, ev->caxis.value);
            break;
        default:
            break;
    }
    return 1;  // never filter the original event out
}

bool btn(SDL_GameControllerButton b) {
    return g_pad != nullptr && SDL_GameControllerGetButton(g_pad, b) != 0;
}

// Whether any of `names` is held: a button down, or a trigger past half.
bool held(const BindingList& names) {
    if (g_pad == nullptr) return false;
    return std::any_of(names.begin(), names.end(), [](const std::string& n) {
        const SDL_GameControllerButton b =
            SDL_GameControllerGetButtonFromString(n.c_str());
        if (b != SDL_CONTROLLER_BUTTON_INVALID) return btn(b);
        const SDL_GameControllerAxis a =
            SDL_GameControllerGetAxisFromString(n.c_str());
        return a != SDL_CONTROLLER_AXIS_INVALID &&
               SDL_GameControllerGetAxis(g_pad, a) > kTriggerPressed;
    });
}

bool is_input_name(const std::string& n) {
    return SDL_GameControllerGetButtonFromString(n.c_str()) !=
               SDL_CONTROLLER_BUTTON_INVALID ||
           n == "lefttrigger" || n == "righttrigger";
}

// The Config field a pad_* key names, or nullptr.
BindingList* field_of(const std::string& key) {
    if (key == "pad_jump") return &g_cfg.jump;
    if (key == "pad_attack") return &g_cfg.attack;
    if (key == "pad_confirm") return &g_cfg.confirm;
    if (key == "pad_back") return &g_cfg.back;
    if (key == "pad_pause") return &g_cfg.pause;
    return nullptr;
}

bool axis_past(SDL_GameControllerAxis a, int sign) {
    if (g_pad == nullptr) return false;
    const int v = SDL_GameControllerGetAxis(g_pad, a);
    return sign > 0 ? v > g_cfg.deadzone : v < -g_cfg.deadzone;
}

}  // namespace

// jump/attack get NO synthetic key — gameplay reads the polled accessors,
// and stray SPACE events would double-trigger menus.
SDL_Keycode key_for_input(const std::string& name, Context context,
                          const Config& cfg, bool modifier) {
    if (name == "dpup") return SDLK_UP;
    if (name == "dpdown") return SDLK_DOWN;
    if (name == "dpleft") return SDLK_LEFT;
    if (name == "dpright") return SDLK_RIGHT;
    if (holds(cfg.modifier, name)) return SDLK_UNKNOWN;
    if (context == Context::kPlay && modifier) {
        if (holds(cfg.quicksave, name)) return SDLK_F6;
        if (holds(cfg.quickload, name)) return SDLK_F9;
        if (holds(cfg.cheats, name)) return SDLK_F7;
        if (holds(cfg.bug_report, name)) return SDLK_F5;
        return SDLK_UNKNOWN;
    }
    if (context == Context::kPlay)
        return holds(cfg.pause, name) ? SDLK_ESCAPE : SDLK_UNKNOWN;
    if (holds(cfg.confirm, name)) return SDLK_RETURN;
    if (holds(cfg.back, name) || holds(cfg.pause, name)) return SDLK_ESCAPE;
    return SDLK_UNKNOWN;
}

ContextScope::ContextScope(Context c) : prev_(g_context) { g_context = c; }
ContextScope::~ContextScope() { g_context = prev_; }

void init(const Config& cfg) {
    g_cfg = cfg;
    if (g_inited) return;
    // Report face buttons by POSITION on every pad.  SDL2's default reports
    // a Switch-type pad by its labels, which would make "a" its right
    // button and every layout here wrong on it.  The hint only takes effect
    // if nothing started the controller subsystem before this line (an
    // SDL_Init(EVERYTHING) upstream would), so that case is reported.
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0)
        std::fprintf(stderr,
                     "gamepad: controllers were initialised before the "
                     "button-position hint; a Switch-type pad may read by "
                     "its printed labels\n");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "gamepad: init failed: %s (keyboard only)\n",
                     SDL_GetError());
        return;
    }
    SDL_AddEventWatch(gamepad_event_watch, nullptr);
    open_first_available();
    g_inited = true;
}

void init_from_options(const GameOptions& opts) {
    Config pcfg;
    pcfg.jump = inputs_from_string(opts.pad_jump, pcfg.jump);
    pcfg.attack = inputs_from_string(opts.pad_attack, pcfg.attack);
    pcfg.pause = inputs_from_string(opts.pad_pause, pcfg.pause);
    pcfg.confirm = inputs_from_string(opts.pad_confirm, pcfg.confirm);
    pcfg.back = inputs_from_string(opts.pad_back, pcfg.back);
    pcfg.deadzone = opts.pad_deadzone;
    init(pcfg);
}

void shutdown() {
    if (!g_inited) return;
    SDL_DelEventWatch(gamepad_event_watch, nullptr);
    if (g_pad != nullptr) SDL_GameControllerClose(g_pad);
    g_pad = nullptr;
    g_inited = false;
}

bool connected() { return g_pad != nullptr; }

std::optional<PadFamily> printed_family() {
    if (g_pad == nullptr) return std::nullopt;
    switch (SDL_GameControllerGetType(g_pad)) {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE:
            return PadFamily::kXbox;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            return PadFamily::kPlayStation;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
            return PadFamily::kNintendo;
        default:
            return std::nullopt;
    }
}

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
           axis_past(SDL_CONTROLLER_AXIS_LEFTY, -1) || jump_held();
}
bool down() {
    return btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
           axis_past(SDL_CONTROLLER_AXIS_LEFTY, +1);
}
bool jump_held() { return !held(g_cfg.modifier) && held(g_cfg.jump); }
bool attack_held() { return !held(g_cfg.modifier) && held(g_cfg.attack); }

bool is_modifier(const std::string& name) { return holds(g_cfg.modifier, name); }
bool fire_held() { return held(g_cfg.confirm) || held(g_cfg.jump); }

std::string binding(const std::string& key) {
    const BindingList* f = field_of(key);
    return f == nullptr ? std::string{} : join_binding(*f);
}

bool apply_binding(const std::string& key, const std::string& value) {
    BindingList* f = field_of(key);
    if (f == nullptr) return false;
    BindingList names = valid_bindings(value, is_input_name);
    if (names.empty()) return false;
    *f = std::move(names);
    return true;
}

BindingList inputs_from_string(const std::string& value,
                               const BindingList& def) {
    const BindingList out = valid_bindings(value, [](const std::string& n) {
        if (is_input_name(n)) return true;
        std::fprintf(stderr, "gamepad: unknown button name '%s'\n", n.c_str());
        return false;
    });
    return out.empty() ? def : out;
}

void arm_capture(Uint32 deadline) {
    g_capture_armed = true;
    g_capture_deadline = deadline;
    g_captured.reset();
}

void disarm_capture() { g_capture_armed = false; }

std::optional<std::string> take_capture() {
    std::optional<std::string> out;
    out.swap(g_captured);
    return out;
}

void feed_capture(const std::string& name) {
    g_capture_armed = false;
    g_captured = name;
    push_key(SDLK_UNKNOWN, true);   // wake the menu's event loop
}

}  // namespace olduvai::presentation::gamepad
