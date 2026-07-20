# Format-parser fuzzing

Pre-emptive hardening for the public release: users feed the engine corrupt,
truncated, or foreign-release game files, and every parser must **reject
cleanly (throw), never crash / over-read / over-allocate**. `fuzz_formats.cpp`
is one libFuzzer harness over all nine binary formats (selector byte + payload;
see its header for the mapping).

## Build (clang only — libFuzzer)

Apple Command Line Tools clang lacks libFuzzer; use Homebrew LLVM (or any
clang with `-fsanitize=fuzzer`):

```sh
brew install llvm   # once
CC=/opt/homebrew/opt/llvm/bin/clang CXX=/opt/homebrew/opt/llvm/bin/clang++ \
  cmake --preset fuzz \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build --preset fuzz --target fuzz_formats
```

`OLDUVAI_FUZZ` requires `OLDUVAI_SANITIZE` (a fuzzer over an uninstrumented
library finds nothing).

## Run

```sh
mkdir -p /tmp/fuzzcorp
# seed from real files for depth (LOCAL ONLY — never commit game-derived bytes)
ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/tests/fuzz_formats \
    -max_total_time=600 -rss_limit_mb=2048 -malloc_limit_mb=512 -timeout=10 \
    -artifact_prefix=/tmp/ /tmp/fuzzcorp <seed_dir>
```

Seeding the corpus from valid headers (a `selector` byte prefix + a real
`.PC1`/`.MAT`/`.MDI`/`.VOC`/`.DUR`/`.CUR` sample) reaches the deep decode
branches an empty corpus can't — that is where the real bugs were.

## Content policy

The corpus and any crash reproducers contain game-derived bytes → **`/tmp`
only, never committed** (enforced by `scripts/check_tree.sh`). This dir holds
only the harness + this README.

## Findings so far (2026-07-10, first run)

Seven bugs on hostile input, all fixed at the root. Two formats reached deep
decode paths: `.MDI` (music) and `.PC1` (IFF/ILBM images).

`.MDI` — `formats::write_vlq` (mdi.cpp) + `MidiSequencer::load` (midi_seq.cpp):
- #1 write_vlq — a corrupt tick delta overran a 5-byte stack; clamped to the
  SMF 4-byte VLQ max (0x0FFFFFFF).
- #2 read-bounds — a truncated track over-read past the buffer at the
  meta-type / d1 / d2 reads; every read now bounded against the track `end`,
  and `read_vlq` takes an explicit `end`.
- #3 event cap — a crafted track grew `events_` to ~384 MB; capped at 1<<20
  events (~16 MB, ~100x any real track).
- #4/#5 hang — two distinct crafted tracks spun the bounded `while (pos < end)`
  loop forever: one a zero-consume `continue` stall, one an oscillation
  (`pos` cycling between values) an equality-only progress check missed. Fixed
  with an absolute iteration ceiling (`end + 16`) — valid parsing consumes >=1
  byte per iteration, so it never fires on real input.

`.PC1` — `parse_pc1` / `decode_planes` (pc1.cpp):
- #6 shift-UB — a hostile BMHD (`nplanes` up to 255) shifted an int by >=32 in
  the plane accumulator; reject `nplanes > 8` (a `uint8_t` palette index can't
  hold more, and no real ILBM uses more).
- #7 OOM — a 16-bit `width`/`height` (65535x65506) forced a ~4 GB pixel/plane
  allocation from a tiny file (the plane buffer zero-pads to the declared size
  regardless of BODY length); cap the area at 16 Mpx, far above any VGA
  screen-sized asset.

Regression: golden_trace + opl_music (real-`.MDI` playback) + the PC1 decode
tests pin that every clamp/bound/cap is inert on valid data. Confirmation
re-fuzz: 248k runs, 0 new artifacts after the last fix.
