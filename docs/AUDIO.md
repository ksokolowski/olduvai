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

## Where to put the assets you supply

Two music devices need a file this project cannot ship: `mt32-builtin` needs
your own Roland ROM images, `gm-builtin` needs a SoundFont. Both follow the
same convention — **a per-user directory, or beside the executable** — so there
is one place to learn rather than two.

Explicit settings always win: `--rom-dir <dir>` and `--soundfont <file.sf2>`
(config keys `rom_dir` / `soundfont`), with `$OLDUVAI_MT32_ROMS` and
`$OLDUVAI_SOUNDFONT` just behind them.

**On Windows you can skip all of it and still get General MIDI.** Every Windows
machine has the Microsoft GS Wavetable Synth behind the system MIDI mapper, so
with no ROMs and no SoundFont `auto` routes music there (`gm-host`) rather than
falling back to AdLib. Supply your own assets only if you want the authentic
Roland MT-32, or the engine's own GM rendering instead of the system synth.

### MT-32 / CM-32L ROMs

Searched in this order; the first directory holding a complete pair wins.

| | |
|---|---|
| any platform | `--rom-dir`, then `$OLDUVAI_MT32_ROMS` |
| Windows | `%LOCALAPPDATA%\olduvai\mt32-roms` |
| wherever `$HOME` is set | `~/.config/olduvai/mt32-roms`, then `~/mt32-roms` |
| any platform | `./mt32-roms` — beside the executable, which is how the portable zip is meant to be used |

A directory qualifies when it holds a **control + PCM pair**: either
`CM32L_CONTROL.ROM` + `CM32L_PCM.ROM`, or `MT32_CONTROL.ROM` + `MT32_PCM.ROM`.
Letter case is ignored, and the two files need not agree on it — combining a
legacy dump with a MAME-versioned one gives a mixed-case set, which is normal
and works.

If both pairs are present, **CM-32L wins**: it is the later machine and a
superset of the MT-32 (33 extra PCM samples). `--mt32-model mt32` or `cm32l`
forces the choice, and the startup log always names the pair it loaded — the
two sound audibly different, so which one you got should never be a guess.

When no pair is found the engine prints every directory it searched and the
filenames it wanted. Read that line before suspecting the ROMs themselves.

### GM SoundFont

Any of these, first match wins (`.sf2`):

| | |
|---|---|
| any platform | `--soundfont`, then `$OLDUVAI_SOUNDFONT` |
| wherever `$HOME` is set | `~/.config/olduvai/soundfonts/` — an **override** location: any recognised font here beats every system one |
| Windows | `soundfonts\` beside the executable, then `%LOCALAPPDATA%\olduvai\soundfonts` |
| macOS | Homebrew's `scummvm` / `sounds/sf2` / `soundfonts` dirs under `/opt/homebrew` then `/usr/local`, plus `/Library/Audio/Sounds/Banks` and `~/Library/Audio/Sounds/Banks` |
| any platform | `/usr/share/sounds/sf2`, `/usr/share/soundfonts`, `/usr/share/scummvm` |

Among the system directories the preferred **font** wins over the preferred
directory — `Roland_SC-55.sf2` → `GeneralUser-GS.sf2` (or `GeneralUser GS.sf2`)
→ `FluidR3_GM.sf2` → `default-GM.sf2` — so an SC-55 anywhere on the list beats
a FluidR3 sitting earlier. The config directory above is the exception only to
that ordering, not to the names: a font there wins over every system copy, but
it still has to be called one of them. **An arbitrarily-named `.sf2` is never
auto-discovered anywhere** — rename it, or point at it with `--soundfont`.

Roland SC-55 is the most faithful GM voice (the Sound Canvas set, same lineage
as Windows' `gm.dls`); ScummVM ships it under GPLv3. Linux specifics, including
the `scummvm-data` package, are in [LINUX_APPIMAGE.md](LINUX_APPIMAGE.md).

Windows users need a SoundFont only to override the system synth — see the note
at the top of this section. Explicit `--music-device gm-builtin` uses the
engine's own rendering and does need one.

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

## Enhanced SFX (enhanced mode only)

Both modes play your own samples bit-exactly — enhanced mode changes how they
are *mixed*, never the sample data:

- **Polyphony.** Faithful mode caps the digital (`sb-dac`) effects at one
  voice, because the original Sound Blaster had one DAC voice and a new effect
  cut off whatever was still playing. Enhanced mode raises the pool to eight,
  so rapid retriggers overlap instead of truncating each other.
- **Music balance.** Enhanced mode mixes the synth layer at 60% so effects sit
  above the soundtrack; faithful mode leaves it at full level.

The Options menu's music and SFX volume sliders override both levels in either
mode. Note this applies to the `sb-dac` backend — the synth SFX backends
(`gm-sfx`/`mt32-sfx`/`opl`) don't use samples.

## Fidelity notes

- OPL music/SFX and the golden gameplay trace are independent — audio never
  touches gameplay RNG or the deterministic frame loop.
- Known deliberate deviation (both this engine and the reference): the EXE's
  'A' mode shares ONE OPL chip, so an SFX steals music channel 3 (key-off +
  patch swap) while it plays; both engines render SFX on a separate chip
  instance and mix digitally, so the music is never interrupted. Volume
  balance is calibrated to the single-chip output stage.
