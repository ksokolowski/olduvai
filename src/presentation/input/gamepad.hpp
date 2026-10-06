// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game controller support with configurable mapping, in two parts:
//  1. A global SDL event watch (a named function: SDL_DelEventWatch matches by
//     pointer) handles hotplug and turns mapped buttons into keyboard events
//     by Context (dpad -> arrows; menu: confirm -> RETURN, back/pause ->
//     ESCAPE; play: pause -> ESCAPE), so every event-driven loop works with
//     a pad unchanged.
//  2. Polled accessors (dpad or left stick past a deadzone; mapped buttons)
//     for the SDL_GetKeyboardState sites (gameplay input, skip-held checks),
//     which synthetic events do not reach.
// Mapping from play.json: pad_jump / pad_attack / pad_pause / pad_confirm /
// pad_back, each up to two SDL controller names ("a", "b,y", "start",
// "leftshoulder", "lefttrigger", ...; a trigger is pressed past half), and
// pad_deadzone (default 8000).

#pragma once

#include <SDL.h>

#include <optional>
#include <string>

#include "presentation/input/button_layout.hpp"   // PadFamily

namespace olduvai::presentation {
struct GameOptions;   // (game_app.hpp) — for init_from_options
}

namespace olduvai::presentation::gamepad {

// Each action's buttons by SDL controller name.  The shortcuts act in play
// only while the modifier is held (the RetroArch / gptokeyb shape), and
// then send their keyboard key (F6, F9, F7, F5); Select + Start stays free
// for PortMaster's quit.
struct Config {
    BindingList jump{"a"};
    BindingList attack{"x"};
    BindingList pause{"start"};
    BindingList confirm{"a"};
    BindingList back{"b"};
    BindingList modifier{"back"};
    BindingList quicksave{"rightshoulder"};
    BindingList quickload{"leftshoulder"};
    BindingList cheats{"y"};
    BindingList bug_report{"x"};
    int deadzone = 8000;
};

// What a press sends to the event loops.  In play only Pause does (as Esc):
// jump and attack are read as held state, so a face button can attack in
// play and back out of a menu.  In a menu Confirm sends Enter, Back and
// Pause Esc.  The D-pad sends arrows in both.
enum class Context { kMenu, kPlay };

// The context for the events pumped while it lives (a play loop's poll);
// menu everywhere else.
class ContextScope {
  public:
    explicit ContextScope(Context c);
    ~ContextScope();
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

  private:
    Context prev_;
};

// The key a press of the button named `name` sends in `context`, the
// modifier held or not (SDLK_UNKNOWN: none).
SDL_Keycode key_for_input(const std::string& name, Context context,
                          const Config& cfg, bool modifier = false);
// Whether `name` is the shortcut modifier (a capture never binds it).
bool is_modifier(const std::string& name);

// Init the controller subsystem, register the watch, open connected pads.
// Once per process, after SDL_Init; idempotent.
void init(const Config& cfg);
// Build a Config from a GameOptions' pad_* keys (inputs_from_string with the
// per-action defaults) and init().  The session's gamepad-setup one-liner.
void init_from_options(const GameOptions& opts);
void shutdown();

bool connected();
// The letters the connected pad prints, when SDL knows its type; nullopt
// with no pad or an unknown one (a handheld's built-in pad usually).
std::optional<PadFamily> printed_family();

// Live polled state — dpad OR left stick (deadzone-gated) OR mapped button.
// While the modifier is held the buttons belong to the shortcuts: jump and
// attack read as released.
bool left();
bool right();
bool up();
bool down();
bool jump_held();
bool attack_held();
// confirm OR jump (the "fire" sense used by skip-held helpers).
bool fire_held();

// The live binding of a pad_* key as SDL names ("b,y"); "" for another key.
// The Options menu reads the mapping from here, not from the launch options.
std::string binding(const std::string& key);
// Rebind the action a pad_* key names, from now on.  False for another key
// or when no name in `value` is a button (the binding stays).
bool apply_binding(const std::string& key, const std::string& value);

// Parse a binding setting ("b,y"); unknown names are dropped with a warning
// on stderr, and `def` stands when none is left.
BindingList inputs_from_string(const std::string& value, const BindingList& def);

// Capture (Options -> Controls): the next button or trigger pressed before
// `deadline` (SDL ticks) is recorded instead of acting, the D-pad and Guide
// excepted, and a wake-up key event (SDLK_UNKNOWN) is queued for the menu.
void arm_capture(Uint32 deadline);
void disarm_capture();
// The recorded name, once; nullopt while waiting.
std::optional<std::string> take_capture();
// Record `name` as if pressed (the menu_script `pad:<name>` token).
void feed_capture(const std::string& name);

}  // namespace olduvai::presentation::gamepad
