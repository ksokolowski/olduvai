// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/audio.hpp"

#include "presentation/hd_sfx.hpp"
#include "presentation/opl_sfx.hpp"
#include "presentation/resample.hpp"
#include "presentation/soundfont_pick.hpp"
#include "presentation/wav_io.hpp"

#include <SDL.h>

#include "presentation/dynlib.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>

#include "formats/mdi.hpp"

namespace olduvai::presentation {

namespace {

// SDL_AUDIODEVICEREMOVED watch: SDL_DelEventWatch matches by FUNCTION
// POINTER + userdata, so ctor-install and dtor-remove must reference the
// SAME function — two identical-body lambdas would not match, leaking the
// watch and dangling `this` across the settings-reinit audio rebuild.
int audio_device_watch(void* ud, SDL_Event* ev);

void* dlopen_first(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (void* h = dyn_open(name)) return h;
    }
    return nullptr;
}

// The libmt32emu C API subset (interface version 1+).
using Mt32CreateFn = void* (*)(void* report_handler, void* instance_data);
using Mt32AddRomFn = int (*)(void*, const char*);
using Mt32OpenFn = int (*)(void*);
using Mt32RateFn = void (*)(void*, double);
using Mt32PlayMsgFn = void (*)(void*, std::uint32_t);
using Mt32RenderFn = void (*)(void*, std::int16_t*, std::uint32_t);
using Mt32FreeFn = void (*)(void*);

struct Mt32Api {
    Mt32CreateFn create = nullptr;
    Mt32AddRomFn add_rom = nullptr;
    Mt32OpenFn open = nullptr;
    Mt32RateFn set_rate = nullptr;
    Mt32PlayMsgFn play_msg = nullptr;
    Mt32RenderFn render = nullptr;
    Mt32FreeFn free_ctx = nullptr;
};
Mt32Api g_mt32;

// Silent libmt32emu report handler.  Passing NULL to mt32emu_create_context
// installs libmt32emu's default handler, which routes printDebug() to stderr
// and spams "Rhythm: Attempted to play unmapped key N" every time a rhythm
// note lands on a drum slot the MT-32 ROM rhythm map doesn't populate (e.g.
// rocky.mdi / L4 boss: ch9 = 91x note 85).  That drop is EXE/hardware-
// faithful — a real MT-32 silently ignores those keys — so the messages are
// pure noise.  The Python renderer installs the same no-op handler
// (a silent report handler).  Layout matches c_interface.h
// mt32emu_report_handler_i_v0 (15 fns); getVersionID returns 0 for v0.
// mt32emu_report_handler_i is { const v0* v0; } passed BY VALUE — one pointer
// wide, so the v0-struct address IS the by-value representation (what
// create()'s void* receives).
struct Mt32ReportHandlerV0 {
    std::uint32_t (*getVersionID)(void*);
    void (*printDebug)(void*, const char*, va_list);
    void (*onErrorControlROM)(void*);
    void (*onErrorPCMROM)(void*);
    void (*showLCDMessage)(void*, const char*);
    void (*onMIDIMessagePlayed)(void*);
    std::uint32_t (*onMIDIQueueOverflow)(void*);
    void (*onMIDISystemRealtime)(void*, std::uint8_t);
    void (*onDeviceReset)(void*);
    void (*onDeviceReconfig)(void*);
    void (*onNewReverbMode)(void*, std::uint8_t);
    void (*onNewReverbTime)(void*, std::uint8_t);
    void (*onNewReverbLevel)(void*, std::uint8_t);
    void (*onPolyStateChanged)(void*, std::uint8_t);
    void (*onProgramChanged)(void*, std::uint8_t, const char*, const char*);
};
std::uint32_t mt32_rh_version(void*) { return 0; }  // VERSION_0
void mt32_rh_debug(void*, const char*, va_list) {}   // swallow the spam
void mt32_rh_noop(void*) {}
void mt32_rh_noop_str(void*, const char*) {}
std::uint32_t mt32_rh_overflow(void*) { return 1; }  // BOOL_TRUE (recover)
void mt32_rh_noop_u8(void*, std::uint8_t) {}
void mt32_rh_prog(void*, std::uint8_t, const char*, const char*) {}
const Mt32ReportHandlerV0 kSilentReportHandler = {
    mt32_rh_version, mt32_rh_debug, mt32_rh_noop, mt32_rh_noop,
    mt32_rh_noop_str, mt32_rh_noop, mt32_rh_overflow, mt32_rh_noop_u8,
    mt32_rh_noop, mt32_rh_noop, mt32_rh_noop_u8, mt32_rh_noop_u8,
    mt32_rh_noop_u8, mt32_rh_noop_u8, mt32_rh_prog,
};

void* load_mt32emu() {
#ifdef __APPLE__
    void* h = dlopen_first({"libmt32emu.dylib", "libmt32emu.2.dylib",
                            "/opt/homebrew/lib/libmt32emu.dylib",
                            "/usr/local/lib/libmt32emu.dylib"});
#elif defined(_WIN32)
    void* h = dlopen_first({"libmt32emu.dll", "mt32emu.dll",
                            "libmt32emu-2.dll"});
#else
    void* h = dlopen_first({"libmt32emu.so.2", "libmt32emu.so",
                            "libmt32emu.dylib"});
#endif
    if (h != nullptr) return h;
    if (const char* env = std::getenv("OLDUVAI_MT32EMU")) {
        if ((h = dyn_open(env)) != nullptr) return h;
    }
    return nullptr;
}

// ROM discovery: rom_dir arg -> $OLDUVAI_MT32_ROMS ->
// ~/.config/olduvai/mt32-roms -> ./mt32-roms.  Accepts the MT-32 or
// CM-32L control+PCM pair in either letter case.
bool add_rom_pair(void* ctx, const std::string& dir) {
    auto try_pair = [&](const char* ctl, const char* pcm) {
        const std::string a = dir + "/" + ctl;
        const std::string b = dir + "/" + pcm;
        if (g_mt32.add_rom(ctx, a.c_str()) >= 0 &&
            g_mt32.add_rom(ctx, b.c_str()) >= 0) {
            return true;
        }
        return false;
    };
    return try_pair("CM32L_CONTROL.ROM", "CM32L_PCM.ROM") ||
           try_pair("MT32_CONTROL.ROM", "MT32_PCM.ROM") ||
           try_pair("cm32l_control.rom", "cm32l_pcm.rom") ||
           try_pair("mt32_control.rom", "mt32_pcm.rom");
}

std::vector<std::string> rom_search_dirs(const std::string& rom_dir) {
    std::vector<std::string> dirs;
    if (!rom_dir.empty()) dirs.push_back(rom_dir);
    if (const char* env = std::getenv("OLDUVAI_MT32_ROMS")) {
        dirs.push_back(env);
    }
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(std::string(home) + "/.config/olduvai/mt32-roms");
        dirs.push_back(std::string(home) + "/mt32-roms");
    }
    dirs.push_back("./mt32-roms");
    return dirs;
}

