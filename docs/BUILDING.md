# Building Olduvai from source

Requires **CMake ≥ 3.21** and a **C++17** compiler. **SDL2** is required for
the presentation layer — without it the engine core still builds headless
(formats, systems, tests of those layers).

## SDL policy: SDL2-native, sdl2-compat-clean

The engine targets the **SDL2 API only** (decision 2026-07-18): old hardware
and handhelds get real SDL2, while modern systems increasingly provide
[sdl2-compat](https://github.com/libsdl-org/sdl2-compat) — the SDL2 ABI
implemented on SDL3, which Homebrew now ships as `sdl2`.  Both are supported;
the test suite runs against whichever is installed.

One discipline keeps the shim happy: **never `SDL_PushEvent` a synthetic
event of a type SDL normally generates internally** (text input especially —
sdl2-compat's `Event2to3` returns NULL for app-pushed `SDL_TEXTINPUT` and
crashes; 2026-07-17 `report_form` segfault).  Deliver synthetic input by
calling the consuming handler directly instead.

## Quick build (the game only)

```sh
cmake --preset release
cmake --build --preset release       # → build/release/olduvai — nothing else
```

Build flavours are CMake presets (`CMakePresets.json`), one tree per flavour
under `build/`: `release` (dev default), `asan` (ASan + fatal UBSan), `fuzz`
(libFuzzer harness).  Plain `cmake -B <dir>` still works if you prefer your
own tree.  The generated build is plain Makefiles, so
`make -C build/release` works too.

The default build produces only the game binary; everything else is walled
behind explicit targets:

```sh
cmake --build --preset release --target tests   # then: ctest --preset release
cmake --build --preset release --target tools   # dev utilities (dumps, shots, trace)
```

Test binaries land in `build/release/tests/`, tools in `build/release/tools/`.

Trying the GUI first-run flow from a terminal: `OLDUVAI_FORCE_GUI=1
build/release/olduvai --game-dir /nonexistent` shows the real dialogs (a terminal
launch otherwise gets the text report; `OLDUVAI_NO_GUI=1` forces that).

The test suite is data-gated: suites that need game files skip cleanly
(exit 77) when none are present. To run them, symlink your game files as
`game_data/` in the repo root (or set `OLDUVAI_GAME_DATA`).

## Linux

```sh
sudo apt install cmake g++ make libsdl2-dev libasound2-dev   # Debian/Ubuntu
cmake --preset release && cmake --build --preset release
```

Portable AppImage (bundles SDL2 + FluidSynth; needs ImageMagick for the
icon):

```sh
packaging/build_appimage_linux.sh      # → ./olduvai-x86_64.AppImage
```

Details (what gets bundled, GM SoundFont discovery, best-sound setup):
[`LINUX_APPIMAGE.md`](LINUX_APPIMAGE.md).

## macOS

```sh
brew install cmake sdl2
cmake --preset release && cmake --build --preset release
```

Optional audio backends load at runtime if present — `brew install mt32emu`
(needs your own MT-32 ROM images) and `brew install fluid-synth` (General
MIDI; a SoundFont is auto-discovered or passed with `--soundfont`).

App bundle and dmg:

```sh
brew install dylibbundler
cmake --build --preset release --target olduvai_app   # → build/release/Olduvai.app
packaging/make_bundle.sh                     # quick local .app with bundled
                                           # dylibs (no dmg)
packaging/make_dmg_macos.sh                  # native-arch dmg (quick, brew SDL2)
packaging/make_dmg_macos.sh --universal      # arm64+x86_64 fat dmg — the release
                                           # shape; builds a version-pinned
                                           # universal SDL2 from source first
```

## Windows (MSYS2)

From an **MSYS2 UCRT64** shell:

```sh
pacman -S zip mingw-w64-ucrt-x86_64-{cmake,gcc,ninja,SDL2}
cmake --preset release -G Ninja
cmake --build --preset release
```

The MinGW shape is a **self-contained `olduvai.exe`** (static runtime +
static SDL2 — no DLLs).

### Windows (MSVC — the shipped toolchain)

From a **Developer Command Prompt for VS 2022** (or after `vcvars64.bat`),
with the official [SDL2 VC development package](https://github.com/libsdl-org/SDL/releases)
extracted somewhere:

```cmd
cmake --preset release -G Ninja -DCMAKE_PREFIX_PATH=C:\path\to\SDL2-2.32.10\cmake
cmake --build --preset release
```

Static CRT (`/MT`) means no VC++ redistributable; `SDL2.dll` is copied
beside the exe automatically and ships in the zip.

Portable zip (either toolchain):

```sh
sh packaging/package_windows.sh        # → olduvai-<version>-windows-x86_64.zip
```

## Audio backends at a glance

OPL/AdLib FM music and sound effects are **built in** (vendored Nuked-OPL3 —
the authentic 1991 sound; no external dependency). MT-32 (libmt32emu) and
General MIDI (FluidSynth) are loaded at runtime when installed. On Linux,
`apt install scummvm-data` provides a Roland Sound Canvas SoundFont that is
auto-selected for the most faithful GM sound. The full music-device ×
SFX-backend matrix is in [`AUDIO.md`](AUDIO.md).

## Build options (CMake `-D...=ON`)

| Option | Default | What |
|---|---|---|
| `OLDUVAI_WERROR` | OFF | warnings as errors (CI uses it) |
| `OLDUVAI_LTO` | ON | IPO/LTO for Release |
| `OLDUVAI_SANITIZE` | OFF | ASan + fatal UBSan (the `asan` preset) |
| `OLDUVAI_FUZZ` | OFF | libFuzzer parser harness (clang + SANITIZE) |
| `OLDUVAI_HARDEN` | ON (Linux) | RELRO/NX/PIE/FORTIFY |
| `OLDUVAI_STATIC_RUNTIME` / `OLDUVAI_STATIC_SDL` | ON (Windows) | self-contained exe |
