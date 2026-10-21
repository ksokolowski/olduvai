// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// compute_menu_layout against the shipped menus.json.

#include "doctest/doctest.h"

#include <map>
#include <string>

#include "presentation/input/button_layout.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/menu/menu_render.hpp"

using namespace olduvai::presentation;

namespace {
struct FakeBindings : MenuBindings {
    std::map<std::string, std::string> v;
    std::string get(const std::string& k) override {
        const auto it = v.find(k);
        return it == v.end() ? std::string{} : it->second;
    }
    void set(const std::string& k, const std::string& val) override { v[k] = val; }
};
}  // namespace

// Long button labels on both slots of every Gamepad row, for each pad family:
// the slab must widen so no label runs into its values and both slots stay
// inside the frame.  Found 2026-10-07: "Triangle - top" (the PlayStation
// family) did not fit, so its shapes go without the position.
TEST_CASE("menu layout: every pad family's Gamepad rows fit their slab") {
  for (const PadFamily fam : {PadFamily::kXbox, PadFamily::kNintendo, PadFamily::kPlayStation}) {
    CAPTURE(static_cast<int>(fam));
    MenuModel m = built_in_menu_model();
    label_pad_rows(m, fam);
    FakeBindings b;
    // The longest labels of each family: top and right (Triangle / Circle;
    // "Y - top" / "B - right" or "X - top" / "A - right").
    for (const PadSlot& s : kPadSlots) b.v[s.key] = "y,b";
    Menu menu(m, b);
    menu.open("controls_pad");
    const MenuLayout L = compute_menu_layout(menu, 320, 200);
    CHECK(L.slab_x >= 4);
    CHECK(L.slab_x + L.slab_w <= 316);
    const auto w = [](const std::string& t) { return 8 * static_cast<int>(t.size()); };
    bool long_label_seen = false;
    for (const MenuRow& r : menu.rows()) {
        if (!r.value) continue;
        CAPTURE(r.label);
        const int label_end = L.label_x + w(r.label);
        if (r.value2) {
            CHECK(label_end + 8 <= L.value_col - w(*r.value));
            CHECK(L.value_col + 8 <= L.value_right - w(*r.value2));
            long_label_seen = long_label_seen || *r.value == pad_button_label("y", fam);
        } else {
            CHECK(label_end + 8 <= L.value_right - w(*r.value));
        }
    }
    CHECK(long_label_seen);
  }
}
