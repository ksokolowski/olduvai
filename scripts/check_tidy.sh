#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# clang-tidy gate — the standard C++ toolchain, configured by ./.clang-tidy.
#
# WHY changed-files-by-default: the tree has pre-existing findings (notably
# run_platform_level / run_boss_level blowing past the size + cognitive-
# complexity thresholds — those are KNOWN and tracked in
# docs/internal/GAME_APP_DECOMP.md, not news).  A whole-tree gate would be red
# from day one and would therefore be ignored, which is worse than no gate.
# Gating the DIFF means new code is held to the bar while the known monsters
# stay visible in --all without blocking anyone.  Same ratchet discipline as
# the golden hashes.
#
#   check_tidy.sh              lint files changed vs origin/master (the gate)
#   check_tidy.sh --diff       lint only the changed LINES (the CI gate)
#   check_tidy.sh --all        whole tree (the periodic health read)
#   check_tidy.sh --metrics    just the size / cognitive-complexity KPIs
#
# --diff vs the default: the default lints whole FILES that changed, so touching
# one line of game_app.cpp reports run_platform_level's cognitive complexity and
# the gate is red for a pre-existing, tracked, deliberate condition.  --diff
# reports only diagnostics ON THE CHANGED LINES (clang-tidy-diff.py, shipped
# with clang-tidy), which is what makes it safe to run in CI on a tree that has
# 81 known findings.  Use --diff in automation, the default by hand.
#
# BASE SELECTION IS THE WHOLE GAME.  origin/master is right for a topic branch
# and WRONG for a direct push to master: there origin/master IS the pushed
# commit, the diff is empty, and the gate passes without checking anything —
# the same silent-green rot the missing-base-ref guard below exists to stop.
# CI must pass the push's before-SHA via OLDUVAI_TIDY_BASE.
#
# clang-tidy needs a compile database: CMakePresets sets
# CMAKE_EXPORT_COMPILE_COMMANDS=ON, so configure any preset first.
# Skips (77) when clang-tidy or the database is absent — same convention as the
# data-gated tests, so CI without LLVM stays green.

SKIP=77
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="${OLDUVAI_COMPILE_DB:-${ROOT}/build/release}"

# ONE supported install: the pinned wheel from requirements-dev.txt — `uv tool
# install` locally, pip-into-a-venv on the runner (.gitea/workflows/ci.yml).
# Both put clang-tidy on PATH, so PATH is the lookup.
#
# No brew/xcrun fallback, deliberately.  A distribution LLVM is a DIFFERENT
# clang-tidy from the one CI runs, and the two can disagree about whether a
# change passes: checks are added, removed and retuned between releases.  A
# gate that goes red in CI and green on the author's machine is a gate people
# learn to route around — the failure mode section 4b warns about, and the same
# argument the version check below makes about the version rather than the
# install.  Skipping is the honest outcome: the gate is not "unavailable on a
# Mac", it is unavailable until the pinned tool is installed, on either OS.
#
# uv's bin dir is checked as well as PATH because `uv tool install` does not
# itself put ~/.local/bin on a login shell's PATH — a fresh install otherwise
# looks absent to this script while sitting exactly where uv reported it.
TIDY="$(command -v clang-tidy 2>/dev/null)"
if [ -z "${TIDY}" ]; then
    c="${UV_TOOL_BIN_DIR:-${HOME}/.local/bin}/clang-tidy"
    [ -x "$c" ] && TIDY="$c"
fi

if [ -z "${TIDY}" ]; then
    echo "check_tidy: SKIP — clang-tidy not found."
    echo "  uv tool install clang-tidy==22.1.8   (see requirements-dev.txt)"
    exit ${SKIP}
fi
if [ ! -f "${DB}/compile_commands.json" ]; then
    echo "check_tidy: SKIP — no compile database at ${DB}"
    echo "  run: cmake --preset release"
    exit ${SKIP}
fi

cd "${ROOT}" || exit 1

# The pin exists because a different LLVM shifts the cognitive-complexity
# numbers BACKLOG section 5 compares ACROSS COMMITS — an unpinned bump reads as
# code churn rather than as a tool change, which is the one thing that table
# must never do.  Restricting resolution to the pinned wheel narrows this but
# does not close it: the installed tool can be upgraded, and anything named
# clang-tidy earlier on PATH still wins.  So check rather than assume.
#
# A warning, not a failure.  The wrong version still lints correctly; what it
# cannot do is produce numbers comparable to the recorded table.  Failing here
# would break a working gate over a reporting concern, and a gate that refuses
# to run is a gate people stop running.
#
# Pin read from requirements-dev.txt rather than repeated here, so there is one
# place to bump.  Both spellings carry the version the same way: "LLVM version
# 22.1.8" (pip/uv wheel) and "Homebrew LLVM version 22.1.8" (brew).
PIN="$(sed -n 's/^clang-tidy==\([0-9.]*\).*/\1/p' \
        "${ROOT}/requirements-dev.txt" 2>/dev/null | head -1)"
