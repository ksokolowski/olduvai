#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The level-EDGE widescreen margins actually render their enhancement.
#
# WHY THIS EXISTS.  A level's first screen has no left neighbour and its last
# has no right one, so those margins are invented, and three screens get their
# own hand-tuned answer: open sky + the continuing lake beside the L1 island,
# a voided dirt band at L3's dead-end so the margin offers no walkable ledge to
# nowhere, and pure black at the L7 cave hall's warp seams.  On 2026-09-21 a
# check found that NO gate covered any of them: `cave_exit_margins` pins L3
# screen 4's cave exit and `wide_transition` pins L1's pan, and neither ever
# composes a level edge.  The rules lived in the code and in the owner's
# memory, which is exactly how an enhancement gets refactored away and nobody
# notices until a playtest.
#
# PROPERTIES, NOT HASHES — the same choice cave_exit_margins made.  An
# enhanced widescreen --play-shot is NOT reproducible byte-for-byte run to run
# (measured: two runs of one binary differ), so a pinned hash would flake.
# What the enhancements mean is expressible directly:
#
#   L1 screen 18, right margin  — not black (sky + mountains), and its BOTTOM
#                                 band is blue-dominant: the lake continues.
#   L3 screen 9,  right margin  — the bottom 24 rows (kWideDeadendVoidRows,
#                                 the dirt band) are BLACK while the band
#                                 above still shows terrain: backdrop, no
#                                 walkable ledge to nowhere.
#   L7 screens 10 / 12          — the hall's OUTER seams are black: S10's
#                                 LEFT (the S9 cave-descent warp) and S12's
#                                 RIGHT (the S13 teleport warp).
#
# Two traps this gate was written around, both of which produced a confident
# wrong answer first:
#
#   * SCREEN 11 IS NOT A TEST.  The cave hall's middle screen has neighbours
#     on both sides, so nothing is invented there and its margins are ordinary
#     peeks.  Only the outer seams exercise the rule.
#   * INTERNAL LEVEL 3 IS `--level 5`.  The display and internal level numbers
#     swap slots 3 and 5 (core/constants.hpp), and the policy keys off the
#     INTERNAL one.  `--level 3` photographs a different level entirely and
#     its bright margin reads as "the enhancement is gone".
#
# WHICH CHECKS HAVE TEETH — measured by MUTATION, not assumed.  Each rule was
# disabled in edge_margin_policy.hpp and the gate re-run against the rebuilt
# engine (2026-09-21).  The result is uncomfortable and is recorded rather
# than hidden, because a green check that cannot fail is worse than no check:
#
#   DISCRIMINATING
#     * "the lake continues"       — fails the moment water_continues is off.
#     * "the dirt band is voided"  — fails when the black base AND the void
#                                    rule are both off.  With only the void
#                                    rule off it still passes: on a TILE level
#                                    the black base already suppresses the
#                                    mirror, so void_ground_right is
#                                    belt-and-braces there and this check
#                                    guards the PAIR, not the void rule alone.
#   SANITY ONLY (kept, but they cannot catch a lost rule)
#     * "the right margin is sky, not black" — any fill is non-black.
#     * "the band above still shows terrain" — true of every fill.
#     * both L7 seam checks — the hall's own edge columns are black, so a
#       mirror of them is black too; the margins stay black with every policy
#       rule disabled.  They would catch a margin that started showing
#       CONTENT, which is the regression class for a cave, so they earn their
#       place — but they do not protect `skip_tile_extension`.
#
#   NO CHECK DISCRIMINATES `skip_tile_extension`, AND NONE CAN (measured
#   2026-09-21): a skip-only mutation — the rule dropped, every other rule
#   intact, rebuilt and the whole gate re-run — left ALL checks green,
#   including the L1 pair.  The reason is mechanical (bg_compose.cpp
#   fill_no_neighbour_margin, gated on the rule): at BOTH of the rule's own
#   sites (L1 end, L7 cave-hall seams) the authored tiles never cross the
#   screen edge — L1's island is fully inset so the extension draws nothing
#   over the backdrop sky, and the hall's warp seams are black with their
#   row-continuation suppressed on its own — so the margins are byte-identical
#   with the rule on and off.  There is no "screen where the tile extension
#   WOULD draw a pattern" among the rules' sites; the rule is protected by
#   NAME in tests/test_edge_margin_policy.cpp, and this gate cannot add pixel
#   teeth that the geometry does not offer.  Do not spend another mutation on
#   this: the answer is "no such check exists", and it is recorded here.
#
# Anyone extending this gate: run the mutation before believing a new check.
#
# Each check names the rule it protects, so a failure says which enhancement
# was lost rather than "the picture changed".
set -u

SKIP=77
# Same argument shape as the other margin gates: game data first, binary
# second, both overridable by env.
GAME_DIR="${OLDUVAI_GAME_DATA:-${GAME_DIR:-${1:-$(dirname "$0")/../game_data}}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -x "${BINARY}" ]; then
    echo "level_edge_margins: SKIP — no binary at ${BINARY}"
    exit ${SKIP}
fi
if [ -z "${GAME_DIR}" ] || [ ! -d "${GAME_DIR}" ]; then
    echo "level_edge_margins: SKIP — no GAME_DIR"
    exit ${SKIP}
fi
if ! python3 -c "import numpy, PIL" 2>/dev/null; then
    echo "level_edge_margins: SKIP — numpy/Pillow not available"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
