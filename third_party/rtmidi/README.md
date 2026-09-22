# RtMidi

Realtime MIDI input/output C++ classes by Gary P. Scavone.

- **Source:** upstream RtMidi 6.0.0 (https://github.com/thestk/rtmidi),
  single-translation-unit C++ (`RtMidi.cpp` + `RtMidi.h`).  Vendored with
  ONE local patch, described below.
- **License:** MIT (see `LICENSE`).  Compatible with the other vendored
  third-party libraries here.
- **Why:** the host-MIDI output path (`--midi-port` / `--list-midi-ports`)
  routes the game's MDI music to a real hardware/software MIDI OUT port
  (e.g. a Roland MT-32 / CM-32L, or MUNT advertising a virtual port), the
  same way the Python reference engine does via `mido` + `python-rtmidi`.

Only the MIDI-OUT subset is used: `RtMidiOut::getPortCount()`,
`getPortName()`, `openPort()`, and `sendMessage()`.

## Local patch: a CoreMIDI failure must not kill the process

`MidiInCore::getCoreMidiClientSingleton` and its `MidiOutCore` twin are
declared `throw()`, and both call `error( RtMidiError::DRIVER_ERROR, ... )`
when `MIDIClientCreate` fails.  `MidiApi::error` THROWS for that type, and a
throw out of a `throw()` function calls `std::terminate` — so the failure
cannot be caught by any caller, however carefully it is wrapped.  The process
dies with

    libc++abi: terminating due to uncaught exception of type RtMidiError:
    MidiInCore::initialize: error creating OS-X MIDI client object (-304).

which is exactly what olduvai's gate caught on 2026-09-20: `menu_script`
aborted mid-run because the Sound-card probe enumerates MIDI ports, and
macOS had refused a new MIDI client while several engine processes were
starting at once.

The patch moves the reporting one level up: the `throw()` helper records
`errorString_`, releases the `CFStringRef` it used to leak on that path, and
returns 0; each `initialize()` then calls `error(...)` itself, where throwing
is legal and `HostMidiPlayer::open` / `host_midi_list_ports` already catch
`RtMidiError`.  Four marked sites, all reading `// olduvai local patch`.

Verified by fault injection rather than by argument — forcing the failure
(`if ( result != noErr )` → `if ( true )`): upstream aborts with the message
above and exit 134; patched, `--list-midi-ports` prints "No MIDI output ports
found." and exits 0, and the engine starts and runs.

**When updating:** re-apply it, or check whether upstream has fixed it.

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
