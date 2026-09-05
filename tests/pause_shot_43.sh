#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Pause overlay under --aspect 4:3 — the empty §3.20 cell with the DIFFERENT
# logical HEIGHT.
#
# WHY THIS EXISTS.  4:3 is the only mode whose aspect_logical height is 240
# instead of 200, and every present, overlay flush and text-overlay restore is
# mapped through the logical size — a defect there is a whole-screen defect.
# The arithmetic was pinned by test_aspect (b4045d0); nothing RENDERED in the
# mode until this gate.
#
# THE MODE MUST PROVE ITSELF:
#   1. The PNG is exactly 640x480 — logical 320x240 at an exact 2x integer
#     scale (--window 640x480), so no partial-scaling ambiguity.
#   2. The hash matches the golden.
#   3. A CONTROL run at the same window with --aspect keep must NOT match the
#      shot.  This replaces an earlier claim that "three distinct hashes across
#      the three pause gates" proved each cell rendered its own mode: nothing
#      compared them, and the three gates use different windows, so their
#      hashes differ by dimensions alone and proved nothing about the aspect
#      flag.  The sibling stretch gate was in fact passing while rendering
#      keep-mode output; this is the assertion that catches that shape.
#
# Determinism: verified byte-identical over two consecutive runs.
#
# Regenerate after an intentional change:
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_PAUSE_SHOT=/tmp/ps43.png ./build/release/olduvai --play --level 1 \
#       --render-scale 1 --aspect 4:3 --window 640x480 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/pause_43_golden.sha256"
SHOT="$(mktemp -u /tmp/pause_43.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "pause_shot_43: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "pause_shot_43: SKIP — binary not found"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

png_size() {
    python3 -c "
import struct,sys
d=open(sys.argv[1],'rb').read(33)
print('%d %d' % struct.unpack('>II', d[16:24]))" "$1"
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_PAUSE_SHOT="${SHOT}" timeout 60 \
    "${BINARY}" --play --level 1 --render-scale 1 --aspect 4:3 \
    --window 640x480 --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "pause_shot_43: FAIL — no shot produced (pause overlay not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

SIZE="$(png_size "${SHOT}")"
if [ "${SIZE}" != "640 480" ]; then
    echo "pause_shot_43: FAIL — expected a 640x480 shot (logical 320x240 @ 2x), got ${SIZE}."
    rm -f "${SHOT}"
    exit 1
fi

# The control: same window, --aspect keep (which letterboxes 320x200 into
# 640x480 instead of filling it).  If this matches, the aspect flag changed
# nothing and the golden below is pinning the wrong mode.
CTRL="$(mktemp -u /tmp/pause_43_ctrl.XXXXXX).png"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_PAUSE_SHOT="${CTRL}" timeout 60 \
    "${BINARY}" --play --level 1 --render-scale 1 --aspect keep \
    --window 640x480 --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"
if [ -s "${CTRL}" ] && [ "$(sha256 "${CTRL}")" = "$(sha256 "${SHOT}")" ]; then
    echo "pause_shot_43: FAIL — --aspect 4:3 renders identically to --aspect"
    echo "  keep at this window, so the gate proves nothing."
    rm -f "${SHOT}" "${CTRL}"
    exit 1
fi
rm -f "${CTRL}"

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "pause_shot_43: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "pause_shot_43: FAIL — rendered pause differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
exit 1