OUT_DIR="$(mktemp -d /tmp/level_edge_margins.XXXXXX)"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
MARGIN=64
# The window is pinned to the COMPOSITE's aspect — (320 + 2*64) : 200 — so the
# shot is the wide composite with no letterbox, and the margin is a known
# fraction of the width whatever backing scale the display applies (this Mac
# returns 1792x800 for an 896x400 window).  Without this the shot is whatever
# the window happened to be: an earlier version of this gate measured a
# 640x400 image that was not a composite at all, and three of its five checks
# passed anyway — on the level's own art.  ASSERT THE SHAPE, NOT THE FLAGS.
WINDOW=896x400
FAIL=0

shot() {   # $1 level  $2 screen  $3 output name
    # OLDUVAI_FIRSTRUN_PRESET is not optional with an isolated config: without
    # a play.json the engine asks Classic-or-Enhanced through a MODAL dialog
    # and the run waits forever (cost seven hours on 2026-09-21).
    # The run's output (both streams) is KEPT alongside the shot (as $3.log),
    # like engine_err.sh's kept output — the L3 block asserts on it.
    XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_FIRSTRUN_PRESET=enhanced \
        OLDUVAI_WS_FORCE_MARGIN="${MARGIN}" timeout 120 \
        "${BINARY}" --play --level "$1" --start-screen "$2" --enhanced \
        --aspect widescreen --window "${WINDOW}" \
        --play-shot "${OUT_DIR}/$3" --play-shot-frame 3 \
        --game-dir "${GAME_DIR}" >"${OUT_DIR}/$3.log" 2>&1 </dev/null
    [ -s "${OUT_DIR}/$3" ]
}

check() {   # $1 shot  $2 rule-name  $3 python expression over margins
    RESULT=$(python3 - "${OUT_DIR}/$1" "${MARGIN}" "$3" <<'PY'
import sys
import numpy as np
from PIL import Image
path, margin, rule = sys.argv[1], int(sys.argv[2]), sys.argv[3]
im = np.asarray(Image.open(path).convert("RGB")).astype(int)
h, w, _ = im.shape
# The image must BE the composite: same aspect as (320 + 2*margin) : 200.
want = (320 + 2 * margin) / 200.0
if abs(w / h - want) > 0.01:
    print(f"FAIL not a wide composite: {w}x{h}, aspect {w/h:.3f} != {want:.3f}")
    raise SystemExit
m = round(margin / (320 + 2 * margin) * w)        # native margin -> output px
left, right = im[:, :m, :], im[:, w - m:, :]
# Bands in NATIVE rows (the shot is scaled by the display's backing factor),
# so they line up with the constants the engine uses: the dead-end void is
# the bottom kWideDeadendVoidRows = 24 rows, and the lake sits in the same
# band.  `above` is the terrain strip over it.
sc = h / 200.0
rows = lambda a, lo, hi: a[int(lo * sc):int(hi * sc), :, :]
bottom = lambda a: rows(a, 176, 200)
above = lambda a: rows(a, 140, 170)
blue_dominant = lambda a: (a[:, :, 2].mean() > a[:, :, 0].mean() + 20 and
                           a[:, :, 2].mean() > a[:, :, 1].mean() + 20)
black = lambda a: a.mean() < 12.0   # 7 on a real void band, 150+ without
print("OK" if eval(rule) else "FAIL")
PY
)
    if [ "${RESULT}" = "OK" ]; then
        echo "level_edge_margins: OK — $2"
    else
        echo "level_edge_margins: FAIL — $2"
        FAIL=1
    fi
}

# ── L1 island end screen: open sky, and the lake continues ──────────────────
if shot 1 18 l1_end.png; then
    check l1_end.png "L1 end screen: the right margin is sky, not black (sanity)" \
        "not black(right)"
    check l1_end.png "L1 end screen: the lake continues into the right margin" \
        "blue_dominant(bottom(right))"
else
    echo "level_edge_margins: FAIL — no shot for L1 screen 18"; FAIL=1
fi

# ── L3 dead-end: the dirt band is voided, the backdrop above it is not ──────
# `--level 5` IS internal level 3 (the display/internal slot swap).  The log
# assertion is §6's "one assertion that reports the resolved internal level":
# without it a capture of the WRONG level (the trap above) passes the pixel
# checks — it is, after all, some level's real margins.
if shot 5 9 l3_deadend.png; then
    if ! grep -qF "game: level 5 (internal 3)" "${OUT_DIR}/l3_deadend.png.log"; then
        echo "level_edge_margins: FAIL — --level 5 did not resolve to internal 3"
        echo "  the run said:"
        tail -6 "${OUT_DIR}/l3_deadend.png.log" 2>/dev/null | sed 's/^/    /'
        FAIL=1
    else
        echo "level_edge_margins: OK — --level 5 resolves to internal 3"
    fi
    check l3_deadend.png "L3 dead-end: the right margin's dirt band is voided" \
        "black(bottom(right))"
    check l3_deadend.png "L3 dead-end: the band above it still shows terrain (sanity)" \
        "not black(above(right))"
else
    echo "level_edge_margins: FAIL — no shot for L3 screen 9"; FAIL=1
fi

# ── L7 cave hall: the OUTER warp seams stay a closed cave ───────────────────
if shot 7 10 l7_hall_left.png; then
    check l7_hall_left.png \
        "L7 cave hall: S10's left seam (the cave-descent warp) is black" \
        "black(left)"
else
    echo "level_edge_margins: FAIL — no shot for L7 screen 10"; FAIL=1
fi
if shot 7 12 l7_hall_right.png; then
    check l7_hall_right.png \
        "L7 cave hall: S12's right seam (the teleport warp) is black" \
        "black(right)"
else
    echo "level_edge_margins: FAIL — no shot for L7 screen 12"; FAIL=1
fi

rm -rf "${CFG_DIR}"
if [ ${FAIL} -eq 0 ]; then
    rm -rf "${OUT_DIR}"
    exit 0
fi
echo "  shots kept: ${OUT_DIR}"
exit 1
