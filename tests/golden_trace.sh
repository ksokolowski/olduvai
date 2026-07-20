#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Golden-trace regression gate — invoked by CTest.
#
# Runs the native cross-engine trace harness (olduvai_trace) for the
# canonical 300-frame L1 screen-0 scripted-input run and diffs the output
# against the checked-in golden fixture.  The fixture was validated against
# the Python oracle via the reference implementation's oracle-diff tooling
# (300/300 frames identical, 15 fields per frame incl. shared RNG state,
# re-validated 2026-07-03 after the F1 reseed removal).
#
# A gameplay-logic or RNG-stream regression in systems/ fails THIS test in
# CI on any machine with game data — it no longer waits for a manual
# oracle-diff run.  When the trace legitimately changes (a fidelity fix that
# alters the oracle too), re-validate against the Python oracle FIRST,
# then regenerate:  build/release/tools/olduvai_trace <game_dir> 300 > tests/fixtures/...
#
# Skip (exit 77 = CTest SKIP_RETURN_CODE) when game data is absent.

# Game-data resolution: $OLDUVAI_GAME_DATA, argv, else repo-local
# game_data/ (gitignored — symlink it to wherever your copy lives).
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
TRACE_BIN="${2:-$(dirname "$0")/../build/release/tools/olduvai_trace}"
GOLDEN="$(dirname "$0")/fixtures/golden_trace_l1_300.txt"
SKIP=77

# The executable may be HISTORIK.EXE or the SQZ container GOG ships.
if [ ! -f "${GAME_DIR}/FILESA.CUR" ] ||
   { [ ! -f "${GAME_DIR}/HISTORIK.EXE" ] && [ ! -f "${GAME_DIR}/PREH.SQZ" ]; }; then
    echo "golden_trace: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
if [ ! -x "${TRACE_BIN}" ]; then
    echo "golden_trace: SKIP — olduvai_trace binary not found at ${TRACE_BIN}"
    exit ${SKIP}
fi
if [ ! -f "${GOLDEN}" ]; then
    echo "golden_trace: FAIL — golden fixture missing: ${GOLDEN}"
    exit 1
fi

ACTUAL="$(mktemp /tmp/golden_trace.XXXXXX)"
"${TRACE_BIN}" "${GAME_DIR}" 300 > "${ACTUAL}" || {
    echo "golden_trace: FAIL — olduvai_trace exited non-zero"
    rm -f "${ACTUAL}"
    exit 1
}

# --strip-trailing-cr: on Windows the trace binary's stdout is text-mode
# (\r\n); the fixture is LF.  Line CONTENT must still match exactly.
if diff -u --strip-trailing-cr "${GOLDEN}" "${ACTUAL}" > /tmp/golden_trace_diff.txt 2>&1; then
    echo "golden_trace: OK — 300/300 frames match the oracle-validated fixture"
    rm -f "${ACTUAL}"
    exit 0
fi

echo "golden_trace: FAIL — trace diverged from the golden fixture."
echo "First divergent lines (fields: frame px py sprite gravity club energy"
echo "lives food score fireball death rng_state climbing entity_count):"
head -20 /tmp/golden_trace_diff.txt
rm -f "${ACTUAL}"
exit 1
