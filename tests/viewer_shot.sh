#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless asset-viewer regression gate — invoked by CTest.
#
# WHY THIS EXISTS.  `--viewer` is a shipped, user-reachable feature that NO
# test executed — src/presentation/viewer.cpp measured 0.0% coverage both with
# and without game assets, the only file in the tree with that distinction.
# It decodes and composes real game assets, so it exercises the sprite/palette
# path from a completely different direction than the gameplay gates do, and
# nothing was watching it.
#
# The hooks needed were already there and had simply never been wired to a
# test: --viewer-frames bounds the run and --viewer-shot dumps a frame.
#
# Golden = hash, not image: the content policy (CONTRIBUTING.md) forbids game
# imagery in the tree, and the shot contains decoded game artwork.  On failure
# the shot PNG is kept for eyeballing.
#
# Determinism — verified byte-identical across repeated runs before the golden
# was taken.  The viewer composes at a fixed size, so unlike mainmenu_shot the
# hash does not move with --window/--render-scale; both are pinned anyway so a
# future change to that assumption fails loudly here instead of silently
# rehashing.  XDG_CONFIG_HOME is isolated so the user's play.json cannot leak
# into the compose.
#
# Regenerate after an intentional viewer change (then update the .sha256):
#   SDL_VIDEODRIVER=dummy XDG_CONFIG_HOME=$(mktemp -d) \
#   ./build/release/olduvai --viewer --viewer-frames 3 --viewer-shot /tmp/v.png \
#       --render-scale 1 --window 640x400 --game-dir <game_dir>
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/viewer_golden.sha256"
SHOT="$(mktemp -u /tmp/viewer_shot.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "viewer_shot: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "viewer_shot: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" timeout 60 \
    "${BINARY}" --viewer --viewer-frames 3 --viewer-shot "${SHOT}" \
    --render-scale 1 --window 640x400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "viewer_shot: FAIL — no shot produced (viewer did not compose a frame)"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    echo "viewer_shot: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "viewer_shot: FAIL — rendered viewer frame differs from the golden hash."
echo "  shot=${SHOT}  golden=${GOLDEN}"
echo "  Eyeball the shot; if the viewer renders correctly and the change is"
echo "  intentional, regenerate the hash (see header)."
exit 1
