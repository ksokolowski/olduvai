#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Enhanced: the fly-in balloons float away — invoked by CTest.
#
# Owner idea, 2026-09-17: the balloons vanish with the sprite swap when a ride
# ends (the L1 screen-12 landing, the boss fly-in); in Enhanced they now rise
# away like the death halo does (render/rising_balloons.hpp).
#
# The boss fly-in is the deterministic half — 42 ticks, no replay, no input —
# so it is the gate: the bunch's own palette colour (162,0,32) is counted in a
# full-frame shot.  Present while it rises, absent once it is off the top, and
# never in Classic.  (The L1 landing shares the effect and is covered by the
# unit test in tests/test_banners.cpp's neighbour, test_rising_balloons.cpp.)
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "balloon_flyaway: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "balloon_flyaway: SKIP — binary not found: ${BINARY}"
    exit 77
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
. "$(dirname "$0")/lib/engine_err.sh"
engine_err_init
WORK="$(mktemp -d /tmp/olduvai_balloon.XXXXXX)"
mkdir "${WORK}/cfg"

shot() {  # shot <name> <frame> <mode flags>
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_REAL_SHOT=1 timeout 120 \
        "${BINARY}" --play --level 2 --game-dir "${GAME_DIR}" $3 \
        --window 896x400 --play-shot "${WORK}/$1.png" \
        --play-shot-frame "$2" >"${ERR}" 2>&1
}
ENH="--enhanced --hd-profile mmpx --render-scale 2 --aspect widescreen"
CLASSIC="--profile dos --render-scale 1"
shot rising 44 "${ENH}"       # mid-rise, a couple of ticks after the fly-in
shot gone    70 "${ENH}"      # long off the top
shot classic 44 "${CLASSIC}"  # Classic never arms it

python3 - "${WORK}" <<'EOF'
import os, struct, sys, zlib
work = sys.argv[1]

def bunch_pixels(path):
    d = open(path, 'rb').read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 3
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, _depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = {2: 3, 6: 4}[ctype]
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride, prev, n = w * bpp, bytearray(w * bpp), 0
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            if (line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]) == (162, 0, 32):
                n += 1
        prev = line
    return n

rc = 0
want = {'rising': (100, None), 'gone': (None, 20), 'classic': (None, 20)}
for name, (lo, hi) in want.items():
    path = os.path.join(work, name + '.png')
    if not os.path.exists(path):
        print('balloon_flyaway: FAIL (%s) — no shot written' % name)
        rc = 1
        continue
    n = bunch_pixels(path)
    if lo is not None and n < lo:
        print('balloon_flyaway: FAIL (%s) — %d balloon pixels, want >= %d '
              '(the fly-away did not draw)' % (name, n, lo))
        rc = 1
    elif hi is not None and n > hi:
        print('balloon_flyaway: FAIL (%s) — %d balloon pixels, want <= %d '
              '(balloons where there should be none)' % (name, n, hi))
        rc = 1
    else:
        print('balloon_flyaway: PASS (%s, %d balloon pixels)' % (name, n))
sys.exit(rc)
EOF
rc=$?
if [ ${rc} -eq 0 ]; then
    rm -rf "${WORK}"
    engine_err_clean "${ERR}"
else
    echo "  kept: ${WORK}"
    engine_said "${ERR}"   # the checks above run in python; this is the run
fi
exit ${rc}