// The libfluidsynth C API subset.
using FsNewSettingsFn = void* (*)();
using FsSetNumFn = int (*)(void*, const char*, double);
using FsNewSynthFn = void* (*)(void*);
using FsSfLoadFn = int (*)(void*, const char*, int);
using FsNoteOnFn = int (*)(void*, int, int, int);
using FsNoteOffFn = int (*)(void*, int, int);
using FsProgFn = int (*)(void*, int, int);
using FsCcFn = int (*)(void*, int, int, int);
using FsBendFn = int (*)(void*, int, int);
using FsWriteFn = int (*)(void*, int, void*, int, int, void*, int, int);
using FsDelSynthFn = void (*)(void*);
using FsDelSettingsFn = void (*)(void*);

struct FsApi {
    FsNewSettingsFn new_settings = nullptr;
    FsSetNumFn setnum = nullptr;
    FsNewSynthFn new_synth = nullptr;
    FsSfLoadFn sfload = nullptr;
    FsNoteOnFn noteon = nullptr;
    FsNoteOffFn noteoff = nullptr;
    FsProgFn program = nullptr;
    FsCcFn cc = nullptr;
    FsBendFn bend = nullptr;
    FsWriteFn write_s16 = nullptr;
    FsDelSynthFn del_synth = nullptr;
    FsDelSettingsFn del_settings = nullptr;
};
FsApi g_fs;

void* load_fluidsynth() {
#ifdef __APPLE__
    void* h = dlopen_first({"libfluidsynth.dylib", "libfluidsynth.3.dylib",
                            "/opt/homebrew/lib/libfluidsynth.dylib",
                            "/usr/local/lib/libfluidsynth.dylib"});
#elif defined(_WIN32)
    void* h = dlopen_first({"libfluidsynth-3.dll", "fluidsynth.dll",
                            "libfluidsynth.dll"});
#else
    void* h = dlopen_first({"libfluidsynth.so.3", "libfluidsynth.so.2",
                            "libfluidsynth.so"});
#endif
    if (h != nullptr) return h;
    if (const char* env = std::getenv("OLDUVAI_FLUIDSYNTH")) {
        if ((h = dyn_open(env)) != nullptr) return h;
    }
    return nullptr;
}

// SoundFont discovery: explicit path -> $OLDUVAI_SOUNDFONT ->
// ~/.config/olduvai/soundfonts -> system SoundFont directories.  The precedence
// core lives in soundfont_pick.hpp (select_soundfont); this supplies the real
// directories, name preference, and a filesystem predicate.
std::string find_soundfont(const std::string& override_path) {
    auto exists = [](const std::string& p) {
        std::ifstream f(p);
        return f.good();
    };
    if (!override_path.empty() && exists(override_path)) return override_path;
    if (const char* env = std::getenv("OLDUVAI_SOUNDFONT")) {
        if (exists(env)) return env;
    }
    std::string config_dir;
    if (const char* home = std::getenv("HOME")) {
        config_dir = std::string(home) + "/.config/olduvai/soundfonts";
    }
    const std::vector<std::string> system_dirs = {
        "/usr/share/sounds/sf2", "/usr/share/soundfonts", "/usr/share/scummvm"};
    // Roland SC-55 first: it is the Sound Canvas set (same lineage as the
    // Windows gm.dls) and the most faithful GM voice.  Debian/Ubuntu ship it as
    // scummvm-data's /usr/share/scummvm/Roland_SC-55.sf2 (GPLv3).  Then the
    // clean-provenance free faces.
    const std::vector<std::string> names = {
        "Roland_SC-55.sf2", "GeneralUser-GS.sf2", "GeneralUser GS.sf2",
        "FluidR3_GM.sf2", "default-GM.sf2"};
    return select_soundfont(config_dir, system_dirs, names, exists);
}

void sdl_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<SdlAudio*>(userdata);
    self->mix(reinterpret_cast<std::int16_t*>(stream), len / 4);
}

// The catalog's note-event rows (channel, note, velocity, program, duration
// ms) for the MIDI SFX backends — pre-rendered to PCM in the constructor.
// note2 = optional second chord note (-1 = none).  SFX_GENERIC is an
// EXE-proven 2-note BellSinger cluster (notes 79 + 77, velocity 127).
struct MidiSfx { const char* id; int ch, note, vel, prog, ms, note2; };
constexpr MidiSfx kMidiSfx[] = {
    {"SFX_HIT", 9, 43, 127, -1, 150, -1},
    {"SFX_JUMP_APEX", 9, 73, 127, -1, 250, -1},
    {"SFX_GENERIC", 7, 79, 127, 46, 200, 77},
    {"SFX_WAIT_AND_PLAY", 9, 49, 100, -1, 400, -1},
};
}  // namespace

