// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The no-neighbour widescreen margin rules, asserted by NAME.
//
// A level's first screen has no left neighbour to peek and its last has no
// right one, so those margins are invented — and what looks right there was
// decided by looking at the result, one screen at a time: open sky beside the
// L1 island, its lake continuing outward, a voided dirt band at L3's dead-end,
// pure black at the L7 cave hall's warp seams.
//
// Until this file those rules were asserted only by pixel goldens, which fail
// with "the hash changed" — they cannot say WHICH enhancement was lost, and a
// golden regenerated in good faith takes the rule with it.  Each case below
// names the rule, so losing one reads as "L1 end screen: sky_only" instead of
// "wide_l1_s18 differs".
#include "doctest/doctest.h"
#include "presentation/render/edge_margin_policy.hpp"

using olduvai::presentation::edge_margin_policy;
using olduvai::presentation::EdgeMarginPolicy;

namespace {

// The policy reads four fields; build a state with just those set.
olduvai::systems::SystemsState at(int level, int screen, int secret = 0) {
    olduvai::systems::SystemsState s{};
    s.current_level = level;
    s.current_screen = screen;
    s.secret_flag = secret;
    return s;
}

constexpr bool kFond = true, kNoFond = false;
constexpr bool kNeighbour = true, kEdge = false;

}  // namespace

TEST_CASE("edge margins: the L1 island end screen is open sky, with its lake") {
    // Screen 18 with no right neighbour: the margin beside the island must be
    // backdrop only, the lake must continue into it, and the tile extension
    // must NOT draw the island's ground and palm back over it.
    const EdgeMarginPolicy end =
        edge_margin_policy(at(1, 18), kFond, kFond, kNeighbour, kEdge);
    CHECK(end.sky_only);
    CHECK(end.water_continues);
    CHECK(end.skip_tile_extension);

    // The level-complete handler bumps the screen to kLastScreen + 1 the
    // instant it fires, and the end fade composes from THAT state — the rule
    // has to survive into the fade's first frame or the margin reverts to the
    // mirror fallback for one visible frame.
    const EdgeMarginPolicy fading =
        edge_margin_policy(at(1, 19), kFond, kFond, kNeighbour, kEdge);
    CHECK(fading.sky_only);
    CHECK(fading.water_continues);

    // The FIRST screen keeps the ground extension: it is the same level and
    // the same missing neighbour, and it must NOT get the island treatment.
    const EdgeMarginPolicy start =
        edge_margin_policy(at(1, 0), kFond, kFond, kEdge, kNeighbour);
    CHECK_FALSE(start.sky_only);
    CHECK_FALSE(start.water_continues);
    CHECK_FALSE(start.skip_tile_extension);
}

TEST_CASE("edge margins: L3's dead-end offers no ledge to nowhere") {
    // Screens 9 and 17, right edge: void the dirt band so the strip reads as
    // backdrop instead of a walkable ledge the player would be lured toward.
    for (const int screen : {9, 17}) {
        const EdgeMarginPolicy p =
            edge_margin_policy(at(3, screen), kNoFond, kNoFond, kNeighbour, kEdge);
        CHECK(p.void_ground_right);
    }
    // With a real neighbour on the right there is nothing to invent, so the
    // rule must not fire — it would void a band the peek is about to fill.
    const EdgeMarginPolicy peeked =
        edge_margin_policy(at(3, 9), kNoFond, kNoFond, kNeighbour, kNeighbour);
    CHECK_FALSE(peeked.void_ground_right);

    // A neighbouring screen on the same level is untouched by it.
    const EdgeMarginPolicy other =
        edge_margin_policy(at(3, 10), kNoFond, kNoFond, kNeighbour, kEdge);
    CHECK_FALSE(other.void_ground_right);
}

TEST_CASE("edge margins: the L7 cave hall stays a closed cave") {
    // Screens 10-12: black base AND no tile extension, which together are
    // what "pure black at the warp seams" means.
    for (const int screen : {10, 11, 12}) {
        const EdgeMarginPolicy p =
            edge_margin_policy(at(7, screen), kNoFond, kNoFond, kEdge, kEdge);
        CHECK(p.black_base);
        CHECK(p.skip_tile_extension);
    }
    // Screen 13 is past the hall: it keeps the layer extension.
    const EdgeMarginPolicy past =
        edge_margin_policy(at(7, 13), kNoFond, kNoFond, kEdge, kEdge);
    CHECK_FALSE(past.skip_tile_extension);
}

TEST_CASE("edge margins: backdrop and secret rooms pick the fill") {
    // A FOND level extends its backdrop; a tile level gets the black base and
    // rebuilds from authored patterns instead of mirroring edge objects.
    CHECK_FALSE(edge_margin_policy(at(1, 5), kFond, kFond, kEdge, kEdge).black_base);
    CHECK(edge_margin_policy(at(3, 5), kNoFond, kNoFond, kEdge, kEdge).black_base);

    // Secret rooms keep their deliberate self-tile: no black base, no
    // surface-style repeat.
    const EdgeMarginPolicy secret =
        edge_margin_policy(at(3, 5, /*secret=*/1), kNoFond, kNoFond, kEdge, kEdge);
    CHECK_FALSE(secret.black_base);
    CHECK_FALSE(secret.repeat_no_backdrop);

    // A surface screen does repeat.
    CHECK(edge_margin_policy(at(3, 5), kNoFond, kNoFond, kEdge, kEdge)
              .repeat_no_backdrop);
}

TEST_CASE("edge margins: a missing FOND buffer is not a tile level") {
    // The two backdrop inputs answer different questions, and the first
    // version of this policy conflated them: it derived the black base from
    // the FOND POINTER instead of the screen's own visual_background flag.
    // A screen that WANTS a backdrop but was handed no buffer (asset missing,
    // cache not built) would then have been painted black, where the original
    // fell back to the mirror.  Caught by reading ws_backdrop(), which returns
    // null for both reasons — not by any pixel golden, none of which covers a
    // missing asset.
    const EdgeMarginPolicy missing_asset =
        edge_margin_policy(at(1, 5), /*has_backdrop=*/false,
                           /*screen_uses_backdrop=*/true, kEdge, kEdge);
    CHECK_FALSE(missing_asset.black_base);

    // A genuine tile screen — no FOND buffer AND no FOND wanted — still gets it.
    const EdgeMarginPolicy tile_screen =
        edge_margin_policy(at(3, 5), /*has_backdrop=*/false,
                           /*screen_uses_backdrop=*/false, kEdge, kEdge);
    CHECK(tile_screen.black_base);
}
