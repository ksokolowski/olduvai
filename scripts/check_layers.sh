#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Architecture layering lint: lower layers must never include from higher
# ones (docs/ARCHITECTURE.md — enforced by review only until 2026-07-20).
#
#   formats < prepare < core < systems < enhance < presentation < app
#
# Also enforces the SDL-free-by-construction claim: SDL headers may appear
# only under src/presentation/ and src/app/ (several presentation-layer
# files are deliberately SDL-free and link into olduvai_formats — that is a
# build grouping, not a layering statement, and stays legal here).
set -eu

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
    echo "check_layers: not a git repository" >&2
    exit 2
}

fail=0

rank() {
    case "$1" in
        formats)      echo 0;;
        prepare)      echo 1;;
        core)         echo 2;;
        systems)      echo 3;;
        enhance)      echo 4;;
        presentation) echo 5;;
        app)          echo 6;;
        *)            echo 99;;
    esac
}

# NOTE the '**' patterns: 'src/*/*.cpp' alone matches only ONE level, so the
# moment a layer grows subdirectories (presentation/ is 120 files and wants
# them) every moved file would silently stop being checked while this script
# still printed OK.  Keep both forms.
for f in $(git ls-files 'src/*/*.cpp' 'src/*/*.hpp' 'src/*/**/*.cpp' 'src/*/**/*.hpp' | sort -u); do
    layer=$(echo "$f" | cut -d/ -f2)
    r=$(rank "$layer")
    incs=$(grep -oE '#include "(formats|prepare|core|systems|enhance|presentation|app)/' "$f" \
           | sed 's|#include "||; s|/||' | sort -u)
    for inc in $incs; do
        ir=$(rank "$inc")
        if [ "$ir" -gt "$r" ]; then
            echo "check_layers: $f (layer '$layer') includes from HIGHER layer '$inc'" >&2
            fail=1
        fi
    done
done

# SDL containment (src/app is SDL-guarded by OLDUVAI_HAVE_SDL — allowed).
sdl_hits=$(git grep -lE '#include [<"]SDL' -- \
    'src/formats' 'src/prepare' 'src/core' 'src/systems' 'src/enhance' \
    2>/dev/null || true)
if [ -n "${sdl_hits}" ]; then
    echo "check_layers: SDL include in an SDL-free layer:" >&2
    echo "${sdl_hits}" >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "check_layers: OK"
fi
exit "$fail"