SdlAudio::SdlAudio(const std::string& music_device,
                   const std::string& rom_dir,
                   const std::string& soundfont,
                   const std::string& sfx_backend,
                   int audio_rate, int audio_buffer,
                   const std::string& midi_port) {
    // SFX backend flags are resolved AFTER the music backend loads (below),
    // so "auto" can pair to the music device the way Python's
    // _resolve_sfx_backend does.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "audio: SDL audio init failed (%s) — running silent\n",
                     SDL_GetError());
        return;
    }
    // --audio-rate / --audio-buffer overrides.  Sane bounds only — a wild
    // value would otherwise propagate into every synth render.  0/unset
    // keeps the defaults (device_rate_ = 48000, 2048-frame buffer).  The
    // buffer is rounded down to a power of two (SDL wants one).
    if (audio_rate >= 8000 && audio_rate <= 192000) device_rate_ = audio_rate;
    Uint16 want_samples = 2048;
    if (audio_buffer >= 64 && audio_buffer <= 16384) {
        Uint16 p = 64;
        while (static_cast<int>(p) * 2 <= audio_buffer) p = p * 2;
        want_samples = p;
    }
    SDL_AudioSpec want{};
    want.freq = device_rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = want_samples;
    want.callback = sdl_callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    device_samples_ = want_samples;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::fprintf(stderr,
                     "audio: could not open an audio device (%s) — running silent\n",
                     SDL_GetError());
        return;
    }
    device_rate_ = have.freq;
    // Device-unplug recovery: an SDL event watch sees SDL_AUDIODEVICEREMOVED
    // no matter which loop (surface / boss / menu) is pumping events, so
    // pulling the headphones mid-game reopens the default output instead of
    // permanent silence.  The watch runs on the pumping (main) thread.
    SDL_AddEventWatch(audio_device_watch, this);
    event_watch_installed_ = true;


    // ── Host-MIDI music path (opt-in) ───────────────────────────────────────
    // "host-midi" (and the Python back-compat alias "mt32") route MDI music to
    // a real MIDI OUT port via RtMidi instead of rendering audio.  When it
    // opens a port we mark host_midi_active_ and SKIP every synth-music setup
    // block below (none of mt32_/fs_synth_/opl_music_ ever bind), so the music
    // backend is "host-midi" and the SDL device only ever renders SFX.  If no
    // port opens (no device, RtMidi absent), we fall through to the regular
    // synth selection so the user still gets sound.  Note: bare "mt32" only
    // means host MIDI here, NOT the builtin emulator — that is "mt32-builtin".
    std::string music_device_eff = music_device;
    if (music_device == "host-midi" || music_device == "mt32") {
        if (host_midi_.open(midi_port)) {
            host_midi_active_ = true;
            music_backend_ = "host-midi";
            std::fprintf(stderr, "host-midi: routing music to port \"%s\"\n",
                         host_midi_.port_name().c_str());
        } else {
            std::fprintf(stderr,
                "host-midi: no MIDI output available; falling back to the "
                "auto synth chain.\n");
            music_device_eff = "auto";
        }
    }
    // "gm-host": host MIDI out with the MT-32 → General MIDI program
    // translation applied (build_gm_midi gm_translate; keyed off the
    // backend name at the play_music call sites).  For GM synths behind
    // the OS MIDI mapper — on Windows the always-present Microsoft GS
    // Wavetable Synth — where raw Roland programs would pick wrong
    // instruments.  Plain "host-midi" stays untranslated for real MT-32
    // hardware / MUNT ports.
    if (music_device == "gm-host") {
        if (host_midi_.open(midi_port)) {
            host_midi_active_ = true;
            music_backend_ = "gm-host";
            std::fprintf(stderr, "gm-host: routing GM music to port \"%s\"\n",
                         host_midi_.port_name().c_str());
        } else {
            std::fprintf(stderr,
                "gm-host: no MIDI output available; falling back to the "
                "auto synth chain.\n");
            music_device_eff = "auto";
        }
    }
    // MT-32 first when requested or in auto mode.
    if (!host_midi_active_ &&
        (music_device_eff == "auto" || music_device_eff == "mt32-builtin" ||
         music_device_eff == "mt32")) {
        mt32_lib_ = load_mt32emu();
        if (mt32_lib_ != nullptr) {
            g_mt32.create = reinterpret_cast<Mt32CreateFn>(
                dyn_sym(mt32_lib_, "mt32emu_create_context"));
            g_mt32.add_rom = reinterpret_cast<Mt32AddRomFn>(
                dyn_sym(mt32_lib_, "mt32emu_add_rom_file"));
            g_mt32.open = reinterpret_cast<Mt32OpenFn>(
                dyn_sym(mt32_lib_, "mt32emu_open_synth"));
            g_mt32.set_rate = reinterpret_cast<Mt32RateFn>(
                dyn_sym(mt32_lib_, "mt32emu_set_stereo_output_samplerate"));
            g_mt32.play_msg = reinterpret_cast<Mt32PlayMsgFn>(
                dyn_sym(mt32_lib_, "mt32emu_play_msg"));
            g_mt32.render = reinterpret_cast<Mt32RenderFn>(
                dyn_sym(mt32_lib_, "mt32emu_render_bit16s"));
            g_mt32.free_ctx = reinterpret_cast<Mt32FreeFn>(
                dyn_sym(mt32_lib_, "mt32emu_free_context"));
            if (g_mt32.create != nullptr && g_mt32.add_rom != nullptr &&
                g_mt32.open != nullptr && g_mt32.play_msg != nullptr &&
                g_mt32.render != nullptr) {
                void* ctx = g_mt32.create(
                    const_cast<Mt32ReportHandlerV0*>(&kSilentReportHandler),
                    nullptr);
                bool roms_ok = false;
                for (const auto& dir : rom_search_dirs(rom_dir)) {
                    if (add_rom_pair(ctx, dir)) {
                        roms_ok = true;
                        break;
                    }
                }
                if (roms_ok) {
                    if (g_mt32.set_rate != nullptr) {
                        g_mt32.set_rate(ctx, device_rate_);
                    }
                    if (g_mt32.open(ctx) == 0) {
                        mt32_ = ctx;
                        music_backend_ = "mt32-builtin";
                    }
                }
                if (mt32_ == nullptr && g_mt32.free_ctx != nullptr) {
                    g_mt32.free_ctx(ctx);
                }
            }
            if (mt32_ == nullptr) {
                dyn_close(mt32_lib_);
                mt32_lib_ = nullptr;
            }
        }
    }
    // GM (FluidSynth + SoundFont) next in the auto chain.
    if (!host_midi_active_ && mt32_ == nullptr &&
        (music_device_eff == "auto" || music_device_eff == "gm-builtin" ||
         music_device_eff == "gm")) {
        const std::string sf = find_soundfont(soundfont);
        if (!sf.empty()) {
            std::fprintf(stderr, "gm-builtin: soundfont = %s\n", sf.c_str());
            fs_lib_ = load_fluidsynth();
            if (fs_lib_ != nullptr) {
                g_fs.new_settings = reinterpret_cast<FsNewSettingsFn>(
                    dyn_sym(fs_lib_, "new_fluid_settings"));
                g_fs.setnum = reinterpret_cast<FsSetNumFn>(
                    dyn_sym(fs_lib_, "fluid_settings_setnum"));
                g_fs.new_synth = reinterpret_cast<FsNewSynthFn>(
                    dyn_sym(fs_lib_, "new_fluid_synth"));
                g_fs.sfload = reinterpret_cast<FsSfLoadFn>(
                    dyn_sym(fs_lib_, "fluid_synth_sfload"));
                g_fs.noteon = reinterpret_cast<FsNoteOnFn>(
                    dyn_sym(fs_lib_, "fluid_synth_noteon"));
                g_fs.noteoff = reinterpret_cast<FsNoteOffFn>(
                    dyn_sym(fs_lib_, "fluid_synth_noteoff"));
                g_fs.program = reinterpret_cast<FsProgFn>(
                    dyn_sym(fs_lib_, "fluid_synth_program_change"));
                g_fs.cc = reinterpret_cast<FsCcFn>(
                    dyn_sym(fs_lib_, "fluid_synth_cc"));
                g_fs.bend = reinterpret_cast<FsBendFn>(
                    dyn_sym(fs_lib_, "fluid_synth_pitch_bend"));
                g_fs.write_s16 = reinterpret_cast<FsWriteFn>(
                    dyn_sym(fs_lib_, "fluid_synth_write_s16"));
                g_fs.del_synth = reinterpret_cast<FsDelSynthFn>(
                    dyn_sym(fs_lib_, "delete_fluid_synth"));
                g_fs.del_settings = reinterpret_cast<FsDelSettingsFn>(
                    dyn_sym(fs_lib_, "delete_fluid_settings"));
                if (g_fs.new_settings != nullptr &&
                    g_fs.new_synth != nullptr && g_fs.sfload != nullptr &&
                    g_fs.write_s16 != nullptr) {
                    fs_settings_ = g_fs.new_settings();
                    if (g_fs.setnum != nullptr) {
                        g_fs.setnum(fs_settings_, "synth.sample-rate",
                                    device_rate_);
                    }
                    fs_synth_ = g_fs.new_synth(fs_settings_);
                    if (fs_synth_ != nullptr &&
                        g_fs.sfload(fs_synth_, sf.c_str(), 1) >= 0) {
                        music_backend_ = "gm-builtin";
                    } else if (fs_synth_ != nullptr) {
                        g_fs.del_synth(fs_synth_);
                        fs_synth_ = nullptr;
                    }
                }
            }
            if (fs_synth_ == nullptr && fs_lib_ != nullptr) {
                dyn_close(fs_lib_);
                fs_lib_ = nullptr;
            }
        }
    }
