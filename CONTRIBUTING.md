# Contributing to Olduvai

## Content policy: an allowlist

This repository may contain:

- Original C++ engine source, build system, CI, tests.
- Authored runtime catalogs (human-written name→index mappings). Citation
  comments referencing evidence (e.g. function/offset identifiers) are
  fine: they are references to evidence, not copies of it.
- Factual format documentation (archive layout, sprite formats, level
  formats) written from scratch, in original wording.
- Synthetic test fixtures: hand-authored byte sequences exercising decoder
  edge cases. **Never bytes taken from the game files.**
- Bundled third-party libraries (as sources, with their license
  files), inventoried in THIRD-PARTY-NOTICES.md.
- A **small, curated** set of stills and short **silent** clips of the
  engine's own output, under `assets/screenshots/` and nowhere else.  An
  engine whose point is rendering fidelity cannot show what it does without
  frames, and the widescreen margins and the vector HUD are *this project's*
  output, not the original's.  The rules, the copyright notes and the
  regeneration recipe are in
  [assets/screenshots/README.md](assets/screenshots/README.md); the public
  position is in [LEGAL.md](LEGAL.md). `check_tree.sh` enforces location,
  count, size and silence; **the rest of the curation is a human job**:
  engine output only, never game art on its own, and never a route for
  shipping game files.

It must NEVER contain:

- Game files or any byte ranges of them (`*.CUR`, `*.VGA`, `HISTORIK.EXE`).
- Data tables copied verbatim from the game executable (tile tables,
  object/spawn tables, sprite definition blocks). The engine reads them
  from the user's own files at run time.
- Decompilation or disassembly text in any form, not even in comments.
  Cite offsets instead.
- Screenshots, clips or any other game imagery outside
  `assets/screenshots/`, and any clip with audio.
- Roland MT-32 / CM-32L ROMs.
- AI co-authorship trailers or attributions (`Co-Authored-By`,
  "Generated with ..."), anywhere: commit messages or files.

**Knowledge crosses the boundary; content does not.**

## Attribution in source

Code comments describe formats and algorithms **generically**. Never
attribute an implementation to the original game's developer or publisher
(no "as used by <company>"). LZSS, PackBits, IFF/ILBM and friends are
industry-common techniques; describe the parameters of the variant, cite
public references (e.g. format wikis), and leave it at that. The game's
name appears in the README and user-facing docs only.

## User-facing language

User-visible strings (UI, logs, errors, CLI help) say *prepare*, *index*,
*cache*, *decode*, *read*. They never say *extract*, *rip*, *dump*, or
*decompile*. Technical docs and internal identifiers are not restricted.

## Contributions and copyright (read before submitting anything)

Olduvai is developed by a **single copyright holder**, and the project
deliberately preserves the ability to offer licenses other than the GPL
(dual licensing). To keep that possible:

- **External contributions are not accepted at this time.** Bug reports,
  Findings-style evidence, and discussion are very welcome; patches are
  not merged.
- If and when contributions open, they will require a **Contributor
  License Agreement (CLA)** assigning or broadly licensing the
  contribution to the project author, so the codebase keeps a single
  licensing authority. A patch submitted without a signed CLA will not
  be merged, however good it is.

## Naming

"Olduvai" is the project's claimed trademark (see README). Forks must be
renamed. Do not use the name for derived products, packages, or services
without written permission.

## Commit discipline

- Sole-human-author commits. Enable the hook once per clone:

  ```sh
  git config core.hooksPath scripts/hooks
  ```

- CI rejects forbidden trailers and denylisted content on every push
  (`scripts/check_tree.sh`, `scripts/check_commit_range.sh`).

## Engineering direction (release candidate)

The fun is the invariant. The game plays the way the 1991 original did, and
no cleanup is allowed to change that. The engine is feature-complete and
its untangling is done; what holds it there is the same discipline that did
the work, and the gates below it:

- **Byte-identical, gate-verified moves.** A refactor preserves emitted
  behaviour exactly: the same gameplay math, RNG consumption/order,
  frame-loop order, transition timing (see [docs/FRAME_LOOP.md](docs/FRAME_LOOP.md)).
  It lands only when the full test suite and the cross-engine trace corpus
  stay green ([docs/METHOD.md](docs/METHOD.md)).
- **One seam per commit.** Extract a single cohesive unit at a time behind
  a small dependency struct, so the moved code reads verbatim; no drive-by
  behaviour changes riding along in the same change.
- **No rewrites.** I reshape working code in place; I don't replace it
  with a "cleaner" version whose behaviour then has to be re-earned.
- **Gate first where the eyes are the judge.** Anything whose result isn't
  covered by a test (a visual composite, audio timing) gets a gate, or a
  playtest, before it's touched.
- **The gates hold the line.** A function may not grow past the
  `.clang-tidy` size and complexity thresholds without a reason in
  `scripts/complexity_baseline.txt` (the ratchet); the whole tree reads
  clean under the widened clang-tidy set; `scripts/metrics/coverage_layers.sh`
  says what the suite actually runs.

When in doubt, favour the play experience over the diff.

## Code style

- C++17, no exceptions across module boundaries for control flow, no raw
  `new`/`delete` (use values and `std::vector`/`std::unique_ptr`).
- Layering (lower layers must not include from higher ones):
  `formats` → `prepare` → `core` → `systems` → `enhance` → `presentation` →
  `app` ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)).  `formats`, `core`,
  `systems` and `enhance` must not touch SDL.
- Every game-behaviour constant cites its evidence
  (`// FUN_xxxx_yyyy +0xNN`) in a comment.

## Where a shared helper lives (the anti-duplication rule)

`scripts/check_layers.sh` ranks the seven layers, but says nothing about
ownership *inside* a layer, and `presentation/` is the largest. With no
rule, a four-line local copy in an anonymous namespace is always the cheaper
move: internal linkage, no header edit, no rebuild fan-out, and no compiler,
linker or check will ever object. That is how `slurp` reached **nine** copies
and the upscale-then-upload idiom **six**.

So, the rule:

- A **stateless leaf helper** used by 2+ translation units lives in a shared
  header of its layer: `presentation/image_out.hpp` (image/file output),
  `presentation/window_util.hpp` (SDL/window/texture), `systems/sprite_ids.hpp`
  (entity sprite ids + classification), `formats/byteorder.hpp` (integer
  reads), or a new small header if none fits.
- Adding a **new local copy of a helper that already exists elsewhere** is a
  review finding, not a style preference. Grep before you write the four lines.
- **Exception, and it is a real one:** a verbatim extraction may carry its
  private helper along, because carrying it is what keeps the before/after
  byte-diff a valid proof. Note it in the commit message so the count is
  visible rather than silently growing.

The point is not line count. It is that a duplicated helper diverges: the
FramePresenter extraction froze a live `opts.hd_profile` into a copy and the
whole 27-test corpus passed straight through the bug (`b6ec12f` → `def8235`).
