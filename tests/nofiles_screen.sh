#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# A frontend launch with no game files must SAY so on screen when no message
# box can be shown.  On a handheld (KNULLI: kmsdrm / mali, no desktop) the box
# fails, and the launch used to exit in under a second with the explanation
# only in a log file — from the Ports menu, nothing happened at all.
#
# SDL_VIDEODRIVER=dummy fails the box the same way.  stdin and stderr are not
# terminals, so the binary takes the frontend-launch path.  The screen itself
# is saved by OLDUVAI_NOFILES_SHOT instead of waiting for a button.  Needs no
# game data — that is the point.
set -eu
BIN="${1:?usage: nofiles_screen.sh <olduvai binary>}"

# Windows: the scenario is unreachable, so skip (78 = a PLATFORM skip, not the
# game-data 77).  launched_from_gui() there asks whether the process owns its
# console (GetConsoleProcessList), not whether stdin/stderr are terminals —
# under ctest the console is shared, so this run is a terminal launch and
# never reaches the dialog (gitea windows jobs, 9a9bbe8: log line, rc 1, no
# screen, 0.15 s).  And a real Windows frontend launch always HAS a message
# box — the native one — so the fallback screen exists for desktop-less
# systems only.  Forcing GUI mode here instead would raise that native box and
# block the CI runner.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "nofiles_screen: SKIP — Windows always has a native message box;" \
             "the fallback screen is for desktop-less systems" >&2
        exit 78 ;;
esac
T=$(mktemp -d "${TMPDIR:-/tmp}/nofiles.XXXXXX"); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/game" "$T/cfg"

rc=0
XDG_CONFIG_HOME="$T/cfg" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
OLDUVAI_NOFILES_SHOT="$T/shot.bmp" \
    "$BIN" --game-dir "$T/game" </dev/null >"$T/out.log" 2>&1 || rc=$?

fail=0
[ "$rc" -ne 0 ] || { echo "FAIL: exited 0 with no game files"; fail=1; }
grep -q "Missing in" "$T/out.log" || { echo "FAIL: no log line"; fail=1; }
# The screen exists and is not a blank fill: the text lines put hundreds of
# distinct bytes in it.  A 1024x768 BMP of one colour would be all repeats.
if [ ! -s "$T/shot.bmp" ]; then
    echo "FAIL: no missing-files screen was drawn"; fail=1
else
    distinct=$(od -An -v -tu1 "$T/shot.bmp" | tr -s ' ' '\n' | sort -u | wc -l)
    [ "$distinct" -gt 64 ] || { echo "FAIL: the screen is blank ($distinct values)"; fail=1; }
fi
# Quitting from it must not write a config (the dialog's own quit path doesn't).
[ ! -f "$T/cfg/olduvai/play.json" ] || { echo "FAIL: wrote a config"; fail=1; }

[ "$fail" -eq 0 ] && echo "nofiles_screen: OK"
exit "$fail"
