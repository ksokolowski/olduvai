// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The on-screen answer to a save, for a moment: "Saved", or "Save failed"
// (read-only folder, full disk).  The ding alone said neither.  Drawn under
// the HUD by both HUDs: Enhanced in its text layer, Classic in the bitmap
// font.  SDL-free: the caller passes its millisecond clock.
#pragma once

#include <cstdint>

namespace olduvai::presentation {

struct SaveNotice {
    static constexpr std::uint32_t kShowMs = 1500;
    static constexpr int kBaselineY = 40;   // native, under the HUD

    void post(bool saved, std::uint32_t now_ms) {
        saved_ = saved;
        until_ms_ = now_ms + kShowMs;
        posted_ = true;
    }
    // The text at `now_ms`, or null once it has gone (wrap-safe).
    const char* text(std::uint32_t now_ms) const {
        if (!posted_ || static_cast<std::int32_t>(until_ms_ - now_ms) <= 0)
            return nullptr;
        return saved_ ? "Saved" : "Save failed";
    }
    bool saved() const { return saved_; }

private:
    bool posted_ = false;
    bool saved_ = false;
    std::uint32_t until_ms_ = 0;
};

}  // namespace olduvai::presentation
