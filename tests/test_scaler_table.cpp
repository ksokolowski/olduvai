// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The scaler table (enhance/upscale.cpp) is the one statement of what each HD
// profile runs at each factor.  Three things repeat it and must not drift: the
// Runs row and docs (describe_hd_scaler), the Options menu's names, and the
// table in docs/SETTINGS.md.  Each is held to the table here, and the table to
// what upscale_rgba really executes.
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/pixel_scalers.hpp"
#include "enhance/upscale.hpp"
#include "enhance/xbrz_scale.hpp"
#include "presentation/menu/menu_model.hpp"

using namespace olduvai::enhance;
using Pixels = std::vector<std::uint8_t>;

namespace {

// A 9x7 card with edges, a dot and a transparent corner: enough that every
// scaler's output differs from every other's.
Pixels card() {
    constexpr int w = 9, h = 7;
    Pixels px(w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y * w + x) * 4;
            const bool on = x + y > 7 || (x == 2 && y == 3);
            px[i] = on ? 240 : 20;
            px[i + 1] = on ? 190 : 30;
            px[i + 2] = on ? 60 : 110;
            px[i + 3] = (x == 0 && y == 0) ? 0 : 255;
        }
    return px;
}

// The scaler a description names, for the 2x (or fixed) forms it can be run as.
Pixels run_named(const std::string& name, const Pixels& px, int w, int h) {
    if (name == "Scale2x") return scale2x(px, w, h);
    if (name == "Scale3x") return scale3x(px, w, h);
    if (name == "Eagle") return eagle_2x(px, w, h);
    if (name == "MMPX") return mmpx_2x(px, w, h);
    FAIL("unknown scaler name in a description: " << name);
    return {};
}

}  // namespace

TEST_CASE("scaler table: what describe_hd_scaler says is what upscale_rgba runs") {
    const Pixels px = card();
    constexpr int w = 9, h = 7;
    const std::string two_pass = ", two passes";
    for (const std::string& profile : supported_hd_profiles()) {
        for (int s = 2; s <= 4; ++s) {
            CAPTURE(profile);
            CAPTURE(s);
            const std::string said = describe_hd_scaler(profile, s);
            const Pixels got = upscale_rgba(px, w, h, s, profile);
            REQUIRE(got.size() == std::size_t(w * s) * (h * s) * 4);
            if (said == "nearest") {
                CHECK(got == nearest_scale(px, w, h, s));
            } else if (said == "OmniScale") {
                CHECK(got == omniscale(px, w, h, s));
            } else if (said == "xBRZ") {
                CHECK(got == xbrz_scale(px, w, h, s));
            } else if (said.rfind("Scale3x (", 0) == 0) {          // no 3x form
                CHECK(s == 3);
                CHECK(got == scale3x(px, w, h));
            } else if (said.size() > two_pass.size() &&
                       said.compare(said.size() - two_pass.size(), two_pass.size(),
                                    two_pass) == 0) {
                CHECK(s == 4);
                const std::string base = said.substr(0, said.size() - two_pass.size());
                CHECK(got == run_named(base, run_named(base, px, w, h), w * 2, h * 2));
            } else {
                CHECK(got == run_named(said, px, w, h));
            }
        }
    }
}

TEST_CASE("scaler table: scale 1 is a no-op, other factors replicate, unknown throws") {
    const Pixels px = card();
    for (const std::string& profile : supported_hd_profiles()) {
        CAPTURE(profile);
        CHECK(describe_hd_scaler(profile, 1) == "none");
        CHECK(upscale_rgba(px, 9, 7, 1, profile) == px);
        CHECK(describe_hd_scaler(profile, 5) == "nearest");
        CHECK(upscale_rgba(px, 9, 7, 5, profile) == nearest_scale(px, 9, 7, 5));
    }
    CHECK_THROWS(upscale_rgba(px, 9, 7, 2, "bogus"));
    CHECK(describe_hd_scaler("bogus", 2) == "unsupported");
    CHECK(hd_profile_label("bogus").empty());
}

TEST_CASE("scaler table: the Options menu names the profiles as the table does") {
    const auto model = olduvai::presentation::built_in_menu_model();
    const auto& items = model.screens.at("video").items;
    const auto it = std::find_if(items.begin(), items.end(), [](const auto& m) {
        return m.key == "hd_profile";
    });
    REQUIRE(it != items.end());
    const auto& profiles = supported_hd_profiles();
    REQUIRE(it->values.size() == profiles.size());
    REQUIRE(it->value_labels.size() == profiles.size());
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        CAPTURE(profiles[i]);
        CHECK(it->values[i] == profiles[i]);
        CHECK(it->value_labels[i] == hd_profile_label(profiles[i]));
    }
}

TEST_CASE("scaler table: docs/SETTINGS.md lists what each profile runs, word for word") {
    std::ifstream in(OLDUVAI_TEST_SOURCE_DIR "/docs/SETTINGS.md");
    REQUIRE(in.good());
    std::map<std::string, std::vector<std::string>> rows;   // profile -> x2, x3, x4
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("| `", 0) != 0) continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        for (std::string c; std::getline(ss, c, '|');) {
            const auto b = c.find_first_not_of(' ');
            const auto e = c.find_last_not_of(' ');
            cells.push_back(b == std::string::npos ? "" : c.substr(b, e - b + 1));
        }
        // cells[0] is the empty text before the first bar
        if (cells.size() == 5 && cells[1].size() > 2 && cells[1].front() == '`' &&
            is_supported_hd_profile(cells[1].substr(1, cells[1].size() - 2)))
            rows[cells[1].substr(1, cells[1].size() - 2)] = {cells[2], cells[3], cells[4]};
    }
    for (const std::string& profile : supported_hd_profiles()) {
        CAPTURE(profile);
        if (profile == "native") continue;      // Off: the table has no row for no scaler
        REQUIRE(rows.count(profile) == 1);
        for (int s = 2; s <= 4; ++s) {
            CAPTURE(s);
            CHECK(rows[profile][std::size_t(s - 2)] == describe_hd_scaler(profile, s));
        }
    }
}
