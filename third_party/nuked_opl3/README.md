# Nuked OPL3

Cycle-accurate Yamaha YMF262 (OPL3) / YM3812 (OPL2) emulator by Nuke.YKT.

- **Source:** vendored from the upstream
  [nukeykt/Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (the same
  core DOSBox-X uses). Single-translation-unit C (`opl3.c` + `opl3.h`).
- **License:** LGPL-2.1-or-later (see the header banner in `opl3.c`/`opl3.h`).
- **Why:** the original Prehistorik sound effects are AdLib FM, driven by raw
  OPL register writes — not digital samples. Olduvai's `--sfx-backend opl`
  path renders them through this emulator, byte-faithful to the walked
  `HISTORIK.EXE` pipeline (`FUN_1fe0_018b` voice install → note-on). The
  Python reference renders the identical register stream through this same
  core, so the two outputs match.

API used: `OPL3_Reset(chip, samplerate)`, `OPL3_WriteReg(chip, reg, val)`,
`OPL3_GenerateStream(chip, int16* buf, numframes)` (interleaved stereo).

To update: re-copy `opl3.c`/`opl3.h` from upstream and re-run the
olduvai test suite (opl_music PCM parity pins the behaviour).
