// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL audio output: music + effects mixer.  AdLib music plays the raw music
// container through the EXE-faithful OPL driver (vendored Nuked-OPL3); the
// melodic paths (MT-32 / GM / host MIDI) take the converted MIDI stream.
// Effects are pre-rendered PCM voices.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "formats/voc.hpp"
#include "presentation/audio/sound_card.hpp"
#include "presentation/audio/host_midi.hpp"
#include "presentation/audio/midi_seq.hpp"
#include "presentation/audio/opl_music.hpp"

namespace olduvai::presentation {

// The melodic synth seam: libmt32emu (MT-32/CM-32L) and libfluidsynth (GM +
// SoundFont).  `send` feeds one channel-voice message; `render` pulls `frames`
// stereo s16 frames (2*frames interleaved samples).  Everything drives the
// synth through these two calls; each implementation owns its library and
// quirks (audio.cpp).  Null = no melodic synth.
class PcmMidiSynth {
public:
    virtual ~PcmMidiSynth() = default;
    virtual void send(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) = 0;
    virtual void render(int frames, std::int16_t* out) = 0;
};

// The devices an SdlAudio opens.
struct AudioSetup {
    std::string music_device = "auto";
    std::string rom_dir;       // MT-32 / CM-32L ROMs (mt32-builtin)
    std::string soundfont;     // .sf2 (gm-builtin)
    std::string sfx_backend = "auto";
    int rate = 0;              // Hz; 0 or out of range: 48000
    int buffer = 0;            // frames, rounded to a power of two; 0: 2048
    std::string midi_port;     // host MIDI port name; empty: the default
    bool offline = false;      // no SDL device or callback: render_offline()
    // "auto" (CM-32L if its ROMs are present, else MT-32), "cm32l", "mt32".
    std::string mt32_model = "auto";
};

// The devices a runtime options struct names (GameOptions, the app's
// PlaySettings); rate, buffer, port and offline are the caller's.
template <class Opts>
AudioSetup audio_setup_of(const Opts& o) {
    AudioSetup s;
    s.music_device = o.music_device;
    s.rom_dir = o.rom_dir;
    s.soundfont = o.soundfont;
    s.sfx_backend = o.sfx_backend;
    if (!o.mt32_model.empty()) s.mt32_model = o.mt32_model;
    return s;
}

class SdlAudio {
public:
    // setup.music_device: "auto" (MT-32, GM, a host MIDI port if one opens, then
    // OPL), "mt32-builtin", "gm-builtin", "opl", "none"; "host-midi" / "mt32" /
    // "gm-host" send music to a MIDI OUT port (RtMidi) instead of rendering it.
    // setup.rom_dir overrides ROM discovery.
    // setup.sfx_backend:
    //   "opl"      AdLib FM (Nuked-OPL3)
    //   "sb-dac"   digital VOC samples
    //   "midi" / "mt32-sfx" / "gm-sfx"  catalog notes via the active synth
    //   "none"     no effects (the Sound card's Off)
    explicit SdlAudio(const AudioSetup& setup = {});
    ~SdlAudio();
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    bool ok() const { return device_ != 0; }
    bool music_available() const {
        return opl_music_ != nullptr || synth_ != nullptr || host_midi_active_;
    }
    const std::string& active_music_backend() const { return music_backend_; }
    // False when the SFX backend is "none" (the Sound card's Off).
    bool sfx_enabled() const { return !sfx_off_; }
    // A pre-rendered effect is ready to play (any backend).
    bool has_sfx(const std::string& id) const;
    // The requested music device could not start and AdLib FM plays instead.
    // --render-audio treats it as a skip.
    bool music_fell_back() const { return music_fell_back_; }
    // play_music() feeds a GM synth, so MT-32 programs map to GM (build_gm_midi
    // gm_translate).  gm-builtin = FluidSynth; gm-host = a GM device behind
    // host MIDI (Windows GS Wavetable Synth).
    bool wants_gm_translation() const {
        return music_backend_ == "gm-builtin" || music_backend_ == "gm-host";
    }
    // A melodic synth or host MIDI renders the music: drop runtime CC /
    // aftertouch / pitch bend in build_gm_midi (mt32_strict), as the EXE's
    // MPU-401 branch never forwards them (FUN_1ecd_0599).  OPL keeps them (the
    // EXE's AdLib branch uses them).
    bool drop_runtime_modulation() const {
        return synth_ != nullptr || host_midi_active_;
    }

    void load_sfx(const std::string& id, const formats::VocAudio& voc);
    void play_sfx(const std::string& id);          // backend dispatch
    // Start the track (loops), from the raw music container: OPL consumes it
    // as-is (the FF 7F voice patches must reach the chip); melodic synths get
    // build_gm_midi() with this backend's flags.
    void play_music(const std::vector<std::uint8_t>& raw_mdi, int track_id);
    void stop_music();
    // Enhanced mix: SFX polyphony (retriggers overlap) and music ducked under
    // the SFX.  Classic: one voice at a fixed balance, like the SB DAC.
    // music/sfx < 0 keep the current value.
    void set_mix_balance(bool enhanced, float music = -1.0f, float sfx = -1.0f);
    // Ramp the music to silence over ~1.8 s and leave it muted (EXE
    // MDI_FadeStop 1f75:00e4); the next play_music() restores it.  Used before
    // the tally's BONUS.MDI.  Ramps from the current gain: after a
    // set_music_fade() to silence it returns at once.
    void fade_out_music();
    // Set the music gain directly (1 = full, 0 = silent), for a fade the
    // caller paces: the ending fades picture and music together.  Host MIDI
    // has no gain, so this does nothing there.
    void set_music_fade(float gain);

