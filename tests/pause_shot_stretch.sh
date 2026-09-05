#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Pause overlay under --aspect stretch — the other empty §3.20 cell.
#
# WHY THIS EXISTS.  stretch is the ONLY mode that disables SDL logical scaling
# entirely (aspect_logical returns {0,0}), so every present is a raw 1:1
# framebuffer upload.  A defect there (e.g. someone "simplifying" the {0,0}
# sentinel into a default logical size) changes the whole screen and nothing
# else in the tree would see it.
#
# THE WINDOW IS THE WHOLE TEST, and the first version got it wrong.  It ran at
# --window 320x200, where the window IS the native size — so `stretch` (scaling
# disabled, fill the window) and `keep` (logical 320x200 at 1x) produce BYTE-
# IDENTICAL output.  Measured 2026-08-24: both, and the committed golden, hashed
# 624c5b84...  The gate passed while proving nothing; it would have passed with
# --aspect stretch ignored entirely.  The 320x200 dimension assertion was inert
# for the same reason — the PNG is window-sized whatever the mode does.
#
# 800x400 (2:1) separates all three modes, because none of them fits it:
#   keep    -> logical 320x200 (8:5), pillarboxed to 640x400
#   4:3     -> logical 320x240 (4:3), pillarboxed narrower
#   stretch -> no logical size, fills 800x400
#
# THE MODE MUST PROVE ITSELF, and now it does so IN THIS SCRIPT rather than by
# an argument about other files:
#   1. The PNG is exactly 800x400.
#   2. The hash matches the golden.
#   3. A CONTROL run at the same window with --aspect keep must NOT match the
#      shot.  That is the assertion the old "three distinct hashes across the
#      three pause gates" prose only claimed — nothing compared them, and the
#      three gates use different windows anyway, so distinctness was guaranteed
#      by dimensions alone and proved nothing about the aspect flag.
#
# Determinism: verified byte-identical over two consecutive runs.
#
# Regenerate after an intentional change:
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   OLDUVAI_PAUSE_SHOT=/tmp/psstr.png ./build/release/olduvai --play --level 1 \
#       --render-scale 1 --aspect stretch --window 800x400 --game-dir <game_dir>
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/pause_stretch_golden.sha256"
SHOT="$(mktemp -u /tmp/pause_str.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "pause_shot_stretch: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "pause_shot_stretch: SKIP — binary not found"
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
    "${BINARY}" --play --level 1 --render-scale 1 --aspect stretch \
    --window 800x400 --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "pause_shot_stretch: FAIL — no shot produced (pause overlay not reached?)"
    rm -f "${SHOT}"
    exit 1
fi

SIZE="$(png_size "${SHOT}")"
if [ "${SIZE}" != "800 400" ]; then
    echo "pause_shot_stretch: FAIL — expected an 800x400 shot, got ${SIZE}."
    rm -f "${SHOT}"
    exit 1
fi

# The control: same window, --aspect keep.  If this matches, the aspect flag
# changed nothing and the golden below is pinning the wrong mode.
CTRL="$(mktemp -u /tmp/pause_stretch_ctrl.XXXXXX).png"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_PAUSE_SHOT="${CTRL}" timeout 60 \
    "${BINARY}" --play --level 1 --render-scale 1 --aspect keep \
    --window 800x400 --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"
if [ -s "${CTRL}" ] && [ "$(sha256 "${CTRL}")" = "$(sha256 "${SHOT}")" ]; then
    echo "pause_shot_stretch: FAIL — --aspect stretch renders identically to"
    echo "  --aspect keep at this window, so the gate proves nothing."
    rm -f "${SHOT}" "${CTRL}"
    exit 1
fi
rm -f "${CTRL}"

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "pause_shot_stretch: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "pause_shot_stretch: FAIL — rendered pause differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
exit 1
