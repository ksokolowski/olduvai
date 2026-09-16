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
#   corpus  — the replay/trace gates alone.  All 17 of them as of 2026-09-06:
#             golden_trace, boss_golden_trace, _walk, _walk_l3, _walk_l7,
#             _l5s10, _l5s11, _l7s4, _secret, _cave, _cavebat, _climb, _fight,
#             _deep_run, _l4_fight, _l6_fight, _l6_slam_hd, plus
#             boss_replay_record, plus the ten adopted oracle scenarios
#             (l1_balloon_flight, l1_food_route, l1_full_clear, l1_secret_dive,
#             l2_boss_fight, l3_icy_route, l3_icy_walk, l5_darkwoods_deep,
#             l5_darkwoods_walk, l7_volcanic_walk).  This is the reach of the per-frame
#             cross-engine diff.  DO NOT trust this sentence — trust `ctest -N`
#             and diff it against what this file actually invokes; the last
#             time it was left to prose, three gates went unmeasured for two
#             weeks (see the boss-fight block below).
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
# NOTE ON `|| true`, EVERYWHERE BELOW.  run_game returns 1 for any outcome that
# is not an explicit quit (game_app.cpp: `rc = outcome == kQuit ? 0 : 1`, since
# 2026-06-10), and several corpus scenarios END IN GAME OVER by design — that
# is what they are for.  Under `set -e` such a run aborts this script before it
# prints anything.  It did: `deep_run` was added to the loop below on 2026-08-03
# (a0270b8) without the guard its sibling loop already carried, and from then
# until 2026-09-06 this script exited 1 with EMPTY OUTPUT every time.  Nobody
# saw it because it is report-only and nothing runs it on a schedule.
LLVM_PROFILE_FILE="${PROF}/c-trace-%p.profraw" \
    "${TRACE}" "${GAME_DIR}" 300 >/dev/null 2>&1 || true

CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-boss-%p.profraw" \
    timeout 180 "${BIN}" --play --level 2 --play-frames 300 \
    --trace "${PROF}/bt.jsonl" --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
rm -rf "${CFG}"

# golden_trace_walk: the scenario that leaves screen 0 (§3.15).
CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-walk-%p.profraw" \
    timeout 180 "${BIN}" --play --level 1 \
    --replay "${ROOT}/tests/fixtures/walk_in.jsonl" \
    --trace "${PROF}/wt.jsonl" --play-frames 400 \
    --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
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
        --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
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
    --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true )
rm -rf "${W}"

# ── The three boss-fight gates ─────────────────────────────────────────────
# golden_trace_l4_fight / _l6_fight (2026-08-23) and golden_trace_l6_slam_hd
# (§3.3c's gate, 2026-08-24).  ABSENT FROM THIS LIST UNTIL 2026-09-06: they
# were registered ctests for two weeks while this script did not know they
# existed, so every corpus figure quoted in that window understated reach by
# whatever boss_l4.cpp / boss_l6.cpp they execute.  §3.15 item 3 names exactly
# this ("new recordings must be added to oracle_reach.sh's explicit corpus list
# to count") and the list is hand-kept, so WHEN YOU ADD A GATE, diff this file
# against `ctest -N` — that is how these three were found.
#
# level:frames:input:forced-smooth
for spec in 4:470:l4_boss_fight:0 6:800:l6_boss_fight:0 6:600:l6_slam_deaths:1; do
    lvl="${spec%%:*}"; r="${spec#*:}"
    frm="${r%%:*}"; r="${r#*:}"
    inp="${r%%:*}"; sm="${r##*:}"
    # l6_slam_hd's whole subject is the smooth-motion pose-hold, so replicate
    # its gate rather than a tidier approximation: reach of what the gates
    # ACTUALLY run is the only thing this script is allowed to claim.
    SM=""; FL=""
    if [ "${sm}" = "1" ]; then
        SM="OLDUVAI_FORCE_SMOOTH=1"
        FL="--enhanced --hd-profile mmpx --render-scale 2"
    fi
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    # shellcheck disable=SC2086  # SM and FL are deliberately word-split
    XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-${inp}-%p.profraw" \
        env ${SM} timeout 300 "${BIN}" --play --level "${lvl}" ${FL} \
        --replay "${ROOT}/tests/fixtures/${inp}.jsonl" \
        --trace "${PROF}/${inp}.jsonl" --play-frames "${frm}" \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
    rm -rf "${CFG}"
