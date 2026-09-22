# Olduvai on handhelds (KNULLI)

Olduvai runs on Linux handhelds with the [KNULLI](https://knulli.org) firmware.
Two devices are supported, because they are the two it has been played through
on:

| Device | CPU | Screen | Download |
|---|---|---|---|
| **TrimUI Smart Pro** | Allwinner A133, 64-bit ARM | 1280x720 | `olduvai-<version>-knulli-trimui.zip` |
| **Powkiddy A12** | Rockchip RK3128, 32-bit ARM | 1024x600 | `olduvai-<version>-knulli-a12.zip` |

Both files are on the
[Releases page](https://github.com/ksokolowski/olduvai/releases/latest), next to the
desktop builds, and are covered by the same `SHA256SUMS.txt`.

Other devices and firmwares are not supported yet. The binary may well run on
other KNULLI devices with the same CPU architecture, but nobody has checked,
and the screen, video driver and audio setup differ from device to device.
**Support through [PortMaster](https://portmaster.games/)** — which reaches
many more devices and firmwares — **is the next goal.**

## Enhanced HD, in your hands

The enhanced mode is not a desktop luxury that falls away on a handheld. On the
TrimUI Smart Pro the full **Enhanced HD** experience runs at close to the
original game's full speed — the boss fights at full speed, the levels within a
few percent of it:

- **smooth motion** — the 18 Hz game logic is interpolated into extra frames,
  so the caveman glides instead of stepping;
- **widescreen** — the 16:9 panel is filled with the level itself, the
  neighbouring screens drawn live into the side margins, not black bars;
- **HD graphics** — sprites and backgrounds upscaled for the panel, with a
  crisp vector HUD and menus;
- the enhanced transitions and animation touches of the desktop build.

**Classic DOS** is one menu choice away and is exactly the 1991 game.

The handheld profile is tuned for this class of device — a quad-core 64-bit
ARM chip and a 720p screen, a common class among current handhelds — and
that is where Enhanced HD is meant to be the natural way to play. On older,
slower hardware such as the Powkiddy A12, Classic runs at full speed and
Enhanced is smooth but a little slower than the original. Bringing the same
experience to many more handhelds is what the planned PortMaster support is
for.

## What you need

- A device above, running KNULLI.
- **Your own copy of the game.** Olduvai is an engine only and ships no game
  content. [*Prehistorik 1+2* on GOG.com](https://www.gog.com/game/prehistorik_12)
  works, and so do the original DOS files. See [LEGAL.md](../LEGAL.md).

## Installing

1. Unzip the download. You get `Olduvai.sh`, an `olduvai` folder and an
   `images` folder.
2. Copy `Olduvai.sh` and the `olduvai` folder into **`/userdata/roms/ports/`** on
   the SD card — the large "share" partition. KNULLI formats it as exFAT, so a
   Windows or macOS computer can write to it directly with the card in a reader;
   KNULLI's network share works too.
3. Copy your game files into **`olduvai/game/`**:
   `FILESA.CUR`, `FILESB.CUR`, `FILESA.VGA`, `FILESB.VGA`, and either
   `HISTORIK.EXE` or `PREH.SQZ`. The GOG release has `PREH.SQZ` instead of
   `HISTORIK.EXE` — that is correct, and Olduvai reads it directly.
4. On the device, refresh the game list (or restart EmulationStation) and start
   **Olduvai** from the **Ports** menu.

If a file is missing or misnamed, the game says so on screen — which files, and
the exact folder it looked in — and returns to the menu at the press of a
button.

`olduvai/README.txt` inside the download says the same, for reading on the card.

**Optional — a name and icon in the Ports menu.** `olduvai/gamelist-entry.xml`
holds the menu entry: a description of the game and its two modes, and the
Olduvai icon. Paste its `<game>` block into
`/userdata/roms/ports/gamelist.xml` (with EmulationStation stopped) and copy
`images/olduvai.png` into `/userdata/roms/ports/images/`. Do **not** copy it
over an existing `gamelist.xml`: the other ports would lose their names and
artwork. Without it, the port still works; its entry just has no description
or icon.

## Playing

- **Controls:** d-pad or left stick to move, **A** jump, **X** attack,
  **Start** pause menu, **B** back. The pad works without any setup — KNULLI
  hands its own mapping to the game.
- **Style:** the TrimUI Smart Pro starts in **Enhanced HD** (smooth motion,
  widescreen); the Powkiddy A12 starts in **Classic DOS**, which runs there at
  the original game's full speed. Switch under *Options → Style* in the pause
  menu; your choice is remembered at the next launch. Enhanced on the A12 is
  smooth but a little slower than the original.
- **Quit** with *Quit → Exit Game → Yes* in the pause menu — it returns to KNULLI.

## Music

By default you hear the **Sound Blaster** sound of 1991 — FM music (the AdLib
chip) with digital sound effects — which needs nothing extra.

- **Roland MT-32 / CM-32L:** if you own the ROM images, copy them into
  `olduvai/mt32-roms/` (`CM32L_CONTROL.ROM` + `CM32L_PCM.ROM`, and/or
  `MT32_CONTROL.ROM` + `MT32_PCM.ROM`) and the Roland sound is used instead.
- **General MIDI:** copy a SoundFont (`.sf2`) into `olduvai/soundfonts/`. It is
  used when no Roland ROMs are present; the order is Roland, then General MIDI,
  then Sound Blaster. General MIDI needs the firmware's FluidSynth library,
  which the A12's KNULLI has.

To choose instead of taking the order above, pick a **Sound card** under
*Options → Audio*: Sound Blaster, AdLib, Roland MT-32 or General MIDI (the
last two appear once their ROMs or SoundFont are in place), or Off. All
options are in [AUDIO.md](AUDIO.md).

## If something goes wrong

Read the log: **`/userdata/system/logs/olduvai.log`** (rewritten at every
launch). Its first line names the exact build, for example
`olduvai 0.9.7 (61a624e, 2026-09-13 15:08)` — include it in any bug report.

- **Nothing on screen:** the log names SDL and the video driver. Check that the
  download matches your device — the two launchers differ exactly there.
- **The game does not start:** the log names the folder it searched and the
  files it expected. Check the names in `olduvai/game/`.

Settings live in the port's KNULLI home (`/userdata/system/.config/olduvai/play.json`);
deleting that file returns to the device defaults.
