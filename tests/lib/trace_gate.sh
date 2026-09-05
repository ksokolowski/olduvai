#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Shared driver for every golden_trace_* replay gate.
#
# WHY THIS EXISTS.  Twelve gate scripts were written in one week by copying the
# previous one and substituting names — and the substitution went wrong twice:
# two headers claimed screen crossings their scenario never made, and a copied
# `--play-frames 400` truncated a 1015-frame golden while the SHORTER gate
# stayed green because its scenario fit inside the bug.  The per-scenario
# scripts keep their headers (that documentation is the point of them); the
# LOGIC lives here, once.
#
#   trace_gate.sh <name> <level> <start-screen|-> <input> <golden> <frames> \
#                 <timeout> <game_dir> <binary>
#
# <input>/<golden> are basenames under tests/fixtures/.  Exit 77 = SKIP (no
# game data / no binary), 1 = divergence or missing fixture, 0 = every frame
# matches the golden.
#
# TRACE_GATE_FLAGS (env, word-split deliberately) carries extra CLI flags
# through to the binary — e.g. the enhanced/mmpx stack golden_trace_l6_slam_hd
# needs.  Keep it for DISPLAY/RENDER flags only: level, replay, trace, frames
# and game-dir are this driver's own parameters.

NAME="$1"; LEVEL="$2"; SS="$3"; INPUT="$4"; GOLDEN_NAME="$5"
FRAMES="$6"; TMO="$7"; GAME_DIR="$8"; BINARY="$9"
FIX="$(dirname "$0")/../fixtures"
SCRIPT="${FIX}/${INPUT}"
GOLDEN="${FIX}/${GOLDEN_NAME}"
SKIP=77

[ "${SS}" = "-" ] && SS="" || SS="--start-screen ${SS}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "${NAME}: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "${NAME}: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi
if [ ! -f "${GOLDEN}" ] || [ ! -f "${SCRIPT}" ]; then
    echo "${NAME}: FAIL — fixture missing (${GOLDEN} / ${SCRIPT})"
    exit 1
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
ACTUAL="$(mktemp "/tmp/${NAME}.XXXXXX")"

# shellcheck disable=SC2086  # SS is deliberately word-split
# shellcheck disable=SC2086  # TRACE_GATE_FLAGS likewise
XDG_CONFIG_HOME="${CFG_DIR}" timeout "${TMO}" "${BINARY}" --play \
    --level "${LEVEL}" ${SS} ${TRACE_GATE_FLAGS-} \
    --replay "${SCRIPT}" --trace "${ACTUAL}" --play-frames "${FRAMES}" \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG_DIR}"

if [ ! -s "${ACTUAL}" ]; then
    echo "${NAME}: FAIL — no trace produced (scenario not reached?)"
    rm -f "${ACTUAL}"
    exit 1
fi

if diff -u --strip-trailing-cr "${GOLDEN}" "${ACTUAL}" \
        > "/tmp/${NAME}_diff.txt" 2>&1; then
    echo "${NAME}: OK — $(wc -l < "${ACTUAL}" | tr -d ' ') frames match"
    rm -f "${ACTUAL}" "/tmp/${NAME}_diff.txt"
    exit 0
fi

echo "${NAME}: FAIL — the trace diverged from the fixture."
echo "  diff: /tmp/${NAME}_diff.txt   (first lines below)"
head -20 "/tmp/${NAME}_diff.txt"
rm -f "${ACTUAL}"
exit 1
