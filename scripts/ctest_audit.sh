#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Run a ctest preset and audit its SKIPS — the one copy of that policy, used
# by scripts/gate_local.sh and by the gitea CI jobs.
#
#   sh scripts/ctest_audit.sh <preset> [extra ctest args...]
#
# WHY.  `ctest` counts a skip as a pass: "100% tests passed out of 66" over a
# run where 52 tests found no game files and checked nothing.  On a machine
# that HAS the files, a skip of an `assets`-labelled test (the label
# CMakeLists.txt derives from SKIP_RETURN_CODE 77) means the gate ran nowhere,
# so it is a FAILURE.  Any other skip is the test's own verdict that THIS
# platform cannot express its scenario (port_bundle on a case-insensitive
# disk) — printed, never counted.
#
# Strict only where the files are: ${OLDUVAI_GAME_DATA:-<repo>/game_data} is
# what every asset test resolves, so that is what decides.  Without it the
# asset skips are the expected, honest outcome and are only listed.
#
# Exit: 0 ok, 1 ctest failed, 2 an asset test skipped on an asset machine.
#
# OLDUVAI_GATE_ALLOW_SKIP="a b"   acknowledge named skips (still printed).
# OLDUVAI_AUDIT_OUT=<file>        append "platform: <t>" / "unexpected: <t>"
#                                 lines for a caller's summary (gate_local).
#
# NOT `ctest ... | tee`: in a pipeline `$?` is tee's, always 0, and a gate
# built that way once reported "OK" over a log saying "(Failed)".  POSIX sh has
# no PIPESTATUS, so redirect, then print.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
preset="${1:?usage: ctest_audit.sh <preset> [ctest args...]}"; shift
ALLOW="${OLDUVAI_GATE_ALLOW_SKIP:-}"
OUT="${OLDUVAI_AUDIT_OUT:-/dev/null}"

data="${OLDUVAI_GAME_DATA:-${ROOT}/game_data}"
if [ -f "${data}/FILESA.VGA" ]; then strict=1; else strict=0; fi

LOG="$(mktemp "${TMPDIR:-/tmp}/ctest_audit.XXXXXX")"
( cd "${ROOT}" && ctest --preset "${preset}" --output-on-failure "$@" ) \
    > "${LOG}" 2>&1
rc=$?
cat "${LOG}"

# ctest marks skips as "***Skipped", whatever SKIP_RETURN_CODE produced it.
SKIPPED="$(grep -oE '[A-Za-z_0-9]+ \.+ *\*\*\*Skipped' "${LOG}" \
           | awk '{print $1}' | sort -u | tr '\n' ' ')"
rm -f "${LOG}"

# Fails CLOSED: if the label query breaks or lists nothing, every skip
# counts — a broken lookup must not make the gate lenient.
ASSET_TESTS="$(cd "${ROOT}" && ctest --preset "${preset}" -N -L assets 2>/dev/null \
               | sed -n 's/^ *Test *#[0-9]*: *//p' | tr '\n' ' ')"
if [ -n "${SKIPPED}" ] && [ -z "${ASSET_TESTS}" ] && [ ${strict} -eq 1 ]; then
    echo "ctest_audit: WARNING — could not list the assets-labelled tests;" \
         "counting every skip (fail closed)"
fi

UNEXPECTED=""
n_asset_skips=0
for t in ${SKIPPED}; do
    case " ${ALLOW} " in
        *" ${t} "*) echo "ctest_audit: acknowledged skip — ${t}"; continue ;;
    esac
    case " ${ASSET_TESTS} " in
        "  "|*" ${t} "*)
            if [ ${strict} -eq 1 ]; then
                UNEXPECTED="${UNEXPECTED}${t} "
                echo "unexpected: ${t}" >> "${OUT}"
            else
                n_asset_skips=$((n_asset_skips + 1))
            fi ;;
        *)  echo "ctest_audit: platform skip — ${t} (needs no game files; not counted)"
            echo "platform: ${t}" >> "${OUT}" ;;
    esac
done
if [ ${strict} -eq 0 ] && [ ${n_asset_skips} -gt 0 ]; then
    echo "ctest_audit: no game files at ${data} — ${n_asset_skips} asset" \
         "test(s) skipped, as expected here"
fi

if [ ${rc} -ne 0 ]; then
    echo "ctest_audit: FAIL — ctest exited ${rc}"
    exit 1
fi
if [ -n "${UNEXPECTED}" ]; then
    echo "ctest_audit: FAIL — game files are at ${data}, yet these skipped:" \
         "${UNEXPECTED}"
    exit 2
fi
echo "ctest_audit: OK (${preset}$([ ${strict} -eq 1 ] && echo ', every asset test ran'))"
exit 0
