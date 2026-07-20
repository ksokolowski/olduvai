#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Wide-transition present regression gate (CC2d) — invoked by CTest.
#
# Replays two reference-corpus input scripts under forced widescreen and
# hashes every frame the wide-transition present produces via
# OLDUVAI_WIDE_TRANSITION_DUMP (the wide HD buffer after the HUD-bar
# splice, before texture upload — the CPU-side output of
# present_wide_transition).  Gates the CC2d extraction of that present
# into WidescreenPresenter, which no other test exercises end-to-end.
#
# Scenarios:
#   ws_pan_walk  — l1 rightward walk/fight → kind-1 panorama pans
#   ws_cave_sign — l1 cave-2 entry → kind-2 fade pair
#
# Determinism (host-independent by construction):
#  - --hd-profile mmpx: INTEGER upscaler. NEVER switch this to omniscale —
#    its float codegen is not bit-stable across LTO relinks.
#  - --window 896x400 + OLDUVAI_WS_FORCE_MARGIN=64 pin the margin and the
#    renderer output size on any host/driver (native_w 448 x scale 2).
#  - XDG_CONFIG_HOME → fresh temp dir (user play.json cannot leak).
#  - --transitions classic forces smooth-motion off → deterministic frame
#    counts (bare --replay now KEEPS smooth for demo playback, so a scenario
#    that pins goldens must request classic explicitly).
#  - Audio muted (dummy driver) — no dev-machine noise, no device deps.
#
# Goldens = hash lists (sha256sum format), not images (content policy).
# Regenerate after an intentional presentation change:
#   run a scenario with OLDUVAI_WIDE_TRANSITION_DUMP=<dir>, eyeball frames,
#   then (cd <dir> && sha256sum *.png) > tests/fixtures/<scenario>.sha256
#
# Skip (77) when game data or the binary is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
FIX="$(dirname "$0")/fixtures"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "wide_transition: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "wide_transition: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

# sha256 <file> — portable (Linux sha256sum / macOS shasum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
FAIL=0

# run_scenario <name> <play-frames>
run_scenario() {
    GOLDEN="${FIX}/$1.sha256"
    OUT_DIR="$(mktemp -d /tmp/wide_transition.XXXXXX)"
    CFG_DIR="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG_DIR}" OLDUVAI_WS_FORCE_MARGIN=64 \
        OLDUVAI_WIDE_TRANSITION_DUMP="${OUT_DIR}" timeout 120 \
        "${BINARY}" --play --level 1 --enhanced --hd-profile mmpx \
        --render-scale 2 --aspect widescreen --window 896x400 \
        --transitions classic \
        --replay "${FIX}/$1.jsonl" --play-frames "$2" \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1
    rm -rf "${CFG_DIR}"
    SCEN_FAIL=0
    while read -r WANT NAME; do
        [ -n "${NAME}" ] || continue
        if [ ! -s "${OUT_DIR}/${NAME}" ]; then
            echo "wide_transition[$1]: FAIL — frame ${NAME} not produced"
            SCEN_FAIL=1
        elif [ "$(sha256 "${OUT_DIR}/${NAME}")" != "${WANT}" ]; then
            echo "wide_transition[$1]: FAIL — ${NAME} differs from golden hash"
            echo "  shot=${OUT_DIR}/${NAME}  golden=${GOLDEN}"
            SCEN_FAIL=1
        fi
    done < "${GOLDEN}"
    # Extra frames = the present ran MORE times than the golden run — also a
    # behaviour change; count them.
    GOT=$(ls "${OUT_DIR}" 2>/dev/null | wc -l)
    WANTN=$(wc -l < "${GOLDEN}")
    if [ "${GOT}" -ne "${WANTN}" ]; then
        echo "wide_transition[$1]: FAIL — frame count ${GOT} != golden ${WANTN}"
        SCEN_FAIL=1
    fi
    if [ ${SCEN_FAIL} -eq 0 ]; then
        rm -rf "${OUT_DIR}"
    else
        FAIL=1   # keep OUT_DIR for eyeballing
    fi
}

run_scenario ws_pan_walk 400
run_scenario ws_cave_sign 400

[ ${FAIL} -eq 0 ] && echo "wide_transition: PASS"
exit ${FAIL}
