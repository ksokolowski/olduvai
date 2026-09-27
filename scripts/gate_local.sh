#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The honest local gate — the full suite, on a machine that HAS the game files.
#
# WHY THIS EXISTS.  20 of the 31 registered tests needed the user's own game
# files, and no CI runner had them, so CI really exercised 11.  (Since
# 2026-09-21 the gitea LINUX runner mounts them and runs the same audit —
# scripts/ctest_audit.sh; the Windows runner and GitHub still do not, and
# the slow label and the Mac toolchain are still only here.)  Worse, `ctest`
# counts a skip as a pass and prints "100% tests passed out of 31" — the suite
# reads fully green on a machine where most of it never ran.  That gap is the
# measured reason duplication keeps recurring: every `slurp` copy sits on an
# asset-load path and every pure upload site on a present path, and no
# always-green test touches either (docs/internal/BACKLOG.md §1).
#
# So on a machine WITH assets, a SKIP of an asset-gated test is a FAILURE.  That
# is the whole idea: where the files are, a silent skip means the gate ran
# nowhere at all.  (A test that needs no
# game files and skips for a platform reason is printed, not counted.)
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
    --help|-h) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
PLATFORM_SKIPS=""

# run_ctest <summary-name> <preset> [extra ctest args...]
#
# Both call sites (the build lanes and the `slow` label below) need identical
# handling, and this repo has paid for "three copies of one policy" before
# (BACKLOG §3.14a) — so the run and the skip audit are ONE script,
# scripts/ctest_audit.sh, shared with CI; this function only keeps the summary.
# The three lints CI's build-and-lint runs BEFORE it builds anything.  This
# gate ran none of them: it is the test gate, and the difference was invisible
# until a layering violation (a shared header placed in core/, which is ABOVE
# prepare) went green here and red there for four commits — while the pre-push
# hook dutifully reported the red run each time.  A local gate that does not
# cover CI's cheap checks is a gate that teaches you to trust the wrong green.
run_lints() {
    printf '\n\033[1m═══ lints (what CI runs first) ═══\033[0m\n'
    sh "${ROOT}/scripts/check_tree.sh" || return 1
    sh "${ROOT}/scripts/check_layers.sh" || return 1
    sh "${ROOT}/scripts/check_commit_range.sh" HEAD || return 1
    # GCC over the changed TUs.  CI builds with GCC and MSVC; this machine is
    # clang + libc++, and the difference that bites is transitive includes
    # (check_gcc.sh's header says which ones did).  It needs the compile
    # database, so it skips loudly on a fresh tree and runs on every later
    # gate — which is when it matters, because by then something is changed.
    sh "${ROOT}/scripts/check_gcc.sh" || return 1
    # clang-tidy on the unpushed lines, as CI's build-and-lint runs it: CI's
    # base is the push's before-SHA, which before a push is origin/master.
    # 77 = no clang-tidy or no compile database.
    _rc=0
    OLDUVAI_TIDY_BASE="$(git -C "${ROOT}" rev-parse origin/master)" \
        sh "${ROOT}/scripts/check_tidy.sh" --diff || _rc=$?
    [ ${_rc} -eq 0 ] || [ ${_rc} -eq 77 ] || return 1
    # The complexity ratchet: every function over a size or cognitive-
    # complexity threshold is in scripts/complexity_baseline.txt, with its
    # numbers and a reason.
    _rc=0
    sh "${ROOT}/scripts/check_tidy.sh" --ratchet || _rc=$?
    [ ${_rc} -eq 0 ] || [ ${_rc} -eq 77 ] || return 1
}

run_ctest() {
    _name="$1"; shift
    _preset="$1"; shift
    _out="$(mktemp -t olduvai_gate_XXXXXX)"
    # The run and the skip audit are scripts/ctest_audit.sh — one copy, which
    # CI's asset-bearing jobs call too.  ALLOW travels as the env it reads.
    OLDUVAI_GATE_ALLOW_SKIP="${ALLOW}" OLDUVAI_AUDIT_OUT="${_out}" \
        sh "${ROOT}/scripts/ctest_audit.sh" "${_preset}" "$@"
    _rc=$?
    UNEXPECTED="$(sed -n 's/^unexpected: //p' "${_out}" | tr '\n' ' ')"
    PLATFORM="$(sed -n 's/^platform: //p' "${_out}" | tr '\n' ' ')"
    rm -f "${_out}"

    if [ ${_rc} -eq 1 ]; then
        SUMMARY="${SUMMARY}\n  ${_name}: FAIL — ctest failed"
        STATUS=1
    elif [ ${_rc} -ne 0 ]; then
        SUMMARY="${SUMMARY}\n  ${_name}: FAIL — skipped on an asset machine: ${UNEXPECTED}"
        STATUS=1
    elif [ -n "${PLATFORM}" ]; then
        SUMMARY="${SUMMARY}\n  ${_name}: OK — platform skip, not asset-gated: ${PLATFORM}"
        PLATFORM_SKIPS="${PLATFORM_SKIPS}${PLATFORM}"
    else
        SUMMARY="${SUMMARY}\n  ${_name}: OK"
    fi
}

if ! run_lints; then
    echo ""
    echo "gate_local: FAILED — a lint CI runs first said no; nothing was built."
    exit 1
fi

# One job per CPU.  Tests are parallel-safe (own mktemp dirs, read-only game
# files); if a timing test turns flaky, retry at OLDUVAI_GATE_JOBS=1 first.
JOBS="${OLDUVAI_GATE_JOBS:-$(getconf _NPROCESSORS_ONLN)}"

for lane in ${LANES}; do
    echo ""
    echo "═══ ${lane} ═══════════════════════════════════════════════════════"
    cmake --preset "${lane}" >/dev/null
    # `tools` too: the dev tools are EXCLUDE_FROM_ALL and no CI job builds them,
    # so an API change leaves them uncompilable until someone needs one.
    cmake --build --preset "${lane}" --parallel "${JOBS}" \
        --target all tests tools >/dev/null

    # release-full adds the `slow` label (hd_text_screens, ~256 s, the only
    # classic-present coverage); run alongside the rest it adds no wall-clock.
    preset="${lane}"
    if [ "${lane}" = release ]; then preset=release-full; fi
    run_ctest "${lane}" "${preset}" -j "${JOBS}"
done

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
if [ -n "${PLATFORM_SKIPS}" ]; then
    echo "gate_local: OK — every asset-gated test ran; platform skips listed" \
         "above run in CI, not here."
else
    echo "gate_local: OK — the full suite ran, nothing silently skipped."
fi
