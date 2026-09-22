# Screenshots and clips

A **small, curated** set of stills and short clips of *this engine's own
output*, used to show what Olduvai does — in this repository's README and in
a port's store listing. They live here and nowhere else.

## What they are — and are not

- Frames rendered by Olduvai while it runs a **legitimately owned copy** of
  the game. Olduvai is an independent engine recreation; it ships no game
  files and no game data — see [LEGAL.md](../../LEGAL.md).
- The game's artwork visible in these frames belongs to its rights holders,
  and this project holds no license to it. The frames are here to identify
  the game and illustrate the engine — the classic mode, the HD scalers, and
  the widescreen margins and vector HUD this engine adds. They are no
  substitute for the game: to play, you need your own copy.
- This is established practice: Wikipedia articles about games illustrate
  gameplay with a reduced screenshot, stores show gameplay screenshots in
  their listings — including the game's
  [GOG.com page](https://www.gog.com/game/prehistorik_12), where we recommend
  getting it — and comparable engine recreations illustrate theirs the same
  way.
- **Clips are silent.** The game's music and sound effects are not ours, so no
  clip carries an audio track.
- **Removal on request.** A rights holder who wants any file here removed can
  open an issue, and it will be taken down.
- None of these files is part of a release binary.

## Rules

| Rule | Enforced by |
|---|---|
| Only in this directory; PNG, JPEG or MP4 | `scripts/check_tree.sh` |
| At most 16 files, each at most 2 MiB | `scripts/check_tree.sh` |
| Clips have no audio track | `scripts/check_tree.sh` |
| Engine output only: gameplay and the engine's own menus and overlays — never game art on its own (sprite sheets, tile sets, a bare background, a title screen as standalone art) | review |
| Short: a clip shows a feature in seconds, not a level walkthrough (aim for 30 s or less) | review |
| No larger than it needs to be (a still at 960x600 or 1280x800) | review |

The machine checks cover location, count, size and audio. They cannot tell a
frame from a photograph, so the rest of the curation is a human job.

## Choosing the frame

- **Classic — canonical, 16:10.** The game's 320x200 grid is 16:10 and scales
  by whole numbers (640x400, 960x600, 1280x800), so every pixel stays square
  and even. Scale with nearest neighbour.
- **As it looked in 1991 — 4:3.** DOS showed 320x200 on a 4:3 monitor with
  pixels 1.2x taller than wide; `--aspect 4:3` reproduces that, at the cost of
  a non-integer vertical scale.
- **Enhanced widescreen — 16:9** (a 1280x720 window). The side margins are this
  engine's own output. Wider screens are out of scope.

## Naming

`<mode>-<level-or-screen>[-<detail>].<ext>` — lower case, hyphens, no spaces:
`classic-l1.png`, `enhanced-smooth-x3-l1.png`, `widescreen-l1.png`,
`classic-l1-clip.mp4`. JPEG only where a store requires it.

## Regenerating

From your own game files, with the config isolated (`--no-config`, or a
scratch `XDG_CONFIG_HOME`) so a saved `play.json` cannot silently change the
mode being captured — and check the output dimensions afterwards:

```sh
# a still: composed frame N of a replay
olduvai --game-dir <your game> --no-config --profile dos --play --level 1 \
    --replay tests/fixtures/l1_full_clear.jsonl \
    --play-shot classic-l1.png --play-shot-frame 180

# a clip: every steady classic frame, then silent H.264 at the game's 18.2 Hz
OLDUVAI_DUMP_STEADY=frames olduvai --game-dir <your game> --no-config \
    --profile dos --play --level 1 \
    --replay tests/fixtures/l1_full_clear.jsonl --play-frames 420
ffmpeg -framerate 18.2065 -i frames/steady_fb_%04d.bmp \
    -vf scale=640:400:flags=neighbor -c:v libx264 -pix_fmt yuv420p -an \
    -movflags +faststart classic-l1-clip.mp4
```

### Compress before committing — and keep it PNG

Pixel art is JPEG's worst case: flat palette colours and hard edges are
exactly where it rings and smears, and it is not even smaller. Measured on
`classic-l1.png` (2026-09-22): JPEG q85 was 218 KB and lossy, against 17 KB
as a lossless PNG. So:

- **Classic** — the game draws in a handful of colours (14 in that frame), so
  save as an **indexed PNG** with exactly those colours: lossless,
  pixel-identical, and a third smaller than a plain RGB PNG.
- **Enhanced** — the HD scalers blend neighbours into thousands of colours;
  **quantise to 256** (no dither). The source palette is small enough that
  this is visually indistinguishable — 47 dB PSNR against JPEG q85's 36 dB at
  the same size — and it took `enhanced-widescreen-l1.png` from 643 to 256 KB.

```python
from PIL import Image
im = Image.open("shot.png").convert("RGB")
n = len(im.getcolors(1 << 24) or [])
if 0 < n <= 256:   # classic: lossless, exact palette
    im.quantize(colors=n, dither=Image.Dither.NONE).save("shot.png", optimize=True)
else:              # enhanced: 256 colours
    im.quantize(colors=256, dither=Image.Dither.NONE).save("shot.png", optimize=True)
```

`OLDUVAI_DUMP_STEADY` writes frames before upscaling (320x200, classic HUD),
so a clip made this way is classic. For an HD clip, `OLDUVAI_DUMP_OUTPUT=<dir>`
dumps the final output (scaler, margins, vector HUD); drop the audio with `-an`.

A clip **with sound** carries the game's music and effects, so it never
belongs here, and `check_tree.sh` rejects any tracked MP4 with an audio
track. (`OLDUVAI_AUDIO_CAPTURE=<wav>` can record the engine's output for a
preview on your own device; every frame dump stamps its frames on the same
clock, in `<dir>/frames.txt`, so the two line up.)
