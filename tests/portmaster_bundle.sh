#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The PortMaster package (packaging/build_port_portmaster.sh): the layout and
# metadata PortMaster-New's build_release.py --do-check wants, a launcher that
# takes every device fact from PortMaster, and a package that carries nothing
# of the player's.  PortMaster's own checker passed on it (2026-09-28); this
# pins what it checked, plus what it cannot see.
#
# Needs bash and python3 (the generator writes port.json with it); skips
# (78) without them, and on Windows.  No game files.
set -eu
SCRIPT="$1"          # packaging/build_port_portmaster.sh
REPO="$2"            # CMAKE_SOURCE_DIR
command -v bash >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1 || {
    echo "portmaster_bundle: SKIP — needs bash and python3" >&2; exit 78; }
# The packaging scripts are Linux/macOS developer tools, never run on
# Windows, whose filesystems cannot hold the executable bit the package
# needs (port_bundle skips there for its own filesystem reason).
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "portmaster_bundle: SKIP — packaging does not run on Windows" >&2
        exit 78 ;;
esac
T=$(mktemp -d "${TMPDIR:-/tmp}/portmaster_bundle.XXXXXX"); trap 'rm -rf "$T"' EXIT
printf 'bin64' > "$T/bin64"
printf 'bin32' > "$T/bin32"
P="$T/out"
bash "$SCRIPT" --binary-aarch64 "$T/bin64" --binary-armhf "$T/bin32" \
     --out "$P" --release-zip "$T/olduvai.zip" >/dev/null
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# 1. PortMaster's layout: the listing at the root, the port in olduvai/.
for f in port.json README.md screenshot.png gameinfo.xml Olduvai.sh \
         olduvai/olduvai.aarch64 olduvai/olduvai.armhf olduvai/olduvai.gptk \
         olduvai/README.txt olduvai/licenses/FluidSynth-LICENSE.txt \
         olduvai/licenses/LICENSE olduvai/licenses/THIRD-PARTY-NOTICES.md \
         olduvai/fonts/NotoSans-Regular.ttf; do
    [ -s "$P/$f" ] || bad "missing $f"
done
for f in Olduvai.sh olduvai/olduvai.aarch64 olduvai/olduvai.armhf; do
    [ -x "$P/$f" ] || bad "$f is not executable"
done

# 2. port.json: version 4, the fields build_release.py reads, an allowed
#    genre, the engine's name, both architectures, and no ready-to-run flag
#    (the player brings the game files).
python3 - "$P" <<'PY' || fail=1
import json, re, struct, sys, xml.etree.ElementTree as ET
p = sys.argv[1]
j = json.load(open(f"{p}/port.json", encoding="utf-8"))
a = j["attr"]
errs = []
def need(c, m):
    if not c: errs.append(m)
need(j["version"] == 4, "port.json version is not 4")
need(j["name"] == "olduvai.zip", "port.json name")
need(sorted(j["items"]) == ["Olduvai.sh", "olduvai"], "port.json items")
need(a["title"] == "Olduvai", "title is not the engine's name")
need(a["rtr"] is False, "rtr must be false: the player brings the game")
need(set(a["arch"]) == {"aarch64", "armhf"}, "arch")
genres = {"action", "adventure", "arcade", "casino/card", "fps", "platformer",
          "puzzle", "racing", "rhythm", "rpg", "simulation", "sports",
          "strategy", "visual novel", "other"}
need(a["genres"] and set(a["genres"]) <= genres, "genres not PortMaster's")
for k in ("desc", "inst", "porter", "min_glibc"):
    need(a.get(k), f"attr.{k} empty")
need("PREH.SQZ" in a["inst"] and "HISTORIK.EXE" in a["inst"],
     "inst must name both executable forms")
need(re.fullmatch(r"[a-z0-9][a-z0-9._]*", "olduvai"), "portname")
# gameinfo.xml: one game, the launcher's path, an image that ships.
g = ET.parse(f"{p}/gameinfo.xml").getroot().findall("game")
need(len(g) == 1, "gameinfo.xml must hold one game")
if g:
    need(g[0].findtext("path") == "./Olduvai.sh", "gameinfo path")
    need(g[0].findtext("name") == "Olduvai", "gameinfo name")
    img = g[0].findtext("image") or ""
    import os
    need(os.path.isfile(os.path.join(p, img)), "gameinfo image does not ship")
# The screenshot: PNG, 4:3, at least 640x480.
with open(f"{p}/screenshot.png", "rb") as f:
    head = f.read(24)
w, h = struct.unpack(">II", head[16:24])
need(head[:8] == b"\x89PNG\r\n\x1a\n", "screenshot is not a PNG")
need(w * 3 == h * 4 and w >= 640, f"screenshot {w}x{h} is not 4:3 >= 640x480")
for e in errs:
    print("FAIL: " + e)
sys.exit(1 if errs else 0)
PY

