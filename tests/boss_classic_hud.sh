#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The CLASSIC (non-HD) boss HUD — the lives digits, and that they track lives.
#
# WHY THIS EXISTS (BACKLOG §3.19).  Every other boss gate passes `--enhanced`:
# boss_widescreen, boss_golden_trace, boss_l4_victory, boss_pause_shot,
# boss_replay_record.  Nothing exercised the bitmap HUD at all, and §3.18 proved
# it the expensive way — moving the classic lives draw onto BossHud needed a
# set_classic_font call the driver did not make, so classic mode drew NO lives
# digits.  It built clean, all 45 tests stayed green, and it shipped in the
# working tree until a grep for the setter found it.
#
# WHAT IT PINS, and why it is not a hash.  A golden hash would pin the whole
# frame, so any legitimate change to the arena art or the fight's first 60
# frames would fail it for the wrong reason.  This asserts the BEHAVIOUR
# instead: run the same fight twice with a different LIVES COUNT (--god seeds
# 99 on a boss entry; the default is 3) and require that
#
#   1. the two frames DIFFER — if the HUD is not drawn, both are identical and
#      the §3.18 bug reappears as a failure here, which is the whole point; and
#   2. every differing pixel lies inside the lives-digit field.  Nothing else
#      about the fight may move: --god deliberately leaves boss damage and
#      energy alone, so the digits are the only legitimate difference.
#
# THE CONFIG TRAP, which cost a wrong verification claim before this was
# written.  `--play` with no isolated XDG_CONFIG_HOME reads the DEVELOPER'S
# ~/.config/olduvai/play.json, and a normal player config has `enhanced: true`
# with a render_scale.  A run meant to be classic then silently takes the HD
# path — the shot comes back scaled (1280x800 at render_scale 4) instead of
# 320x200, and a "classic" A/B compares two enhanced frames while reporting on
# classic.  Hence the isolated CFG_DIR below AND the 320x200 assertion: if the
# config leaks or a default changes, this fails loudly instead of testing the
# wrong path quietly.
#
# Reads the BMP with stdlib struct, not Pillow, so it needs only python3.
#
# Skip (77) when game data, the binary, or python3 is absent.

SKIP=77
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_classic_hud: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_classic_hud: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "boss_classic_hud: SKIP — python3 not available"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
OUT_DIR="$(mktemp -d /tmp/boss_classic_hud.XXXXXX)"

# One classic boss frame.  $1 = output path, $2 = extra flag (may be empty).
# NO --enhanced: this is the whole point.  The config dir is isolated per run.
capture() {
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086  # $2 is deliberately word-split: it is either
    # empty (pass no extra flag) or --god. Quoting it would pass an empty
    # argument to the binary in the no-flag case.
    XDG_CONFIG_HOME="${CFG_DIR}" timeout 90 \
        "${BINARY}" --play --level 2 \
        --play-shot "$1" --play-shot-frame 60 \
        --game-dir "${GAME_DIR}" $2 >/dev/null 2>&1
    rm -rf "${CFG_DIR}"
}

capture "${OUT_DIR}/lives3.bmp" ""
capture "${OUT_DIR}/lives99.bmp" "--god"

for f in lives3 lives99; do
    if [ ! -s "${OUT_DIR}/${f}.bmp" ]; then
        echo "boss_classic_hud: FAIL — no ${f} shot produced (boss not reached?)"
        rm -rf "${OUT_DIR}"
        exit 1
    fi
done

RESULT=$(python3 - "${OUT_DIR}/lives3.bmp" "${OUT_DIR}/lives99.bmp" <<'PY'
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bpp = struct.unpack('<H', d[28:30])[0]
    return w, h, bpp, d[off:]

w1, h1, bpp, a = load(sys.argv[1])
w2, h2, bpp2, b = load(sys.argv[2])

if (w1, abs(h1)) != (w2, abs(h2)) or bpp != bpp2:
    print("FAIL shots disagree on geometry: %dx%d/%d vs %dx%d/%d"
          % (w1, abs(h1), bpp, w2, abs(h2), bpp2))
    raise SystemExit

# The classic boss shot is the raw 320x200 arena framebuffer.  Anything else
# means the run was NOT classic — almost certainly a leaked play.json with
# enhanced:true, which is exactly how a previous "classic" check tested the HD
# path by mistake.
if (w1, abs(h1)) != (320, 200):
    print("FAIL expected a 320x200 classic shot, got %dx%d — the run was not "
          "classic (leaked config? changed default profile?)" % (w1, abs(h1)))
    raise SystemExit

bypp = bpp // 8
row = w1 * bypp
diffs = []
for y in range(abs(h1)):
    sy = (abs(h1) - 1 - y) if h1 > 0 else y   # BMP rows are bottom-up when h>0
    base = sy * row
    for x in range(w1):
        i = base + x * bypp
        if a[i:i+3] != b[i:i+3]:
            diffs.append((x, y))

if not diffs:
    print("FAIL the two frames are identical — the classic lives digits are "
          "not being drawn at all (the BACKLOG 3.19 / 3.18 regression)")
    raise SystemExit

# draw_text puts the 2-digit field at x=48, baseline y=8, with ~6px glyphs.
# Allow a generous box so a font-metric tweak does not fail this, while still
# refusing a difference anywhere else in the arena.
X0, X1, Y0, Y1 = 44, 72, 0, 10
stray = [p for p in diffs if not (X0 <= p[0] <= X1 and Y0 <= p[1] <= Y1)]
if stray:
    xs = [p[0] for p in stray]; ys = [p[1] for p in stray]
    print("FAIL %d of %d differing pixels are OUTSIDE the lives field "
          "(x %d-%d, y %d-%d) — --god changed more than the digits"
          % (len(stray), len(diffs), min(xs), max(xs), min(ys), max(ys)))
    raise SystemExit

print("OK %d pixels differ, all inside the lives-digit field" % len(diffs))
PY
)

rm -rf "${OUT_DIR}"

case "${RESULT}" in
    OK*) echo "boss_classic_hud: ${RESULT}"; exit 0 ;;
    *)   echo "boss_classic_hud: ${RESULT}"; exit 1 ;;
esac
