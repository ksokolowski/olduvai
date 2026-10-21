#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The sharp output filter changes only what it should — invoked by CTest.
#
# Paused frames (deterministic: the frozen tick, no smooth-motion timing),
# --output-filter nearest against sharp: in Enhanced, a 1000x625 window
# (640x400 at 1.5625) must differ and a 1280x800 one (exactly 2x) must be
# identical; Classic scales by whole multiples, so 1000x625 is identical too.
#
# Skips (77) when game data or the binary is absent.
set -u
GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"
if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "output_filter: SKIP — game data not found at ${GAME_DIR}"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "output_filter: SKIP — binary not found: ${BINARY}"
    exit 77
fi
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK=$(mktemp -d /tmp/output_filter.XXXXXX)
fail=0
trap '[ "${fail}" -eq 0 ] && rm -rf "${WORK}"' EXIT
LEAD=$(printf 'wait %.0s' $(seq 30))

shoot() {   # <dir> <filter> <window> <flags...>
    d="$1" f="$2" w="$3"; shift 3
    mkdir -p "${d}/cfg"
    XDG_CONFIG_HOME="${d}/cfg" OLDUVAI_MENU_SCRIPT="${LEAD} esc shot quit" \
        OLDUVAI_MENU_SCRIPT_DIR="${d}" timeout 60 "${BINARY}" --no-config \
        --play --level 1 --music-device off --game-dir "${GAME_DIR}" \
        --window "${w}" --output-filter "${f}" "$@" > "${d}/run.log" 2>&1
}

check() {   # <name> <window> <same|differ> <flags...>
    name="$1" w="$2" want="$3"; shift 3
    shoot "${WORK}/${name}.nearest" nearest "${w}" "$@"
    shoot "${WORK}/${name}.sharp" sharp "${w}" "$@"
    a="${WORK}/${name}.nearest/000.png" b="${WORK}/${name}.sharp/000.png"
    if [ ! -s "${a}" ] || [ ! -s "${b}" ]; then
        echo "FAIL: ${name}: no shot"; fail=1; return
    fi
    if cmp -s "${a}" "${b}"; then got=same; else got=differ; fi
    if [ "${got}" = "${want}" ]; then
        echo "  ${name}: ${got}"
    else
        echo "FAIL: ${name}: nearest and sharp ${got}, expected ${want}"
        fail=1
    fi
}

HD="--enhanced --hd-profile xbrz --render-scale 2 --aspect keep"
# shellcheck disable=SC2086  # HD is a flag list
check enhanced_fraction 1000x625 differ ${HD}
# shellcheck disable=SC2086
check enhanced_whole 1280x800 same ${HD}
check classic_fraction 1000x625 same --render-scale 1
[ "${fail}" -eq 0 ] && echo "output_filter: OK"
exit "${fail}"
