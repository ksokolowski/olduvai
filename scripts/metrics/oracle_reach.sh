#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# What does the validation actually EXECUTE in systems/?
#
# WHY THIS EXISTS.  CLAUDE.md states the bar: "the bar for 'done' is a green
# cross-engine scenario corpus, not a passing unit test."  That is a claim about
# reach, and nothing measured it.  On 2026-08-02 a refactor of monster_ai's
# RunningAway branch passed golden_trace — the zero-tolerance oracle diff — and
# a probe then showed the canonical 300-frame run enters that branch ZERO times.
# The green light was real and meant nothing, and it was found by luck.
#
# So: measure it.  Two figures, because they answer different questions.
#
#   corpus  — the replay/trace gates alone (golden_trace, boss_golden_trace,
#             golden_trace_walk/_secret/_cave/_climb, boss_replay_record).  This is the reach of the
#             per-frame cross-engine diff.
#   all     — those plus the doctest suite, whose monster/player tables are
#             ALSO reference-generated.  This is the reach of "verified against
#             the oracle" in the broad sense.
#
# The gap between them is the interesting part: code the unit tables carry that
# the replay corpus never touches.  A refactor there cannot lean on "the golden
# traces pass" — they never ran it.
#
#   scripts/metrics/oracle_reach.sh [game_dir]
#
# Needs the user's game files (the corpus replays real levels) and llvm-cov +
# llvm-profdata, which ship with the Xcode command line tools.  Skips (77)
# without either, same convention as the asset-gated tests.

set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-${ROOT}/game_data}}"
SKIP=77

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "oracle_reach: SKIP — game data not found at ${GAME_DIR}"
    exit ${SKIP}
fi
COV="$(xcrun --find llvm-cov 2>/dev/null || command -v llvm-cov 2>/dev/null || true)"
PROFDATA="$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata 2>/dev/null || true)"
if [ -z "${COV}" ] || [ -z "${PROFDATA}" ]; then
    echo "oracle_reach: SKIP — llvm-cov / llvm-profdata not found"
    exit ${SKIP}
fi

cd "${ROOT}"
cmake --preset coverage >/dev/null
cmake --build --preset coverage --parallel 8 \
    --target olduvai olduvai_trace olduvai_tests >/dev/null

PROF="$(mktemp -d /tmp/olduvai_reach.XXXXXX)"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
BIN=build/coverage/olduvai
TRACE=build/coverage/tools/olduvai_trace
UNIT=build/coverage/tests/olduvai_tests

# ── The corpus: exactly what the three trace gates run ──────────────────────
LLVM_PROFILE_FILE="${PROF}/c-trace-%p.profraw" \
    "${TRACE}" "${GAME_DIR}" 300 >/dev/null 2>&1

CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-boss-%p.profraw" \
    timeout 180 "${BIN}" --play --level 2 --play-frames 300 \
    --trace "${PROF}/bt.jsonl" --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG}"

# golden_trace_walk: the scenario that leaves screen 0 (§3.15).
CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-walk-%p.profraw" \
    timeout 180 "${BIN}" --play --level 1 \
    --replay "${ROOT}/tests/fixtures/walk_in.jsonl" \
    --trace "${PROF}/wt.jsonl" --play-frames 400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1
rm -rf "${CFG}"

# golden_trace_secret / golden_trace_cave: underwater and underground (§3.15).
# name:start-screen:input-script — climb shares walk_jump_in with the level
# walks below, so it is named per SCRIPT rather than per scenario.
for scen in secret:5:secret_l1_in cave:2:cave_l1_in cavebat:6:cave_l1_in climb:0:walk_jump_in fight:0:fight_l1_in deep_run:0:deep_run_l1_in; do
    nm="${scen%%:*}"; rest="${scen#*:}"; scr="${rest%%:*}"; inp="${rest##*:}"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-${nm}-%p.profraw" \
        timeout 180 "${BIN}" --play --level 1 --start-screen "${scr}" \
        --replay "${ROOT}/tests/fixtures/${inp}.jsonl" \
        --trace "${PROF}/${nm}.jsonl" --play-frames 20000 \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1
    rm -rf "${CFG}"
