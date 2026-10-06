# xBRZ: vendored

"Scale by rules", a pixel-art upscaler by Zenju, used as the `xbrz` HD profile.

| | |
|---|---|
| upstream | https://sourceforge.net/projects/xbrz/ (`xBRZ_1.9.zip`, 2026-01-25) |
| archive sha256 | `b2dff73b3abd24a18a7cde78d5ff5ed8f0922296dce6ed734dce2264cd0a0fc9` |
| licence | GPL-3.0, named by its GPLv3 URL in every file header; the headers do not say "or later", so the compiled binary is conveyed under GPL-3.0 only (see `THIRD-PARTY-NOTICES.md`) |
| local changes | listed below; each modified file says so in its header |

## Local changes

1. C++17 spelling: two `[[likely]]` attributes dropped, and the lambda in
   `blendPixel` gets its empty parameter list (upstream writes `[&] -> bool`,
   which needs C++23).
2. Only the unbuffered ARGB scaler is kept. Upstream's default path builds a
   64 MB colour-distance table on first use (about 80 ms, 130 MB peak, on a
   Mac), which a handheld should not pay; the unbuffered path computes the same
   distance directly, is 5 to 25 percent slower per frame, and is the exact
   (unrounded) one. Removed with it: the RGB and buffered-ARGB paths, the
   bilinear and nearest helpers, the RGB conversion routines, the MSVC debugger
   hooks, and the `ColorFormat` parameter of `xbrz::scale`.
3. A `DistancePath` parameter on `xbrz::scale`: `Reference` is upstream's colour
   distance; `AlphaTable` skips its two divisions by 255 for opaque pairs and
   reads the other alphas from a `constexpr` table. They agree bit for bit (the
   same 3000-case comparison, and `test_upscale`'s random-alpha test on every
   host). The default is `AlphaTable` on armhf builds only: on a 320x200 frame it
   cuts a Cortex-A7 (Powkiddy A12) from 45/48/52 ms to 31/35/38 ms at x2/x3/x4
   on one thread, but on a Cortex-A53 (TrimUI) the same change is 38% slower, so
   aarch64 and desktop keep `Reference`.
4. The rotated getters of kernel corner `a` removed: upstream never reads
   that corner after rotation, and clang's `-Wunused-function` rejects them.

## How it was checked (2026-10-04)

- Output of the trimmed copy equals upstream's `argbUnbuffered`, byte for byte,
  on 3000 random cases (1x1 to 40x30, factors 2 to 6, opaque, partly
  transparent and partial-alpha images).
- AddressSanitizer and UBSan clean on the same cases, including the
  scratch-buffer trick the scaler plays at the end of its own output, and
  slice-by-slice runs equal whole-image runs.
- Floating point: one case in 4500 differs between a build that fuses
  multiply-adds (the arm64 and armhf default) and one that does not; builds with
  `-ffp-contract=off` agree at every optimisation level. `CMakeLists.txt` pins
  that flag on `xbrz.cpp`, as it does for `omniscale.cpp`, so goldens match on
  every platform.

## Re-sync

Take the new `xbrz.h`, `xbrz_config.h`, `xbrz_tools.h`, `xbrz.cpp`, redo the two
changes above, rerun the comparison against upstream's unbuffered path, and
update the version and hash here.
