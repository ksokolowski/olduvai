# Audio backends — the choice matrix

Two independent knobs: `--music-device` and `--sfx-backend` (config keys
`music_device` / `sfx_backend`). Defaults are `auto` for both.

## Music devices

| Device | Renders via | Needs | Character |
|---|---|---|---|
| `auto` | first available of: MT-32 → GM → OPL | — | best available |
| `opl` | EXE-faithful AdLib driver on vendored Nuked-OPL3 (`opl_music.cpp`) — plays the container's authored FF 7F voice patches | nothing (built in) | the 1991 AdLib sound; PCM byte-parity with the reference renderer (18/18 tracks via `tools/opl_music_dump`) |
| `gm-builtin` | FluidSynth (dlopen'd) + a SoundFont | `libfluidsynth` + a `.sf2` (auto-discovery prefers the Roland SC-55 face — `apt install scummvm-data`; see `LINUX_APPIMAGE.md`) | closest to the Windows GS Wavetable sound |
| `mt32-builtin` | libmt32emu (dlopen'd) | `libmt32emu` + **your own** MT-32/CM-32L ROMs (`--rom-dir`) | the authentic Roland target the composer scored for |
| `host-midi` / `mt32` | RtMidi → a real MIDI OUT port, raw MT-32 stream | a MIDI device/synth on the port | real hardware |
| `gm-host` | RtMidi → MIDI OUT with MT-32→GM program translation | a GM synth on the port (e.g. Windows GS Wavetable) | zero-setup GM on Windows |
| `none` | — | — | silence |

The `auto` chain: MT-32 ROMs → GM (SoundFont found) → OPL. OPL is the
always-works fallback; it needs nothing.

## SFX backends

| Backend | Renders via | Notes |
|---|---|---|
| `auto` | pairs to the music device | MT-32 music → `mt32-sfx`, GM music → `gm-sfx`, otherwise → `sb-dac` |
| `sb-dac` | the game's digital VOC samples | band-limited (windowed-sinc) upsampling + ~2 ms edge declick (`resample.hpp`) — the samples are 4 kHz recordings; this removes the imaging and per-trigger pop the analog SB output stage never produced |
| `opl` | FM synthesis, walked from the EXE's AdLib branch (`opl_sfx.cpp`, same Nuked-OPL3 core as music) | the 3 FM effects the EXE 'A' branch has; ids without an AdLib record fall through to VOC |
| `mt32-sfx` / `gm-sfx` | catalog note events baked to PCM through the active synth | |

## What the original EXE did (mode byte `DS:0x8db5`)

| 1991 setup | Music | SFX |
|---|---|---|
| `'A'` AdLib-only card | OPL FM | OPL FM (channel 3 voice swap) |
| Sound Blaster | OPL FM | digital VOC via the SB DSP |
| `'R'` Roland MT-32 | MT-32 | digital VOC via the SB DSP (not OPL) |
| `'I'` PC-speaker | buzzer variants (`*BUZ.MDI`) | — (buzzer mode is a follow-up) |

FM sound effects only ever occurred on an AdLib-only setup — any machine with
a Sound Blaster played digital samples. That is why `auto` never selects
`opl` SFX: it is an explicit nostalgic opt-in.

## Recommended combinations

| Goal | Flags |
|---|---|
| Best out-of-the-box (default) | *(none — auto: GM/SC-55 music + digital SFX)* |
| Pure 1991 AdLib nostalgia | `--music-device opl --sfx-backend opl` |
| Sound Blaster memories | `--music-device opl` *(digital SFX by auto-pair)* |
| Authentic Roland | `--music-device mt32-builtin --rom-dir <roms>` |

## Enhanced SFX (optional, enhanced mode only)

Faithful mode always plays the native samples bit-exactly. Enhanced mode can
use an optional baked variant of the digital (`sb-dac`) effects:

- **HD SFX bake** (opt-in, offline):

  ```sh
  olduvai --decode-sfx            # samples -> <cache>/hd_sfx_src/*.wav
  pip install audiosr             # heavyweight; deliberately not a project dep
  python3 scripts/bake_hd_sfx.py  # super-resolution -> <cache>/hd_sfx/*.wav
  ```

  In enhanced mode the engine prefers a baked variant when present (log line
  `sfx: HD bake variant loaded ...`); delete `<cache>/hd_sfx/*.wav` to fall
  back. Everything derives from the user's own files into the user's cache —
  nothing is shipped. Note the bake applies to the `sb-dac` backend; the
  synth SFX backends (`gm-sfx`/`mt32-sfx`/`opl`) don't use samples.

## Fidelity notes

- OPL music/SFX and the golden gameplay trace are independent — audio never
  touches gameplay RNG or the deterministic frame loop.
- Known deliberate deviation (both this engine and the reference): the EXE's
  'A' mode shares ONE OPL chip, so an SFX steals music channel 3 (key-off +
  patch swap) while it plays; both engines render SFX on a separate chip
  instance and mix digitally, so the music is never interrupted. Volume
  balance is calibrated to the single-chip output stage.
