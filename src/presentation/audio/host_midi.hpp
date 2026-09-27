// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Host MIDI output: send MDI music to a real MIDI OUT port (an MT-32 / CM-32L,
// or MUNT's virtual port).  Opt-in; the bundled synths stay the default.  No
// audio rendering: a wall-clock thread advances the shared MidiSequencer at a
// virtual 1000 samples/s and sends each due event.  Without RtMidi a stub
// reports the feature unavailable and the rest of the engine is unaffected.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "presentation/audio/midi_seq.hpp"

namespace olduvai::presentation {

// This build links RtMidi (CoreMIDI; ALSA when libasound was found).  False:
// every entry point below is a no-op.
bool host_midi_available();

// Enumerate MIDI OUT port names (index order matches the host driver).  Empty
// when no ports exist or the feature is unavailable in this build.
std::vector<std::string> host_midi_list_ports();

// Streams a build_gm_midi() byte stream to a host port on its own thread,
// looping until stop().  Public methods: main thread only.
class HostMidiPlayer {
public:
    HostMidiPlayer();
    ~HostMidiPlayer();
    HostMidiPlayer(const HostMidiPlayer&) = delete;
    HostMidiPlayer& operator=(const HostMidiPlayer&) = delete;

    // Open an output port.  Empty = a port whose name contains "MT-32"/"MUNT",
    // else the first.  False when unavailable, no ports, or open fails.
    bool open(const std::string& port_name);
    bool is_open() const { return open_.load(std::memory_order_relaxed); }
    const std::string& port_name() const { return port_name_; }

    // (Re)start the looping stream with a fresh MIDI byte buffer.  Silences any
    // currently-playing track first.  No-op when no port is open.
    void play(const std::vector<std::uint8_t>& midi);
    // Stop the stream and silence the port (all-notes-off on every channel).
    void stop();

    // Backend state (RtMidiOut, or empty in the stub); public only so the
    // .cpp's send helper can name it.
    struct Impl;

private:
    void pump();                 // wall-clock thread body
    void all_notes_off();        // CC 123 on all 16 channels

    std::unique_ptr<Impl> impl_;
    std::string port_name_;
    std::atomic<bool> open_{false};

    std::thread thread_;
    std::atomic<bool> running_{false};   // pump thread should keep going
    // The pump advances seq_ each ~ms tick; play()/stop() reload it on the main
    // thread.  A plain mutex is fine at this rate; `seq_loaded_` lets an idle
    // pump skip it.
    std::mutex seq_mu_;
    MidiSequencer seq_;
    std::atomic<bool> seq_loaded_{false};
};

}  // namespace olduvai::presentation
