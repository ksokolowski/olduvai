// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL audio output — music + effects mixer.
// Effects: the digital path is a single voice (a new sample replaces the
// playing one), 8-bit unsigned PCM from the VOC container.
// Music: the AdLib path renders the RAW music container through the
// EXE-faithful OPL driver (opl_music.cpp, vendored Nuked-OPL3 — always in
// the build, no external dependency); the melodic paths (MT-32 / GM / host
// MIDI) take the converted MIDI stream.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "formats/voc.hpp"
#include "presentation/host_midi.hpp"
#include "presentation/midi_seq.hpp"
#include "presentation/opl_music.hpp"

namespace olduvai::presentation {

class SdlAudio {
public:
    // device: "auto" tries mt32 (library + ROMs) then opl; or force
    // "mt32-builtin" / "opl" / "none".  rom_dir overrides ROM discovery.
    //   "mt32" / "host-midi" — route MDI music to a real hardware/software
    //     MIDI OUT port (opt-in; needs RtMidi in the build + a port).  No
    //     audio rendering for music — a wall-clock thread streams events out
    //     the host port.  SFX still render through the SDL device.
    // sfx_backend:
    //   "opl"     — AdLib FM rendered through Nuked-OPL3 (EXE-faithful default)
    //   "sb-dac"  — digital VOC samples
    //   "midi"/"mt32-sfx" — catalog note events through the active synth
    // audio_rate: SDL device sample rate in Hz (0 = device default / auto).
    // audio_buffer: device buffer in sample frames, power-of-two (0 = the
    // 2048-frame default).  Both mirror the reference --audio-rate /
    // --audio-buffer knobs; out-of-range values are clamped/ignored.
    // midi_port: host MIDI OUT port name for the host-midi music device
    // (empty = pick a sensible default).  Ignored unless music_device selects
    // host MIDI.
    explicit SdlAudio(const std::string& music_device = "auto",
                      const std::string& rom_dir = "",
                      const std::string& soundfont = "",
                      const std::string& sfx_backend = "auto",
                      int audio_rate = 0, int audio_buffer = 0,
                      const std::string& midi_port = "");
    ~SdlAudio();
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    bool ok() const { return device_ != 0; }
    bool music_available() const {
        return opl_music_ != nullptr || mt32_ != nullptr ||
               fs_synth_ != nullptr || host_midi_active_;
    }
    const std::string& active_music_backend() const { return music_backend_; }
    // True when play_music() feeds a General MIDI synth, i.e. the MDI
    // conversion must map Roland MT-32 programs to GM (build_gm_midi's
    // gm_translate).  gm-builtin = FluidSynth; gm-host = host MIDI out to a
    // GM device (e.g. the Windows GS Wavetable synth behind the MIDI mapper).
    bool wants_gm_translation() const {
        return music_backend_ == "gm-builtin" || music_backend_ == "gm-host";
    }
    // True when a MELODIC synth (MT-32 or GM/FluidSynth) will render the music.
    // Callers pass this as build_gm_midi()'s mt32_strict so runtime CC /
    // aftertouch / pitch-bend events (which the EXE 'R'/MPU-401 branch never
    // forwards, per FUN_1ecd_0599) are dropped — matching the Python MT-32
    // render and keeping GM's character close to the MT-32 (owner request).
    // The OPL/AdLib backend (mt32_/fs_ both null) keeps those events, as the
    // EXE 'A' branch genuinely uses them.
    bool drop_runtime_modulation() const {
        // Host MIDI mirrors the EXE 'R'/MPU-401 branch (a real MT-32), so it
        // drops runtime CC/aftertouch/pitch-bend just like the builtin MT-32.
        return mt32_ != nullptr || fs_synth_ != nullptr || host_midi_active_;
    }

    void load_sfx(const std::string& id, const formats::VocAudio& voc);
    void play_sfx(const std::string& id);          // backend dispatch
    // Start the track (loops).  Takes the RAW music container: the backend
    // decides the stream shape internally — the OPL driver consumes it as-is
    // (the FF 7F voice patches must reach the chip), the melodic synths get
    // build_gm_midi() with this backend's strict/translate flags.
    void play_music(const std::vector<std::uint8_t>& raw_mdi, int track_id);
    void stop_music();
    // Enhanced-mode mix balance.  Raises SFX polyphony (rapid retriggers
    // overlap instead of cutting off) and ducks the music under the SFX so
    // effects sit above the soundtrack.  No-op / single-voice + 1.0 gain in
    // faithful (default) mode — original SB DAC was one voice at a fixed
    // hardware balance.  music/sfx <0 keep the current value (knob override).
    void set_mix_balance(bool enhanced, float music = -1.0f, float sfx = -1.0f);
    // Gradually ramp the music mix to silence, then leave it muted —
    // blocks ~1.8s (mirrors EXE MDI_FadeStop 1f75:00e4).  The next
    // play_music() restores full gain.  Used at the score-tally
    // transition so the level/boss track fades before BONUS.MDI.
    void fade_out_music();

