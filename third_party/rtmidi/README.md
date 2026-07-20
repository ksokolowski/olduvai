# RtMidi

Realtime MIDI input/output C++ classes by Gary P. Scavone.

- **Source:** upstream RtMidi 6.0.0 (https://github.com/thestk/rtmidi),
  single-translation-unit C++ (`RtMidi.cpp` + `RtMidi.h`).  Vendored
  verbatim — no local edits.
- **License:** MIT (see `LICENSE`).  Compatible with the other vendored
  third-party libraries here.
- **Why:** the host-MIDI output path (`--midi-port` / `--list-midi-ports`)
  routes the game's MDI music to a real hardware/software MIDI OUT port
  (e.g. a Roland MT-32 / CM-32L, or MUNT advertising a virtual port), the
  same way the Python reference engine does via `mido` + `python-rtmidi`.

Only the MIDI-OUT subset is used: `RtMidiOut::getPortCount()`,
`getPortName()`, `openPort()`, and `sendMessage()`.

## Build wiring

RtMidi needs a per-platform compile define selecting its backend API:

- macOS: `__MACOSX_CORE__` (CoreMIDI; always present, no extra package).
- Linux: `__LINUX_ALSA__` (ALSA sequencer; needs `libasound2-dev`).

The CMake option `OLDUVAI_WITH_RTMIDI` (default ON) gates the feature.  On
Linux without ALSA the option degrades gracefully — RtMidi is *not* built
and a stub satisfies the API, so the rest of the engine is unaffected and
`--list-midi-ports` reports the feature as unavailable.  `OLDUVAI_HAVE_RTMIDI`
is defined only when RtMidi is actually compiled in.

## To update

Re-copy `RtMidi.cpp` / `RtMidi.h` / `LICENSE` from a tagged upstream
release.  Keep this README's version line in sync.
