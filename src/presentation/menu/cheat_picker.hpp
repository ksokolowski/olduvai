// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// CheatPicker: the --cheats power-up overlay (F7 opens; UP/DOWN select;
// ENTER/SPACE or 1-6 grant; ESC or F7 closes).  Pauses the world while open.
// Both draw paths render row_label(), so HD and classic read the same.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SDL.h>

#include "enhance/canvas.hpp"
#include "formats/mat.hpp"   // Sprite

namespace olduvai::enhance { class HdText; }

namespace olduvai::presentation {

struct FrameBuffer;

class CheatPicker {
public:
    static constexpr int kRows = 6;

    bool open() const { return open_; }
    int sel() const { return sel_; }
    void open_picker() {
        open_ = true;
        sel_ = 0;
    }
    void close() { open_ = false; }

    // "> N NAME" for the selected row (N is the 1-6 hotkey), "  N NAME"
    // otherwise — the number is advertised so the hotkeys are discoverable.
    std::string row_label(int i) const;

    // Handle one key while the picker is up.  Returns true when the picker
    // consumed it, which is ALWAYS while open: the overlay swallows every key
    // it does not act on, so a stray press cannot reach gameplay behind it.
    // `grant(bonus_type)` applies the pick; the picker closes itself after.
    bool handle_key(SDL_Keycode sym, const std::function<void(int)>& grant);

    // The bonus type behind row `i` (the picker's rows are bonus types 0-5).
    static const char* name(int i);

    // The panel: HD in the vector font on the output-res overlay, classic in
    // the game charset on the 320x200 frame.  Same layout.
    void draw_hd(const enhance::Canvas& cv,
                 const enhance::HdText& font) const;
    void draw_native(FrameBuffer& f,
                     const std::vector<formats::Sprite>& charset) const;

private:
    bool open_ = false;
    int sel_ = 0;
};

}  // namespace olduvai::presentation
