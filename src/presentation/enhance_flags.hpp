// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Per-feature enhanced-mode toggles.  Parity with the reference
// implementation's enhancement-name set: `--enhanced` enables the whole
// bundle, `--enhance a,b` enables a named subset (union of both).  Each
// visible/audible enhancement is gated on its own flag so users can opt into
// individual effects; the bare `--enhanced` sets every flag (so the existing
// all-on behaviour is byte-identical).  The default-DOS path leaves all false.

#pragma once

#include <string>

namespace olduvai::presentation {

struct EnhanceFlags {
    bool smooth_motion = false;   // sub-frame motion interpolation
    bool cinematic_cue = false;   // tally completion chime (SFX_WAIT_AND_PLAY)
    bool hud_overlay   = false;   // enhanced HUD bars + vector HUD text
    bool fluid_bubbles = false;   // secret-area modelled rising bubbles
    bool secret_slide  = false;   // secret enter/exit slide transition
    bool descent_pan   = false;   // L3 trunk-descent smooth pan
    bool hd_text       = false;   // vector (FreckleFace) text rendering

    bool any() const {
        return smooth_motion || cinematic_cue || hud_overlay ||
               fluid_bubbles || secret_slide || descent_pan || hd_text;
    }
    static EnhanceFlags all() {
        return EnhanceFlags{true, true, true, true, true, true, true};
    }
};

// Set one flag by its menu/config name in underscore form ("smooth_motion",
// as in the "enhance.smooth_motion" menu keys).  Returns false for unknown
// names (the caller decides whether that is an error).
inline bool set_enhance_flag(EnhanceFlags& f, const std::string& name,
                             bool on) {
    if (name == "smooth_motion") { f.smooth_motion = on; return true; }
    if (name == "cinematic_cue") { f.cinematic_cue = on; return true; }
    if (name == "hud_overlay")   { f.hud_overlay   = on; return true; }
    if (name == "fluid_bubbles") { f.fluid_bubbles = on; return true; }
    if (name == "secret_slide")  { f.secret_slide  = on; return true; }
    if (name == "descent_pan")   { f.descent_pan   = on; return true; }
    if (name == "hd_text")       { f.hd_text       = on; return true; }
    return false;
}

}  // namespace olduvai::presentation
