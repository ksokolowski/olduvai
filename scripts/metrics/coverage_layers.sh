#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Line coverage per layer: what the WHOLE ctest suite (unit, shell, golden
# and trace gates, slow ones included) executes under src/.
#
#   scripts/metrics/coverage_layers.sh [--files N] [--profile DATA]
#
# Prints one row per layer, then the N least-covered files (default 15) with
# at least 40 lines, which is where to look for the next test.  The merged
# profile is kept as build/coverage/suite.profdata; --profile re-reads one
# without re-running the suite.
#
# oracle_reach.sh answers a narrower question (what the trace corpus reaches
# in systems/); this one is the suite's reach everywhere.
#
# WHERE THIS IS WRONG.  A line counts as covered when any test ran it, not
# when a test checked what it did: a shot golden covers every line that drew
# the frame.  A process ended by SIGKILL (the tests' `timeout -s KILL`) writes
# no profile, so code only such a run reaches reads as uncovered.  Needs the
# game files (the asset tests skip without them and their lines read cold)
# and llvm-cov / llvm-profdata; skips (77) without either.

set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SKIP=77
NFILES=15
DATA=""
while [ $# -gt 0 ]; do
    case "$1" in
        --files) NFILES="$2"; shift 2 ;;
        --profile) DATA="$2"; shift 2 ;;
        *) echo "coverage_layers: unknown argument $1" >&2; exit 2 ;;
    esac
done

COV="$(xcrun --find llvm-cov 2>/dev/null || command -v llvm-cov 2>/dev/null || true)"
PROFDATA="$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata 2>/dev/null || true)"
if [ -z "${COV}" ] || [ -z "${PROFDATA}" ]; then
    echo "coverage_layers: SKIP — llvm-cov / llvm-profdata not found"
    exit ${SKIP}
fi
if [ ! -f "${ROOT}/game_data/FILESA.VGA" ] && [ -z "${OLDUVAI_GAME_DATA}" ]; then
    echo "coverage_layers: SKIP — game data not found"
    exit ${SKIP}
fi

BUILD="${ROOT}/build/coverage"
cd "${ROOT}"
cmake --preset coverage >/dev/null
cmake --build --preset coverage --parallel "$(getconf _NPROCESSORS_ONLN)" \
    --target all tests tools >/dev/null

PROF="$(mktemp -d /tmp/olduvai_coverage.XXXXXX)"
trap 'rm -rf "${PROF}"' EXIT
if [ -z "${DATA}" ]; then
    # Tests fail under instrumentation only by timing; the report is still
    # the reach, so a failure is printed, not fatal.
    LLVM_PROFILE_FILE="${PROF}/%p-%m.profraw" \
        ctest --test-dir "${BUILD}" -j "$(getconf _NPROCESSORS_ONLN)" \
            --output-on-failure >"${PROF}/ctest.log" 2>&1 ||
        grep -E "tests passed|Failed|\*\*\*" "${PROF}/ctest.log" | tail -8
    DATA="${BUILD}/suite.profdata"
    "${PROFDATA}" merge -sparse "${PROF}"/*.profraw -o "${DATA}"
fi

# Every instrumented executable the suite can have run.
find "${BUILD}" -type f -perm -u+x ! -path '*/CMakeFiles/*' \
    ! -name '*.sh' ! -name '*.dylib' ! -name '*.a' > "${PROF}/objects.txt"
OBJS=""
while read -r exe; do
    if [ -z "${OBJS}" ]; then OBJS="${exe}"; else OBJS="${OBJS} -object=${exe}"; fi
done < "${PROF}/objects.txt"

# shellcheck disable=SC2086
"${COV}" report -instr-profile="${DATA}" ${OBJS} \
    -ignore-filename-regex="third_party/|tests/|build/|^${ROOT}/tools/" 2>/dev/null |
awk '
    # Paths come relative to their common prefix (src/, or the repo root).
    # Columns: file, regions (3), functions (3), lines: total $8, missed $9.
    $1 ~ /\.(cpp|hpp|h)$/ {
        f = $1; sub(/^src\//, "", f)
        split(f, p, "/"); layer = p[1]
        L[layer] += $8; M[layer] += $9
        T += $8; TM += $9
        if ($8 >= 40) print ($8 - $9) / $8, $8, $9, f > "/dev/stderr"
    }
    END {
        if (T == 0) { print "coverage_layers: the report had no src/ rows"; exit 1 }
        printf "%-14s %8s %8s %7s\n", "layer", "lines", "missed", "cover"
        for (l in L) printf "%-14s %8d %8d %6.1f%%\n", l, L[l], M[l], 100 * (L[l] - M[l]) / L[l] | "sort"
        close("sort")
        printf "%-14s %8d %8d %6.1f%%\n", "TOTAL", T, TM, 100 * (T - TM) / T
    }' 2>"${PROF}/files.txt"
echo
echo "least-covered files (>= 40 lines):"
sort -n "${PROF}/files.txt" | head -n "${NFILES}" |
    awk '{ printf "  %5.1f%%  %5d lines  %5d missed  %s\n", 100 * $1, $2, $3, $4 }'
