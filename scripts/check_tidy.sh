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
#   check_tidy.sh --all        whole tree (the periodic health read)
#   check_tidy.sh --metrics    just the size / cognitive-complexity KPIs
#
# clang-tidy needs a compile database: CMakePresets sets
# CMAKE_EXPORT_COMPILE_COMMANDS=ON, so configure any preset first.
# Skips (77) when clang-tidy or the database is absent — same convention as the
# data-gated tests, so CI without LLVM stays green.

SKIP=77
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="${OLDUVAI_COMPILE_DB:-${ROOT}/build/release}"

# brew's llvm is keg-only, so clang-tidy is usually NOT on PATH.
TIDY="$(command -v clang-tidy 2>/dev/null)"
[ -n "${TIDY}" ] || for c in /opt/homebrew/opt/llvm/bin/clang-tidy \
                             /usr/local/opt/llvm/bin/clang-tidy \
                             "$(xcrun --find clang-tidy 2>/dev/null)"; do
    [ -x "$c" ] && TIDY="$c" && break
done

if [ -z "${TIDY}" ]; then
    echo "check_tidy: SKIP — clang-tidy not found (brew install llvm)"
    exit ${SKIP}
fi
if [ ! -f "${DB}/compile_commands.json" ]; then
    echo "check_tidy: SKIP — no compile database at ${DB}"
    echo "  run: cmake --preset release"
    exit ${SKIP}
fi

cd "${ROOT}" || exit 1

# macOS: the compile database is generated with /usr/bin/c++ (AppleClang) and
# carries no -isysroot, so brew's keg-only clang-tidy cannot find the SDK's C++
# headers — <filesystem> et al fail to resolve.  A BROKEN PARSE STILL EMITS
# WARNINGS, just wrong ones (it invented a second empty-catch finding and
# under-reported cognitive complexity by ~160), which is worse than no gate.
# Pass the SDK explicitly and treat any clang-diagnostic-error as fatal below.
EXTRA=""
if [ "$(uname -s)" = "Darwin" ]; then
    SDK="$(xcrun --show-sdk-path 2>/dev/null)"
    [ -n "${SDK}" ] && EXTRA="--extra-arg=-isysroot${SDK}"
fi

case "${1:-}" in
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
N=$(grep -c "warning:" "${OUT}" 2>/dev/null || echo 0)

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
