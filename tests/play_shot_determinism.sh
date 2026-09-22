#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Enhanced --play-shot REPRODUCIBILITY gate (BACKLOG §6).
#
# An enhanced --play-shot used to be irreproducible run to run: the GET READY /
# NOT ENOUGH FOOD vector banners animated off SDL_GetTicks(), so a captured
# frame held whatever wall-clock offset the run happened to reach.  That
# silently invalidated every before/after byte comparison — the natural way to
# prove a render refactor changed nothing.  The banner now takes its clock from
# the logic tick (state.frame_counter x 1000/18 ms) in the headless capture
# modes, matching the classic/frame-counted vs smooth/wall-clock split of
# PARITY_CHECKLIST T8.  (The old note fingered the fluid bubbles and the HD
# warm thread; both were measured innocent.)
#
# This gate asserts BOTH halves of the fix:
#   1. two runs of the SAME frame are byte-identical (the regression), and
#   2. two DIFFERENT frames differ — the banner still animates, so the fix is
#      not a silent "freeze the banner to a constant".
# L1 s18 carries the persistent NOT ENOUGH FOOD banner (colour + vertical bob
# move every tick), so (2) needs no GET READY window.
#
# --hd-profile mmpx (INTEGER upscaler, bit-stable across relinks); M=64 +
# --window pin the output size on any host/driver; XDG to a fresh temp dir so
# the user's play.json cannot leak settings in; audio muted.  Hash-free, so
# there is no golden to regenerate.
#
# Skip (77) without game data or the binary.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "play_shot_determinism: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "play_shot_determinism: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"   # mute test runs
. "$(dirname "$0")/lib/engine_err.sh"
engine_err_init

# shot <out> <frame>
shot() {
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_WS_FORCE_MARGIN=64 timeout 60 \
        "${BINARY}" --play --level 1 --start-screen 18 \
        --enhanced --hd-profile mmpx --aspect widescreen \
        --render-scale 2 --window 896x400 \
        --game-dir "${GAME_DIR}" \
        --play-shot "$1" --play-shot-frame "$2" >"${ERR}" 2>&1
    rc=$?
    rm -rf "${CFG_DIR}"
    return ${rc}
}

A="$(mktemp -u /tmp/psd_a.XXXXXX).png"
B="$(mktemp -u /tmp/psd_b.XXXXXX).png"
C="$(mktemp -u /tmp/psd_c.XXXXXX).png"

shot "${A}" 30
shot "${B}" 30
shot "${C}" 60

for f in "${A}" "${B}" "${C}"; do
    if [ ! -s "${f}" ]; then
        echo "play_shot_determinism: FAIL — no shot produced (${f})"
        engine_said "${ERR}"
        rm -f "${A}" "${B}" "${C}"
        exit 1
    fi
done

if ! cmp -s "${A}" "${B}"; then
    echo "play_shot_determinism: FAIL — two runs of frame 30 differ."
    echo "  The enhanced capture is non-reproducible again.  A wall-clock"
    echo "  animated element (the banner clock is the known one) is back."
    rm -f "${A}" "${B}" "${C}"
    exit 1
fi

if cmp -s "${A}" "${C}"; then
    echo "play_shot_determinism: FAIL — frame 30 and frame 60 are identical."
    echo "  The banner clock is frozen rather than tick-driven; the GET READY /"
    echo "  NOT ENOUGH FOOD effects no longer animate."
    rm -f "${A}" "${B}" "${C}"
    exit 1
fi

engine_err_clean "${ERR}"
echo "play_shot_determinism: PASS"
rm -f "${A}" "${B}" "${C}"
exit 0
