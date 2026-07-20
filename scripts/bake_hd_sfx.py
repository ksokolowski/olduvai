#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""Optional HD SFX bake — audio super-resolution for the decoded samples.

Pipeline (all in the user's local cache; nothing enters any repository):

    olduvai --decode-sfx                  # engine: VOCs -> hd_sfx_src/*.wav
    python3 scripts/bake_hd_sfx.py        # this tool: -> hd_sfx/*.wav
    olduvai --play --enhanced ...         # engine prefers the baked set

Requires the `audiosr` package (PyTorch-based; heavyweight — deliberately
NOT a project dependency):

    pip install audiosr

The output keeps the source file's content-addressed name, which is how the
engine matches a bake to its sample.  Rerunning is idempotent; delete
hd_sfx/*.wav to fall back to the native samples.
"""
from __future__ import annotations

import argparse
import os
import sys
import wave
from pathlib import Path


def cache_root() -> Path:
    env = os.environ.get("OLDUVAI_CACHE_DIR")
    if env:
        return Path(env)
    xdg = os.environ.get("XDG_CACHE_HOME")
    if xdg:
        return Path(xdg) / "olduvai"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "olduvai"
    return Path.home() / ".cache" / "olduvai"


def read_wav_mono(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)
    samples = [
        int.from_bytes(raw[i : i + 2], "little", signed=True) / 32768.0
        for i in range(0, len(raw), 2)
    ]
    return samples, rate


def write_wav_mono16(path: Path, samples, rate: int) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for s in samples:
            v = max(-1.0, min(1.0, float(s)))
            frames += int(v * 32767).to_bytes(2, "little", signed=True)
        w.writeframes(bytes(frames))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache-dir", type=Path, default=None,
                    help="olduvai cache root (default: auto-detect)")
    ap.add_argument("--target-rate", type=int, default=48000)
    args = ap.parse_args()

    root = args.cache_dir if args.cache_dir else cache_root()
    src_dir = root / "hd_sfx_src"
    dst_dir = root / "hd_sfx"
    sources = sorted(src_dir.glob("*.wav"))
    if not sources:
        print(f"bake_hd_sfx: no sources in {src_dir} — "
              "run `olduvai --decode-sfx` first", file=sys.stderr)
        return 1

    try:
        import torch  # noqa: F401
        from audiosr import build_model, super_resolution
    except ImportError:
        print("bake_hd_sfx: the `audiosr` package is required "
              "(pip install audiosr).  It is deliberately not a project "
              "dependency — this bake is optional.", file=sys.stderr)
        return 1

    dst_dir.mkdir(parents=True, exist_ok=True)
    model = build_model(model_name="basic")
    ok = 0
    for src in sources:
        out = dst_dir / src.name
        print(f"bake_hd_sfx: {src.name} ...")
        # audiosr consumes a file path and yields upsampled audio at 48 kHz.
        waveform = super_resolution(model, str(src),
                                    guidance_scale=3.5, ddim_steps=50)
        samples = waveform.squeeze().tolist()
        if args.target_rate != 48000:
            # audiosr emits 48 kHz; integer-step decimation only (keep simple)
            step = 48000 / args.target_rate
            samples = [samples[int(i * step)]
                       for i in range(int(len(samples) / step))]
        write_wav_mono16(out, samples, args.target_rate)
        print(f"bake_hd_sfx:   -> {out}")
        ok += 1
    print(f"bake_hd_sfx: {ok}/{len(sources)} baked into {dst_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
