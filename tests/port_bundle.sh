#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The KNULLI port bundles: the two exFAT hazards, the content-policy guard,
# and the PUBLIC bundle's promise that it carries nothing of the player's.
#
# WHY THIS EXISTS.  The destination filesystem is exFAT, which cannot store
# symbolic links AND is case-insensitive.  The owner's ROM directory trips both:
# CM32L_PCM.ROM is a symlink to cm32l_pcm.rom, so a naive `cp -r` lands a broken
# link, while a naive dereferencing copy of the whole directory lands two names
# that are the SAME file on the destination and one silently overwrites the
# other.  Either failure produces an MT-32 backend that reports itself
# unavailable with nothing visibly wrong — the class of silent failure this
# project keeps paying for, and the reason these are assertions rather than a
# comment in the packaging script.
#
# The other two checks: the script must refuse to write a bundle inside the repo
# (it contains game files, and CONTRIBUTING.md's allowlist is absolute), and the
# shipped README must name BOTH executable forms, because GOG and other CD-era
# releases have no HISTORIK.EXE and a reader told otherwise concludes their copy
# is broken.
#
# Needs no game files: the fixtures below are built from scratch.  It does need
# a filesystem that can EXPRESS the hazard, which is the one thing "runs
# anywhere and must never skip" got wrong — see the probe below.
set -eu
SCRIPT="$1"          # packaging/build_port_knulli.sh
REPO="$2"            # CMAKE_SOURCE_DIR
# An explicit template, so TMPDIR is honoured: macOS `mktemp -d` with no
# template ignores TMPDIR and uses the per-user folder, which is
# case-insensitive — pointing TMPDIR at a case-sensitive volume is how this
# test runs on a Mac instead of skipping (see the SKIP message below).
T=$(mktemp -d "${TMPDIR:-/tmp}/port_bundle.XXXXXX"); trap 'rm -rf "$T"' EXIT

mkdir -p "$T/roms" "$T/game" "$T/out"

# ── The fixture needs the very properties it exists to test ─────────────────
# The source directory this models is a case-SENSITIVE one holding SYMLINKS —
# the owner's Linux/macOS ROM dir, where CM32L_PCM.ROM points at cm32l_pcm.rom.
# On a filesystem without those properties the fixture cannot be built at all:
# `ln -s` fails outright, and cm32l_pcm.rom / CM32L_PCM.ROM are ONE path, so
# the second create returns "Already exists".  Verified on a vfat loopback:
#     ln: Permission denied      (no symlinks)
#     ln: Already exists         (case-folded collision)
#
# This is not a defect in the packaging script; the SCENARIO is unreachable
# there.  The script is a Linux/macOS developer tool and is never run on
# Windows.  Skipping is the honest answer, and the absence of this probe is
# what kept the gitea `windows` job red for 13 commits: registered with no
# SKIP_RETURN_CODE and `set -e`, the first failing `ln` failed the job.
#
# Exit 78, not the usual 77: that code is reserved for "needs the owner's game
# files" and CMakeLists derives the `assets` label from it.  This test needs no
# game files at all.
if ! ln -s probe_target "$T/probe_link" 2>/dev/null; then
    echo "port_bundle: SKIP — this filesystem cannot store symbolic links," >&2
    echo "  so the source directory the exFAT hazard is about cannot be built." >&2
    exit 78
fi
rm -f "$T/probe_link"
printf 'a' > "$T/CaseProbe"
if [ -e "$T/caseprobe" ]; then
    rm -f "$T/CaseProbe"
    echo "port_bundle: SKIP — this filesystem is case-INSENSITIVE, so the" >&2
    echo "  colliding pair (cm32l_pcm.rom / CM32L_PCM.ROM) cannot both exist." >&2
    echo "  (macOS: set TMPDIR to a case-sensitive APFS volume to run it.)" >&2
    exit 78
fi
rm -f "$T/CaseProbe"
printf 'ctl'   > "$T/roms/cm32l_ctrl_1_02.rom"
printf 'pcm'   > "$T/roms/cm32l_pcm.rom"
ln -s cm32l_ctrl_1_02.rom "$T/roms/CM32L_CONTROL.ROM"
ln -s cm32l_pcm.rom       "$T/roms/CM32L_PCM.ROM"
printf 'ctl32' > "$T/roms/MT32_CONTROL.ROM"
printf 'pcm32' > "$T/roms/mt32_pcm.rom"
# Decoys the script must NOT ship — the real directory holds ~25 of these.
printf 'x' > "$T/roms/mt32_ctrl_1_06.rom"
printf 'x' > "$T/roms/legacy.zip"
for f in FILESA.CUR FILESB.CUR FILESA.VGA FILESB.VGA PREH.SQZ; do
    printf 'x' > "$T/game/$f"
done
printf 'bin' > "$T/fakebin"
printf 'sf2' > "$T/font.sf2"

bash "$SCRIPT" --binary "$T/fakebin" --game-dir "$T/game" \
             --rom-dir "$T/roms" --soundfont "$T/font.sf2" --out "$T/out" >/dev/null

fail=0
R="$T/out/olduvai/mt32-roms"

# 1. No symlinks survive: exFAT cannot store them.
if [ -n "$(find "$T/out" -type l 2>/dev/null)" ]; then
    echo "FAIL: symlinks in the bundle"; fail=1
fi

# 2. The four canonical ROMs are present as regular files with real content.
# Names are NORMALISED to canonical upper case on the way in.  The source may
# hold any mixture (the owner's MT-32 PCM dump is lowercase); the engine matches
# case-insensitively either way, but writing one fixed case removes any question
# about what lands on a case-insensitive destination.
for n in CM32L_CONTROL.ROM CM32L_PCM.ROM MT32_CONTROL.ROM MT32_PCM.ROM; do
    if [ ! -f "$R/$n" ] || [ ! -s "$R/$n" ]; then
        echo "FAIL: missing or empty $n"; fail=1
    fi
