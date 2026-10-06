// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/cheat_picker.hpp"

#include <cstddef>

#include "presentation/input/actions.hpp"        // key_is
#include "presentation/render/game_render.hpp"   // FrameBuffer
#include "presentation/render/hud_render.hpp"    // draw_text, text_width
#include "presentation/render/text_overlay.hpp"  // draw_centered_overlay_row

namespace olduvai::presentation {

namespace {
const char* const kPowerupNames[CheatPicker::kRows] = {
    "SPRING", "BOMB", "TIMER", "EXTRA LIFE", "SHIELD", "AXE"};
}  // namespace

const char* CheatPicker::name(int i) {
    return (i >= 0 && i < kRows) ? kPowerupNames[i] : "";
}

std::string CheatPicker::row_label(int i) const {
    return (i == sel_ ? std::string("> ") : std::string("  ")) +
           std::to_string(i + 1) + " " + name(i);
}

bool CheatPicker::handle_key(SDL_Keycode sym,
                             const std::function<void(int)>& grant) {
    if (!open_) return false;
    if (key_is(sym, Action::kBack) || key_is(sym, Action::kCheats)) {
        open_ = false;
    } else if (key_is(sym, Action::kUp)) {
        sel_ = (sel_ + kRows - 1) % kRows;
    } else if (key_is(sym, Action::kDown)) {
        sel_ = (sel_ + 1) % kRows;
    } else if (key_is(sym, Action::kConfirm)) {
        grant(sel_);
        open_ = false;
    } else if (sym >= SDLK_1 && sym <= SDLK_6) {
        grant(static_cast<int>(sym - SDLK_1));
        open_ = false;
    }
    // Consumed either way: the overlay swallows every key while it is up.
    return true;
}

void CheatPicker::draw_hd(const enhance::Canvas& cv,
                          const enhance::HdText& font) const {
    // Backdrop panel, native 50..270 x 44..182 (fits the 190 px hint line).
    const int px0 = cv.w * 50 / 320, px1 = cv.w * 270 / 320;
    const int py0 = cv.h * 44 / 200, py1 = cv.h * 182 / 200;
    for (int y = py0; y < py1 && y < cv.h; ++y)
        for (int x = px0; x < px1 && x < cv.w; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * cv.w + x) * 4;
            cv.px[o] = 16;
            cv.px[o + 1] = 16;
            cv.px[o + 2] = 36;
            cv.px[o + 3] = 214;
        }
    draw_centered_overlay_row(cv, font, 52, "- POWER-UP CHEAT -");
    for (int i = 0; i < kRows; ++i)
        draw_centered_overlay_row(cv, font, 72 + i * 14,
                                  row_label(i));
    draw_centered_overlay_row(cv, font, 170,
                              "1-6  UP/DOWN  ENTER  ESC");
}

void CheatPicker::draw_native(FrameBuffer& f,
                              const std::vector<formats::Sprite>& charset) const {
    if (charset.empty()) return;
    // A fixed palette, readable over any level's.
    std::vector<formats::Rgb> pal(16, formats::Rgb{200, 200, 200});
    pal[7] = formats::Rgb{170, 170, 185};    // unselected rows + hint
    pal[14] = formats::Rgb{252, 224, 64};    // selected row
    pal[15] = formats::Rgb{252, 252, 252};   // title
    constexpr int A = 200;   // panel opacity (/256) over the scene
    for (int y = 44; y < 182; ++y)
        for (int x = 50; x < 270; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * 320 + x) * 4;
            f.px[o]     = static_cast<std::uint8_t>((f.px[o]     * (256 - A) + 16 * A) >> 8);
            f.px[o + 1] = static_cast<std::uint8_t>((f.px[o + 1] * (256 - A) + 16 * A) >> 8);
            f.px[o + 2] = static_cast<std::uint8_t>((f.px[o + 2] * (256 - A) + 40 * A) >> 8);
            f.px[o + 3] = 255;
        }
    const auto row = [&](int baseline, const std::string& s, int col) {
        draw_text(f, charset, pal, (320 - text_width(charset, s)) / 2,
                  baseline, s, col);
    };
    row(56, "- POWER-UP CHEAT -", 15);
    for (int i = 0; i < kRows; ++i)
        row(76 + i * 12, row_label(i), i == sel_ ? 14 : 7);
    row(176, "1-6  UP/DOWN  ENTER  ESC", 7);
}

}  // namespace olduvai::presentation
