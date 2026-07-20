// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Host-MIDI output — route the game's MDI music to a real hardware/software
// MIDI OUT port (e.g. a Roland MT-32 / CM-32L, or MUNT advertising a virtual
// port).  Opt-in; the bundled software synths (mt32-builtin / gm-builtin /
// opl) stay the default.  Mirrors the Python reference's `--music-device mt32
// --midi-port NAME` path.
//
// The bundled synths are sample-clocked (advanced inside the SDL audio
// callback); host MIDI has no audio rendering, so a wall-clock timer thread
// advances the same MidiSequencer and sends each due event's raw bytes to
// the port.  The sequencer's seamless-loop handling (all-notes-off at the
// seam, initial-tempo restore) is reused verbatim by driving advance() with
// a virtual 1000-samples-per-second "rate" against elapsed wall-clock.
//
// Build-time gating: when RtMidi is compiled in (OLDUVAI_HAVE_RTMIDI), the
// real CoreMIDI/ALSA backend is used.  Otherwise a stub satisfies the API so
// the Linux-without-ALSA case (and any --list-midi-ports call) degrades
// gracefully — no port enumeration, a clear "not available" report, and the
// rest of the engine is unaffected.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "presentation/midi_seq.hpp"

namespace olduvai::presentation {

// True when this build actually links RtMidi (CoreMIDI on macOS, ALSA on
// Linux when libasound was found).  When false, every entry point below is a
// graceful no-op that reports the feature as unavailable.
bool host_midi_available();

// Enumerate MIDI OUT port names (index order matches the host driver).  Empty
// when no ports exist or the feature is unavailable in this build.
std::vector<std::string> host_midi_list_ports();

// Streams a MIDI byte stream (the build_gm_midi() output, identical to the
// builtin-synth path) out a host MIDI OUT port on a wall-clock thread, looping
// until stop()/destruction.  All public methods are main-thread only; the
// player owns its pump thread.
class HostMidiPlayer {
public:
    HostMidiPlayer();
    ~HostMidiPlayer();
    HostMidiPlayer(const HostMidiPlayer&) = delete;
    HostMidiPlayer& operator=(const HostMidiPlayer&) = delete;

    // Open an output port.  Empty name = pick a sensible default (a port whose
    // name contains "MT-32"/"MUNT", else the first available).  Returns false
    // when the feature is unavailable, no ports exist, or open fails.
    bool open(const std::string& port_name);
    bool is_open() const { return open_.load(std::memory_order_relaxed); }
    const std::string& port_name() const { return port_name_; }

    // (Re)start the looping stream with a fresh MIDI byte buffer.  Silences any
    // currently-playing track first.  No-op when no port is open.
    void play(const std::vector<std::uint8_t>& midi);
    // Stop the stream and silence the port (all-notes-off on every channel).
    void stop();

    // Opaque per-build backend state (RtMidiOut, or empty in the stub).  Public
    // only so the translation unit's free send helper can name the type; never
    // touched outside host_midi.cpp.
    struct Impl;

private:
    void pump();                 // wall-clock thread body
    void all_notes_off();        // CC 123 on all 16 channels

    std::unique_ptr<Impl> impl_;
    std::string port_name_;
    std::atomic<bool> open_{false};

    std::thread thread_;
    std::atomic<bool> running_{false};   // pump thread should keep going
    // Sequencer + its guard.  The pump thread advances seq_ on its wall-clock
    // tick; play()/stop() on the main thread reload it.  The pump runs at ~ms
    // granularity (not a real-time audio callback), so a plain mutex on every
    // tick is fine.  `seq_loaded_` lets the pump skip the lock when idle.
    std::mutex seq_mu_;
    MidiSequencer seq_;
    std::atomic<bool> seq_loaded_{false};
};

}  // namespace olduvai::presentation
