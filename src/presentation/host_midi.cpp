// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/host_midi.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef OLDUVAI_HAVE_RTMIDI
#include "rtmidi/RtMidi.h"
#endif

namespace olduvai::presentation {

// ── RtMidi-backed implementation vs. graceful stub ───────────────────────────
//
// Everything platform-specific lives behind Impl + the three free functions.
// When RtMidi is not compiled in (Linux without ALSA, or OLDUVAI_WITH_RTMIDI
// OFF), host_midi_available() is false and the rest no-ops cleanly.

#ifdef OLDUVAI_HAVE_RTMIDI

struct HostMidiPlayer::Impl {
    RtMidiOut out;   // throws RtMidiError on a backend with no MIDI support;
                     // constructed inside a try below.
};

bool host_midi_available() { return true; }

std::vector<std::string> host_midi_list_ports() {
    std::vector<std::string> names;
    try {
        RtMidiOut probe;
        const unsigned int n = probe.getPortCount();
        names.reserve(n);
        for (unsigned int i = 0; i < n; ++i) {
            names.push_back(probe.getPortName(i));
        }
    } catch (const RtMidiError&) {
        // No backend / driver problem → no ports.  Caller prints a clean
        // "no ports" message; never crash on enumeration.
    }
    return names;
}

namespace {
// Find the port whose name best matches a preference, else the first one.
// Returns -1 when there are no ports.
int pick_default_port(RtMidiOut& out) {
    const unsigned int n = out.getPortCount();
    if (n == 0) return -1;
    static const char* kPrefer[] = {"MT-32", "MT32", "MUNT", "CM-32", "CM32"};
    for (const char* pref : kPrefer) {
        for (unsigned int i = 0; i < n; ++i) {
            const std::string nm = out.getPortName(i);
            if (nm.find(pref) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
    }
    return 0;
}
}  // namespace

bool HostMidiPlayer::open(const std::string& port_name) {
    if (open_.load(std::memory_order_relaxed)) return true;
    try {
        impl_ = std::make_unique<Impl>();
        RtMidiOut& out = impl_->out;
        const unsigned int n = out.getPortCount();
        if (n == 0) {
            std::fprintf(stderr,
                "host-midi: no MIDI output ports found.  Connect a MIDI "
                "device, run MUNT, or create a virtual MIDI port "
                "(CoreMIDI / ALSA seq).\n");
            impl_.reset();
            return false;
        }
        int idx = -1;
        if (!port_name.empty()) {
            for (unsigned int i = 0; i < n; ++i) {
                if (out.getPortName(i) == port_name) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            if (idx < 0) {
                std::fprintf(stderr,
                    "host-midi: port \"%s\" not found; falling back to the "
                    "default port.\n", port_name.c_str());
            }
        }
        if (idx < 0) idx = pick_default_port(out);
        if (idx < 0) { impl_.reset(); return false; }
        out.openPort(static_cast<unsigned int>(idx));
        port_name_ = out.getPortName(static_cast<unsigned int>(idx));
        open_.store(true, std::memory_order_relaxed);
        return true;
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "host-midi: failed to open port: %s\n",
                     e.getMessage().c_str());
        impl_.reset();
        return false;
    }
}

namespace {
void send3(HostMidiPlayer::Impl* impl, std::uint8_t s, std::uint8_t d1,
           std::uint8_t d2) {
    if (impl == nullptr) return;
    // 2-byte messages (program change 0xC0, channel pressure 0xD0) must not
    // pad a third byte — some drivers reject the extra byte.
    const int hi = s & 0xF0;
    std::vector<unsigned char> msg;
    if (hi == 0xC0 || hi == 0xD0) {
        msg = {s, d1};
    } else {
        msg = {s, d1, d2};
    }
    try {
        impl->out.sendMessage(&msg);
    } catch (const RtMidiError&) {
        // A transient send failure shouldn't take down the pump thread.
    }
}
}  // namespace

#else  // ── stub: feature unavailable in this build ─────────────────────────

struct HostMidiPlayer::Impl {};

bool host_midi_available() { return false; }
std::vector<std::string> host_midi_list_ports() { return {}; }

bool HostMidiPlayer::open(const std::string&) { return false; }

namespace {
void send3(HostMidiPlayer::Impl*, std::uint8_t, std::uint8_t, std::uint8_t) {}
}  // namespace

#endif  // OLDUVAI_HAVE_RTMIDI

// ── platform-independent player logic ────────────────────────────────────────

HostMidiPlayer::HostMidiPlayer() = default;

HostMidiPlayer::~HostMidiPlayer() {
    stop();
}

void HostMidiPlayer::all_notes_off() {
    for (int chn = 0; chn < 16; ++chn) {
        send3(impl_.get(), static_cast<std::uint8_t>(0xB0 | chn), 123, 0);
    }
}

void HostMidiPlayer::play(const std::vector<std::uint8_t>& midi) {
    if (!open_.load(std::memory_order_relaxed)) return;
    // Stop the existing pump (joins the thread) before swapping the track, so
    // there's no concurrent access to seq_ during the reload.
    stop();
    {
        std::lock_guard<std::mutex> lock(seq_mu_);
        seq_ = MidiSequencer();
        seq_loaded_.store(seq_.load(midi), std::memory_order_relaxed);
    }
    if (!seq_loaded_.load(std::memory_order_relaxed)) return;
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread([this] { pump(); });
}

void HostMidiPlayer::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    seq_loaded_.store(false, std::memory_order_relaxed);
    if (open_.load(std::memory_order_relaxed)) all_notes_off();
}

void HostMidiPlayer::pump() {
    // Drive the shared MidiSequencer with a virtual 1000-"samples"-per-second
    // clock so one advance() sample == one wall-clock millisecond.  This reuses
    // the sequencer's seamless loop (all-notes-off at the seam + initial-tempo
    // restore) and tempo handling verbatim — identical event timing to the
    // builtin-synth path, just clocked by wall time instead of audio samples.
    constexpr int kRate = 1000;          // 1 advance-sample == 1 ms
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    while (running_.load(std::memory_order_relaxed)) {
        const auto now = clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - last)
                              .count();
        if (elapsed_ms <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // Advance `last` by the WHOLE milliseconds consumed, not to `now`:
        // duration_cast truncates, and `last = now` silently discarded the
        // sub-millisecond remainder — up to 1 ms of musical time per
        // iteration.  At a ~2-3 ms wake cadence that compounds to a 15-25%
        // tempo drop (the "GM music too slow" 2026-07-19 Windows field
        // report; OPL music is audio-sample-clocked and was unaffected).
        if (elapsed_ms > 250) {
            // Cap a long stall (e.g. a debugger pause) so we don't
            // fast-forward a huge burst of events on resume; resync fully.
            elapsed_ms = 250;
            last = now;
        } else {
            last += std::chrono::milliseconds(elapsed_ms);
        }
        {
            std::lock_guard<std::mutex> lock(seq_mu_);
            if (seq_loaded_.load(std::memory_order_relaxed)) {
                seq_.advance(static_cast<int>(elapsed_ms), kRate,
                             [&](std::uint8_t s, std::uint8_t d1,
                                 std::uint8_t d2) {
                                 send3(impl_.get(), s, d1, d2);
                             });
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}  // namespace olduvai::presentation
