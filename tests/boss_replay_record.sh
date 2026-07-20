#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Boss-loop input record+replay — headless regression gate (invoked by CTest).
#
# run_boss_level supports --replay/--trace and, since this test, --record-inputs.
# The boss reader resolves inputs at `at(frame)` (frame 0 input-free), so the
# recorder must emit at that same frame for a record→replay round-trip.  We
# prove it by replaying a synthetic corpus into the L2 (T-Rex) boss WHILE
# recording, then byte-comparing the recorded file to the input: identical =
# the record and replay frame conventions agree.
#
# Determinism: --play-frames pins the frame count; dummy drivers mute audio and
# need no display.  Skip (77) when game data or the binary is absent.
set -eu

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
GAME_DIR=$(cd "${GAME_DIR}" 2>/dev/null && pwd || echo "${GAME_DIR}")
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
case "${BINARY}" in /*) ;; *) BINARY="$(pwd)/${BINARY}" ;; esac
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "boss_replay_record: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${BINARY}" ]; then
    echo "boss_replay_record: SKIP — binary not found: ${BINARY}"
    exit ${SKIP}
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
FAIL=0

WORK=$(mktemp -d /tmp/boss_replay_record.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

# Frame-aligned corpus (time_ms are multiples of 55 so the frame→time_ms
# round-trip is exact): hop right while jumping, then release.
cat > "${WORK}/in.jsonl" <<'EOF'
{"time_ms":275,"key":"right","action":"press"}
{"time_ms":440,"key":"up","action":"press"}
{"time_ms":550,"key":"right","action":"release"}
{"time_ms":605,"key":"up","action":"release"}
EOF

( cd "${WORK}" && timeout 60 "${BINARY}" --play --level 2 \
    --replay in.jsonl --record-inputs out.jsonl --play-frames 20 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1 )

if [ ! -s "${WORK}/out.jsonl" ]; then
    echo "boss_replay_record: FAIL — boss loop wrote no recorded inputs"; FAIL=1
elif ! diff -u "${WORK}/in.jsonl" "${WORK}/out.jsonl" >/dev/null 2>&1; then
    echo "boss_replay_record: FAIL — record→replay did not round-trip:"
    diff -u "${WORK}/in.jsonl" "${WORK}/out.jsonl" || true
    FAIL=1
fi

if [ ${FAIL} -eq 0 ]; then
    echo "boss_replay_record: OK"
fi
exit ${FAIL}