#ifdef _WIN32
    // Windows auto-chain slot: no MT-32 ROMs, no SoundFont — but every
    // Windows machine has the Microsoft GS Wavetable Synth behind the MIDI
    // mapper, and translated GM there beats the OPL rendition as a default.
    // Explicit `--music-device opl` still gives the authentic AdLib path.
    // The port picker prefers MT-32/MUNT ports; when it lands on one, send
    // RAW MT-32 (host-midi) — a MUNT user gets the authentic device with
    // no flags at all.  GM translation is only for GM synths like the GS.
    if (!host_midi_active_ && mt32_ == nullptr && fs_synth_ == nullptr &&
        music_device_eff == "auto" && host_midi_.open(midi_port)) {
        host_midi_active_ = true;
        const std::string& pn = host_midi_.port_name();
        const bool mt32_port =
            pn.find("MT-32") != std::string::npos ||
            pn.find("MT32") != std::string::npos ||
            pn.find("MUNT") != std::string::npos ||
            pn.find("CM-32") != std::string::npos ||
            pn.find("CM32") != std::string::npos;
        music_backend_ = mt32_port ? "host-midi" : "gm-host";
        std::fprintf(stderr,
                     "%s: no MT-32 ROMs or SoundFont found — routing %s "
                     "music to \"%s\" (use --music-device opl for AdLib)\n",
                     music_backend_.c_str(), mt32_port ? "MT-32" : "GM",
                     pn.c_str());
    }
