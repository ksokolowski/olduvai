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

Other devices and firmwares: the KNULLI zips above are for those two devices
only. For everything else that runs [PortMaster](https://portmaster.games/) see
[the PortMaster section](#portmaster) below.

## PortMaster

`olduvai-<version>-portmaster.zip` is a PortMaster package for both 64-bit and
32-bit ARM devices. It has been played on the TrimUI Smart Pro and the Powkiddy
A12 (KNULLI) and on a 640x480 R36S clone (ArkOS); other devices and firmwares
that PortMaster supports should work, and a report either way is welcome.

1. Copy the zip into PortMaster's **`autoinstall`** folder (the one that holds
   `place-zips-here-for-autoinstallation.txt`).
2. Open PortMaster once; it installs the package and may update itself first.
3. Refresh the game list in your frontend (restarting it does it), and Olduvai
   appears under Ports.
4. Copy your game files into **`ports/olduvai/game/`**, as in
   [What you need](#what-you-need). The first launch without them shows which
   files are missing and where they go.

What it does for you: it starts Enhanced HD at the largest scale that fits the
panel's height, a step every 200 lines: x2 from 400 lines (a 4:3 panel such as
640x480 is filled at 4:3), x3 from 600 (the A12, the TrimUI) and x4 from 800 (a
1280x800 panel is exactly 4x). The x2 wide and x4 steps have not yet been tried
on real hardware of their class. Panels shorter than 400 lines start in Classic
DOS; your choice under *Options → Style* is remembered. Settings, saves and bug reports
live inside the port folder, so an update never touches them. MT-32 ROMs go in
`olduvai/mt32-roms/` and a SoundFont in `olduvai/soundfonts/`, as described
under Music below. If it does not start, `olduvai/log.txt` has the reason.

## Enhanced HD, in your hands

The enhanced mode is not a desktop luxury that falls away on a handheld. On the
TrimUI Smart Pro the full **Enhanced HD** experience runs at close to the
original game's full speed: the boss fights at full speed, the levels within a
few percent of it:

- **smooth motion**: the 18 Hz game logic is interpolated into extra frames,
  so the caveman glides instead of stepping;
- **widescreen**: the 16:9 panel is filled with the level itself, the
  neighbouring screens drawn live into the side margins, not black bars;
- **HD graphics**: sprites and backgrounds upscaled for the panel, with a
  crisp vector HUD and menus;
- the enhanced transitions and animation touches of the desktop build.

**Classic DOS** is one menu choice away and is exactly the 1991 game.

The handheld profile is tuned for this class of device (a quad-core 64-bit
ARM chip and a 720p screen, common among current handhelds), and
that is where Enhanced HD is meant to be the natural way to play. On older,
slower hardware such as the Powkiddy A12, Enhanced HD is smooth but a little
slower than the original's pace, and very playable; Classic runs there at full
speed and is one menu choice away.

## What you need

- A device above, running KNULLI.
- **Your own copy of the game.** Olduvai is an engine only and ships no game
  content. [*Prehistorik 1+2* on GOG.com](https://www.gog.com/game/prehistorik_12)
  works, and so do the original DOS files. See [LEGAL.md](../LEGAL.md).

## Installing

1. Unzip the download. You get `Olduvai.sh`, an `olduvai` folder and an
   `images` folder.
2. Copy `Olduvai.sh` and the `olduvai` folder into **`/userdata/roms/ports/`** on
   the SD card, the large "share" partition. KNULLI formats it as exFAT, so a
   Windows or macOS computer can write to it directly with the card in a reader;
   KNULLI's network share works too.
3. Copy your game files into **`olduvai/game/`**:
   `FILESA.CUR`, `FILESB.CUR`, `FILESA.VGA`, `FILESB.VGA`, and either
   `HISTORIK.EXE` or `PREH.SQZ`. The GOG release has `PREH.SQZ` instead of
   `HISTORIK.EXE`. That is correct, and Olduvai reads it directly.
4. On the device, refresh the game list (or restart EmulationStation) and start
   **Olduvai** from the **Ports** menu.

If a file is missing or misnamed, the game says so on screen (which files, and
the exact folder it looked in) and returns to the menu at the press of a
button.

`olduvai/README.txt` inside the download says the same, for reading on the card.

**Optional: a name and icon in the Ports menu.** `olduvai/gamelist-entry.xml`
holds the menu entry: a description of the game and its two modes, and the
Olduvai icon. Paste its `<game>` block into
`/userdata/roms/ports/gamelist.xml` (with EmulationStation stopped) and copy
`images/olduvai.png` into `/userdata/roms/ports/images/`. Do **not** copy it
over an existing `gamelist.xml`: the other ports would lose their names and
artwork. Without it, the port still works; its entry just has no description
or icon.

## Playing

- **Controls:** d-pad or left stick to move, **A** jump (and confirm in
  menus), **B** attack (and back in menus), **Start** pause menu. The pad works
  without any setup: KNULLI hands its own mapping to the game, and the
  handheld launchers use the *Nintendo* button layout, which matches the labels
  printed on both devices.
- **Shortcuts:** hold **Select** and press **R1** to quicksave, **L1** to
  quickload, the top button for cheats (when on) and the left one for a bug
  report. *Options → Controls → Shortcuts* lists them. Select + Start is
  left free: PortMaster uses it to quit.
- **Remapping:** *Options → Controls* in the pause menu. *Button layout*
  switches between Nintendo and Xbox (for a pad that prints A at the bottom).
  *Gamepad* lists each action with two slots, its button and a spare: pick a
  slot with left/right, press A, then press the button you want (L2 and R2
  work too). Each slot names the button as your device prints it, with
  where it sits (*A - right*). Picking a button another action uses swaps
  the two, so jump, attack and pause each keep a button of their own (menu
  back may share one with attack, since it only works in menus). Waiting
  five seconds cancels a pick. *Reset to layout* and *Reset all* undo it;
  the change takes effect when you leave Options
  and choose Apply, and is saved for the next launch.
- **Style:** both the TrimUI Smart Pro and the Powkiddy A12 start in
  **Enhanced HD** (smooth motion, widescreen, xBRZ at x3). Switch under
  *Options → Style* in the pause menu to **Classic DOS**, which runs on the A12
  at the original game's full speed; your choice is remembered at the next
  launch. Enhanced on the A12 is smooth but a little slower than the original.
- **Quit** with *Quit → Exit Game → Yes* in the pause menu; it returns to KNULLI.
  On a keyboard F10 asks the same question. PortMaster's own hotkey, Start +
  Select, leaves at once.

## Music

By default you hear the **Sound Blaster** sound of 1991, FM music (the AdLib
chip) with digital sound effects, which needs nothing extra.

- **Roland MT-32 / CM-32L:** if you own the ROM images, copy them into
  `olduvai/mt32-roms/` (`CM32L_CONTROL.ROM` + `CM32L_PCM.ROM`, and/or
  `MT32_CONTROL.ROM` + `MT32_PCM.ROM`) and the Roland sound is used instead.
- **General MIDI:** copy a SoundFont (`.sf2`) into `olduvai/soundfonts/`. It is
  used when no Roland ROMs are present; the order is Roland, then General MIDI,
  then Sound Blaster. The General MIDI synth (FluidSynth) is built in.

To choose instead of taking the order above, pick a **Sound card** under
*Options → Audio*: Sound Blaster, AdLib, Roland MT-32 or General MIDI (the
last two appear once their ROMs or SoundFont are in place), or Off. All
options are in [AUDIO.md](AUDIO.md).

## If something goes wrong

Read the log: **`olduvai/log.txt`**, in the port folder (rewritten at every
launch). Its first line names the exact build, for example
`olduvai 0.9.7 (61a624e, 2026-09-13 15:08)`. Include it in any bug report.

- **Nothing on screen:** the log names SDL and the video driver. Check that the
  download matches your device; the two launchers differ exactly there.
- **The game does not start:** the log names the folder it searched and the
  files it expected. Check the names in `olduvai/game/`.

Settings and the quicksave live in the port too, under `olduvai/conf/olduvai/`
(`play.json`, `saves/`); deleting `play.json` returns to the device defaults.
Builds before 0.9.10 kept them in `/userdata/system/.config/olduvai/`; the
first launch of a newer one copies them over, once, and never overwrites.