    void mix(std::int16_t* out, int frames);       // audio-thread callback

    // Deterministic headless render (offline ctor only): 2*frames s16.
    // Sequencer backends (mt32-builtin / gm) run mix() in fixed chunks, like
    // real playback.  OPL plays the raw container through opl_music_ directly.
    std::vector<std::int16_t> render_offline(
        const std::vector<std::uint8_t>& midi_stream, int frames);

private:
    // Constructor phases, in order: resolve_and_bake_sfx reads music_backend_.
    void select_music_backend(const std::string& music_device,
                              const std::string& rom_dir,
                              const std::string& soundfont,
                              const std::string& midi_port,
                              const std::string& mt32_model);
    // Phases of select_music_backend.
    bool try_host_midi(const char* backend, const std::string& port);
    void try_windows_host_fallback(const std::string& port);
    void start_opl_music();
    static void report_music_failure(const std::string& device,
                                     const std::string& rom_dir);
    void resolve_and_bake_sfx(const std::string& sfx_backend);
    // Pre-render phases of resolve_and_bake_sfx.
    void bake_opl_sfx();
    void bake_midi_sfx();

    std::uint32_t device_ = 0;
    int device_rate_ = 48000;
    std::uint16_t device_samples_ = 2048;   // for the unplug-reopen path
    bool event_watch_installed_ = false;
public:
    // Device-removed recovery: reopen the output with the original spec (from
    // the ctor's event watch).
    void reopen_device();
private:
    std::map<std::string, std::vector<std::int16_t>> sfx_;   // mono s16
    mutable std::mutex mu_;
    // Every effect is a pre-rendered PCM voice, mixed apart from the music
    // synth. Classic caps the pool at 1 (the SB DAC); enhanced raises it.
    struct SfxVoice { const std::vector<std::int16_t>* buf = nullptr;
                      std::size_t pos = 0; };
    std::vector<SfxVoice> sfx_voices_;
    int sfx_poly_ = 1;            // max concurrent SFX voices (1 = faithful)
    // Enhanced ducks the synth layer under the effects (constant while
    // enhanced).
    float music_balance_ = 1.0f;  // music level under SFX (1.0 = faithful)
    float sfx_balance_ = 1.0f;    // SFX level
    // AdLib music driver; null unless OPL music is active.
    std::unique_ptr<OplMusicPlayer> opl_music_;
    // Active melodic synth (MT-32 or GM); null for OPL, host MIDI or no music.
    std::unique_ptr<PcmMidiSynth> synth_;
    MidiSequencer seq_;
    // Host MIDI music: streams to a MIDI OUT port on its own thread; the SDL
    // device renders SFX only.
    HostMidiPlayer host_midi_;
    bool host_midi_active_ = false;
    std::string music_backend_ = "none";
    bool midi_sfx_ = false;
    bool opl_sfx_ = false;   // sfx_backend == "opl": render AdLib FM via Nuked
    bool sfx_off_ = false;   // sfx_backend == "none": play_sfx is silent
    bool music_fell_back_ = false;   // see music_fell_back()
    // Music gain (0..1): read lock-free by the callback, ramped by
    // fade_out_music(), reset by play_music().
    std::atomic<float> music_gain_{1.0f};
    // Real-time health counters, written only by the audio callback (relaxed).
    // Overrun: a callback longer than its buffer budget (audible dropout).
    // Lock-wait: time blocked on mu_ by main-thread contention.  Printed at
    // teardown with OLDUVAI_AUDIO_STATS; always collected.
    std::atomic<std::uint64_t> cb_count_{0};
    std::atomic<std::uint64_t> cb_overruns_{0};
    std::atomic<std::uint64_t> cb_worst_ns_{0};
    std::atomic<std::uint64_t> cb_worst_wait_ns_{0};
    // OLDUVAI_AUDIO_CAPTURE=<wav>: every mixed buffer, written at teardown,
    // plus a <wav>.sync with the performance counter at the first callback (the
    // clock the frame dumps stamp, image_out.hpp).  Appended under mu_.  The
    // game's audio: never for the repo (check_tree rejects clips with audio).
    void write_capture();
    std::string capture_path_;
    std::vector<std::int16_t> capture_;   // interleaved stereo s16
    std::uint64_t capture_t0_ = 0;
};

// Which Sound cards can sound here (sound_card.hpp): libmt32emu + a ROM pair,
// FluidSynth + a SoundFont, host MIDI + an output port.  File and library
// checks only, cached per (rom_dir, soundfont).
SoundCardAvail probe_sound_cards(const std::string& rom_dir,
                                 const std::string& soundfont);

}  // namespace olduvai::presentation
