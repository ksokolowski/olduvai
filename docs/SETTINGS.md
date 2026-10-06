# Settings

Most players never touch this file: *Options* in the game writes it.  This
page is the reference for everything it can hold.

## Where, and who wins

`play.json`, a flat JSON object:

| Platform | Path |
|---|---|
| Linux, macOS | `~/.config/olduvai/play.json` (`$XDG_CONFIG_HOME` honoured) |
| Windows | `%APPDATA%\olduvai\play.json` |
| KNULLI and PortMaster handhelds | inside the port: `olduvai/conf/olduvai/play.json` |

Precedence, lowest first: engine defaults → `--default-profile` (a launcher's
device defaults) → `play.json` → `--profile` → command-line flags.
`--save-config` writes the flags you passed into the file.  Deleting the file
returns to the defaults.

## Profiles

| Profile | What it is |
|---|---|
| `dos` | The 1991 game, faithful to its quirks (the engine defaults). |
| `hd` | Enhanced: HD upscaling, widescreen, smooth motion, vector HUD. |
| `dos-handheld` | `dos`, in the handheld family. |
| `hd-handheld` | `hd` tuned for 720p / 600p handheld panels (`xbrz` ×3, lighter pacing). |
| `dos-handheld-43` | `dos` filling a 4:3 handheld panel (640×480), in its own family. |
| `hd-handheld-43` | `hd-handheld` for a 4:3 panel: `xbrz` ×2, filling it at 4:3. |
| `dos-handheld-x2` | `dos`, in the handheld family for wide panels of 400 to 599 lines. |
| `hd-handheld-x2` | `hd-handheld` for those panels: `xbrz` ×2, widescreen. Untested on a device of its class. |
| `dos-handheld-x4` | `dos`, in the handheld family for panels of 800 lines and up. |
| `hd-handheld-x4` | `hd-handheld` for those panels: `xbrz` ×4 (a 1280×800 panel is exactly 4×). Untested on a device of its class. |

*Options → Style* switches between Classic and Enhanced within the family
the session started in.

## Keys

**Game**

| Key | Values |
|---|---|
| `game_dir` | folder with your game files |
| `autofire` | `off`, `slow`, `medium`, `fast` |

**Display**

| Key | Values |
|---|---|
| `enhanced` | `true` / `false` — the Enhanced HD umbrella |
| `hd_profile` | `native`, `retro`, `smooth`, `eagle`, `xbrz`, `mmpx`, `omniscale` (default); an old `xbr` reads as `xbrz` |
| `render_scale` | `2`, `4` |
| `aspect` | `keep`, `4:3`, `stretch`, `widescreen` |
| `fullscreen` | `true` / `false` (ALT+ENTER toggles) |
| `vga_scan` | `true` / `false` — the VGA hold-frame scanout (Classic) |
| `transitions` | `smooth`, `classic` |
| `display_mode` | `gpu`, `cpu` |
| `hd_font` | `freckle`, `noto` |
| `banner_fx` | `caveman`, `fire`, `rainbow`, `gold`, `pulse` |
| `smooth_subframes` | `0`-`12` (`0` = auto) — smooth-motion sub-frames per tick |
| `smooth_vsync` | `auto`, `off` |

What `hd_profile` runs at each `render_scale`. The names describe a look;
at x3 three of them are the same scaler, and the game says so on the first use.

| `hd_profile` | x2 | x3 | x4 |
|---|---|---|---|
| `retro` | nearest | nearest | nearest |
| `smooth` | Scale2x | Scale3x | Scale2x, two passes |
| `eagle` | Eagle | Scale3x (Eagle has no 3x form) | Eagle, two passes |
| `xbrz` | xBRZ | xBRZ | xBRZ |
| `mmpx` | MMPX | Scale3x (MMPX has no 3x form) | MMPX, two passes |
| `omniscale` | OmniScale | OmniScale | OmniScale |

**Audio** (the backends and what they need: [AUDIO.md](AUDIO.md))

| Key | Values |
|---|---|
| `music_device` | `auto`, `opl`, `mt32-builtin`, `gm-builtin`, `host-midi`, `gm-host`, `none` |
| `sfx_backend` | `auto` (pairs with the music), `opl`, `sb-dac`, `mt32-sfx`, `gm-sfx`, `midi`, `none` |
| `music_volume`, `sfx_volume` | `0`-`100` |
| `rom_dir` | folder with MT-32 / CM-32L ROM images |
| `mt32_model` | `auto`, `cm32l`, `mt32` |
| `soundfont` | a `.sf2` file for General MIDI |
| `audio_rate`, `audio_buffer` | device rate / buffer; `0` = the device's default |

`--sound-card` (and *Options → Audio → Sound card*) sets the music and
effects backends together, by the 1991 card's name.

**Controls**

| Key | Values |
|---|---|
| `pad_jump`, `pad_attack`, `pad_pause`, `pad_back`, `pad_confirm` | one or two SDL button names by **position**, comma-separated (`"b,y"`): `a` bottom, `b` right, `x` left, `y` top, `start`, `back`, `leftshoulder`, `rightshoulder`, `lefttrigger`, `righttrigger`, `leftstick`, `rightstick` |
| `pad_deadzone` | stick deadzone, default `8000` |
| `key_left`, `key_right`, `key_up`, `key_down`, `key_attack`, `key_pause`, `key_quit` | one or two SDL key names for play, comma-separated (`"Space,Left Ctrl"`); unset means the defaults below |

*Options → Controls* edits the same keys, with an Xbox and a Nintendo layout.
Menu confirm follows jump. In play only pause acts as a menu key, so menu
back may share a button with attack: the Nintendo layout puts both on B.
A `pad_back` of `back` saved with the first Nintendo layout reads as B.

The keyboard moves with the arrows or WASD; Space or Left Ctrl attacks, Esc
pauses and backs out, and Enter, Keypad Enter or Space confirms. In play,
F6 quicksaves, F9 quickloads, F7 opens cheats (with `--cheats`) and F5 the
bug report; on a pad, hold Select and press R1, L1, the top or the left
button. F10 asks "Exit game?" (the same question as *Pause → Exit Game*), and
it opens on No, so a single key press never ends a run; `key_quit` moves it.
A quit signal (a window's close button, Alt+F4, Cmd+Q, a handheld's PortMaster
hotkey Start + Select) leaves at once.

*Gamepad* and *Keyboard* under *Options → Controls* give each action two
slots: pick one, press Enter, then the button or key. Esc or five seconds
cancels; Backspace empties a spare slot. The keyboard screen moves play
only: menus always answer to the keys above.

A row names its button as the connected pad prints it, with where it sits
(*A - right*): Nintendo letters on a Nintendo-style pad, PlayStation names on
a PlayStation pad, Xbox letters otherwise, and the handheld profile's family
when the pad does not report a type. The keys above stay positional either
way. The two shoulders and the back button read L1, R1, Select and Start on
every pad; a Switch controller prints those as L, R, minus and plus.

**Diagnostics**

| Key | Values |
|---|---|
| `bug_report_dir` | where F5 writes reports (default `~/olduvai/bug_reports`; `$OLDUVAI_BUG_DIR` overrides) |

Every flag and its default: `olduvai --help`.
