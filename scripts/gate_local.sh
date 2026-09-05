#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The honest local gate — the full suite, on a machine that HAS the game files.
#
# WHY THIS EXISTS.  20 of the 31 registered tests need the user's own game
# files, and no CI runner has them, so CI really exercises 11.  Worse, `ctest`
# counts a skip as a pass and prints "100% tests passed out of 31" — the suite
# reads fully green on a machine where most of it never ran.  That gap is the
# measured reason duplication keeps recurring: every `slurp` copy sits on an
# asset-load path and every pure upload site on a present path, and no
# always-green test touches either (docs/internal/BACKLOG.md §1).
#
# So on a machine WITH assets, a SKIP is a FAILURE.  That is the whole idea:
# the owner's machine is the only place the other 18 can run, so a silent skip
# there means the gate ran nowhere at all.
#
#   scripts/gate_local.sh              release + asan, full suite, strict
#   scripts/gate_local.sh --release    release lane only (faster iteration)
#
# Genuinely-unavailable assets are acknowledged explicitly, never ignored:
#
#   OLDUVAI_GATE_ALLOW_SKIP="sqz_parity" scripts/gate_local.sh
#
# Acknowledged skips are still PRINTED on every run — an allowance that goes
# quiet is how a gate rots.  sqz_parity is the usual one: it needs PREH.SQZ,
# which unpacked game distributions do not carry.

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ALLOW="${OLDUVAI_GATE_ALLOW_SKIP:-}"
LANES="release asan"

case "${1:-}" in
    --release) LANES="release" ;;
    --help|-h) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    "") ;;
    *) echo "gate_local: unknown argument '$1' (try --help)" >&2; exit 2 ;;
esac

cd "${ROOT}"

if [ ! -f game_data/FILESA.VGA ]; then
    echo "gate_local: FAIL — no game files at ${ROOT}/game_data"
    echo ""
    echo "This gate exists precisely to run what CI cannot.  Without the game"
    echo "files it would degrade to the 11 always-green tests, which is what"
    echo "CI already does — run 'ctest --preset release -LE assets' for that."
    exit 1
fi

STATUS=0
SUMMARY=""

# run_ctest <summary-name> <preset> [extra ctest args...]
#
# One copy of the skip audit, which is the point of this whole script: on a
# machine WITH the game files a SKIP means the gate ran nowhere at all, so it
# is a failure unless acknowledged.  Both call sites (the build lanes and the
# `slow` label below) need identical handling, and this repo has paid for
# "three copies of one policy" before (BACKLOG §3.14a) — so it is a function.
#
# NOT `ctest ... | tee "${LOG}"`.  In a pipeline `$?` is the status of the LAST
# command — tee — which is always 0, so a real ctest failure read as success.
# This gate shipped with exactly that bug and reported "release: OK / asan: OK"
# for a run whose log said "30 - ending_shot (Failed)".  PIPESTATUS would fix
# it in bash; this is /bin/sh, so redirect and print afterwards.
run_ctest() {
    _name="$1"; shift
    _preset="$1"; shift
    LOG="$(mktemp -t olduvai_gate_XXXXXX)"
    if ctest --preset "${_preset}" --output-on-failure "$@" > "${LOG}" 2>&1; then
        CTEST_RC=0
    else
        CTEST_RC=$?
    fi
    cat "${LOG}"

    # ctest marks skips as "***Skipped"; SKIP_RETURN_CODE 77 lands there.
    SKIPPED="$(grep -oE '[A-Za-z_0-9]+ \.+ *\*\*\*Skipped' "${LOG}" \
               | awk '{print $1}' | sort -u | tr '\n' ' ')"
    rm -f "${LOG}"

    UNEXPECTED=""
    for t in ${SKIPPED}; do
        case " ${ALLOW} " in
            *" ${t} "*) echo "gate_local: acknowledged skip — ${t}" ;;
            *) UNEXPECTED="${UNEXPECTED}${t} " ;;
        esac
    done

    if [ ${CTEST_RC} -ne 0 ]; then
        SUMMARY="${SUMMARY}\n  ${_name}: FAIL — ctest exited ${CTEST_RC}"
        STATUS=1
    elif [ -n "${UNEXPECTED}" ]; then
        SUMMARY="${SUMMARY}\n  ${_name}: FAIL — skipped on an asset machine: ${UNEXPECTED}"
        STATUS=1
    else
        SUMMARY="${SUMMARY}\n  ${_name}: OK"
    fi
}

for lane in ${LANES}; do
    echo ""
    echo "═══ ${lane} ═══════════════════════════════════════════════════════"
    cmake --preset "${lane}" >/dev/null
    cmake --build --preset "${lane}" --parallel 8 --target all tests >/dev/null

    # Run the suite in parallel.  MEASURED on this corpus: 239 s serial vs
    # 90 s at -j6, and 90 s is exactly boss_pause_shot's own runtime — that
    # single test is the critical path, so more jobs buy nothing until it is
    # faster.  The tests are parallel-safe by construction: each makes its own
    # mktemp config dir and shot file, and the only shared input (the game
    # files) is read-only.
    #
    # Most of the wall-clock is 18.2 Hz gameplay, not CPU, so the jobs overlap
    # rather than contend.  If a timing-sensitive test ever does turn flaky
    # here, drop to -j1 to confirm before assuming the change under test broke
    # it.
    run_ctest "${lane}" "${lane}" -j "${OLDUVAI_GATE_JOBS:-6}"
done

# ── The `slow` label: registered tests kept out of the everyday suite ───────
# hd_text_screens runs the loading card and the score tally through BOTH present
# stacks in BOTH modes (~256 s), nearly all of it sleeping at 18 Hz through a
# fight, a victory sequence and a fade.  Its four dos cells are the only
# coverage the CLASSIC present path has, on either stack — `tally_pause` is
# reachable from nowhere else.
#
# It is a REGISTERED ctest carrying LABELS `slow`, and the `release` / `asan`
# test presets filter that label out, so the everyday suite stays ~550 s.  This
# runs it — by LABEL, not by filename, so a future slow test joins in without
# touching this script.
#
# IT USED TO BE A LOOSE SCRIPT INVOKED HERE BY NAME, and that cost something
# real: a coverage sweep read `ctest -N`, did not find it, reported it as never
# run, and registered it — when it had been running here the whole time.  Two
# registries, and the one people reach for first did not list it.  Now there is
# one: everything is a ctest, and `slow` decides what the default lane skips.
echo ""
echo "── slow-labelled gates (not in the everyday suite) ──"
run_ctest "slow gates" release-full -L slow

echo ""
echo "═══ gate_local ════════════════════════════════════════════════════════"
printf '%b\n' "${SUMMARY}"

if [ ${STATUS} -ne 0 ]; then
    echo ""
    echo "A skip here is a failure.  Either the asset is genuinely missing from"
    echo "your game copy — acknowledge it with OLDUVAI_GATE_ALLOW_SKIP=\"<name>\""
    echo "so it stays visible — or the test's own data check is wrong."
    exit 1
fi

echo ""
echo "gate_local: OK — the full suite ran, nothing silently skipped."
