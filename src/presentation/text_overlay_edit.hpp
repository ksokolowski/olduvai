// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-canvas text-editor overlay: a TextEditor plus a Tab focus ring
// (text area / [Save] / [Cancel]) rendered on the menu slab.  Opened by a
// `text` menu row (menu.cpp's __edit_text sentinel).  Enter in the text area
// means newline; Save/Cancel are buttons (Tab to reach, Enter to activate);
// Esc always cancels; Ctrl+Enter is a Save accelerator.
#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include "enhance/hd_text.hpp"
#include "presentation/game_render.hpp"   // FrameBuffer
#include "presentation/text_editor.hpp"

namespace olduvai::presentation {

enum class EditFocus { kText, kSave, kCancel };
enum class EditResult { kNone, kSave, kCancel };

struct EditOverlayState {
    TextEditor editor;
    EditFocus focus = EditFocus::kText;
    std::string title;   // shown at the top of the slab (the row label)
};

// Feed one SDL event; returns kSave / kCancel to close, kNone to stay open.
EditResult edit_handle_event(EditOverlayState& st, const SDL_Event& ev);

// One on-screen row after soft word-wrapping a logical line to `cols`
// characters.  `lrow` = the logical line it came from; `start` = byte offset
// into that logical line where this visual row begins.  Wrapping is purely
// visual — the editor's stored text keeps the user's real line breaks (no
// injected newlines).  A word longer than `cols` is hard-broken.
struct VisRow {
    std::string text;
    int lrow;
    int start;
};
std::vector<VisRow> wrap_lines(const std::vector<std::string>& lines, int cols);

// Bitmap path: draw the editor onto a native 320x200 FrameBuffer.
void draw_edit_overlay(FrameBuffer& fb,
                       const std::vector<formats::Sprite>& charset,
                       const EditOverlayState& st);

// Vector path: draw into an output-res RGBA buffer with the HD font.
void draw_edit_overlay_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                              enhance::HdText& font, const EditOverlayState& st);

}  // namespace olduvai::presentation
