# Settings

Most players never touch this file: *Options* in the game writes it.  This
page is the reference for everything it can hold.

## Where, and who wins

`play.json`, a flat JSON object:

| Platform | Path |
|---|---|
| Linux, macOS | `~/.config/olduvai/play.json` (`$XDG_CONFIG_HOME` honoured) |
| Windows | `%APPDATA%\olduvai\play.json` |
| KNULLI handhelds | `/userdata/system/.config/olduvai/play.json` |

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
| `hd-handheld` | `hd` tuned for 720p / 600p handheld panels (`smooth` ×3, lighter pacing). |

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
| `hd_profile` | `native`, `retro`, `smooth`, `eagle`, `xbr`, `mmpx`, `omniscale` (default) |
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
| `pad_jump`, `pad_attack`, `pad_pause`, `pad_back`, `pad_confirm` | SDL button names by **position**: `a` bottom, `b` right, `x` left, `y` top, `start`, `back`, `leftshoulder`, `rightshoulder` |
| `pad_deadzone` | stick deadzone, default `8000` |

*Options → Controls* edits the same keys, with an Xbox and a Nintendo layout.
Menu confirm follows jump.

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
