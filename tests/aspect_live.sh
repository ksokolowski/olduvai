#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# A live Aspect change (Pause -> Options -> Video) shows what a fresh start
# at that aspect shows — invoked by CTest.
#
# The property: the pillarbox bar of the frame after the change equals the
# bar of a run started with that aspect, on the paused frame and on a
# gameplay frame after Apply.  Measured against a fresh run, not a constant,
# so it holds on any host.
#
# Walks through Widescreen and out again, which restored the level's ENTRY
# aspect (its logical size was computed once, at level entry).  Classic has
# no Widescreen value; its walk is the plain one.  Classic from Stretch runs
# at a window the picture does not divide evenly, where whole-number scaling
# shows: it was set only at a start with a logical size.
#
# Skip (77) when game data, the binary or python3 is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "aspect_live: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "aspect_live: SKIP — binary not found: ${BINARY}"
    exit 77
fi
command -v python3 >/dev/null 2>&1 || {
    echo "aspect_live: SKIP — python3 not available"; exit 77; }

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK="$(mktemp -d /tmp/olduvai_aspect_live.XXXXXX)"

# Pause -> Options -> Video, cursor on Aspect.
VIDEO="esc down down down enter down down enter down"
# After the change: shot the paused frame, Apply, resume, shot gameplay.
AFTER="shot esc esc enter esc wait wait shot quit"

# run <name> <script> <flags...>
run() {
    name=$1; script=$2; shift 2
    mkdir -p "${WORK}/${name}"
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" OLDUVAI_MENU_SCRIPT="${script}" \
        OLDUVAI_MENU_SCRIPT_DIR="${WORK}/${name}" timeout 60 \
        "${BINARY}" --play --level 1 --window "${WIN}" \
        --game-dir "${GAME_DIR}" "$@" >/dev/null 2>"${WORK}/${name}.err"
    rm -rf "${CFG}"
}

WIN=896x400
run hd_keep_43   "${VIDEO} right right ${AFTER}" --profile hd --aspect keep
run hd_43_keep   "${VIDEO} left left ${AFTER}"   --profile hd --aspect 4:3
run hd_keep_wide "${VIDEO} right shot quit"      --profile hd --aspect keep
run dos_keep_43  "${VIDEO} right ${AFTER}"       --profile dos --aspect keep
run hd_43        "${VIDEO} ${AFTER}"             --profile hd --aspect 4:3
run hd_keep      "${VIDEO} ${AFTER}"             --profile hd --aspect keep
run hd_wide      "${VIDEO} shot quit"            --profile hd --aspect widescreen
run dos_43       "${VIDEO} ${AFTER}"             --profile dos --aspect 4:3
WIN=1000x700
run dos_stretch_keep "${VIDEO} left left ${AFTER}" --profile dos --aspect stretch
run dos_keep_odd     "${VIDEO} ${AFTER}"           --profile dos --aspect keep

python3 - "${WORK}" <<'EOF'
import os, struct, sys, zlib

work = sys.argv[1]

def png_rows(path):
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
    rows, prev, stride = [], bytearray(w * bpp), w * bpp
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
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows

# The left pillarbox bar: the first column with any non-black pixel.
def bar(path):
    w, h, bpp, rows = png_rows(path)
    for x in range(w):
        if any(sum(r[x * bpp:x * bpp + 3]) > 30 for r in rows):
            return x
    return w

def bars(name):
    d = os.path.join(work, name)
    shots = sorted(f for f in os.listdir(d) if f.endswith('.png'))
    return [bar(os.path.join(d, f)) for f in shots]

rc = 0
for live, fresh in (('hd_keep_43', 'hd_43'), ('hd_43_keep', 'hd_keep'),
                    ('hd_keep_wide', 'hd_wide'), ('dos_keep_43', 'dos_43'),
                    ('dos_stretch_keep', 'dos_keep_odd')):
    got, want = bars(live), bars(fresh)
    if not got or len(got) != len(want):
        err = os.path.join(work, live + '.err')
        tail = open(err, errors='replace').read().strip()[-400:] \
            if os.path.exists(err) else ''
        print('aspect_live: FAIL (%s) — shots %d, want %d; the run said:\n%s'
              % (live, len(got), len(want), tail or '  (nothing)'))
        rc = 1
    elif got != want:
        print('aspect_live: FAIL (%s) — bars %s, a fresh start shows %s'
              % (live, got, want))
        rc = 1
    else:
        print('aspect_live: PASS (%s, bars %s)' % (live, got))
sys.exit(rc)
EOF
rc=$?
if [ ${rc} -eq 0 ]; then rm -rf "${WORK}"; else echo "  kept: ${WORK}"; fi
exit ${rc}