done

# Display levels 3 and 7 = internal L5 and L7 — the transition dispatchers
# (§3.15).  Same walk_in.jsonl, only --level differs.
# level:start-screen — the two transition walks plus the three §3.16 clamp
# scenarios, which are not traversals: they sit on screens whose clamps hold
# the player in place.
for spec in 3: 7:10 5:10 5:11 7:4; do
    lvl="${spec%%:*}"; s="${spec##*:}"
    SS=""; [ -n "${s}" ] && SS="--start-screen ${s}"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-walk${lvl}-%p.profraw" \
        timeout 180 "${BIN}" --play --level "${lvl}" ${SS} \
        --replay "${ROOT}/tests/fixtures/walk_jump_in.jsonl" \
        --trace "${PROF}/w${lvl}.jsonl" --play-frames 400 \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
    rm -rf "${CFG}"
done

W="$(mktemp -d /tmp/olduvai_replay.XXXXXX)"
cat > "${W}/in.jsonl" <<'EOF'
{"time_ms":275,"key":"right","action":"press"}
{"time_ms":440,"key":"up","action":"press"}
{"time_ms":550,"key":"right","action":"release"}
{"time_ms":605,"key":"up","action":"release"}
EOF
( cd "${W}" && LLVM_PROFILE_FILE="${PROF}/c-replay-%p.profraw" timeout 60 \
    "${ROOT}/${BIN}" --play --level 2 --replay in.jsonl \
    --record-inputs out.jsonl --play-frames 20 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1 )
rm -rf "${W}"

# ── The unit suite (reference-generated oracle tables) ──────────────────────
LLVM_PROFILE_FILE="${PROF}/u-unit-%p.profraw" "${UNIT}" >/dev/null 2>&1

SRCS=$(git ls-files 'src/systems/*.cpp')
OBJS="-object ${TRACE} -object ${UNIT}"

# shellcheck disable=SC2086
report() {
    "${PROFDATA}" merge -sparse $1 -o "${PROF}/p.profdata"
    # TOTAL is the LAST line; the one before it is the rule.
    "${COV}" report "${BIN}" ${OBJS} -instr-profile="${PROF}/p.profdata" \
        ${SRCS} 2>/dev/null | tail -1 |
        awk '{printf "regions %-8s functions %-8s lines %-8s branches %s\n",
                     $4, $7, $10, $13}'
}

echo "── reach into src/systems (regions / functions / lines / branches) ──"
printf 'corpus only  '; report "${PROF}/c-*.profraw"
printf 'corpus+unit  '; report "${PROF}/*.profraw"

echo ""
echo "── executed by NOTHING (line coverage 0%) ──"
"${PROFDATA}" merge -sparse "${PROF}"/*.profraw -o "${PROF}/p.profdata"
# shellcheck disable=SC2086
"${COV}" report -show-functions "${BIN}" ${OBJS} \
    -instr-profile="${PROF}/p.profdata" ${SRCS} 2>/dev/null |
    awk 'NF>=10 && $(NF-3)=="0.00%" {print $1, $(NF-5)}' |
    while read -r m ln; do
        # NOT a grep for "0.00%": a branchless function reports 0 of 0 branches
        # as 0.00%, so matching any column flags fully-covered code.  The line
        # column is the one that means "never ran".
        n=$(echo "${m}" | sed 's/.*://')
        d=$(c++filt "${n}" 2>/dev/null | sed 's/olduvai::systems:://; s/(.*//')
        [ -n "${d}" ] && printf '  %-40s %3s lines\n' "${d}" "${ln}"
    done | sort -u

rm -rf "${PROF}"
