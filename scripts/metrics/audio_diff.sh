#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Render EVERYTHING audible and digest it — the before/after instrument for
# audio changes.  Music AND sound effects.
#
#   scripts/metrics/audio_diff.sh before.txt     # capture, then make the change
#   scripts/metrics/audio_diff.sh after.txt
#   diff before.txt after.txt                    # empty = nothing audible moved
#
# WHY THIS IS A TOOL AND NOT A GATE.  A checked-in golden hash for audio was
# considered and refused, and the reason is real: the synths and the sample
# chain are floating point end to end — std::pow in the OPL pitch table feeds an
# integer quantisation, resample_sinc_u8 runs on double with std::sin and
# std::lrint, and the frame accumulator is an FMA-contraction site under LTO.
# A pinned hash would fail across libm, compiler and build-type changes for
# reasons that have nothing to do with a regression.  tests/audio_render.sh
# therefore asserts run-to-run determinism plus "it is not silence", which is
# the honest pair for CI.
#
# NONE OF THAT APPLIES TO A BEFORE/AFTER DIFF ON ONE MACHINE.  Same compiler,
# same libm, same flags, both sides built identically: any difference IS the
# change.  This is the audio counterpart of the OLDUVAI_DUMP_DESCENT byte-diff
# used for the L3 cinematic, and it exists because the refactors of 2026-08-02
# touched resample.hpp and render_adlib_sfx with no waveform check available at
# all — the unit tests pin PROPERTIES (length, monotonicity, endpoints) and a
# property test passes happily while the wave shifts by a sample.
#
# WHAT IT COVERS.  Every synth backend that can load its assets, over the
# synthetic fixture MIDI (hand-authored, no game content), plus every AdLib SFX
# in the catalog.  A backend that cannot load is reported and skipped rather
# than silently dropped — a shorter file must not read as "nothing changed".
#
# Hashes only, never the audio: rendered SFX are decoded game content and must
# not enter the tree (CONTRIBUTING.md).
#
# Skips (77) when game data or the binary is absent.

set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-/dev/stdout}"
GAME_DIR="${OLDUVAI_GAME_DATA:-${ROOT}/game_data}"
BIN="${OLDUVAI_BIN:-${ROOT}/build/release/olduvai}"
SKIP=77

if [ ! -x "${BIN}" ]; then
    echo "audio_diff: SKIP — binary not found: ${BIN}" >&2
    exit ${SKIP}
fi
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "audio_diff: SKIP — game data not found at ${GAME_DIR}" >&2
    exit ${SKIP}
fi

WORK="$(mktemp -d /tmp/olduvai_audio_diff.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT
FIXTURE="${WORK}/fixture.mid"
python3 "${ROOT}/tests/audio_fixture.py" "${FIXTURE}"

SF_ARGS=""
[ -n "${OLDUVAI_SOUNDFONT}" ] && SF_ARGS="--soundfont ${OLDUVAI_SOUNDFONT}"

{
    # ── Music: every backend that can load its assets ──────────────────────
    # OPL included since 2026-08-24: its offline arm plays RAW game-MDI (the
    # FF 7F timbre blocks), which the synthetic fixture carries — and that arm
    # shipped rendering SILENCE until the first human looked at a digest
    # (BACKLOG §6).  Needs --game-dir like the SFX half.
    for dev in gm-builtin mt32-builtin opl; do
        # shellcheck disable=SC2086
        if line="$("${BIN}" --render-audio "${FIXTURE}" --music-device "${dev}" \
                   --game-dir "${GAME_DIR}" \
                   ${SF_ARGS} --render-audio-secs 2 2>/dev/null)"; then
            echo "music ${dev} ${line}"
        else
            # Reported, not dropped: a backend that vanishes between the two
            # captures would otherwise look like "no change" in the diff.
            echo "music ${dev} UNAVAILABLE"
        fi
    done

    # ── Sound effects: the whole catalog ───────────────────────────────────
    if sfx="$("${BIN}" --render-sfx all --game-dir "${GAME_DIR}" 2>/dev/null)"; then
        echo "${sfx}" | while read -r line; do
            [ -n "${line}" ] && echo "sfx ${line}"
        done
    else
        echo "sfx UNAVAILABLE"
    fi
} > "${OUT}"

[ "${OUT}" = "/dev/stdout" ] || echo "audio_diff: wrote ${OUT}" >&2
