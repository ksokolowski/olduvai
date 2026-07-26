#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Writes a tiny SYNTHETIC format-0 Standard MIDI File for the audio_render
# gate: one program change + a handful of notes across two channels.  Entirely
# hand-authored — no game content.  Deterministic (fixed bytes every run).
#   usage: audio_fixture.py <out.mid>
import struct
import sys


def vlq(n: int) -> bytes:
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(b)


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: audio_fixture.py <out.mid>\n")
        return 2
    ev = bytearray()
    ev += vlq(0) + bytes([0xC0, 48])          # program change ch0 -> 48
    for note, ch in [(60, 0), (64, 0), (67, 0), (72, 0), (55, 1), (59, 1)]:
        ev += vlq(0) + bytes([0x90 | ch, note, 90])    # note on
        ev += vlq(240) + bytes([0x80 | ch, note, 0])   # note off
    ev += vlq(0) + bytes([0xFF, 0x2F, 0x00])  # end of track

    hdr = b"MThd" + struct.pack(">I", 6) + struct.pack(">HHH", 0, 1, 480)
    trk = b"MTrk" + struct.pack(">I", len(ev)) + bytes(ev)
    with open(sys.argv[1], "wb") as f:
        f.write(hdr + trk)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
