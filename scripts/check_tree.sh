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

# 3. Images and video live in named directories and nowhere else.
# assets/icon|logo|fonts are the project's own marks — original authored art.
# assets/screenshots is a SMALL CURATED set of the engine's own output: stills
# (owner ruling 2026-09-08) and short silent clips (2026-09-13) — see
# CONTRIBUTING.md, LEGAL.md and assets/screenshots/README.md.  Checked here by
# LOCATION, COUNT, SIZE and one content property (clips carry no audio: the
# game's music and sound are not ours).  Nothing here can tell a frame from a
# photograph, so the rest of the curation is a human job.
media_re='\.(png|jpg|jpeg|gif|bmp|webp|icns|ico|ttf|otf|woff2?|mp4|m4v|mov|webm|mkv|avi)$'
if git ls-files | grep -iE "$media_re" \
        | grep -vE '^assets/(icon|logo|fonts)/[^/]+\.(png|icns|ico|ttf)$' \
        | grep -vE '^assets/screenshots/[a-z0-9-]+\.(png|jpg|mp4)$'; then
    echo "check_tree: images/video only under assets/{icon,logo,fonts,screenshots}" \
         "(screenshots: lower-case-hyphen names, .png/.jpg/.mp4)." >&2
    fail=1
fi
shots=$(git ls-files 'assets/screenshots/*.png' 'assets/screenshots/*.jpg' \
                     'assets/screenshots/*.mp4')
n_shots=$(printf '%s\n' "$shots" | grep -c . || true)
if [ "$n_shots" -gt 16 ]; then
    echo "check_tree: assets/screenshots holds $n_shots files — the curated set" \
         "is capped at 16." >&2
    fail=1
fi
for f in $shots; do
    [ -f "$f" ] || continue
    size=$(wc -c < "$f" | tr -d ' ')
    if [ "$size" -gt 2097152 ]; then
        echo "check_tree: $f is $size bytes — curated media are capped at 2 MiB." >&2
        fail=1
    fi
    case "$f" in
        *.mp4)
            # An MP4 audio track declares a handler box of type 'soun'; with
            # the zero bytes between them removed it reads "hdlrsoun".
            # Verified against ffprobe (silent clip: 0, audio muxed in: 1,
            # index at either end of the file).
            if LC_ALL=C tr -d '\000' < "$f" | LC_ALL=C grep -aq 'hdlrsoun'; then
                echo "check_tree: $f has an audio track — curated clips must be silent." >&2
                fail=1
            fi ;;
    esac
done

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

# 6. Disassembly text in comments.  CONTRIBUTING's content policy: cite an
#    OFFSET and say in prose what happens there — never the instruction
#    itself, "not even in comments".  Seven quoted instructions had crept in
#    (two in one day, 2026-09-21) because nothing checked.  Narrow on purpose:
#    an x86 mnemonic WITH operands inside backticks, or a `word/byte ptr`
#    size operator anywhere — prose like "the jump table" or "cmp" alone
#    stays legal.  (`int` is left out: it matches C++ declarations.)
mnem='(mov|movzx|movsx|xor|and|or|add|sub|adc|sbb|cmp|test|inc|dec|neg|not|shl|shr|sar|sal|rol|ror|imul|mul|idiv|div|lea|les|lds|push|pop|call|lcall|ret|retf|jmp|ljmp|j[a-z]{1,3}|loop|rep[a-z]*|stos[bw]?|lods[bw]?|movs[bw]?|xchg|cbw|cwd)'
disasm=$(git grep -nE "\`$mnem +[^\`]+\`|\b(word|byte|dword) ptr\b" -- \
         'src/*.cpp' 'src/*.hpp' 'src/*.h' 'tests/*.cpp' 'tests/*.hpp' || true)
if [ -n "$disasm" ]; then
    echo "check_tree: disassembly text in a comment (cite the offset, describe it in prose):" >&2
    echo "$disasm" >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "check_tree: OK"
fi
exit "$fail"
