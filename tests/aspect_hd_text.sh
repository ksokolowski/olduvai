#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# HD vector text stays INSIDE the picture, whatever the aspect — invoked by
# CTest.
#
# WHY THIS EXISTS.  BACKLOG §3.23: in HD with Aspect 4:3 the menu's labels
# rendered inside the dark slab while its VALUES landed outside the slab's
# right edge, and the HUD text stretched the full window width while the game
# canvas was pillarboxed narrower.  The text overlay is drawn at OUTPUT
# resolution and only the widescreen path was told where the picture is; every
# other aspect laid its glyphs out across the whole window.  The same is true
# of Keep in a window that is not 16:10 — 4:3 was simply the reported case.
#
# The suite had no HD 4:3 shot at all: pause_shot_43 is CLASSIC (bitmap glyphs
# live in the 320x200 buffer, so they cannot miss the picture).  That gap is
# why this survived.
#
# WHAT IS CHECKED — a property, not a golden hash: with the picture
# pillarboxed, the black bars must stay black.  A glyph drawn against the
# window instead of the picture puts bright pixels there (measured before the
# fix: hundreds).  Then, so the test cannot pass by drawing nothing, the
# picture itself must carry bright text pixels in the HUD band.
#
# Cases (both pillarbox the picture in a 896x400 window):
#   4:3   — logical 320x240*scale: bars ~180 px a side
#   keep  — logical 320x200*scale: bars ~128 px a side
# each as a plain HUD frame and with the pause menu open.
#
# A run that writes no shot keeps its stderr and prints it: the one gate
# failure so far (ASan lane, 2026-09-20) said only "no shot written", and the
# engine's own "window creation failed: <SDL error>" line had gone to
# /dev/null.  See BACKLOG §6.
#
# Skip (77) when game data, the binary or python3 is absent.

GAME_DIR="${OLDUVAI_GAME_DATA:-${1:-$(dirname "$0")/../game_data}}"
BINARY="${2:-$(dirname "$0")/../build/release/olduvai}"

if [ ! -f "${GAME_DIR}/FILESA.VGA" ]; then
    echo "aspect_hd_text: SKIP — game data not found at ${GAME_DIR}/FILESA.VGA"
    exit 77
fi
if [ ! -x "${BINARY}" ]; then
    echo "aspect_hd_text: SKIP — binary not found: ${BINARY}"
    exit 77
fi
command -v python3 >/dev/null 2>&1 || {
    echo "aspect_hd_text: SKIP — python3 not available"; exit 77; }

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
WORK="$(mktemp -d /tmp/olduvai_aspect.XXXXXX)"
rc=0

for aspect in 4:3 keep; do
    tag=$(echo "${aspect}" | tr -d ':')
    CFG="$(mktemp -d /tmp/olduvai_cfg.XXXXXX)"
    XDG_CONFIG_HOME="${CFG}" timeout 60 "${BINARY}" --play --level 1 \
        --profile hd --aspect "${aspect}" --window 896x400 \
        --play-shot "${WORK}/hud_${tag}.png" --play-shot-frame 60 \
        --game-dir "${GAME_DIR}" >/dev/null 2>"${WORK}/hud_${tag}.err"
    # The pause menu, through the menu-script walk (Options -> Video).
    mkdir -p "${WORK}/menu_${tag}"
    XDG_CONFIG_HOME="${CFG}" \
        OLDUVAI_MENU_SCRIPT="esc down down down enter down down enter shot quit" \
        OLDUVAI_MENU_SCRIPT_DIR="${WORK}/menu_${tag}" timeout 60 \
        "${BINARY}" --play --level 1 --profile hd --aspect "${aspect}" \
        --window 896x400 --game-dir "${GAME_DIR}" \
        >/dev/null 2>"${WORK}/menu_${tag}.err"
    rm -rf "${CFG}"
done

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

# The pillarbox width for each aspect in a 896x400 window: the picture keeps
# its logical aspect and is centred, so bar = (896 - 400*logical_aspect)/2.
BARS = {'43': (896 - 400 * 320 // 240) // 2,
        'keep': (896 - 400 * 320 // 200) // 2}

def bright_in_bars(path, bar):
    w, h, bpp, rows = png_rows(path)
    n = 0
    for y in range(h):
        r = rows[y]
        for x in list(range(0, bar - 2)) + list(range(w - bar + 2, w)):
            R, G, B = r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
            if R > 90 and G > 90 and B > 90:
                n += 1
    return n

def bright_inside(path, bar, y0, y1):
    w, h, bpp, rows = png_rows(path)
    n = 0
    for y in range(int(h * y0), int(h * y1)):
        r = rows[y]
        for x in range(bar + 4, w - bar - 4):
            R, G, B = r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
            if R > 150 and G > 150 and B > 150:
                n += 1
    return n

rc = 0
for tag, bar in BARS.items():
    for kind, path, band in (
            ('hud', os.path.join(work, 'hud_%s.png' % tag), (0.0, 0.12)),
            ('menu', os.path.join(work, 'menu_%s' % tag, '000.png'),
             (0.3, 0.7))):
        if not os.path.exists(path):
            # Say WHY: a run that writes no shot has usually failed at
            # startup, and its stderr is the only witness (BACKLOG §6 —
            # this happened once inside a long ctest sequence, with the
            # stderr thrown away).
            err = os.path.join(work, '%s_%s.err' % (kind, tag))
            tail = ''
            if os.path.exists(err):
                with open(err, errors='replace') as f:
                    tail = ''.join(f.readlines()[-6:]).strip()
            print('aspect_hd_text: FAIL (%s %s) — no shot written; the run '
                  'said:\n%s' % (tag, kind, tail or '  (nothing on stderr)'))
            rc = 1
            continue
        out = bright_in_bars(path, bar)
        ins = bright_inside(path, bar, *band)
        if out > 0:
            print('aspect_hd_text: FAIL (%s %s) — %d bright pixels in the '
                  'pillarbox bars: text is laid out against the WINDOW, not '
                  'the picture' % (tag, kind, out))
            rc = 1
        elif ins < 200:
            print('aspect_hd_text: FAIL (%s %s) — only %d bright pixels inside '
                  'the picture; the text did not render at all' % (tag, kind, ins))
            rc = 1
        else:
            print('aspect_hd_text: PASS (%s %s, bars clean, %d text px inside)'
                  % (tag, kind, ins))
sys.exit(rc)
EOF
rc=$?
if [ ${rc} -eq 0 ]; then rm -rf "${WORK}"; else echo "  kept: ${WORK}"; fi
exit ${rc}
