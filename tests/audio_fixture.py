#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Writes a tiny SYNTHETIC format-0 Standard MIDI File for the audio_render
# gate: program changes + a sustained chord across two channels.  Entirely
# hand-authored — no game content.  Deterministic (fixed bytes every run).
#   usage: audio_fixture.py <out.mid>
#
# THE BUG THIS FIXTURE USED TO HAVE.  It rendered to PURE DIGITAL SILENCE —
# measured peak 0, rms 0, not one non-zero sample in 88,200 — and the gate
# passed anyway, because two runs of silence hash the same.  Two independent
# causes, both about WHERE and WHEN the notes sit:
#
#   * Channel.  The notes were on MIDI channel 0.  MT-32 assigns its parts to
#     channels 1-9 (0-indexed) and leaves channel 0 unassigned, so nothing on
#     it is ever heard.  Channels 1 and 2 are audible on all three backends:
#     MT-32 parts, GM non-percussion (9 is drums), and OPL melodic (0-5).
#   * Time.  MIDI deltas are CUMULATIVE, and the old loop emitted each
#     note-off with a delta of 240 before the next note-on.  At 480 ticks per
#     quarter and the default 120 BPM that is 0.25 s per note, so the only
#     channel-1 notes began at tick 960 — exactly t=1.000 s, one sample past
#     the end of the 1-second render window.
#
# So: sound on channels 1 and 2, starting at tick 0, held past the window.
# Note numbers 60/62/65/67 are chosen to sit well away from a ties-to-even
# boundary in OPL's frequency-to-fnum rounding (58/70/82 land ~0.0005 from
# one), so the same fixture stays safe if the OPL leg is armed later.
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
    # 480 ticks/quarter (header below) at the default 120 BPM => 960 ticks = 1 s.
    voices = [(1, 60), (1, 65), (2, 62), (2, 67)]
    ev = bytearray()
    for ch in (1, 2):
        ev += vlq(0) + bytes([0xC0 | ch, 48])          # program change -> 48
    for ch, note in voices:
        ev += vlq(0) + bytes([0x90 | ch, note, 90])    # note on, all at tick 0
    # Held to 1.5 s so the whole 1-second render window is inside the note,
    # with no boundary case on the final sample.
    ev += vlq(1440) + bytes([0x80 | voices[0][0], voices[0][1], 0])
    for ch, note in voices[1:]:
        ev += vlq(0) + bytes([0x80 | ch, note, 0])     # note off
    ev += vlq(0) + bytes([0xFF, 0x2F, 0x00])  # end of track

    hdr = b"MThd" + struct.pack(">I", 6) + struct.pack(">HHH", 0, 1, 480)
    trk = b"MTrk" + struct.pack(">I", len(ev)) + bytes(ev)
    with open(sys.argv[1], "wb") as f:
        f.write(hdr + trk)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