done

# ── The ten ADOPTED ORACLE SCENARIOS (§3.15 item 2, 2026-09-06) ────────────
# The reference repo's own scenarios, adopted byte for byte as native gates
# (all `slow`-labelled).  Added HERE in the same commit that registered them,
# which is the discipline item 3 exists to enforce: a gate missing from this
# list is a gate that does not count, and that is how l4_fight / l6_fight /
# l6_slam_hd went unmeasured for two weeks.
#
# Worth +10.6 corpus line points and +12.6 branch points on their own, and they
# also move corpus+unit — so they reach code nothing else in the tree runs.
for spec in 1:l1_balloon_flight 1:l1_food_route 1:l1_full_clear 1:l1_secret_dive \
            2:l2_boss_fight 3:l3_icy_route 3:l3_icy_walk 5:l5_darkwoods_deep \
            5:l5_darkwoods_walk 7:l7_volcanic_walk; do
    lvl="${spec%%:*}"; inp="${spec##*:}"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" LLVM_PROFILE_FILE="${PROF}/c-${inp}-%p.profraw" \
        timeout 600 "${BIN}" --play --level "${lvl}" \
        --replay "${ROOT}/tests/fixtures/${inp}.jsonl" \
        --trace "${PROF}/${inp}.jsonl" --play-frames 20000 \
        --game-dir "${GAME_DIR}" >/dev/null 2>&1 || true
    rm -rf "${CFG}"
done

# ── Did the corpus actually RUN? ───────────────────────────────────────────
# With `|| true` on every run above, a broken binary would produce no profraw
# files and this script would happily report 0.00% as though that were a
# measurement.  Refuse to: an instrument that cannot tell "did not run" from
# "reach is low" is worse than no instrument, which is the lesson of the
# month it spent exiting 1 in silence.
NPROF=$(find "${PROF}" -name 'c-*.profraw' | wc -l | tr -d ' ')
if [ "${NPROF}" -lt 25 ]; then
    echo "oracle_reach: FAIL — only ${NPROF} corpus profraw files; expected >= 25." >&2
    echo "  The corpus did not run.  Do NOT read the figures below as reach." >&2
    rm -rf "${PROF}"
    exit 1
fi

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

# ── Per-file CORPUS reach ──────────────────────────────────────────────────
# The totals above hide where the corpus is thin, and §3.15 item 2's whole
# argument is per-file ("monster_ai sits at 28.7% and collisions at 31.8% from
# the scenario side").  Those numbers were quoted from a run nobody could
# reproduce because this section did not exist — so they rotted for a month
# while `fight` and `deep_run` were landing and moving them.  Print them.
echo ""
echo "── per-file CORPUS reach (lines) — where the replay corpus is thin ──"
"${PROFDATA}" merge -sparse "${PROF}"/c-*.profraw -o "${PROF}/c.profdata"
# shellcheck disable=SC2086
# llvm-cov prints the BASENAME in column 1, not the path, and lines-Cover is
# column 10 (Regions/Missed/Cover, Functions/Missed/Executed, Lines/Missed/
# Cover, Branches/...).  Both were checked against real output — the first
# version of this block matched /^src\/systems\// and silently printed
# nothing, which is the same "green measurement measuring nothing" this script
# exists to prevent.  Sorted thinnest first: that is the question being asked.
"${COV}" report "${BIN}" ${OBJS} -instr-profile="${PROF}/c.profdata" ${SRCS} \
    2>/dev/null |
    awk '$1 ~ /\.cpp$/ { gsub("%","",$10); printf "%7.2f  %s\n", $10, $1 }' |
    sort -n | awk '{ printf "  %-30s %6s%%\n", $2, $1 }'

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
