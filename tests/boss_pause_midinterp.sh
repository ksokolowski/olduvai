#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Headless BOSS-arena Mid-Interpolation Pause compose regression gate — CTest.
#
# BACKLOG §3.4: the boss pause present used to re-draw the frozen fight at
# INTEGER positions while the live fight's last present was at the FLOAT
# position (a VSYNC tick ends at elapsed/frame_ms < 1.0 — fractional), so ESC
# could nudge the player by up to a sub-pixel times hd_scale (four HD pixels).
# The fix feeds the fight's {use_float_pos, fx, fy} triple into BOTH pause
# composes (native + wide) at the pause freeze, and this gate pins that the
# pause reproduces the fight's granularity.
#
# It is a SEPARATE gate from boss_pause_shot because the plain pause runs
# under the DISCRETE sub-frame fill, whose last sub-frame is alpha 1.0 and
# therefore converges — the float shadow equals the integer position and the
# pause would be "correctly inert" no matter what the compose reads.  That is
# the exact gap §3.4 called out: the pause path's smooth-motion behaviour was
# effectively ungated.  This gate force-opens the pause with
# OLDUVAI_BOSS_PAUSE_MIDINTERP=1, which injects a deterministic mid-tick float
# state (boss_use_float=true, fx/fy = player + 8 px) INTO the same memory the
# fight loop writes, so the pause compose CANNOT hide behind convergence.  The
# 8 px offset is a stress value, deliberately above the defect's sub-pixel
# range, so the player shift is not masked by the pause menu slab.
#
# Golden = hash, not image (content policy, CONTRIBUTING.md).  Determinism
# recipe is boss_pause_shot's own.  Regenerate after an intentional change:
# the boss_pause_shot.sh header command plus OLDUVAI_BOSS_PAUSE_MIDINTERP=1.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
GOLDEN="$(dirname "$0")/fixtures/boss_pause_midinterp_golden.sha256"
SHOT="$(mktemp -u /tmp/boss_pause_midinterp.XXXXXX).png"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_pause_midinterp: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_pause_midinterp: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

# sha256 <file> — portable (Linux sha256sum / macOS shasum).
sha256() {
    if command -v sha256sum >"${ERR}" 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs
. "$(dirname "$0")/lib/engine_err.sh"
engine_err_init
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_WS_FORCE_MARGIN=64 \
    OLDUVAI_BOSS_PAUSE_SHOT="${SHOT}" OLDUVAI_BOSS_PAUSE_MIDINTERP=1 timeout 90 \
    "${BINARY}" --play --level 2 --render-scale 2 --window 896x400 \
    --enhanced --hd-profile mmpx --aspect widescreen \
    --game-dir "${GAME_DIR}" >"${ERR}" 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${SHOT}" ]; then
    echo "boss_pause_midinterp: FAIL — no shot produced (boss pause not reached?)"
    engine_said "${ERR}"
    rm -f "${SHOT}"
    exit 1
fi

if [ "$(sha256 "${SHOT}")" = "$(cat "${GOLDEN}")" ]; then
    engine_err_clean "${ERR}"
    echo "boss_pause_midinterp: PASS"
    rm -f "${SHOT}"
    exit 0
fi

echo "boss_pause_midinterp: FAIL — mid-interpolation pause overlay differs from"
echo "  the golden hash.  shot=${SHOT}  golden=${GOLDEN}"
echo "  The frozen player must sit at the injected +8 px float offset, not at"
echo "  the integer position.  If the change is intentional, regenerate the"
echo "  hash (see header)."
exit 1