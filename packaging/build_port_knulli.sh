#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Assemble a KNULLI port bundle — a directory you copy to /userdata/roms/ports
# on the device's SD card.  Builds no image and flashes nothing.  Two devices,
# both tested on the owner's hardware:
#
#   --device trimui   TrimUI Smart Pro   aarch64  1280x720  SDL video: mali
#   --device a12      Powkiddy A12       armhf    1024x600  SDL video: default
#
# Two kinds of bundle:
#
#   PRIVATE (default)  for your own device: copies your game files, MT-32 ROMs
#                      and a SoundFont in (--game-dir, --rom-dir, --soundfont).
#   --public           the release artifact: game/ and mt32-roms/ are EMPTY,
#                      no SoundFont, and the licence texts travel with the
#                      binary.  The player copies their own files in.
#
# SHIPS NO GAME CONTENT FROM THIS REPO.  Game files and ROMs come only from the
# paths given as arguments, outside the tree, and the script REFUSES to write a
# bundle inside the repo — a private bundle contains game files, and
# CONTRIBUTING.md's allowlist is absolute.  A public bundle carries no
# screenshot either: LEGAL.md keeps the curated ones out of every release.
#
# THE DESTINATION IS exFAT, and it fails in two ways that look like nothing:
#
#   1. It cannot store SYMBOLIC LINKS.  The owner's ROM directory reaches the
#      canonical names through symlinks (CM32L_PCM.ROM -> cm32l_pcm.rom), so a
#      plain `cp -r` lands broken links.
#   2. It is CASE-INSENSITIVE.  So dereferencing the whole directory would write
#      both CM32L_PCM.ROM and cm32l_pcm.rom, which are ONE file there, and one
#      silently overwrites the other.
#
# Either way MT-32 reports itself unavailable with nothing visibly wrong.  Hence:
# copy an EXPLICIT list, always dereferenced, normalised to one fixed case.
# tests/port_bundle.sh asserts all of that.
set -euo pipefail

DEVICE="trimui" PUBLIC=0
BINARY="" GAME_DIR="" ROM_DIR="" SOUNDFONT="" OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --device)    DEVICE="$2"; shift 2 ;;
        --public)    PUBLIC=1; shift ;;
        --binary)    BINARY="$2"; shift 2 ;;
        --game-dir)  GAME_DIR="$2"; shift 2 ;;
        --rom-dir)   ROM_DIR="$2"; shift 2 ;;
        --soundfont) SOUNDFONT="$2"; shift 2 ;;
        --out)       OUT="$2"; shift 2 ;;
        -h|--help)
            sed -n '4,37p' "$0" | sed 's/^# \{0,1\}//'
            echo
            echo "usage: $0 [--device trimui|a12] --binary P --out O"
            echo "          (--public | --game-dir D --rom-dir R --soundfont F)"
            exit 0 ;;
        *) echo "build_port_knulli: unknown option '$1'" >&2; exit 2 ;;
    esac
done
# The device's starting style — a DEFAULT under the player's play.json, so a
# menu choice still sticks.  A12: Classic, because Enhanced there runs ~15 Hz
# logic against the original's 18.2 (measured 2026-09-13; Classic 18.25 Hz,
# zero overruns).  Enhanced stays one menu choice away (owner: acceptable).
case "$DEVICE" in
    trimui) DEVICE_NAME="TrimUI Smart Pro"; DEFAULT_PROFILE="hd-handheld" ;;
    a12)    DEVICE_NAME="Powkiddy A12";     DEFAULT_PROFILE="dos-handheld" ;;
    *) echo "build_port_knulli: --device is trimui or a12" >&2; exit 2 ;;
esac
required="BINARY OUT"
[ "$PUBLIC" -eq 1 ] || required="$required GAME_DIR ROM_DIR SOUNDFONT"
for v in $required; do
    if [ -z "${!v}" ]; then
        echo "build_port_knulli: --$(echo "$v" | tr 'A-Z_' 'a-z-') is required" >&2
        exit 2
    fi
