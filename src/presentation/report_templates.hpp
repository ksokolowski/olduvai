// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Per-tag description skeletons + the tag / reproducibility value lists for
// the F5 bug-report form.  Mirrors the reference implementation's dialog so
// the two engines prefill the same guidance.
#pragma once

#include <string>
#include <vector>

namespace olduvai::presentation {

inline const std::vector<std::string> kReportTags = {
    "collision", "visual", "gameplay", "entity", "audio", "crash", "other"};
inline const std::vector<std::string> kReportRepro = {
    "every", "sometimes", "once", "unknown"};

inline const std::string& report_template(const std::string& tag) {
    static const std::string collision =
        "Collided wrong at: ...\n"
        "Should have: hit / missed\n"
        "Repro: 1. ...";
    static const std::string visual =
        "Looks wrong: ...\n"
        "Where: sprite / HUD / palette\n"
        "DOS shows: ...";
    static const std::string gameplay =
        "What happened: ...\nExpected: ...\nRepro: 1. ...";
    static const std::string generic =
        "What happened: ...\nExpected: ...";
    if (tag == "collision") return collision;
    if (tag == "visual") return visual;
    if (tag == "gameplay") return gameplay;
    return generic;
}

// True if `desc` is still an unedited template (any tag's skeleton, or empty):
// while true, cycling the tag re-templates the description; once the user
// types, the field is theirs and tag changes leave it alone.
inline bool is_report_template(const std::string& desc) {
    if (desc.empty()) return true;
    for (const auto& t : kReportTags)
        if (desc == report_template(t)) return true;
    return false;
}

}  // namespace olduvai::presentation
