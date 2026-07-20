# Project assets

This directory holds everything the engine ships beside its binary — and the
**only graphic assets in the repository and in release binaries**. Nothing
here is taken from, derived from, or traced over the original game's artwork
(`scripts/check_tree.sh` enforces in CI that no image or font files exist
anywhere else in the tree; see the content policy in
[CONTRIBUTING.md](../CONTRIBUTING.md)). Three kinds of content live here:

- **`icon/` + `logo/` — the project's own creative work.** The bone logo and
  the fire-styled "OLDUVAI" wordmark are the project's marks — see the "Project name and marks"
  section of [LEGAL.md](../LEGAL.md) for the trademark note. Licensed with the
  repository (GPL-3.0-or-later); the license does not grant rights to use the
  name or the marks to identify other projects.
- **`fonts/` — bundled third-party typefaces** (Freckle Face, Noto Sans),
  SIL OFL 1.1, each with its license text beside it. Not our work — see
  [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
- **`data/` — the declarative menu model** (`menus.json`, shared with the
  reference engine). Compiled into the binary at build time AND copied
  beside it (`<build>/data/menus.json`, searched first at runtime) so a
  user can customise the menus without rebuilding.

## Inventory

| File | What it is |
|---|---|
| `icon/icon_src.png` | Icon master, 1024×1024 — the bone on a rounded plate |
| `icon/Olduvai.icns` | macOS app-bundle icon (from the master) |
| `icon/olduvai.ico` | Windows exe icon, multi-resolution 16–256 px (from the master) |
| `logo/olduvai-logo.png` | Full logo master — fire-styled wordmark + bone |
| `logo/olduvai-logo-1200.png` | Full logo, 1200 px wide (README header) |
| `logo/olduvai-wordmark.png` | Wordmark only (transparent) |
| `logo/bone.png` | Bone only, transparent, ~1000 px |
| `logo/bone-256.png` / `logo/bone-128.png` | Small transparent bone variants |
| `fonts/FreckleFace-Regular.ttf` (+LICENSE) | HD vector-text face — also the wordmark's face (OFL 1.1) |
| `fonts/NotoSans-Regular.ttf` (+LICENSE) | Alternate HD text face (OFL 1.1) |

All logo PNGs have transparent backgrounds.

## How they are made (regeneration)

The wordmark/logo/bone PNGs are **rendered by the engine's own code** — the
same vector-text renderer (`HdText`, Freckle Face, SIL OFL 1.1), "caveman"
fire-and-blood banner shade, and vector-bone silhouette the main menu draws
at runtime — via the dev tool `tools/logo_render.cpp`:

```sh
cmake --build --preset release --target olduvai_logo
build/release/tools/olduvai_logo assets/logo
magick assets/logo/olduvai-logo.png -resize 1200x assets/logo/olduvai-logo-1200.png
magick assets/logo/bone.png -resize 256x assets/logo/bone-256.png
magick assets/logo/bone.png -resize 128x assets/logo/bone-128.png
```

Re-run after changing the menu title styling so the logo stays in sync.
(Rendering output of an OFL-licensed font is unrestricted; the font itself
ships beside the binary under its own license — see
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).)

Icon formats, from the master:

```sh
# Windows .ico
magick assets/icon/icon_src.png -define icon:auto-resize=256,128,64,48,32,24,16 assets/icon/olduvai.ico
# macOS .icns: iconutil over an iconset generated from the master
```

The runtime window icon is `icon_src.png` embedded into the binary at build
time (`cmake/embed_binary.cmake`); the AppImage desktop icon and the README
header reference these files directly.
