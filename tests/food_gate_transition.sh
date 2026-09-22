#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# No classic NOT ENOUGH FOOD sprites in an Enhanced transition — invoked by
# CTest.
#
# WHY THIS EXISTS.  Owner report, 2026-09-17 (A12, L1): sliding into or out of
# the food-gate screen in Enhanced showed the CLASSIC cue sprites, because the
# frames a transition composes are built from scratch assets
# (build_surface_screen_assets) that did not carry enhanced_vector_banners.
# (The vector banner riding the slide is the other half of that report; it is
# pinned by test_banners.cpp.)  The same banner also stayed on top of the
# pause menu; step 3 pins that it is hidden while a menu is open.
#
# How, without storing any game imagery (CONTRIBUTING.md):
#  1. Classic, entering L1 screen 18 with OLDUVAI_FORCE_FOOD=0 and =45: the
#     last transition frames differ only where the cue is drawn — that
#     difference IS the cue, as a pixel template (rows 60+, below the HUD).
#  2. Enhanced, leaving and entering screen 18 with no food: slide the template
#     across every transition frame; no frame may match it.
#  3. Enhanced, paused on the gate screen (OLDUVAI_PAUSE_SHOT): no banner-red
#     pixels in the banner rows (682 before the fix, 0 after).
# --transitions classic keeps the frame counts deterministic; the margin is
# pinned (OLDUVAI_WS_FORCE_MARGIN) so the wide frames have a fixed width.
#
# Skip (77) when game data or the binary is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "food_gate_transition: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "food_gate_transition: SKIP — binary not found: ${BINARY}"
    exit 77
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK="$(mktemp -d /tmp/olduvai_foodgate.XXXXXX)"
mkdir "${WORK}/cfg"

# Hold right from screen 17 (reaches 18 at frame 75); hold left from 18
# (reaches 17 at frame 56).
printf '%s\n' '{"time_ms":2200,"key":"right","action":"press"}' \
    '{"time_ms":12000,"key":"right","action":"release"}' > "${WORK}/right.jsonl"
printf '%s\n' '{"time_ms":2200,"key":"left","action":"press"}' \
    '{"time_ms":9000,"key":"left","action":"release"}' > "${WORK}/left.jsonl"

run() {  # run <dir> <food> <mode flags> <start-screen> <replay> <frames>
    mkdir "${WORK}/$1"
    # shellcheck disable=SC2086
    XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_FORCE_FOOD="$2" \
        OLDUVAI_WS_FORCE_MARGIN=64 OLDUVAI_DUMP_TRANSITION="${WORK}/$1" \
        "${BINARY}" --play --level 1 --start-screen "$4" \
        --replay "${WORK}/$5" --play-frames "$6" --game-dir "${GAME_DIR}" \
        $3 --transitions classic --window 896x400 >/dev/null 2>&1
}
CLASSIC="--profile dos --render-scale 1"
ENH="--enhanced --hd-profile mmpx --render-scale 2 --aspect widescreen"
run c_hungry 0 "${CLASSIC}" 17 right.jsonl 80
run c_fed 45 "${CLASSIC}" 17 right.jsonl 80
run e_enter 0 "${ENH}" 17 right.jsonl 80
run e_leave 0 "${ENH}" 18 left.jsonl 60

XDG_CONFIG_HOME="${WORK}/cfg" OLDUVAI_FORCE_FOOD=0 \
    OLDUVAI_PAUSE_SHOT="${WORK}/pause.png" "${BINARY}" --play --level 1 \
    --start-screen 18 --game-dir "${GAME_DIR}" ${ENH} --window 896x400 \
    >/dev/null 2>&1

python3 - "${WORK}" <<'EOF'
import glob, os, struct, sys

work = sys.argv[1]

def load(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w = struct.unpack('<i', d[18:22])[0]
    h = struct.unpack('<i', d[22:26])[0]
    bpp = struct.unpack('<H', d[28:30])[0] // 8
    stride = (w * bpp + 3) // 4 * 4
    hh = abs(h)
    rows = []
    for y in range(hh):
        yy = hh - 1 - y if h > 0 else y
        r = d[off + yy * stride: off + yy * stride + w * bpp]
        rows.append([r[i:i + 3] for i in range(0, w * bpp, bpp)])
    return w, hh, rows

def last(d):
    files = sorted(glob.glob(os.path.join(work, d, '*.bmp')))
    return files[-1] if files else None

hungry, fed = last('c_hungry'), last('c_fed')
if hungry is None or fed is None:
    print('food_gate_transition: FAIL — no classic transition frames')
    sys.exit(1)
w, h, a = load(hungry)
_, _, b = load(fed)
pts = [(x, y, a[y][x]) for y in range(60, h) for x in range(w)
       if a[y][x] != b[y][x]]
if len(pts) < 200:
    print('food_gate_transition: FAIL — the classic cue was not found '
          '(%d differing pixels); the food hook or the scenario broke' % len(pts))
    sys.exit(1)
xs = [p[0] for p in pts]

def best(path):
    fw, fh, f = load(path)
    top = 0.0
    for dx in range(-max(xs), fw - min(xs)):
        hit = n = 0
        for x, y, c in pts:
            fx = x + dx
            if 0 <= fx < fw and y < fh:
                n += 1
                hit += f[y][fx] == c
        if n >= len(pts) * 0.8:
            top = max(top, hit / n)
    return top

rc = 0
for d in ('e_enter', 'e_leave'):
    frames = sorted(glob.glob(os.path.join(work, d, '*.bmp')))
    if not frames:
        print('food_gate_transition: FAIL (%s) — no transition frames' % d)
        rc = 1
        continue
    worst = max((best(f), os.path.basename(f)) for f in frames)
    if worst[0] >= 0.9:
        print('food_gate_transition: FAIL (%s) — %s shows the classic cue '
              '(match %.2f)' % (d, worst[1], worst[0]))
        rc = 1
    else:
        print('food_gate_transition: PASS (%s, %d frames, best match %.2f)'
              % (d, len(frames), worst[0]))
# 3. The pause shot (PNG): decode it with zlib — no image library needed.
import zlib
def png_rows(path):
    d = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
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

pause = os.path.join(work, 'pause.png')
if not os.path.exists(pause):
    print('food_gate_transition: FAIL (pause) — no pause shot written')
    rc = 1
else:
    w, h, bpp, rows = png_rows(pause)
    red = 0
    for y in range(int(h * 0.44), int(h * 0.62)):
        r = rows[y]
        for x in range(0, w, 2):
            R, G, B = r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
            if R > 150 and B < 60 and R - G > 60:
                red += 1
    if red > 40:
        print('food_gate_transition: FAIL (pause) — the banner is drawn over '
              'the pause menu (%d banner-red pixels)' % red)
        rc = 1
    else:
        print('food_gate_transition: PASS (pause, %d banner-red pixels)' % red)
sys.exit(rc)
EOF
rc=$?
if [ ${rc} -eq 0 ]; then rm -rf "${WORK}"; else echo "  kept: ${WORK}"; fi
exit ${rc}