# 3. The launcher: valid bash, PortMaster's own setup, player state in the
#    port, the profile from the panel PortMaster reports, and no device fact
#    or debug hook set by hand.
L="$P/Olduvai.sh"
bash -n "$L" || bad "launcher is not valid bash"
for want in 'source $controlfolder/control.txt' 'device_info.txt' 'get_controls' \
            'GAMEDIR="/$directory/ports/olduvai"' 'XDG_CONFIG_HOME="$GAMEDIR/conf"' \
            'OLDUVAI_BUG_DIR="$GAMEDIR/bug_reports"' 'SDL_GAMECONTROLLERCONFIG' \
            '$GPTOKEYB "olduvai.${DEVICE_ARCH}"' 'gptokeyb.${DEVICE_ARCH}' \
            'DISPLAY_HEIGHT' '--default-profile "$PROFILE"' 'pm_finish'; do
    grep -qF -- "$want" "$L" || bad "launcher lacks: $want"
done
if grep -v '^[[:space:]]*#' "$L" | grep -qE 'SDL_VIDEODRIVER|XDG_RUNTIME_DIR|OLDUVAI_FRAME_STATS|--profile '; then
    bad "launcher hand-sets a device fact, a debug hook or --profile"
fi
# The profile each panel starts on, run from the launcher's own block.
PICK="$(sed -n '/^W=\${DISPLAY_WIDTH/,/^fi$/p' "$L")"
[ -n "$PICK" ] || bad "launcher has no panel -> profile block"
for panel in 1280x720:hd-handheld 1024x600:hd-handheld 720x720:hd-handheld \
             800x480:hd-handheld-x2 854x480:hd-handheld-x2 960x544:hd-handheld-x2 \
             640x480:hd-handheld-43 320x240:dos-handheld 480x320:dos-handheld \
             0x0:dos-handheld 1280x800:hd-handheld-x4 1600x900:hd-handheld-x4 \
             1920x1080:hd-handheld-x4 2560x1440:hd-handheld-x4; do
    wh="${panel%%:*}"; want="${panel#*:}"
    got="$(DISPLAY_WIDTH="${wh%x*}" DISPLAY_HEIGHT="${wh#*x}" \
           bash -c "$PICK"'
echo "$PROFILE"')"
    [ "$got" = "$want" ] || bad "panel $wh starts $got, want $want"
done

# 4. Nothing of the player's: no game data, ROMs, SoundFonts or settings.
if [ -n "$(find "$P" -type f \( -iname '*.cur' -o -iname '*.vga' -o -iname '*.rom' \
        -o -iname '*.sf2' -o -iname '*.sf3' -o -iname 'historik.exe' \
        -o -iname 'preh.sqz' -o -iname 'play.json' \) 2>/dev/null)" ]; then
    bad "package contains game data, ROMs, a SoundFont or settings"
fi
for d in game mt32-roms soundfonts; do
    extra=$(find "$P/olduvai/$d" -type f ! -name PUT-FILES-HERE.txt 2>/dev/null)
    [ -z "$extra" ] || bad "files in $d: $extra"
done
[ ! -e "$P/olduvai/conf" ] || bad "package ships conf/ (player state)"

# 5. The zip PortMaster installs (its harbourmaster merges olduvai/gameinfo.xml
#    into EmulationStation's list and keeps olduvai/port.json): only the
#    launcher and the port folder at the root, the store files INSIDE it, the
#    image gameinfo names present, executables executable, nothing of the
#    player's.  Loose at the root the installer tolerates but never merges them.
python3 - "$T/olduvai.zip" <<'PY' || fail=1
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = z.namelist()
errs = []
top = {n.split("/")[0] for n in names}
if top != {"Olduvai.sh", "olduvai"}:
    errs.append("zip root is %s, want only Olduvai.sh and olduvai/" % sorted(top))
for need in ("olduvai/port.json", "olduvai/gameinfo.xml", "olduvai/screenshot.png",
             "olduvai/README.md", "olduvai/olduvai.aarch64", "olduvai/olduvai.armhf"):
    if need not in names:
        errs.append("zip lacks " + need)
import re
m = re.search(r"<image>\./(.*?)</image>", z.read("olduvai/gameinfo.xml").decode()) \
    if "olduvai/gameinfo.xml" in names else None
if not m or m.group(1) not in names:
    errs.append("gameinfo.xml's image %r is not in the zip" % (m and m.group(1)))
for exe in ("Olduvai.sh", "olduvai/olduvai.aarch64", "olduvai/olduvai.armhf"):
    if exe in names and not (z.getinfo(exe).external_attr >> 16) & 0o111:
        errs.append(exe + " is not executable in the zip")
bad = [n for n in names if n.lower().endswith((".cur", ".vga", ".rom", ".sf2", ".sf3", ".exe", ".sqz"))
       or n.endswith("play.json")]
if bad:
    errs.append("zip contains the player's files: %s" % bad)
for e in errs:
    print("FAIL: " + e)
sys.exit(1 if errs else 0)
PY

# 6. Refuses to write inside the repo.
if bash "$SCRIPT" --binary-aarch64 "$T/bin64" --out "$REPO/tmp_pm_bundle" \
        >/dev/null 2>&1; then
    rm -rf "$REPO/tmp_pm_bundle"; bad "wrote a package inside the repo"
fi

[ "$fail" -eq 0 ] && echo "portmaster_bundle: OK"
exit "$fail"