#endif
    const bool want_opl =
        !host_midi_active_ &&
        (music_device_eff == "opl" ||
         ((music_device_eff == "auto") && mt32_ == nullptr &&
          fs_synth_ == nullptr));
    if (want_opl) {
        // Authentic AdLib: the EXE-faithful driver on the vendored
        // Nuked-OPL3 core (same emulator as the OPL SFX path — required for
        // the correct music/SFX balance).  Always available: no dlopen, no
        // submodule dependency.
        opl_music_ = std::make_unique<OplMusicPlayer>(device_rate_);
        music_backend_ = "opl";
    }
    // Explicit backend requests must not fail SILENTLY: an exact
    // `--music-device mt32-builtin` with no ROMs (or `gm-builtin` with no
    // SoundFont) skips every other synth block and used to leave the game
    // mute with no hint why.  Say what failed and what to check.  A typo'd
    // device name lands here too (nothing matches it).
    if (music_backend_ == "none" && !host_midi_active_ &&
        music_device_eff != "auto" && music_device_eff != "off" &&
        music_device_eff != "none") {
        std::fprintf(stderr,
                     "audio: music device '%s' could not start — no music.  "
                     "(mt32-builtin needs MT-32/CM-32L ROMs via --rom-dir; "
                     "gm-builtin needs a SoundFont via --soundfont; check the "
                     "device name for typos)\n",
                     music_device_eff.c_str());
    }
    // Resolve the SFX backend, music-aware — mirrors Python's
    // _resolve_sfx_backend so "auto" pairs to the music device: MT-32 music →
    // MT-32 SFX, GM music → GM SFX, OPL/none → SB-DAC VOC samples.  "auto"
    // never selects "opl" (AdLib synth SFX stays opt-in, like the EXE 'A'
    // card).  Explicit choices pass through unchanged.
    std::string sfxb = sfx_backend;
    if (sfxb == "auto") {
        if (mt32_ != nullptr) sfxb = "mt32-sfx";
        else if (fs_synth_ != nullptr) sfxb = "gm-sfx";
        else sfxb = "sb-dac";
    }
    midi_sfx_ = (sfxb == "midi" || sfxb == "mt32-sfx" || sfxb == "gm-sfx");
    opl_sfx_ = (sfxb == "opl");

    // OPL SFX backend: pre-render the AdLib FM voices now (at the device rate)
    // so they're available even when a SFX has no VOC asset to trigger
    // load_sfx().  The renderer emits interleaved stereo (L==R in OPL2 mode);
    // playback is mono (one sample per frame, duplicated to both channels), so
    // down-mix by taking the L channel.  play_sfx() then plays from sfx_.
    if (opl_sfx_ && device_ != 0) {
        for (const auto& id : opl_sfx_ids()) {
            const auto stereo = render_adlib_sfx_by_id(id, device_rate_);
            if (stereo.empty()) continue;
            std::vector<std::int16_t> mono(stereo.size() / 2);
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] = stereo[i * 2];
            sfx_[id] = std::move(mono);
        }
    }
    // MIDI SFX backends (mt32-sfx / gm-sfx): pre-render each catalog note event
    // to PCM through the active synth ONCE, here at construction before any
    // music loads — mirrors the Python build, which renders each SFX to a
    // pygame.mixer.Sound.  Stored in sfx_ and played as independent polyphonic
    // waves via the voice pool, NEVER injected live into the music synth (so
    // they never steal music voices and get their own balance gain).
    if (midi_sfx_ && (mt32_ != nullptr || fs_synth_ != nullptr) &&
        device_ != 0) {
        const int tail_frames = 100 * device_rate_ / 1000;  // Python tail_ms=100
        for (const auto& s : kMidiSfx) {
            const int gate_frames = std::max(1, s.ms * device_rate_ / 1000);
            const int total = gate_frames + tail_frames;
            std::vector<std::int16_t> stereo(
                static_cast<std::size_t>(total) * 2, 0);
            if (mt32_ != nullptr) {
                if (s.prog >= 0) {
                    g_mt32.play_msg(mt32_,
                        static_cast<std::uint32_t>(0xC0 | s.ch) |
                        (static_cast<std::uint32_t>(s.prog) << 8));
                }
                g_mt32.play_msg(mt32_,
                    static_cast<std::uint32_t>(0x90 | s.ch) |
                    (static_cast<std::uint32_t>(s.note) << 8) |
                    (static_cast<std::uint32_t>(s.vel) << 16));
                if (s.note2 >= 0) {
                    g_mt32.play_msg(mt32_,
                        static_cast<std::uint32_t>(0x90 | s.ch) |
                        (static_cast<std::uint32_t>(s.note2) << 8) |
                        (static_cast<std::uint32_t>(s.vel) << 16));
                }
                g_mt32.render(mt32_, stereo.data(),
                              static_cast<std::uint32_t>(gate_frames));
                g_mt32.play_msg(mt32_,
                    static_cast<std::uint32_t>(0x80 | s.ch) |
                    (static_cast<std::uint32_t>(s.note) << 8));
                if (s.note2 >= 0) {
                    g_mt32.play_msg(mt32_,
                        static_cast<std::uint32_t>(0x80 | s.ch) |
                        (static_cast<std::uint32_t>(s.note2) << 8));
                }
                g_mt32.render(mt32_, stereo.data() + gate_frames * 2,
                              static_cast<std::uint32_t>(tail_frames));
            } else {  // fs_synth_ (gm-sfx)
                if (s.prog >= 0 && g_fs.program != nullptr) {
                    g_fs.program(fs_synth_, s.ch, s.prog);
                }
                if (g_fs.noteon != nullptr) {
                    g_fs.noteon(fs_synth_, s.ch, s.note, s.vel);
                    if (s.note2 >= 0)
                        g_fs.noteon(fs_synth_, s.ch, s.note2, s.vel);
                }
                g_fs.write_s16(fs_synth_, gate_frames, stereo.data(), 0, 2,
                               stereo.data(), 1, 2);
                if (g_fs.noteoff != nullptr) {
                    g_fs.noteoff(fs_synth_, s.ch, s.note);
                    if (s.note2 >= 0) g_fs.noteoff(fs_synth_, s.ch, s.note2);
                }
                g_fs.write_s16(fs_synth_, tail_frames,
                               stereo.data() + gate_frames * 2, 0, 2,
                               stereo.data() + gate_frames * 2, 1, 2);
            }
            // Down-mix to mono by AVERAGING both channels (the voice pool
            // duplicates the result to both output channels).  Taking only the
            // left channel loses hard-panned MT-32 patches: SFX_GENERIC's
            // BellSinger (prog 46, the food-pickup ding) is panned hard RIGHT —
            // its left channel is ~17x quieter (peak 1435 vs 24786) and holds
            // only reverb bleed, so take-L + peak-normalize amplified a
            // reverb-wash with no attack instead of the bell strike (the
            // "missing ding").  (L+R)/2 keeps the full strike+decay.
            std::vector<std::int16_t> mono(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
                mono[i] = static_cast<std::int16_t>(
                    (static_cast<int>(stereo[i * 2]) + stereo[i * 2 + 1]) / 2);
            // Peak-normalize to ~-4 dBFS (peak 20000), matching Python's
            // mt32emu_sfx._peak_normalize_int16: libmt32emu's default output
            // gain renders SFX ~18 dB below the music level, so without this
            // the (correct) instrument is nearly inaudible under the music.
            // Scale UP only, never down (mirrors Python).
            int peak = 0;
            for (std::int16_t v : mono) {
                const int a = v < 0 ? -v : v;
                if (a > peak) peak = a;
            }
            if (peak > 0 && peak < 20000) {
                for (auto& v : mono) {
                    int s2 = v * 20000 / peak;
                    if (s2 > 32767) s2 = 32767;
                    if (s2 < -32768) s2 = -32768;
                    v = static_cast<std::int16_t>(s2);
                }
            }
            sfx_[s.id] = std::move(mono);
        }
    }
    SDL_PauseAudioDevice(device_, 0);
}