done

# 3. No case-colliding pair: on exFAT those are ONE file and one would be lost.
dupes=$(find "$R" -type f 2>/dev/null | sed 's|.*/||' | tr 'A-Z' 'a-z' | sort | uniq -d)
if [ -n "$dupes" ]; then
    echo "FAIL: names collide case-insensitively: $dupes"; fail=1
fi

# 4. Exactly the four ROMs — not the versioned dumps or the archives beside them.
n=$(find "$R" -type f 2>/dev/null | wc -l)
if [ "$n" -ne 4 ]; then echo "FAIL: expected 4 ROM files, got $n"; fail=1; fi

# 5. Content policy: refuse to write a bundle inside the repo.
if bash "$SCRIPT" --binary "$T/fakebin" --game-dir "$T/game" --rom-dir "$T/roms" \
                --soundfont "$T/font.sf2" --out "$REPO/tmp_bundle" >/dev/null 2>&1; then
    echo "FAIL: wrote a bundle inside the repo"; rm -rf "$REPO/tmp_bundle"; fail=1
fi

# 6. The README names BOTH executable forms.
if ! grep -q "PREH.SQZ" "$T/out/olduvai/README.txt" 2>/dev/null; then
    echo "FAIL: README omits PREH.SQZ"; fail=1
fi
if ! grep -q "HISTORIK.EXE" "$T/out/olduvai/README.txt" 2>/dev/null; then
    echo "FAIL: README omits HISTORIK.EXE"; fail=1
fi

# 7. The launcher sets the device's video driver.  KNULLI's SDL2 has no KMSDRM;
#    without SDL_VIDEODRIVER=mali nothing reaches the panel.
if ! grep -q "SDL_VIDEODRIVER=mali" "$T/out/Olduvai.sh" 2>/dev/null; then
    echo "FAIL: launcher does not set SDL_VIDEODRIVER=mali"; fail=1
fi

# 8. The launcher states DEVICE DEFAULTS, below the player's play.json.  It
#    used to pass --profile hd plus display flags, which beat play.json, so
#    Classic chosen in the menu came back Enhanced at every launch.
if ! grep -q -- "--default-profile hd-handheld" "$T/out/Olduvai.sh" 2>/dev/null; then
    echo "FAIL: launcher does not pass --default-profile hd-handheld"; fail=1
fi
if grep -qE -- "--profile |--render-scale|--hd-profile|--aspect" "$T/out/Olduvai.sh" 2>/dev/null; then
    echo "FAIL: launcher still overrides the player (--profile / display flags)"; fail=1
fi

# 9. The PUBLIC bundle — the release artifact — carries none of the builder's
#    files: no game data, no ROMs, no SoundFont.  It ships the licence texts
#    and never a gamelist.xml that would overwrite the player's.
P="$T/pub"
bash "$SCRIPT" --public --device trimui --binary "$T/fakebin" --out "$P" >/dev/null
for d in game mt32-roms soundfonts; do
    extra=$(find "$P/olduvai/$d" -type f ! -name PUT-FILES-HERE.txt 2>/dev/null)
    if [ -n "$extra" ]; then echo "FAIL: public bundle ships files in $d: $extra"; fail=1; fi
done
if [ -n "$(find "$P" -type f \( -iname '*.cur' -o -iname '*.vga' -o -iname '*.rom' \
            -o -iname '*.sf2' -o -iname 'historik.exe' -o -iname 'preh.sqz' \) 2>/dev/null)" ]; then
    echo "FAIL: public bundle contains game data, ROMs or a SoundFont"; fail=1
fi
if [ -e "$P/gamelist.xml" ]; then
    echo "FAIL: public bundle ships a gamelist.xml (would overwrite the player's)"; fail=1
fi
for f in olduvai/LICENSE.txt olduvai/licenses/THIRD-PARTY-NOTICES.md \
         olduvai/licenses/Nuked-OPL3-LICENSE.txt olduvai/gamelist-entry.xml; do
    if [ ! -s "$P/$f" ]; then echo "FAIL: public bundle lacks $f"; fail=1; fi
done

# 10. The A12 launcher is not the TrimUI's: no mali (its SDL2 has none — that
#     would be a black screen), and the pipewire XDG_RUNTIME_DIR default.
A="$T/a12"
bash "$SCRIPT" --public --device a12 --binary "$T/fakebin" --out "$A" >/dev/null
if grep -v '^[[:space:]]*#' "$A/Olduvai.sh" | grep -q "SDL_VIDEODRIVER=mali"; then
    echo "FAIL: A12 launcher sets SDL_VIDEODRIVER=mali"; fail=1
fi
if ! grep -q "XDG_RUNTIME_DIR" "$A/Olduvai.sh"; then
    echo "FAIL: A12 launcher lacks the XDG_RUNTIME_DIR default"; fail=1
fi
# The A12 starts in Classic: Enhanced there runs below the original's 18.2 Hz.
if ! grep -q -- "--default-profile dos-handheld" "$A/Olduvai.sh"; then
    echo "FAIL: A12 launcher does not pass --default-profile dos-handheld"; fail=1
fi
if ! grep -q "Powkiddy A12" "$A/olduvai/README.txt"; then
    echo "FAIL: A12 README does not name the device"; fail=1
fi

[ "$fail" -eq 0 ] && echo "port_bundle: OK"
exit "$fail"