GOT="$("${TIDY}" --version 2>/dev/null |
        sed -n 's/.*LLVM version \([0-9.]*\).*/\1/p' | head -1)"
if [ -n "${PIN}" ] && [ -n "${GOT}" ] && [ "${PIN}" != "${GOT}" ]; then
    echo "check_tidy: WARNING — clang-tidy is ${GOT}, requirements-dev.txt pins ${PIN}."
    echo "  ${TIDY}"
    echo "  Findings and the section 5 KPI numbers are comparable to the"
    echo "  recorded table only at the pinned version."
    echo "  uv tool install clang-tidy==${PIN}   (see requirements-dev.txt)"
fi

# macOS: the compile database is generated with /usr/bin/c++ (AppleClang) and
# carries no -isysroot, so a non-Apple clang-tidy cannot find the SDK's C++
# headers — <filesystem> et al fail to resolve.  This is NOT a brew artifact:
# the pinned wheel fails exactly the same way, which is why the injection stays
# after dropping the brew fallback.  A BROKEN PARSE STILL EMITS WARNINGS, just
# wrong ones (it invented a second empty-catch finding and under-reported
# cognitive complexity by ~160), which is worse than no gate.
# Pass the SDK explicitly and treat any clang-diagnostic-error as fatal below.
# DIFF_EXTRA carries the same SDK to clang-tidy-diff.py, which needs it just
# as badly — without it the --diff gate dies on "'string' file not found" and
# reports FAIL for every change made on a Mac.  The spelling differs because
# the shim parses its own argv: clang-tidy takes --extra-arg, the shim's
# argparse option is registered as -extra-arg and will not match a '--' form.
EXTRA=""
DIFF_EXTRA=""
if [ "$(uname -s)" = "Darwin" ]; then
    SDK="$(xcrun --show-sdk-path 2>/dev/null)"
    if [ -n "${SDK}" ]; then
        EXTRA="--extra-arg=-isysroot${SDK}"
        DIFF_EXTRA="-extra-arg=-isysroot${SDK}"
    fi
fi

case "${1:-}" in
--diff)
    BASE="${OLDUVAI_TIDY_BASE:-origin/master}"
    if ! git rev-parse --verify --quiet "${BASE}^{commit}" >/dev/null; then
        echo "check_tidy: FAIL — base ref '${BASE}' does not exist here."
        echo "  A diff gate with no base checks NOTHING while reporting success."
        exit 1
    fi
    # Run the shim DIRECTLY, never as `python3 <path>`: pip/uv install it with a
    # shebang pointing at the environment that owns the clang_tidy module, and
    # the system python3 cannot import it.
    # The wheel installs the shim into the same bin/ as the binary, so those
    # two locations are the whole search.  (An LLVM distribution puts it in
    # ../share/clang/ instead — not looked for here, because such an install is
    # not a supported source for TIDY either.  See the resolution above.)
    DIFFTOOL="$(command -v clang-tidy-diff.py 2>/dev/null)"
    [ -n "${DIFFTOOL}" ] || DIFFTOOL="$(dirname "${TIDY}")/clang-tidy-diff.py"
    if [ ! -x "${DIFFTOOL}" ]; then
        echo "check_tidy: SKIP — clang-tidy-diff.py not found."
        echo "  Searched: PATH and $(dirname "${TIDY}")/"
        echo "  Install the pinned tool: uv tool install clang-tidy==22.1.8"
        exit ${SKIP}
    fi
    # -U0: clang-tidy-diff.py needs zero context or it attributes untouched
    # lines to the change.  -p1 strips git's a//b/ prefix.
    PATCH=$(git diff -U0 "${BASE}...HEAD" -- 'src/*.cpp' 'src/**/*.cpp')
    if [ -z "${PATCH}" ]; then
        echo "check_tidy: OK — no C++ sources changed vs ${BASE}"
        exit 0
    fi
    echo "check_tidy: linting CHANGED LINES vs ${BASE}"
    OUT=$(mktemp /tmp/olduvai_tidy.XXXXXX)
    # Subtract the two KPI checks (a leading '-' removes from the configured
    # set rather than replacing it).  They are function-level, so touching the
    # FIRST LINE of a big function reports its whole-function score as if the
    # change caused it — editing one line of parse_args reported its 142.  Both
    # are measurements by design (see .clang-tidy) and are tracked in BACKLOG
    # section 5 and reported by --metrics; a gate is the wrong instrument.
    printf '%s\n' "${PATCH}" | "${DIFFTOOL}" \
        -clang-tidy-binary "${TIDY}" -path "${DB}" -p1 -j4 -quiet \
        -checks='-readability-function-size,-readability-function-cognitive-complexity' \
        ${DIFF_EXTRA} \
        >"${OUT}" 2>&1
    if grep -q "clang-diagnostic-error" "${OUT}" 2>/dev/null; then
        echo "check_tidy: FAIL — clang-tidy could not parse the sources."
        grep "clang-diagnostic-error" "${OUT}" | head -3 | sed 's|.*/src/|  src/|'
        rm -f "${OUT}"
        exit 1
    fi
    # `grep -c || echo 0` is WRONG: grep -c already prints 0 and then exits 1,
    # so the fallback appends a SECOND 0 and the test dies on "Illegal number".
    N=$(grep -c "warning:" "${OUT}" 2>/dev/null || true)
    [ -n "${N}" ] || N=0
    if [ "${N}" -eq 0 ]; then
        echo "check_tidy: OK — no findings on changed lines"
        rm -f "${OUT}"
        exit 0
    fi
    echo "check_tidy: ${N} finding(s) on lines this change touched:"
    grep "warning:" "${OUT}" | sed 's|.*/src/|  src/|' | sort -u
    echo ""
    echo "These are on code you just wrote or edited. Fix them, or if a finding"
    echo "is a deliberate, justified exception, silence it at the line with a"
    echo "// NOLINT(<check-name>) comment AND say why."
    rm -f "${OUT}"
    exit 1
    ;;
