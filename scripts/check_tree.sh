#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Content-policy lint: fail if denylisted content is tracked in the tree.
# See CONTRIBUTING.md "Content policy".  Run from the repo root; CI runs
# it on every push, and the publish flow runs it as a hard gate.
set -eu

# Every check below pipes `git ls-files`/`git grep`; outside a git repo
# those fail silently in the pipeline and every check passes vacuously.
# Refuse to "pass" in that state (the publish flow runs this inside a
# freshly-extracted export — it must `git init && git add -A` first).
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
    echo "check_tree: not a git repository — refusing a vacuous pass" >&2
    echo "check_tree: (in an extracted export: git init -q && git add -A first)" >&2
    exit 2
}

fail=0

# 1. Game files / ROMs — never tracked, under any path.
pattern='\.(cur|vga|voc|mdi|mat|pc1|dur)$|HISTORIK\.EXE|MT32_(CONTROL|PCM)\.ROM|CM32L'
if git ls-files | grep -iE "$pattern"; then
    echo "check_tree: game files / ROMs must never be committed." >&2
    fail=1
fi

# 2. Verbatim-class data tables (produced at first run, never shipped).
if git ls-files | grep -iE '(tile_tables|objects|sprite_defs)\.json$'; then
    echo "check_tree: verbatim-class data tables must never be committed." >&2
    fail=1
fi

# 3. Screenshots (no game imagery in the repo, README included).
# assets/ holds the ONLY graphics: the project's own marks (bone logo,
# fire-styled wordmark, per-platform icons) — original authored art, never
# game-derived.  Provenance + regeneration: assets/README.md.
if git ls-files | grep -iE '\.(png|jpg|jpeg|gif|bmp|webp|icns|ico|ttf|otf|woff2?)$' \
        | grep -vE '^assets/(icon|logo|fonts)/[^/]+\.(png|icns|ico|ttf)$'; then
    echo "check_tree: image files are not allowed (no game screenshots)." >&2
    fail=1
fi

# 4. AI-attribution lines inside tracked files.
if git grep -ilE 'Co-Authored-By: .*Claude|Generated with.*Claude' -- . ':!scripts/' >/dev/null 2>&1; then
    echo "check_tree: AI-attribution text found in tracked files:" >&2
    git grep -ilE 'Co-Authored-By: .*Claude|Generated with.*Claude' -- . ':!scripts/' >&2
    fail=1
fi

# 5. License headers — every project source file carries the SPDX tag +
#    copyright line (files travel: each must be self-describing; also the
#    per-file assertion of the sole copyright holder).  Vendored third_party/
#    keeps its upstream headers.
missing=$(git ls-files '*.cpp' '*.hpp' '*.c' '*.h' '*.sh' '*.py' '*.cmake' \
                       'CMakeLists.txt' 'scripts/hooks/*' \
          | grep -v '^third_party/' \
          | while read -r f; do
              grep -q 'SPDX-License-Identifier:' "$f" || echo "$f"
            done)
if [ -n "$missing" ]; then
    echo "check_tree: files missing the SPDX license header:" >&2
    echo "$missing" >&2
    fail=1
fi

# Unicode homoglyphs: characters that LOOK like ASCII but are not.  U+2212
# MINUS SIGN is indistinguishable from '-' and U+00D7 MULTIPLICATION SIGN from
# 'x' at a glance, so one copied out of a comment into an expression is a
# baffling compile error, and one copied into a string literal ships the wrong
# glyph silently.  They also render badly in the gitea/GitHub diff views.
#
# Scoped to LIVE code: third_party is upstream, and docs/internal/archive is
# dated snapshots that the planning policy says are never rewritten.
#
# Deliberately NARROW: this is not a no-Unicode rule.  The copyright headers
# carry a Polish name, the UI strings use real typography, and the comment
# separators are house style.  Only the confusable-with-ASCII pair is banned.
# Built with printf so this script does not itself contain the characters it
# bans (which would make the rule flag its own source).
mult=$(printf '\303\227')      # U+00D7 MULTIPLICATION SIGN
minus=$(printf '\342\210\222')  # U+2212 MINUS SIGN
homoglyphs=$(git grep -l -e "$mult" -e "$minus" -- \
                 '*.cpp' '*.hpp' '*.h' '*.sh' '*.cmake' '*.py' '*.cmd' \
             | grep -v '^third_party/' \
             | grep -v '^docs/internal/archive/' || true)
if [ -n "$homoglyphs" ]; then
    echo "check_tree: Unicode homoglyph (U+00D7 'x' / U+2212 '-') in:" >&2
    echo "$homoglyphs" >&2
    echo "  use plain ASCII 'x' and '-' in code and comments." >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "check_tree: OK"
fi
exit "$fail"