    void mix(std::int16_t* out, int frames);       // audio-thread callback

private:
    std::uint32_t device_ = 0;
    int device_rate_ = 48000;
    std::uint16_t device_samples_ = 2048;   // for the unplug-reopen path
    bool event_watch_installed_ = false;
public:
    // SDL_AUDIODEVICEREMOVED recovery: close + reopen the output device with
    // the original spec (called from the event watch installed in the ctor).
    void reopen_device();
private:
    std::map<std::string, std::vector<std::int16_t>> sfx_;   // mono s16
    std::mutex mu_;
    // Polyphonic SFX: every triggered effect is a pre-rendered PCM wave mixed
    // independently of the music synth.  Faithful mode caps the pool at 1
    // (original single-voice SB DAC); enhanced mode raises it so rapid
    // retriggers overlap.
    struct SfxVoice { const std::vector<std::int16_t>* buf = nullptr;
                      std::size_t pos = 0; };
    std::vector<SfxVoice> sfx_voices_;
    int sfx_poly_ = 1;            // max concurrent SFX voices (1 = faithful)
    // Enhanced mix mode active: play_sfx prefers HD bake variants.
    bool sfx_enhanced_ = false;
    // HD SFX bake variants (enhanced mode; loaded from <cache>/hd_sfx when
    // the offline bake produced them — see scripts/bake_hd_sfx.py).
    std::map<std::string, std::vector<std::int16_t>> sfx_hd_;
    float music_balance_ = 1.0f;  // music level under SFX (1.0 = faithful)
    float sfx_balance_ = 1.0f;    // SFX level
    // Authentic AdLib music driver (vendored Nuked-OPL3; null unless the
    // OPL music path is the active backend).
    std::unique_ptr<OplMusicPlayer> opl_music_;
    // libmt32emu handles (dlopen'd; null when absent or no ROMs).
    void* mt32_lib_ = nullptr;
    void* mt32_ = nullptr;
    // libfluidsynth handles (dlopen'd; null when absent or no SoundFont).
    void* fs_lib_ = nullptr;
    void* fs_settings_ = nullptr;
    void* fs_synth_ = nullptr;
    MidiSequencer seq_;
    // Host-MIDI music path (opt-in, --music-device mt32/host-midi).  When
    // active, music streams out a real MIDI OUT port on its own wall-clock
    // thread and NONE of the synth handles above are used for music; the SDL
    // device still renders SFX.
    HostMidiPlayer host_midi_;
    bool host_midi_active_ = false;
    std::string music_backend_ = "none";
    bool midi_sfx_ = false;
    bool opl_sfx_ = false;   // sfx_backend == "opl": render AdLib FM via Nuked
    struct PendingOff { int channel, note; std::int64_t at; };
    std::vector<PendingOff> note_offs_;
    std::int64_t sample_clock_ = 0;
    // Music mix gain (0..1).  Read lock-free by the audio callback, ramped
    // by fade_out_music() on the main thread; reset to 1 by play_music().
    std::atomic<float> music_gain_{1.0f};
    // Real-time health counters (RB1 pre-emptive instrumentation): written by
    // the audio callback only (single writer, relaxed stores), read at
    // teardown.  Overrun = one callback took longer than its buffer budget
    // (frames / device_rate) — the audible-dropout condition on slow hosts.
    // Lock-wait = time the callback spent blocked on mu_ (main-thread
    // contention, the RB1 hazard).  Summary printed at destruction when
    // OLDUVAI_AUDIO_STATS is set; collection is always on (three perf-counter
    // reads per callback).
    std::atomic<std::uint64_t> cb_count_{0};
    std::atomic<std::uint64_t> cb_overruns_{0};
    std::atomic<std::uint64_t> cb_worst_ns_{0};
    std::atomic<std::uint64_t> cb_worst_wait_ns_{0};
};

}  // namespace olduvai::presentation