--all)
    FILES=$(git ls-files 'src/*.cpp' 'src/**/*.cpp')
    LABEL="whole tree"
    ;;
--metrics)
    echo "check_tidy: size / cognitive-complexity KPIs (whole tree)"
    # shellcheck disable=SC2086
    "${TIDY}" -p "${DB}" --quiet ${EXTRA} \
        --checks='-*,readability-function-size,readability-function-cognitive-complexity' \
        $(git ls-files 'src/*.cpp' 'src/**/*.cpp') 2>/dev/null \
        | grep -E "warning:.*(exceeds recommended|cognitive complexity)" \
        | sed 's|.*/src/|src/|' | sort -u
    exit 0
    ;;
*)
    BASE="${OLDUVAI_TIDY_BASE:-origin/master}"
    # A MISSING base ref must not read as "nothing changed".  The public GitHub
    # repo is a fresh-history single-commit export, so origin/master does not
    # exist there: `git diff` returned 128, the error was swallowed, FILES came
    # back empty and this gate printed OK and passed — permanently, silently
    # green.  That is the same rot mode as the asset-gated tests that skip on
    # every machine but the owner's.  Fail loudly instead, and let a caller that
    # genuinely has no base ask for --all.
    if ! git rev-parse --verify --quiet "${BASE}^{commit}" >/dev/null; then
        echo "check_tidy: FAIL — base ref '${BASE}' does not exist here."
        echo "  A diff gate with no base checks NOTHING while reporting success."
        echo "  Use --all for a full sweep, or set OLDUVAI_TIDY_BASE to a real ref."
        exit 1
    fi
    FILES=$(git diff --name-only --diff-filter=ACM "${BASE}"...HEAD -- 'src/*.cpp' 'src/**/*.cpp')
    LABEL="changed vs ${BASE}"
    ;;
esac

if [ -z "${FILES}" ]; then
    echo "check_tidy: OK — no C++ sources ${LABEL}"
    exit 0
fi

echo "check_tidy: linting ${LABEL}"
OUT=$(mktemp /tmp/olduvai_tidy.XXXXXX)
# shellcheck disable=SC2086
"${TIDY}" -p "${DB}" --quiet ${EXTRA} ${FILES} >"${OUT}" 2>&1
if grep -q "clang-diagnostic-error" "${OUT}" 2>/dev/null; then
    echo "check_tidy: FAIL — clang-tidy could not parse the sources."
    grep "clang-diagnostic-error" "${OUT}" | head -3 | sed 's|.*/src/|  src/|'
    echo "  A broken parse yields WRONG findings, so this is fatal, not a warning."
    rm -f "${OUT}"
    exit 1
fi
# See the --diff branch: `|| echo 0` doubles the count grep already printed.
N=$(grep -c "warning:" "${OUT}" 2>/dev/null || true)
[ -n "${N}" ] || N=0

if [ "${N}" -eq 0 ]; then
    echo "check_tidy: OK — no findings"
    rm -f "${OUT}"
    exit 0
fi

echo "check_tidy: ${N} finding(s):"
grep "warning:" "${OUT}" | sed 's|.*/src/|  src/|' | sort -u
echo ""
echo "Fix them, or if a finding is a deliberate, justified exception, silence it"
echo "at the line with a // NOLINT(<check-name>) comment AND say why — an"
echo "unexplained NOLINT is worse than the warning."
rm -f "${OUT}"
exit 1
