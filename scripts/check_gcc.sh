#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Syntax-check the CHANGED translation units with GCC, which the dev Mac is not.
#
# WHY THIS EXISTS.  The everyday toolchain here is Apple clang + libc++; CI
# builds with GCC (13.3 on the runner, 11 in the sdl-floor image) + libstdc++,
# and MSVC on Windows.  The difference that bites is not exotic: it is
# TRANSITIVE INCLUDES.  `for (c : {'a','b'})` needs <initializer_list>, which
# libc++ drags in and libstdc++ does not — so tests/test_transition_geometry.cpp
# compiled here and broke every GCC job and both MSVC jobs on CI (2026-09-20,
# four commits red).  The same class took two days in the MSVC jobs earlier
# that week (an SDL.h in a header main.cpp includes).
#
# Running g++ by hand over "the files I touched" was already the rule
# (CLAUDE.md) and was followed for the SOURCE file of that commit and not for
# the TEST file beside it.  A rule that depends on remembering which files
# count is not a rule, so this walks git.
#
#   scripts/check_gcc.sh                 # changed vs origin/master
#   scripts/check_gcc.sh HEAD~3          # changed vs another ref
#   OLDUVAI_GXX=/path/to/g++ scripts/check_gcc.sh
#
# SKIPS LOUDLY (exit 0) when g++ or the compile database is missing — it is a
# cross-check, not a gate that should block a machine without GCC.  It only
# reports what it actually compiled.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="${1:-origin/master}"
DB="${OLDUVAI_COMPILE_DB:-${ROOT}/build/release}"
GXX="${OLDUVAI_GXX:-}"
if [ -z "${GXX}" ]; then
    for c in g++-16 g++-15 g++-14 g++-13 g++; do
        if command -v "$c" >/dev/null 2>&1; then GXX="$(command -v "$c")"; break; fi
    done
fi
[ -n "${GXX}" ] || { echo "check_gcc: SKIP — no g++ found"; exit 0; }
[ -f "${DB}/compile_commands.json" ] || {
    echo "check_gcc: SKIP — no compile database at ${DB}"; exit 0; }
git -C "${ROOT}" rev-parse --verify --quiet "${BASE}" >/dev/null || {
    echo "check_gcc: SKIP — no such ref: ${BASE}"; exit 0; }

# Committed since BASE, PLUS staged, PLUS unstaged.  The first version took
# only the committed diff and answered "no C++ changes" while the working tree
# held the very file being edited — a check that is silent exactly when you
# are still writing the thing it should catch.
# third_party/ is excluded, and not out of politeness: those sources are
# compiled with -w because they are not ours to lint, and RtMidi.cpp includes
# Apple's CoreMIDI headers, which use blocks (`^`) that GCC cannot parse at
# all — checking it fails with six syntax errors in a system header no matter
# what the patch says.  The check exists to find GCC-only warnings in OUR
# code; a vendored file it cannot compile is noise that would train someone
# to ignore a red lint.
FILES=$( { git -C "${ROOT}" diff --name-only "${BASE}"...HEAD -- '*.cpp' '*.hpp'
           git -C "${ROOT}" diff --name-only HEAD -- '*.cpp' '*.hpp'
           git -C "${ROOT}" diff --name-only --cached -- '*.cpp' '*.hpp'
         } | sort -u | grep -v '^third_party/' | sed 's|^|'"${ROOT}"'/|')
[ -n "${FILES}" ] || { echo "check_gcc: OK (no C++ changes vs ${BASE})"; exit 0; }

python3 - "${DB}/compile_commands.json" "${GXX}" ${FILES} <<'PY'
import json, os, shlex, subprocess, sys

db_path, gxx = sys.argv[1], sys.argv[2]
changed = {os.path.realpath(p) for p in sys.argv[3:]}
db = json.load(open(db_path))

# A changed HEADER has no entry of its own: check every TU that names it, so
# an include added to a header is still compiled by this pass.
def tus_for(path):
    if path.endswith('.cpp'):
        return [e for e in db if os.path.realpath(e['file']) == path]
    base = os.path.basename(path)
    out = []
    for e in db:
        try:
            if base in open(e['file'], errors='replace').read():
                out.append(e)
        except OSError:
            pass
    return out

seen, fails, checked = set(), 0, 0
for path in sorted(changed):
    for e in tus_for(path):
        f = os.path.realpath(e['file'])
        if f in seen:
            continue
        seen.add(f)
        args = e.get('arguments') or shlex.split(e['command'])
        out, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a in ('-o', '-MF', '-MT', '-MQ', '-arch', '-isysroot'):
                skip = True
                continue
            # clang-only spellings GCC rejects outright
            if a == '-c' or a.startswith(('-flto', '-mmacosx', '-fcolor')):
                continue
            out.append(a)
        r = subprocess.run([gxx, '-fsyntax-only', '-Wall', '-Wextra'] + out,
                           cwd=e['directory'], capture_output=True, text=True)
        checked += 1
        msgs = [l for l in r.stderr.splitlines() if ' error' in l or 'warning' in l]
        if r.returncode or msgs:
            fails += 1
            print('check_gcc: %s' % os.path.relpath(f))
            for m in msgs[:6]:
                print('    ' + m.strip()[:160])
print('check_gcc: %s — %d translation unit(s) checked with %s'
      % ('FAIL' if fails else 'OK', checked, os.path.basename(gxx)))
sys.exit(1 if fails else 0)
PY
