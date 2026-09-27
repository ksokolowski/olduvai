#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# A display or audio Apply from the pause rebuilds in place — invoked by CTest.
#
# Before the in-place reinit such an Apply left the level: the music stopped,
# the level reloaded behind a 3 s loading card, the music restarted and the
# pause closed; a boss fight deferred it to the fight's end.  Now the level or
# fight stays and the pause stays open.  Checked per walk (render scale,
# Style, Sound card), from the pause menu, on a platform level and a boss:
#   - the level is entered once (one "game: level" line: no reload),
#   - the rebuild is logged at the expected scale, the audio reopened only
#     for an audio change,
#   - the frame after Apply is the paused frame (the menu script's shot),
#   - a render scale 2 -> 4 doubles the output.
# The state round trip itself is reinit_smoke's.  Music continuity is not
# asserted: it needs a sounding backend (measured by hand, BACKLOG §3.40).
#
# `timeout -k`: a SIGTERM reaches the game as a window close, which returns
# to the title and waits there; a regression that reloads the level replays
# the menu script forever, so only the kill ends it.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "reinit_in_place: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "reinit_in_place: SKIP — binary not found: ${BINARY}"
    exit 77
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK="$(mktemp -d /tmp/olduvai_reinit_in_place.XXXXXX)"
FAIL=0

# Pause -> Options (cursor on Style).
OPTIONS="wait wait esc down down down enter"
# After the change: shot it, leave Options (from a submenu: two escapes; the
# second reaches the confirm dialog), Apply, shot the paused frame, resume,
# play a few frames.
AFTER="enter wait shot esc wait wait wait quit"
APPLY_TOP="shot esc ${AFTER}"
APPLY_SUB="shot esc esc ${AFTER}"

# run <name> <level> <script> <flags...>
run() {
    name=$1; level=$2; script=$3; shift 3
    mkdir -p "${WORK}/${name}"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" OLDUVAI_MENU_SCRIPT="${script}" \
        OLDUVAI_MENU_SCRIPT_DIR="${WORK}/${name}" timeout -k 5 60 \
        "${BINARY}" --play --level "${level}" --game-dir "${GAME_DIR}" "$@" \
        >/dev/null 2>"${WORK}/${name}.err"
    echo $? > "${WORK}/${name}.rc"
    rm -rf "${CFG}"
}

# png_size <file>: "w h" from the IHDR chunk.
png_size() {
    od -An -tu1 -j16 -N8 "$1" |
        awk 'NF == 8 { print $1*16777216+$2*65536+$3*256+$4,
                             $5*16777216+$6*65536+$7*256+$8 }'
}

fail() { echo "reinit_in_place: FAIL ($1) — $2"; FAIL=1; }

# check <name> <expected rebuild line> <audio: yes|no> [<shot: yes|no>]
check() {
    name=$1; want=$2; audio=$3; shot=${4:-yes}
    err="${WORK}/${name}.err"
    [ "$(cat "${WORK}/${name}.rc")" = 0 ] ||
        { fail "${name}" "exit $(cat "${WORK}/${name}.rc"): $(tail -1 "${err}")"; return; }
    entries=$(grep -c '^game: level' "${err}")
    [ "${entries}" = 1 ] || fail "${name}" "level entered ${entries} times"
    grep -q "^${want}" "${err}" ||
        fail "${name}" "no '${want}' in the log: $(grep '^display:' "${err}")"
    if [ "${audio}" = yes ]; then
        grep -q '^display:.*audio reopened' "${err}" ||
            fail "${name}" "the audio was not reopened"
    elif grep -q '^display:.*audio reopened' "${err}"; then
        fail "${name}" "the audio was reopened for a display-only change"
    fi
    [ "${shot}" = no ] && return
    last=$(ls "${WORK}/${name}"/*.png 2>/dev/null | tail -1)
    [ -n "${last}" ] || fail "${name}" "no paused frame after Apply"
}

# Render scale 2 -> 4 (Options -> Video -> Render scale).
run scale 1 "${OPTIONS} down down enter down down right ${APPLY_SUB}" \
    --profile hd --render-scale 2
check scale "display: rebuilt in place at scale 4" no
if [ -s "${WORK}/scale/000.png" ] && [ -s "${WORK}/scale/001.png" ]; then
    set -- $(png_size "${WORK}/scale/000.png") $(png_size "${WORK}/scale/001.png")
    [ "$3" = $(($1 * 2)) ] && [ "$4" = $(($2 * 2)) ] ||
        fail scale "output $1x$2 -> $3x$4, not doubled"
fi

# Style: Enhanced -> Classic (the compose scale drops to 1).
run style 1 "${OPTIONS} right ${APPLY_TOP}" --profile hd
check style "display: rebuilt in place at scale 1" no

# Sound card (Options -> Audio): the audio device is replaced.
run audio 1 "${OPTIONS} down enter right ${APPLY_SUB}" --profile hd --render-scale 2
check audio "display: rebuilt in place at scale 2" yes

# The same three in a boss fight (L2), whose pause deferred them to the
# fight's end before.  `shot` is a platform-only token, so the log is the
# witness; --play-frames makes the fight the whole run.
BOSS_OPTIONS="wait wait wait wait wait esc down enter"
BOSS_AFTER="enter wait wait esc wait wait wait quit"
run boss_scale 2 "${BOSS_OPTIONS} down down enter down down right esc esc ${BOSS_AFTER}" \
    --profile hd --render-scale 2 --play-frames 150
check boss_scale "display: rebuilt in place at scale 4" no no
run boss_style 2 "${BOSS_OPTIONS} right esc ${BOSS_AFTER}" \
    --profile hd --play-frames 150
check boss_style "display: rebuilt in place at scale 1" no no
run boss_audio 2 "${BOSS_OPTIONS} down enter right esc esc ${BOSS_AFTER}" \
    --profile hd --render-scale 2 --play-frames 150
check boss_audio "display: rebuilt in place at scale 2" yes no

if [ ${FAIL} -eq 0 ]; then
    echo "reinit_in_place: PASS (scale, style, audio; the same in a boss fight)"
    rm -rf "${WORK}"
else
    echo "  kept: ${WORK}"
fi
exit ${FAIL}