done

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
mkdir -p "$OUT"
OUT_ABS=$(cd "$OUT" && pwd -P)
case "${OUT_ABS}/" in
    "${REPO_ROOT}/"*)
        echo "build_port_knulli: refusing to write a bundle inside the repo." >&2
        echo "  A private bundle contains game files; the repo must never hold them." >&2
        echo "  repo: ${REPO_ROOT}" >&2
        echo "  out:  ${OUT_ABS}" >&2
        exit 1 ;;
esac

PORT="${OUT_ABS}/olduvai"
mkdir -p "${PORT}/fonts" "${PORT}/mt32-roms" "${PORT}/soundfonts" "${PORT}/game"

# ── the binary ──────────────────────────────────────────────────────────────
cp -L "$BINARY" "${PORT}/olduvai"
chmod +x "${PORT}/olduvai"

# ── fonts, with their licences (the OFL requires them to travel together) ────
for f in "${REPO_ROOT}"/assets/fonts/*.ttf "${REPO_ROOT}"/assets/fonts/*LICENSE*; do
    [ -e "$f" ] && cp -L "$f" "${PORT}/fonts/"
done

# ── licence texts: the same set the desktop packages carry ──────────────────
# (package_windows.sh / make_dmg_macos.sh).  SDL2 is the device's own, loaded
# dynamically, so no SDL text is owed; THIRD-PARTY-NOTICES.md lists it anyway.
mkdir -p "${PORT}/licenses"
cp "${REPO_ROOT}/LICENSE" "${PORT}/LICENSE.txt"
cp "${REPO_ROOT}/THIRD-PARTY-NOTICES.md" "${PORT}/licenses/"
cp "${REPO_ROOT}/third_party/nuked_opl3/LICENSE" "${PORT}/licenses/Nuked-OPL3-LICENSE.txt"
cp "${REPO_ROOT}/third_party/mt32emu/COPYING.LESSER.txt" \
   "${PORT}/licenses/libmt32emu-LICENSE.txt"
cp "${REPO_ROOT}/third_party/rtmidi/LICENSE" "${PORT}/licenses/RtMidi-LICENSE.txt"

if [ "$PUBLIC" -eq 0 ]; then
    # ── MT-32 ROMs: an EXPLICIT list, dereferenced, normalised to upper case ──
    # Resolve each canonical name case-insensitively in the source, because real
    # collections mix cases (a legacy dump beside a MAME-versioned one).  Copy at
    # most these four.  Never `cp -r` this directory: see the header.
    copy_rom() {
        local want="$1" found
        found=$(find "$ROM_DIR" -maxdepth 1 -iname "$want" -print -quit 2>/dev/null || true)
        if [ -n "$found" ] && [ -s "$found" ]; then
            cp -L "$found" "${PORT}/mt32-roms/${want}"
            return 0
        fi
        return 1
    }
    roms=0
    for want in CM32L_CONTROL.ROM CM32L_PCM.ROM MT32_CONTROL.ROM MT32_PCM.ROM; do
        if copy_rom "$want"; then roms=$((roms + 1)); fi
    done
    if [ "$roms" -eq 0 ]; then
        echo "build_port_knulli: WARNING — no MT-32 ROM pair found in ${ROM_DIR};" >&2
        echo "  the mt32-builtin backend will be unavailable on the device." >&2
    fi

    # ── SoundFont ──────────────────────────────────────────────────────────
    cp -L "$SOUNDFONT" "${PORT}/soundfonts/"

    # ── game files: the four archives plus EITHER executable form ──────────
    missing=0
    for want in FILESA.CUR FILESB.CUR FILESA.VGA FILESB.VGA; do
        found=$(find "$GAME_DIR" -maxdepth 1 -iname "$want" -print -quit 2>/dev/null || true)
        if [ -n "$found" ]; then cp -L "$found" "${PORT}/game/${want}"
        else echo "build_port_knulli: missing ${want} in ${GAME_DIR}" >&2; missing=1; fi
    done
    # GOG and other CD-era releases ship the compressed PREH.SQZ instead of
    # HISTORIK.EXE, and the engine reads it directly.  Accept whichever is present.
    exe_found=0
    for want in HISTORIK.EXE PREH.SQZ; do
        found=$(find "$GAME_DIR" -maxdepth 1 -iname "$want" -print -quit 2>/dev/null || true)
        if [ -n "$found" ]; then cp -L "$found" "${PORT}/game/${want}"; exe_found=1; fi
    done
    if [ "$exe_found" -eq 0 ]; then
        echo "build_port_knulli: missing HISTORIK.EXE (or PREH.SQZ) in ${GAME_DIR}" >&2
        missing=1
    fi
    [ "$missing" -eq 0 ] || exit 1
fi
# Empty directories do not survive every archiver; a note keeps each one.
for d in game mt32-roms soundfonts; do
    [ -n "$(ls -A "${PORT}/${d}")" ] || cat > "${PORT}/${d}/PUT-FILES-HERE.txt" <<NOTE
See ../README.txt, section "${d}".
NOTE
done

# ── the launcher ────────────────────────────────────────────────────────────
if [ "$DEVICE" = trimui ]; then
VIDEO_BLOCK=$(cat <<'VIDEO'
# KNULLI's SDL2 on this device has NO KMSDRM and there is no libdrm/libgbm.
# Its video driver is `mali`, over libEGL/libGLESv2 — the same path
# EmulationStation uses.  Without this, nothing reaches the panel.
export SDL_VIDEODRIVER=mali
VIDEO
)
else
VIDEO_BLOCK=$(cat <<'VIDEO'
# This device's SDL2 has NO `mali` driver (only kmsdrm), so the TrimUI's
# SDL_VIDEODRIVER=mali would be wrong here; SDL's own default reaches the
# Mali-400 through EGL.
#
# Audio: the ALSA default PCM goes through the pipewire plugin, which finds its
# server via XDG_RUNTIME_DIR.  EmulationStation sets it for its children;
# exporting a default keeps a run from SSH sounding too ("Host is down").
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
VIDEO
)
fi
{
cat <<'LAUNCH'
#!/bin/bash
# Olduvai on KNULLI — EmulationStation runs this from the Ports menu.
LAUNCH
echo "# Device: ${DEVICE_NAME}.  Generated by packaging/build_port_knulli.sh."
cat <<'LAUNCH'
DIR="$(dirname "$0")/olduvai"
cd "$DIR" || exit 1
mkdir -p /userdata/system/logs

LAUNCH
echo "$VIDEO_BLOCK"
cat <<'LAUNCH'

# Do NOT set SDL_GAMECONTROLLERCONFIG.  KNULLI's launcher already injected it
# from the player's pad configuration, and overriding it loses that mapping.

# Frame-stats prints per-level frame budget accounting to the log on exit. It
# costs a counter per frame and is how a report tells "the port is slow" from
# "the upscaler is slow".
export OLDUVAI_FRAME_STATS=1

# Optional local overrides (debug env vars) — absent in a release bundle.
[ -f ./olduvai.env ] && . ./olduvai.env

# DEVICE DEFAULTS, not overrides.  The handheld profiles carry the values
# measured on these devices and sit BELOW the player's play.json, so
# Classic/Enhanced and Video changes made in the menu stick across launches.
LAUNCH
echo "set -- --game-dir ./game --rom-dir ./mt32-roms --default-profile ${DEFAULT_PROFILE}"
cat <<'LAUNCH'

# A SoundFont dropped into soundfonts/ must be handed over by path: on Linux
# the engine searches only ~/.config/olduvai/soundfonts and /usr/share, never
# beside the binary.  The auto music chain then reads MT-32 ROMs first, this
# SoundFont second (General MIDI, needs the firmware's libfluidsynth), the
# Sound Blaster sound (OPL FM music, digital effects) last.
for sf in ./soundfonts/*.sf2; do
    [ -f "$sf" ] && set -- "$@" --soundfont "$sf"
    break
done

exec ./olduvai "$@" > /userdata/system/logs/olduvai.log 2>&1
LAUNCH
} > "${OUT_ABS}/Olduvai.sh"
chmod +x "${OUT_ABS}/Olduvai.sh"

# ── the README the installer actually reads ─────────────────────────────────
if [ "$PUBLIC" -eq 1 ]; then
    MUSIC_DEFAULT="By default you get the Sound Blaster sound of 1991 - FM music (the AdLib
chip) with digital sound effects - which needs nothing.
If you own Roland MT-32 or CM-32L ROM images, copy them into  olduvai/mt32-roms/
(CM32L_CONTROL.ROM + CM32L_PCM.ROM, and/or MT32_CONTROL.ROM + MT32_PCM.ROM) and
the Roland sound is used instead.  For General MIDI, copy a SoundFont (.sf2)
into  olduvai/soundfonts/ : it is used when no Roland ROMs are present."
    NOTHING_ELSE="Nothing else is required."
else
    MUSIC_DEFAULT="By default you get the Roland CM-32L, emulated from the ROMs in this bundle."
    NOTHING_ELSE="Nothing else needs moving.  Fonts, the MT-32 ROMs and the SoundFont are already
in this bundle."
fi
cat > "${PORT}/README.txt" <<README
Olduvai on the ${DEVICE_NAME} (KNULLI)
$(printf '%*s' "$(( ${#DEVICE_NAME} + 24 ))" '' | tr ' ' '=')

Olduvai is a native recreation of the ENGINE of Prehistorik (1991).  It ships no
game content and reads your own game files.  Get the game from GOG.com
("Prehistorik 1+2") or use your original DOS copy.


1. WHERE THE PORT GOES
----------------------
Copy "Olduvai.sh" and the "olduvai" folder into:

    /userdata/roms/ports/

That is the "share" partition — the large one.  KNULLI formats it as exFAT, so
you can read it directly from Windows or macOS with the card in a reader.  The
alternative is KNULLI's network transfer over Wi-Fi, which needs no reader but
is slower.


2. game  --  WHERE THE GAME FILES GO (the only step you must do)
----------------------------------------------------------------
Put your own copies of these into  olduvai/game/ :

    FILESA.CUR
    FILESB.CUR
    FILESA.VGA
    FILESB.VGA
    and EITHER  HISTORIK.EXE  OR  PREH.SQZ

GOG and other CD-era releases have no HISTORIK.EXE — they ship the executable in
its compressed form, PREH.SQZ, and Olduvai reads that directly.  If that is what
your copy contains, it is correct; nothing is missing.

${NOTHING_ELSE}


3. HOW TO START IT
------------------
Refresh the game list (or restart EmulationStation), then launch "Olduvai" from
the PORTS menu.  Start opens the menu; Options -> Style switches between
Classic DOS and Enhanced HD, and the choice is remembered.


4. IF IT DOES NOT START
-----------------------
Read the log:

    /userdata/system/logs/olduvai.log

The first line names the exact build ("olduvai 0.9.x (<id>, <date>)") — quote it
in a bug report.  After that, two kinds of failure look very different:

  * a video problem names SDL and the video driver.
  * a missing game file names the directory it searched and the files it wanted.


5. mt32-roms / soundfonts  --  MUSIC
------------------------------------
${MUSIC_DEFAULT}

    --music-device mt32-builtin   Roland MT-32 / CM-32L  (needs the ROMs)
    --mt32-model mt32             ... force the MT-32 instead of the CM-32L
    --music-device gm-builtin     General MIDI (needs a SoundFont)
    --music-device opl            FM music (AdLib / Sound Blaster)
    --sfx-backend opl             FM effects too - an AdLib-only card
    --music-device none           silence

Edit Olduvai.sh to add a flag.  Full details are in the project's docs/AUDIO.md.


6. LICENCE
----------
Olduvai is free software, GPL-3.0-or-later (LICENSE.txt).  Third-party
components and their licences are in licenses/ and fonts/.  Source code:
https://github.com/ksokolowski/olduvai
README

# ── EmulationStation metadata: name, description, icon ──────────────────────
# ES reads roms/ports/gamelist.xml.  Without an entry the Ports menu shows the
# bare script filename and no artwork.
#
# PRIVATE bundles ship a gamelist.xml carrying only our entry — fine on the
# owner's card, whose Ports list the owner controls.  A PUBLIC bundle must not:
# a player who copies it over their existing gamelist.xml loses every other
# port's name, artwork and play statistics.  So the public bundle carries the
# entry as a snippet to paste, plus the icon, and the port works without it.
#
# The image is OUR OWN artwork from assets/logo, never a screenshot: LEGAL.md
# keeps the curated screenshots out of every release.
#
# <name> is "Olduvai" — the port is named for the ENGINE, never for the game
# (owner, 2026-09-14: not "Prehistorik (Olduvai engine)"; the game's name is a
# mark we do not own and stays in descriptions).  <desc> sells the
# modes, not the paperwork — the bring-your-own-files note lives in README.txt
# and on the missing-files screen, where it is actionable (owner, 2026-09-13).
#
# <releasedate> is the ORIGINAL GAME's year, 1991, the convention every other
# port in the Ports menu follows.  <developer> stays OURS: naming the original
# publisher would say they made this port, which they did not.
mkdir -p "${OUT_ABS}/images"
cp -L "${REPO_ROOT}/assets/logo/bone-256.png" "${OUT_ABS}/images/olduvai.png"
ENTRY='	<game>
		<path>./Olduvai.sh</path>
		<name>Olduvai</name>
		<desc>The 1991 caveman platformer on Olduvai, a native recreation of its engine. Classic DOS plays it exactly as it was; Enhanced HD adds smooth motion, widescreen margins and sharp upscaled graphics. Switch any time under Options - Style.</desc>
		<image>./images/olduvai.png</image>
		<releasedate>19910101T000000</releasedate>
		<developer>Krzysztof Sokolowski</developer>
		<genre>Platform</genre>
		<players>1</players>
	</game>'
if [ "$PUBLIC" -eq 1 ]; then
    cat > "${PORT}/gamelist-entry.xml" <<SNIPPET
<!-- Optional: gives Olduvai a name, description and icon in the Ports menu.
     Paste the <game> block below into /userdata/roms/ports/gamelist.xml,
     inside <gameList>, with EmulationStation stopped (it rewrites the file on
     exit).  If you have no gamelist.xml there yet, save this whole file as
     /userdata/roms/ports/gamelist.xml instead.  Copy images/olduvai.png into
     /userdata/roms/ports/images/ as well. -->
<gameList>
${ENTRY}
</gameList>
SNIPPET
    cat >> "${PORT}/README.txt" <<'README'


7. OPTIONAL: NAME AND ICON IN THE MENU
--------------------------------------
See olduvai/gamelist-entry.xml.  Do NOT copy it over an existing gamelist.xml —
paste its <game> block in, or the other ports lose their names and artwork.
README
else
    printf '<?xml version="1.0"?>\n<gameList>\n%s\n</gameList>\n' "$ENTRY" \
        > "${OUT_ABS}/gamelist.xml"
fi

echo "Port bundle (${DEVICE_NAME}, $([ "$PUBLIC" -eq 1 ] && echo public || echo private)) written to ${OUT_ABS}"
# Portable listing: BSD find (macOS) has no -printf, and under pipefail the
# GNU form failed the whole script on the Mac AFTER the bundle was written.
(cd "${OUT_ABS}" && find . -type f | sed 's|^\./||' | sort) |
    while IFS= read -r f; do
        printf '  %10s  %s\n' "$(wc -c < "${OUT_ABS}/${f}" | tr -d ' ')" "${f}"
    done
