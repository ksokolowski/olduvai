#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Analyse a rendered PCM WAV, or compare two of them across platforms.
#
#   audio_analyze.py check <wav>            invariants any correct render holds
#   audio_analyze.py compare <a> <b>        cross-platform correlation
#
# WHY INVARIANTS AND NOT A GOLDEN HASH.  A committed reference render cannot
# work here: the MT-32 output depends on WHICH Roland ROMs the user owns
# (MT-32 vs CM-32L, and revision) and the GM output on WHICH SoundFont they
# have, so a golden would false-fail for almost everyone — and it would be
# synth-ROM-derived audio committed to the tree, which this project does not
# do.  Worse, the numbers say a golden could not be shared across platforms
# anyway: measured macOS/arm64 vs Windows/x86-64, MT-32 differs in 97.6% of
# samples (max delta 730 of peak 8148, 1.5% rms) because munt's LA32 and its
# IIR reverb are floating-point and the error compounds.  Rendering at MT-32's
# native 32 kHz — no resampler involved — does not help, so it is the synth
# core, not the sample-rate conversion.
#
# What IS platform-independent is the FIXTURE: tests/audio_fixture.py plays a
# sustained chord of MIDI notes 60/62/65/67.  Any correct render must put its
# energy at those fundamentals, whatever the timbre.  That catches the
# failures worth catching — silence, one dead channel, a wrong or unloaded
# SoundFont, a broken resampler, a wrong sample rate — while tolerating the
# floating-point drift that is inherent and harmless.
#
# `compare` exists for the deliberate cross-platform check: render on one
# machine, copy the WAV, and correlate.  Correlation is the right measure —
# it is blind to the drift and sensitive to actually-different audio.
# Measured: GM 1.000000, MT-32 0.9989 across macOS/Windows.
import math
import struct
import sys
import wave

# tests/audio_fixture.py voices — the pitches the render MUST contain.
NOTES = (60, 62, 65, 67)
# Between-note controls: no correct render should have comparable energy at a
# semitone we never played.  (61/63/66 sit between the chord tones.)
CONTROLS = (61, 63, 66, 71)


def midi_hz(note: int) -> float:
    return 440.0 * (2.0 ** ((note - 69) / 12.0))


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if width != 2:
        raise SystemExit(f"{path}: expected 16-bit PCM, got {width * 8}-bit")
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    left = list(s[0::ch]) if ch > 1 else list(s)
    right = list(s[1::ch]) if ch > 1 else list(s)
    return left, right, rate


def goertzel(samples, rate, freq):
    """Energy at one frequency — no numpy, so the gate never skips for it."""
    n = len(samples)
    k = int(0.5 + (n * freq) / rate)
    w = (2.0 * math.pi / n) * k
    coeff = 2.0 * math.cos(w)
    s0 = s1 = s2 = 0.0
    for x in samples:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    return math.sqrt(max(s1 * s1 + s2 * s2 - coeff * s1 * s2, 0.0))


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs)) if xs else 0.0


def check(path):
    left, right, rate = read_wav(path)
    mono = [(a + b) * 0.5 for a, b in zip(left, right)]
    peak = max((abs(x) for x in mono), default=0)
    problems = []

    if peak <= 100:
        problems.append(f"silent or near-silent (peak {peak})")
    # A dead channel is a real, shipped-once class of bug and a hash would
    # not name it.
    if rms(left) <= 1.0 or rms(right) <= 1.0:
        problems.append(f"a channel is dead (rms L={rms(left):.1f} R={rms(right):.1f})")

    on = sum(goertzel(mono, rate, midi_hz(n)) for n in NOTES)
    off = sum(goertzel(mono, rate, midi_hz(n)) for n in CONTROLS)
    ratio = on / off if off > 0 else float("inf")
    # The fixture's own pitches must dominate semitones it never played.
    if ratio < 2.0:
        problems.append(
            f"energy is not at the fixture's pitches (on/off {ratio:.2f}, need >= 2.0)"
        )

    print(f"  peak={peak} rms={rms(mono):.1f} L/R={rms(left):.0f}/{rms(right):.0f} "
          f"pitch_ratio={ratio:.2f}")
    for p in problems:
        print(f"  FAIL: {p}")
    return 1 if problems else 0


def compare(a_path, b_path):
    a, ar, _ = read_wav(a_path)
    b, br, _ = read_wav(b_path)
    a = [(x + y) * 0.5 for x, y in zip(a, ar)]
    b = [(x + y) * 0.5 for x, y in zip(b, br)]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    cov = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    r = cov / math.sqrt(va * vb) if va > 0 and vb > 0 else 0.0
    peak = max(max((abs(x) for x in a), default=1), 1)
    err = math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(n)) / n)
    print(f"  correlation={r:.6f} rms_err={err:.1f} ({100 * err / peak:.3f}% of peak)")
    # 0.99 tolerates the measured 0.9989 cross-architecture drift with margin,
    # while a genuinely different render (wrong SoundFont, wrong ROM set,
    # wrong rate) falls far below it.
    if r < 0.99:
        print(f"  FAIL: correlation {r:.6f} < 0.99 — these are not the same render")
        return 1
    return 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return check(argv[2])
    if len(argv) >= 4 and argv[1] == "compare":
        return compare(argv[2], argv[3])
    sys.stderr.write(__doc__.split("\n#\n")[0] + "\n")
    sys.stderr.write("usage: audio_analyze.py check <wav> | compare <a> <b>\n")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