void SdlAudio::reopen_device() {
    // Called from the SDL event watch on SDL_AUDIODEVICEREMOVED (main
    // thread).  Reopen the DEFAULT output with the original spec — flags 0
    // means SDL converts, so `have` matches `want` and every synth keeps
    // rendering at device_rate_ unchanged.
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    SDL_AudioSpec want{};
    want.freq = device_rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = device_samples_;
    want.callback = sdl_callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::fprintf(stderr,
                     "audio: output device lost and no replacement opened "
                     "(%s) — running silent\n",
                     SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(device_, 0);
    std::fprintf(stderr, "audio: output device changed — reopened default\n");
}

SdlAudio::~SdlAudio() {
    // Join the host-MIDI pump thread first (it silences the port on stop) so
    // it can't outlive the object; the member's own dtor would also do this.
    if (host_midi_active_) host_midi_.stop();
    if (event_watch_installed_) SDL_DelEventWatch(audio_device_watch, this);
    if (device_ != 0) {
        SDL_PauseAudioDevice(device_, 1);
        SDL_CloseAudioDevice(device_);
    }
    // OLDUVAI_AUDIO_STATS: real-time health summary (collected every run; only
    // printed on request).  overruns > 0 or a worst_lock_wait anywhere near
    // the budget = the RB1 dropout hazard is real on this host.
    if (std::getenv("OLDUVAI_AUDIO_STATS") != nullptr &&
        cb_count_.load(std::memory_order_relaxed) > 0) {
        const double budget_ms =
            1000.0 * device_samples_ / static_cast<double>(device_rate_);
        std::fprintf(
            stderr,
            "audio-stats: callbacks=%llu overruns=%llu worst_mix=%.3fms "
            "worst_lock_wait=%.3fms budget=%.3fms\n",
            static_cast<unsigned long long>(
                cb_count_.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                cb_overruns_.load(std::memory_order_relaxed)),
            cb_worst_ns_.load(std::memory_order_relaxed) / 1e6,
            cb_worst_wait_ns_.load(std::memory_order_relaxed) / 1e6,
            budget_ms);
    }
    if (mt32_ != nullptr && g_mt32.free_ctx != nullptr) {
        g_mt32.free_ctx(mt32_);
    }
    if (mt32_lib_ != nullptr) dyn_close(mt32_lib_);
    if (fs_synth_ != nullptr && g_fs.del_synth != nullptr) {
        g_fs.del_synth(fs_synth_);
    }
    if (fs_settings_ != nullptr && g_fs.del_settings != nullptr) {
        g_fs.del_settings(fs_settings_);
    }
    if (fs_lib_ != nullptr) dyn_close(fs_lib_);
}

void SdlAudio::load_sfx(const std::string& id,
                        const formats::VocAudio& voc) {
    // OPL backend: the AdLib FM voices are pre-rendered in the constructor;
    // don't let a VOC overwrite them.  Ids with no AdLib record (e.g. the
    // enhanced-only SFX_WAIT_AND_PLAY cue) still fall through to the VOC path.
    if (opl_sfx_ && opl_sfx_lookup(id) != nullptr) return;
    // mt32-sfx / gm-sfx: the catalog MIDI SFX are baked to PCM in the
    // constructor.  Don't let the VOC asset clobber that bake — only the
    // SB-DAC backend plays the VOC samples (mirrors Python keeping the MT-32
    // render under a separate key so the VOC is a fallback, not an override).
    if (midi_sfx_ && sfx_.find(id) != sfx_.end()) return;
    if (voc.sample_rate <= 0 || voc.data.empty()) return;
    // Band-limited (windowed-sinc) upsampling + edge declick.  The samples
    // are 4 kHz recordings whose content presses against their Nyquist, so
    // anything short of proper band-limiting leaves audible images; and every
    // one of them opens ~28% of full scale, popping on each trigger.  The
    // real SB's analog output stage smoothed both — owner playtest confirmed
    // the original was never perceived this harsh.  See resample.hpp.
    std::vector<std::int16_t> pcm =
        resample_sinc_u8(voc.data, voc.sample_rate, device_rate_);
    if (pcm.empty()) return;
    apply_edge_fade(pcm, device_rate_);
    // HD SFX bake variant: <cache>/hd_sfx/<fnv1a64-of-sample-bytes>.wav —
    // written offline by scripts/bake_hd_sfx.py from the --decode-sfx export
    // (same content-addressed pattern as the HD sprite bake; the user's own
    // files, the user's cache, nothing shipped).  Loaded alongside the native
    // sample; play_sfx picks per the enhanced flag at trigger time.
    std::vector<std::int16_t> hd;
    {
        const std::filesystem::path hd_path =
            hd_sfx_dir() / (sfx_digest_hex(voc.data) + ".wav");
        int rate = 0, channels = 0;
        std::vector<std::int16_t> wav = read_wav16(hd_path, rate, channels);
        if (!wav.empty() && channels >= 1) {
            if (channels == 2) {   // down-mix: keep L (bake writes mono)
                for (std::size_t i = 0; i < wav.size() / 2; ++i) {
                    wav[i] = wav[i * 2];
                }
                wav.resize(wav.size() / 2);
            }
            hd = resample_linear_s16(wav, rate, device_rate_);
        }
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (!hd.empty()) {
        std::fprintf(stderr, "sfx: HD bake variant loaded for %s\n",
                     id.c_str());
        sfx_hd_[id] = std::move(hd);
    }
    sfx_[id] = std::move(pcm);
}

void SdlAudio::play_sfx(const std::string& id) {
    if (device_ == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    // All SFX (OPL, SB-DAC VOC, and the MIDI backends baked at construction)
    // are pre-rendered PCM in sfx_ — play them as independent polyphonic waves,
    // never live through the music synth.
    // Enhanced mode prefers the HD bake variant when the offline bake
    // produced one; faithful mode always plays the native sample.
    const std::vector<std::int16_t>* buf = nullptr;
    if (sfx_enhanced_) {
        const auto hd = sfx_hd_.find(id);
        if (hd != sfx_hd_.end()) buf = &hd->second;
    }
    if (buf == nullptr) {
        const auto it = sfx_.find(id);
        if (it == sfx_.end()) return;
        buf = &it->second;
    }
    // Pre-rendered PCM wave → polyphonic voice pool.  At the cap, evict the
    // oldest (faithful mode caps at 1, i.e. replace-whatever-plays).
    while (static_cast<int>(sfx_voices_.size()) >= sfx_poly_ &&
           !sfx_voices_.empty()) {
        sfx_voices_.erase(sfx_voices_.begin());
    }
    sfx_voices_.push_back({buf, 0});
}

void SdlAudio::set_mix_balance(bool enhanced, float music, float sfx) {
    std::lock_guard<std::mutex> lock(mu_);
    sfx_poly_ = enhanced ? 8 : 1;
    music_balance_ = enhanced ? 0.6f : 1.0f;   // duck music under SFX
    sfx_enhanced_ = enhanced;
    sfx_balance_ = 1.0f;
    if (music >= 0.0f) music_balance_ = music;  // explicit knob overrides
    if (sfx >= 0.0f) sfx_balance_ = sfx;
}

void SdlAudio::play_music(const std::vector<std::uint8_t>& raw_mdi,
                          int track_id) {
    music_gain_.store(1.0f, std::memory_order_relaxed);   // new track at full
    if (host_midi_active_) {
        // Host MIDI streams on its own wall-clock thread, outside the SDL
        // mixer/mu_ — no audio-callback state to guard here.
        host_midi_.play(formats::build_gm_midi(raw_mdi, track_id,
                                               drop_runtime_modulation(),
                                               wants_gm_translation()));
        return;
    }
    // mt32_/fs_synth_ are created in the ctor and only torn down with the
    // object (main thread owns both ends) — safe to branch on unlocked.
    if (mt32_ != nullptr || fs_synth_ != nullptr) {
        // Convert + parse the new track BEFORE taking the callback's lock: the
        // GM conversion allocates and the sequencer parse walks every event —
        // holding mu_ through that blocks the audio callback for the duration
        // (a dropout window on slow hosts, worst exactly at level
        // transitions).  Under the lock we only key off + move-swap.
        MidiSequencer next;
        next.load(formats::build_gm_midi(raw_mdi, track_id,
                                         drop_runtime_modulation(),
                                         wants_gm_translation()));
        std::lock_guard<std::mutex> lock(mu_);
        // Key off anything still sounding from the PREVIOUS track before the
        // sequencer swap discards its event stream.  fade_out_music() only
        // ramps the MIX gain (voices stay keyed, muted); without this, a
        // sustained note straddling the swap loses its note-off forever and
        // hangs audibly once the gain returns to 1.0 (the occasional "stall
        // note" heard at level transitions on GM).  Same CC-123 treatment as
        // stop_music and the sequencer's own loop seam; the EXE equivalent is
        // the MIDI driver re-init silencing at track start after MDI_FadeStop.
        if (fs_synth_ != nullptr && g_fs.cc != nullptr) {
            for (int chn = 0; chn < 16; ++chn) {
                g_fs.cc(fs_synth_, chn, 123, 0);   // all notes off
            }
        } else if (mt32_ != nullptr) {
            for (int chn = 0; chn < 16; ++chn) {
                g_mt32.play_msg(mt32_, static_cast<std::uint32_t>(
                                           0xB0 | chn) | (123 << 8));
            }
        }
        seq_ = std::move(next);
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (opl_music_ == nullptr) return;
    // The OPL driver takes the RAW container — the FF 7F voice patches the
    // GM conversion strips are exactly what it plays.
    opl_music_->set_loop(true);
    opl_music_->open(raw_mdi);
}

void SdlAudio::fade_out_music() {
    // EXE MDI_FadeStop (1f75:00e4): ramp the MIDI master volume (DS:0x88d6)
    // down by 4 every delay(4)→2-BIOS-tick (~111ms) step until silent, then
    // the caller starts the next track at full.  We ramp the music mix gain
    // the same way: 16 steps (≈ master-vol/4) × ~111ms ≈ 1.8s.  No lock held
    // while sleeping — the callback reads music_gain_ lock-free.
    if (!music_available()) return;
    // Host MIDI mixes on the external device, not our music_gain_ ramp.  A CC7
    // (channel-volume) ramp would be the faithful equivalent, but the host
    // sink only does the EXE 'R' note stream; stopping the track (all-notes-
    // off) before the next play_music() is the cleanest hardware-safe analogue.
    if (host_midi_active_) {
        host_midi_.stop();
        return;
    }
    constexpr int kSteps = 16;
    const Uint32 step_ms = 1000u * 2u / 18u;   // 2 BIOS ticks ≈ 111ms
    for (int s = kSteps - 1; s >= 0; --s) {
        music_gain_.store(static_cast<float>(s) / kSteps,
                          std::memory_order_relaxed);
        SDL_Delay(step_ms);
    }
    music_gain_.store(0.0f, std::memory_order_relaxed);
}

void SdlAudio::stop_music() {
    if (host_midi_active_) {
        host_midi_.stop();
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (fs_synth_ != nullptr) {
        if (g_fs.cc != nullptr) {
            for (int chn = 0; chn < 16; ++chn) {
                g_fs.cc(fs_synth_, chn, 123, 0);   // all notes off
            }
        }
        seq_ = MidiSequencer();
        return;
    }
    if (mt32_ != nullptr) {
        // Release everything: all-notes-off on every channel.
        for (int chn = 0; chn < 16; ++chn) {
            g_mt32.play_msg(mt32_, static_cast<std::uint32_t>(
                                       0xB0 | chn) | (123 << 8));
        }
        seq_ = MidiSequencer();
        return;
    }
    if (opl_music_ == nullptr) return;
    opl_music_->stop();
}

void SdlAudio::mix(std::int16_t* out, int frames) {
    // Real-time health counters (see audio.hpp): time the lock acquisition
    // (main-thread contention = the RB1 hazard) and the whole callback
    // against its buffer budget.  Single audio thread → relaxed stores.
    const std::uint64_t t0 = SDL_GetPerformanceCounter();
    std::unique_lock<std::mutex> lock(mu_, std::try_to_lock);
    if (!lock.owns_lock()) lock.lock();
    const std::uint64_t t_locked = SDL_GetPerformanceCounter();
    // Release MIDI effect notes that came due BEFORE this window (the
    // clock advances after the render, so a fresh note always sounds
    // for at least the current buffer).
    for (auto it = note_offs_.begin(); it != note_offs_.end();) {
        if (it->at <= sample_clock_) {
            if (mt32_ != nullptr) {
                g_mt32.play_msg(mt32_,
                                static_cast<std::uint32_t>(0x80 | it->channel) |
                                    (static_cast<std::uint32_t>(it->note)
                                     << 8));
            } else if (fs_synth_ != nullptr && g_fs.noteoff != nullptr) {
                g_fs.noteoff(fs_synth_, it->channel, it->note);
            }
            it = note_offs_.erase(it);
        } else {
            ++it;
        }
    }
    sample_clock_ += frames;
    // Synth base layer (music + MIDI effects) or silence.  The synth
    // renders whenever it exists — effects must sound with no music.
    if (fs_synth_ != nullptr) {
        if (seq_.loaded()) seq_.advance(frames, device_rate_,
                     [&](std::uint8_t st, std::uint8_t d1, std::uint8_t d2) {
                         const int chn = st & 0x0F;
                         switch (st & 0xF0) {
                             case 0x90:
                                 if (g_fs.noteon != nullptr) {
                                     g_fs.noteon(fs_synth_, chn, d1, d2);
                                 }
                                 break;
                             case 0x80:
                                 if (g_fs.noteoff != nullptr) {
                                     g_fs.noteoff(fs_synth_, chn, d1);
                                 }
                                 break;
                             case 0xC0:
                                 if (g_fs.program != nullptr) {
                                     g_fs.program(fs_synth_, chn, d1);
                                 }
                                 break;
                             case 0xB0:
                                 if (g_fs.cc != nullptr) {
                                     g_fs.cc(fs_synth_, chn, d1, d2);
                                 }
                                 break;
                             case 0xE0:
                                 if (g_fs.bend != nullptr) {
                                     g_fs.bend(fs_synth_, chn,
                                               (d2 << 7) | d1);
                                 }
                                 break;
                             default: break;
                         }
                     });
        g_fs.write_s16(fs_synth_, frames, out, 0, 2, out, 1, 2);
    } else if (mt32_ != nullptr) {
        if (seq_.loaded()) seq_.advance(frames, device_rate_,
                     [&](std::uint8_t st, std::uint8_t d1, std::uint8_t d2) {
                         g_mt32.play_msg(
                             mt32_, static_cast<std::uint32_t>(st) |
                                        (static_cast<std::uint32_t>(d1) << 8) |
                                        (static_cast<std::uint32_t>(d2) << 16));
                     });
        g_mt32.render(mt32_, out, static_cast<std::uint32_t>(frames));
    } else if (opl_music_ != nullptr) {
        opl_music_->render(frames, out);   // zero-fills past a stopped track
    } else {
        for (int i = 0; i < frames * 2; ++i) out[i] = 0;
    }
    // Music level = fade-out ramp x enhanced balance duck.  Applied to the
    // synth layer only (music + any midi-SFX), BEFORE the PCM SFX voices so
    // effects sit above the soundtrack.
    const float mg =
        music_gain_.load(std::memory_order_relaxed) * music_balance_;
    if (mg < 0.999f) {
        for (int i = 0; i < frames * 2; ++i) {
            out[i] = static_cast<std::int16_t>(out[i] * mg);
        }
    }
    // Polyphonic PCM SFX voices over both channels — pre-rendered waves, never
    // routed through the music synth.  Each advances independently; finished
    // voices are pruned.
    for (auto it = sfx_voices_.begin(); it != sfx_voices_.end();) {
        const auto& buf = *it->buf;
        int i = 0;
        for (; i < frames && it->pos < buf.size(); ++i, ++it->pos) {
            const int s = static_cast<int>(buf[it->pos] * sfx_balance_);
            for (int chn = 0; chn < 2; ++chn) {
                int v = out[i * 2 + chn] + s;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                out[i * 2 + chn] = static_cast<std::int16_t>(v);
            }
        }
        if (it->pos >= buf.size()) it = sfx_voices_.erase(it);
        else ++it;
    }
    // Close the health counters: worst lock-wait, worst callback time, and
    // budget overruns (callback longer than frames/rate = audible dropout).
    const std::uint64_t t_end = SDL_GetPerformanceCounter();
    const std::uint64_t pf = SDL_GetPerformanceFrequency();
    const std::uint64_t wait_ns = (t_locked - t0) * 1000000000ull / pf;
    const std::uint64_t total_ns = (t_end - t0) * 1000000000ull / pf;
    const std::uint64_t budget_ns =
        static_cast<std::uint64_t>(frames) * 1000000000ull /
        static_cast<std::uint64_t>(device_rate_);
    cb_count_.fetch_add(1, std::memory_order_relaxed);
    if (total_ns > budget_ns)
        cb_overruns_.fetch_add(1, std::memory_order_relaxed);
    if (total_ns > cb_worst_ns_.load(std::memory_order_relaxed))
        cb_worst_ns_.store(total_ns, std::memory_order_relaxed);
    if (wait_ns > cb_worst_wait_ns_.load(std::memory_order_relaxed))
        cb_worst_wait_ns_.store(wait_ns, std::memory_order_relaxed);
}

namespace {
int audio_device_watch(void* ud, SDL_Event* ev) {
    if (ev->type == SDL_AUDIODEVICEREMOVED && ev->adevice.iscapture == 0)
        static_cast<SdlAudio*>(ud)->reopen_device();
    return 0;
}
}  // namespace

}  // namespace olduvai::presentation
