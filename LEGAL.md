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

- The repository and every release binary contain **no code, art, audio,
  levels, music, or any other data** from the original game, and none will
  ever be accepted.
- This is machine-enforced in CI (`scripts/check_tree.sh`): no game files, no
  byte ranges of them, no data tables copied from the executable, no game
  screenshots — test goldens are SHA-256 hashes, never images.
- The full content policy is in [CONTRIBUTING.md](CONTRIBUTING.md).
- The engine reads the data files from **your** copy of the game, prepares a
  local cache on your machine, and never modifies or redistributes them.

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
