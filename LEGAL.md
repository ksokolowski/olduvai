# Legal

The complete legal position of the Olduvai project, in one place.
Nothing here is legal advice; it states what the project is and does.

## What Olduvai is — and is not

- An **independent engine reimplementation**, written from scratch in C++.
- **Not** a decompilation, a disassembly dump, or a reconstruction of the
  original source code. The engine was written from *behavioural* findings;
  the method is documented in [docs/METHOD.md](docs/METHOD.md).
- The knowledge needed for the engine to interoperate with the original data
  files was obtained by lawful analysis of a legitimately owned copy, for
  interoperability purposes (cf. EU Directive 2009/24/EC, arts. 5(3) and 6).

## No game content — ever

- The repository and every release binary contain **no code, audio, music,
  levels, or data files** from the original game, and none will ever be
  accepted.
- This is machine-enforced in CI (`scripts/check_tree.sh`): no game files, no
  byte ranges of them, no data tables copied from the executable, and no
  images or video outside the directories allowed for them — test goldens
  are SHA-256 hashes, never images.
- The full content policy is in [CONTRIBUTING.md](CONTRIBUTING.md).
- The engine reads the data files from **your** copy of the game, in place,
  and never modifies, copies or redistributes them.

## Screenshots and clips — the one curated exception

- [`assets/screenshots/`](assets/screenshots/README.md) holds a small set of
  stills and short, **silent** clips of *this engine* running a legitimately
  owned copy of the game — enough to show what the engine does: the classic
  mode, the HD scalers, and the widescreen margins and vector HUD it adds.
- This follows established practice: Wikipedia articles about games
  illustrate gameplay with a reduced screenshot to identify the work, and
  stores show gameplay screenshots in their listings — including the game's
  [GOG.com page](https://www.gog.com/game/prehistorik_12), which is where we
  recommend getting the game.
- The game's artwork visible in these frames belongs to its rights holders,
  and this project holds **no license** to it. The frames are few, reduced in
  size, carry no audio, and are shown to identify the game and illustrate the
  engine — never as a substitute for the game. None of them is part of a
  release binary.
- The limits are enforced in CI where a machine can check them (location; at
  most 16 files of at most 2 MiB each; no audio track in any clip). The rest
  is curated by hand, under the rules in the directory's README.
- **Removal on request:** a rights holder who wants any of these files removed
  can open an issue on the project, and it will be taken down.

## The original game

- Prehistorik © 1991 Titus Interactive.
- Prehistorik, Titus, and all related marks are trademarks or registered
  trademarks of their respective owners.
- Olduvai and its maintainers are **in no way affiliated with, associated
  with, or endorsed by** any rights holder of the original game.
- To play, you must own the original game — e.g. [*Prehistorik 1+2* on
  GOG.com](https://www.gog.com/game/prehistorik_12), or your own original
  DOS/floppy copy.

## Project name and marks

"Olduvai"™ is the name of this project and is claimed as a trademark by the
project author. The bone logo and the fire-styled wordmark
([`assets/`](assets/README.md) — original art, not game-derived) are the
project's marks. The GPL covers the *code*, not the *name or marks*:

- Forks and derivative distributions must use a **different name** and must
  not present themselves as Olduvai or as endorsed by this project.
- No product or service may be marketed under the Olduvai name without the
  author's written permission.
- Unmodified redistribution that clearly points back to this repository as
  the origin may keep the name.

## License

- Copyright (C) 2026 Krzysztof Sokołowski (sole copyright holder; every
  project source file carries the SPDX license tag and this notice).
- Code: **GPL-3.0-or-later** — [LICENSE](LICENSE). The license does not grant
  any rights to the project name, the project marks, or any third-party
  trademark.
- Bundled and vendored third-party components are listed with their licenses
  in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); every release binary
  carries the same texts in a `licenses/` directory.
