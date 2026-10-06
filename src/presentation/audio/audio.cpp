// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/audio/audio.hpp"

#include "presentation/audio/opl_sfx.hpp"
#include "presentation/audio/resample.hpp"
#include "presentation/audio/rom_dirs.hpp"
#include "presentation/audio/soundfont_pick.hpp"
#include "presentation/audio/wav_io.hpp"   // write_wav16 (OLDUVAI_AUDIO_CAPTURE)

#include <SDL.h>

#include "presentation/audio/dynlib.hpp"

#ifdef OLDUVAI_VENDORED_MT32EMU
// Vendored Munt libmt32emu — see third_party/mt32emu/OLDUVAI-VENDORING.md.
// Only the C interface is used; the C++ API would couple us to munt's types.
#include <mt32emu/c_interface/c_interface.h>
#endif

#ifdef OLDUVAI_VENDORED_FLUIDSYNTH
// Vendored FluidSynth — see third_party/fluidsynth/OLDUVAI-VENDORING.md.
#include <fluidsynth.h>
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>

#include "formats/mdi.hpp"

namespace olduvai::presentation {

namespace {

// Device-removed watch.  SDL_DelEventWatch matches function pointer + userdata,
// so add and remove must name this one function (two identical lambdas would
// not match and would leak the watch).
int audio_device_watch(void* ud, SDL_Event* ev);

#if !defined(OLDUVAI_VENDORED_MT32EMU) || !defined(OLDUVAI_VENDORED_FLUIDSYNTH)
// A synth loaded at run time (built without its vendored copy).
void* dlopen_first(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (void* h = dyn_open(name)) return h;
    }
    return nullptr;
}
#endif

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

// Silent libmt32emu report handler.  NULL installs the default, which prints
// "Rhythm: Attempted to play unmapped key N" for every drum note the ROM does
// not map (L4 boss: ch9 note 85); a real MT-32 ignores those silently.  Layout
// = c_interface.h mt32emu_report_handler_i_v0 (15 functions, version 0).
// mt32emu_report_handler_i is { const v0* v0; } passed by value, so the v0
// struct's address is its by-value representation.
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

// macOS: the @executable_path candidates come first.  A leaf-name dlopen never
// searches beside the executable, so a dylib bundled in the .app is found only
// by this form.  ../libs is where make_dmg_macos.sh puts SDL2.  The bundled
// copy wins over Homebrew's.
#ifdef OLDUVAI_VENDORED_MT32EMU
// Adapters from our void*-based Mt32Api to munt's C signatures.  Direct casts
// trip -Wcast-function-type-mismatch (a CI -Werror failure), so convert
// explicitly.  The report handler is a one-pointer struct passed by value.
static_assert(sizeof(Mt32ReportHandlerV0) ==
                  sizeof(mt32emu_report_handler_i_v0),
              "our report-handler mirror has drifted from munt's v0 struct — "
              "re-check it against third_party/mt32emu after a re-sync");

void* mt32_shim_create(void* handler_v0, void* instance_data) {
    mt32emu_report_handler_i rh;
    rh.v0 = static_cast<const mt32emu_report_handler_i_v0*>(handler_v0);
    return mt32emu_create_context(rh, instance_data);
}
int mt32_shim_add_rom(void* ctx, const char* path) {
    return mt32emu_add_rom_file(static_cast<mt32emu_context>(ctx), path);
}
int mt32_shim_open(void* ctx) {
    return static_cast<int>(mt32emu_open_synth(static_cast<mt32emu_context>(ctx)));
}
void mt32_shim_set_rate(void* ctx, double rate) {
    mt32emu_set_stereo_output_samplerate(static_cast<mt32emu_context>(ctx),
                                         rate);
}
void mt32_shim_play_msg(void* ctx, std::uint32_t msg) {
    mt32emu_play_msg(static_cast<mt32emu_context>(ctx), msg);
}
void mt32_shim_render(void* ctx, std::int16_t* out, std::uint32_t frames) {
    mt32emu_render_bit16s(static_cast<mt32emu_context>(ctx), out, frames);
}
void mt32_shim_free(void* ctx) {
    mt32emu_free_context(static_cast<mt32emu_context>(ctx));
}
#else
void* load_mt32emu() {
#ifdef __APPLE__
    void* h = dlopen_first({"@executable_path/../libs/libmt32emu.dylib",
                            "@executable_path/libmt32emu.dylib",
                            "libmt32emu.dylib", "libmt32emu.2.dylib",
                            "/opt/homebrew/lib/libmt32emu.dylib",
                            "/usr/local/lib/libmt32emu.dylib"});
#elif defined(_WIN32)
    void* h = dlopen_first({"libmt32emu.dll", "mt32emu.dll",
                            "libmt32emu-2.dll", "mt32emu-2.dll"});
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
#endif  // OLDUVAI_VENDORED_MT32EMU

// Bind the seven C-API entry points MT-32 playback needs; false if one is
// missing.  Vendored build (default): compiled in, no probe.  The dlopen path
// is for -DOLDUVAI_WITH_MT32EMU=OFF (system libmt32emu, $OLDUVAI_MT32EMU).
// Both reinterpret_cast create(): its by-value one-pointer handler struct
// matches our void* in every calling convention we target.
bool bind_mt32_api(Mt32Api& api, void*& lib) {
#ifdef OLDUVAI_VENDORED_MT32EMU
    lib = nullptr;
    api.create = &mt32_shim_create;
    api.add_rom = &mt32_shim_add_rom;
    api.open = &mt32_shim_open;
    api.set_rate = &mt32_shim_set_rate;
    api.play_msg = &mt32_shim_play_msg;
    api.render = &mt32_shim_render;
    api.free_ctx = &mt32_shim_free;
    return true;
#else
    lib = load_mt32emu();
    if (lib == nullptr) return false;
    api.create = reinterpret_cast<Mt32CreateFn>(
        dyn_sym(lib, "mt32emu_create_context"));
    api.add_rom = reinterpret_cast<Mt32AddRomFn>(
        dyn_sym(lib, "mt32emu_add_rom_file"));
    api.open = reinterpret_cast<Mt32OpenFn>(
        dyn_sym(lib, "mt32emu_open_synth"));
    api.set_rate = reinterpret_cast<Mt32RateFn>(
        dyn_sym(lib, "mt32emu_set_stereo_output_samplerate"));
    api.play_msg = reinterpret_cast<Mt32PlayMsgFn>(
        dyn_sym(lib, "mt32emu_play_msg"));
    api.render = reinterpret_cast<Mt32RenderFn>(
        dyn_sym(lib, "mt32emu_render_bit16s"));
    api.free_ctx = reinterpret_cast<Mt32FreeFn>(
        dyn_sym(lib, "mt32emu_free_context"));
    return api.create != nullptr && api.add_rom != nullptr &&
           api.open != nullptr && api.play_msg != nullptr &&
           api.render != nullptr;
#endif
}

// ROM search dirs: rom_dirs.hpp (pinned by tests/test_rom_dirs.cpp); user docs:
// docs/AUDIO.md.  The MT-32 or CM-32L control + PCM pair is matched against
// the files on disk case-insensitively, each file independently (real sets mix
// MT32_CONTROL.ROM with mt32_pcm.rom; Linux filesystems are case-sensitive).
// Model: CM-32L (a superset: 33 more PCM samples) by default; selectable,
// because the two sound different.
enum class Mt32Model { kAuto, kCm32l, kMt32 };

Mt32Model parse_mt32_model(const std::string& s) {
    if (s == "cm32l") return Mt32Model::kCm32l;
    if (s == "mt32") return Mt32Model::kMt32;
    return Mt32Model::kAuto;
}

// Set to the pair actually loaded, so the caller can say which it was.
std::string g_mt32_loaded;
// Did the FluidSynth library load at all?  Separates a missing LIBRARY from a
// missing SOUNDFONT in the diagnostics below.
bool g_fluid_lib_found = false;

// The regular files in `dir`, and a case-insensitive lookup over them —
// ROM names turn up in every case, depending on who copied them.
std::vector<std::string> dir_files(const std::string& dir) {
    std::error_code ec;
    std::vector<std::string> entries;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec)) {
            entries.push_back(e.path().filename().string());
        }
    }
    return entries;
}

std::string find_ci(const std::vector<std::string>& entries, const char* want) {
    const std::string w(want);
    for (const auto& e : entries) {
        if (e.size() != w.size()) continue;
        if (std::equal(e.begin(), e.end(), w.begin(),
                       [](unsigned char a, unsigned char b) {
                           return std::tolower(a) == std::tolower(b);
                       })) {
            return e;
        }
    }
    return std::string();
}

// Whether `dir` holds a CM-32L or MT-32 ROM pair (the identities
// add_rom_pair loads), without reading them.
bool rom_pair_present(const std::string& dir) {
    const std::vector<std::string> e = dir_files(dir);
    const auto pair = [&e](const char* ctl, const char* pcm) {
        return !find_ci(e, ctl).empty() && !find_ci(e, pcm).empty();
    };
    return pair("CM32L_CONTROL.ROM", "CM32L_PCM.ROM") ||
           pair("MT32_CONTROL.ROM", "MT32_PCM.ROM");
}

bool add_rom_pair(const Mt32Api& api, void* ctx, const std::string& dir,
                  Mt32Model model = Mt32Model::kAuto) {
    const std::vector<std::string> entries = dir_files(dir);
    if (entries.empty()) return false;

    // Only the two IDENTITIES matter; case is not part of the
    // key, so the four-spelling ladder collapses.
    const auto try_pair = [&](const char* ctl, const char* pcm,
                              const char* label) {
        const std::string a = find_ci(entries, ctl);
        const std::string b = find_ci(entries, pcm);
        if (a.empty() || b.empty()) return false;
        if (api.add_rom(ctx, (dir + "/" + a).c_str()) >= 0 &&
            api.add_rom(ctx, (dir + "/" + b).c_str()) >= 0) {
            g_mt32_loaded = std::string(label) + " (" + a + " + " + b + ")";
            return true;
        }
        return false;
    };
    // Must short-circuit: try_pair() loads into the same context, so trying
    // both would mix a CM-32L control ROM with an MT-32 PCM ROM.
    switch (model) {
        case Mt32Model::kCm32l:
            return try_pair("CM32L_CONTROL.ROM", "CM32L_PCM.ROM", "CM-32L");
        case Mt32Model::kMt32:
            return try_pair("MT32_CONTROL.ROM", "MT32_PCM.ROM", "MT-32");
        case Mt32Model::kAuto:
            return try_pair("CM32L_CONTROL.ROM", "CM32L_PCM.ROM", "CM-32L") ||
                   try_pair("MT32_CONTROL.ROM", "MT32_PCM.ROM", "MT-32");
    }
    return false;
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

#ifdef OLDUVAI_VENDORED_FLUIDSYNTH
// Compiled in (third_party/fluidsynth): typed shims over the C API, so the
// FsApi table the dlopen path fills binds the same way.
void* fs_new_settings() { return new_fluid_settings(); }
int fs_setnum(void* s, const char* name, double v) {
    return fluid_settings_setnum(static_cast<fluid_settings_t*>(s), name, v);
}
void* fs_new_synth(void* s) {
    return new_fluid_synth(static_cast<fluid_settings_t*>(s));
}
fluid_synth_t* fs(void* synth) { return static_cast<fluid_synth_t*>(synth); }
int fs_sfload(void* sy, const char* f, int reset) {
    return fluid_synth_sfload(fs(sy), f, reset);
}
int fs_noteon(void* sy, int c, int k, int v) {
    return fluid_synth_noteon(fs(sy), c, k, v);
}
int fs_noteoff(void* sy, int c, int k) { return fluid_synth_noteoff(fs(sy), c, k); }
int fs_program(void* sy, int c, int p) {
    return fluid_synth_program_change(fs(sy), c, p);
}
int fs_cc(void* sy, int c, int n, int v) { return fluid_synth_cc(fs(sy), c, n, v); }
int fs_bend(void* sy, int c, int v) { return fluid_synth_pitch_bend(fs(sy), c, v); }
int fs_write_s16(void* sy, int len, void* l, int lo, int li, void* r, int ro,
                 int ri) {
    return fluid_synth_write_s16(fs(sy), len, l, lo, li, r, ro, ri);
}
void fs_del_synth(void* sy) { delete_fluid_synth(fs(sy)); }
void fs_del_settings(void* s) {
    delete_fluid_settings(static_cast<fluid_settings_t*>(s));
}
#else
// Bundle-relative first — see the note above load_mt32emu().
void* load_fluidsynth() {
#ifdef __APPLE__
    void* h = dlopen_first({"@executable_path/../libs/libfluidsynth.dylib",
                            "@executable_path/libfluidsynth.dylib",
                            "libfluidsynth.dylib", "libfluidsynth.3.dylib",
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
        h = dyn_open(env);
        if (h != nullptr) return h;
    }
    return nullptr;
}
#endif  // OLDUVAI_VENDORED_FLUIDSYNTH

// Bind the FluidSynth entry points; false when the library is missing or
// lacks one the synth cannot run without.  Vendored build (default):
// compiled in, no probe.  The dlopen path is for
// -DOLDUVAI_WITH_FLUIDSYNTH=OFF (system libfluidsynth, $OLDUVAI_FLUIDSYNTH).
bool bind_fluid_api(FsApi& api, void*& lib) {
#ifdef OLDUVAI_VENDORED_FLUIDSYNTH
    lib = nullptr;
    api.new_settings = &fs_new_settings;
    api.setnum = &fs_setnum;
    api.new_synth = &fs_new_synth;
    api.sfload = &fs_sfload;
    api.noteon = &fs_noteon;
    api.noteoff = &fs_noteoff;
    api.program = &fs_program;
    api.cc = &fs_cc;
    api.bend = &fs_bend;
    api.write_s16 = &fs_write_s16;
    api.del_synth = &fs_del_synth;
    api.del_settings = &fs_del_settings;
    return true;
#else
    lib = load_fluidsynth();
    if (lib == nullptr) return false;
    api.new_settings = reinterpret_cast<FsNewSettingsFn>(
        dyn_sym(lib, "new_fluid_settings"));
    api.setnum = reinterpret_cast<FsSetNumFn>(
        dyn_sym(lib, "fluid_settings_setnum"));
    api.new_synth = reinterpret_cast<FsNewSynthFn>(
        dyn_sym(lib, "new_fluid_synth"));
    api.sfload = reinterpret_cast<FsSfLoadFn>(
        dyn_sym(lib, "fluid_synth_sfload"));
    api.noteon = reinterpret_cast<FsNoteOnFn>(
        dyn_sym(lib, "fluid_synth_noteon"));
    api.noteoff = reinterpret_cast<FsNoteOffFn>(
        dyn_sym(lib, "fluid_synth_noteoff"));
    api.program = reinterpret_cast<FsProgFn>(
        dyn_sym(lib, "fluid_synth_program_change"));
    api.cc = reinterpret_cast<FsCcFn>(dyn_sym(lib, "fluid_synth_cc"));
    api.bend = reinterpret_cast<FsBendFn>(
        dyn_sym(lib, "fluid_synth_pitch_bend"));
    api.write_s16 = reinterpret_cast<FsWriteFn>(
        dyn_sym(lib, "fluid_synth_write_s16"));
    api.del_synth = reinterpret_cast<FsDelSynthFn>(
        dyn_sym(lib, "delete_fluid_synth"));
    api.del_settings = reinterpret_cast<FsDelSettingsFn>(
        dyn_sym(lib, "delete_fluid_settings"));
    if (api.new_settings == nullptr || api.new_synth == nullptr ||
        api.sfload == nullptr || api.write_s16 == nullptr) {
        dyn_close(lib);
        lib = nullptr;
        return false;
    }
    return true;
#endif
}

// SoundFont discovery: explicit path -> $OLDUVAI_SOUNDFONT ->
// ~/.config/olduvai/soundfonts -> system dirs.  Precedence:
// soundfont_pick.hpp (select_soundfont); this supplies dirs, names and an
// existence check.
std::string find_soundfont(const std::string& override_path) {
    auto exists = [](const std::string& p) {
        const std::ifstream f(p);
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
    // The platform list is in soundfont_pick.hpp so a test pins it.
    const std::vector<std::string> system_dirs = default_soundfont_dirs();
    // Roland SC-55 first (Sound Canvas lineage, the most faithful GM voice;
    // Debian's scummvm-data ships it), then free sets.
    const std::vector<std::string> names = {
        "Roland_SC-55.sf2", "GeneralUser-GS.sf2", "GeneralUser GS.sf2",
        "FluidR3_GM.sf2", "default-GM.sf2"};
    return select_soundfont(config_dir, system_dirs, names, exists);
}

// ---- Melodic synth backends (PcmMidiSynth) -------------------------------
// Each owns its library handle and objects; create() returns null on any
// failure.  send()/render() are the live entry points (audio.hpp).

// libmt32emu (Roland MT-32 / CM-32L): send() forwards a raw packed MIDI message
// (libmt32emu parses the bytes itself); render() pulls stereo s16.
class Mt32Synth final : public PcmMidiSynth {
public:
    static std::unique_ptr<Mt32Synth> create(const std::string& rom_dir,
                                              int rate,
                                              const std::string& model_pref) {
        void* lib = nullptr;
        Mt32Api api;
        if (bind_mt32_api(api, lib)) {
            void* ctx = api.create(
                const_cast<Mt32ReportHandlerV0*>(&kSilentReportHandler),
                nullptr);
            const Mt32Model model = parse_mt32_model(model_pref);
            bool roms_ok = false;
            g_mt32_loaded.clear();
            for (const auto& dir : rom_search_dirs(rom_dir)) {
                if (add_rom_pair(api, ctx, dir, model)) {
                    roms_ok = true;
                    // Name the device (CM-32L vs MT-32), not just
                    // "mt32-builtin": they sound different.
                    std::fprintf(stderr, "mt32: %s from %s\n",
                                 g_mt32_loaded.c_str(), dir.c_str());
                    break;
                }
            }
            if (roms_ok) {
                if (api.set_rate != nullptr) api.set_rate(ctx, rate);
                if (api.open(ctx) == 0) {
                    return std::unique_ptr<Mt32Synth>(
                        new Mt32Synth(lib, ctx, api));
                }
            }
            if (api.free_ctx != nullptr) api.free_ctx(ctx);
        }
        dyn_close(lib);
        return nullptr;
    }
    ~Mt32Synth() override {
        if (ctx_ != nullptr && api_.free_ctx != nullptr) api_.free_ctx(ctx_);
        if (lib_ != nullptr) dyn_close(lib_);
    }
    // Owns the emulator context and the library handle: one owner.
    Mt32Synth(const Mt32Synth&) = delete;
    Mt32Synth& operator=(const Mt32Synth&) = delete;
    void send(std::uint8_t st, std::uint8_t d1, std::uint8_t d2) override {
        api_.play_msg(ctx_, static_cast<std::uint32_t>(st) |
                                (static_cast<std::uint32_t>(d1) << 8) |
                                (static_cast<std::uint32_t>(d2) << 16));
    }
    void render(int frames, std::int16_t* out) override {
        api_.render(ctx_, out, static_cast<std::uint32_t>(frames));
    }

private:
    Mt32Synth(void* lib, void* ctx, const Mt32Api& api)
        : lib_(lib), ctx_(ctx), api_(api) {}
    void* lib_ = nullptr;
    void* ctx_ = nullptr;
    Mt32Api api_;
};

// libfluidsynth (General MIDI + a SoundFont): send() demuxes a channel-voice
// message into the typed fluid_synth_* calls (each guarded — an older
// libfluidsynth may not export every one); render() pulls stereo s16.
class FluidSynth final : public PcmMidiSynth {
public:
    static std::unique_ptr<FluidSynth> create(const std::string& soundfont,
                                              int rate) {
        void* lib = nullptr;
        FsApi api;
        // Report "no FluidSynth" separately from "no SoundFont".
        g_fluid_lib_found = bind_fluid_api(api, lib);
        if (!g_fluid_lib_found) return nullptr;
        void* settings = api.new_settings();
        if (api.setnum != nullptr) {
            api.setnum(settings, "synth.sample-rate", rate);
        }
        void* synth = api.new_synth(settings);
        if (synth != nullptr && api.sfload(synth, soundfont.c_str(), 1) >= 0) {
            return std::unique_ptr<FluidSynth>(
                new FluidSynth(lib, settings, synth, api));
        }
        // Failure: tear down whatever was built while the lib is still open
        // (calling into an already-dlclose'd lib would be UB).
        if (synth != nullptr && api.del_synth != nullptr) api.del_synth(synth);
        if (settings != nullptr && api.del_settings != nullptr) {
            api.del_settings(settings);
        }
        // NOT dyn_close: see the destructor.  new_synth ran, so the OpenMP
        // pool may exist even though initialisation went on to fail.
        return nullptr;
    }
    ~FluidSynth() override {
        if (synth_ != nullptr && api_.del_synth != nullptr) {
            api_.del_synth(synth_);
        }
        if (settings_ != nullptr && api_.del_settings != nullptr) {
            api_.del_settings(settings_);
        }
        // No dyn_close(lib_), on purpose: libfluidsynth's OpenMP (libgomp)
        // worker pool outlives delete_fluid_synth, and unmapping the library
        // under those threads crashes them (7 of 8 renders; 0 of 8 with
        // OMP_NUM_THREADS=1).  dlopen refcounts, so re-selecting reuses the
        // handle.  libmt32emu has no such pool and is closed normally.
    }
    // Owns the synth and its settings: one owner.
    FluidSynth(const FluidSynth&) = delete;
    FluidSynth& operator=(const FluidSynth&) = delete;
    void send(std::uint8_t st, std::uint8_t d1, std::uint8_t d2) override {
        const int chn = st & 0x0F;
        switch (st & 0xF0) {
            case 0x90:
                if (api_.noteon != nullptr) api_.noteon(synth_, chn, d1, d2);
                break;
            case 0x80:
                if (api_.noteoff != nullptr) api_.noteoff(synth_, chn, d1);
                break;
            case 0xC0:
                if (api_.program != nullptr) api_.program(synth_, chn, d1);
                break;
            case 0xB0:
                if (api_.cc != nullptr) api_.cc(synth_, chn, d1, d2);
                break;
            case 0xE0:
                if (api_.bend != nullptr) {
                    api_.bend(synth_, chn, (d2 << 7) | d1);
                }
                break;
            default:
                break;
        }
    }
    void render(int frames, std::int16_t* out) override {
        api_.write_s16(synth_, frames, out, 0, 2, out, 1, 2);
    }

private:
    FluidSynth(void* lib, void* settings, void* synth, const FsApi& api)
        : lib_(lib), settings_(settings), synth_(synth), api_(api) {}
    // Never read (see the destructor); [[maybe_unused]] for
    // -Wunused-private-field.
    [[maybe_unused]] void* lib_ = nullptr;
    void* settings_ = nullptr;
    void* synth_ = nullptr;
    FsApi api_;
};

void sdl_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<SdlAudio*>(userdata);
    self->mix(reinterpret_cast<std::int16_t*>(stream), len / 4);
}

// Catalog SFX note events (channel, note, velocity, program, ms) for the MIDI
// SFX backends, pre-rendered in the constructor.  note2: optional chord note
// (-1 none).  SFX_GENERIC is the EXE's BellSinger 79 + 77 cluster, velocity
// 127.
struct MidiSfx { const char* id; int ch, note, vel, prog, ms, note2; };
constexpr MidiSfx kMidiSfx[] = {
    {"SFX_HIT", 9, 43, 127, -1, 150, -1},
    {"SFX_JUMP_APEX", 9, 73, 127, -1, 250, -1},
    {"SFX_GENERIC", 7, 79, 127, 46, 200, 77},
    {"SFX_WAIT_AND_PLAY", 9, 49, 100, -1, 400, -1},
};
}  // namespace

SdlAudio::SdlAudio(const AudioSetup& setup) {
    // SFX flags resolve after the music backend loads, so "auto" can pair with
    // it.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "audio: SDL audio init failed (%s) — running silent\n",
                     SDL_GetError());
        return;
    }
    // --audio-rate / --audio-buffer, within sane bounds; 0 keeps 48000 Hz /
    // 2048 frames.  The buffer is rounded down to a power of two.
    if (setup.rate >= 8000 && setup.rate <= 192000) device_rate_ = setup.rate;
    Uint16 want_samples = 2048;
    if (setup.buffer >= 64 && setup.buffer <= 16384) {
        Uint16 p = 64;
        while (static_cast<int>(p) * 2 <= setup.buffer) p = p * 2;
        want_samples = p;
    }
    if (const char* cap = std::getenv("OLDUVAI_AUDIO_CAPTURE"))
        capture_path_ = cap;   // before the device starts calling mix()
    SDL_AudioSpec want{};
    want.freq = device_rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = want_samples;
    want.callback = sdl_callback;
    want.userdata = this;
    SDL_AudioSpec have{};
    device_samples_ = want_samples;
    // Offline (render harness): no device, no unplug watch; render_offline()
    // drives mix().
    if (!setup.offline) {
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (device_ == 0) {
            std::fprintf(stderr,
                         "audio: could not open an audio device (%s) — running silent\n",
                         SDL_GetError());
            return;
        }
        device_rate_ = have.freq;
        // Unplug recovery: the event watch sees SDL_AUDIODEVICEREMOVED whatever
        // loop is pumping, and reopens the default output.  Runs on the main
        // thread.
        SDL_AddEventWatch(audio_device_watch, this);
        event_watch_installed_ = true;
    }

    // Order matters: SFX resolution reads music_backend_.
    select_music_backend(setup.music_device, setup.rom_dir, setup.soundfont,
                         setup.midi_port, setup.mt32_model);
    resolve_and_bake_sfx(setup.sfx_backend);
    if (device_ != 0) SDL_PauseAudioDevice(device_, 0);   // offline: no device
}

// Route music to a host MIDI OUT port (RtMidi) as `backend`: no synth binds
// and the SDL device renders SFX only.  False if no port opens.
bool SdlAudio::try_host_midi(const char* backend, const std::string& port) {
    if (!host_midi_.open(port)) {
        std::fprintf(stderr,
                     "%s: no MIDI output available; falling back to the "
                     "auto synth chain.\n", backend);
        return false;
    }
    host_midi_active_ = true;
    music_backend_ = backend;
    std::fprintf(stderr, "%s: routing %smusic to port \"%s\"\n", backend,
                 std::string(backend) == "gm-host" ? "GM " : "",
                 host_midi_.port_name().c_str());
    return true;
}

// Windows auto fallback: no ROMs, no SoundFont, but the GS Wavetable Synth is
// always there, and translated GM beats OPL as a default.  A port that looks
// like an MT-32 / MUNT gets raw MT-32.  `--music-device opl` still selects
// AdLib.  A member: only the Windows body uses the members, so clang-tidy on
// any other platform would make it static.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void SdlAudio::try_windows_host_fallback(const std::string& port) {
#ifdef _WIN32
    if (!host_midi_.open(port)) return;
    host_midi_active_ = true;
    const std::string& pn = host_midi_.port_name();
    bool mt32_port = false;
    for (const char* tag : {"MT-32", "MT32", "MUNT", "CM-32", "CM32"})
        mt32_port = mt32_port || pn.find(tag) != std::string::npos;
    music_backend_ = mt32_port ? "host-midi" : "gm-host";
    std::fprintf(stderr,
                 "%s: no MT-32 ROMs or SoundFont found — routing %s "
                 "music to \"%s\" (use --music-device opl for AdLib)\n",
                 music_backend_.c_str(), mt32_port ? "MT-32" : "GM",
                 pn.c_str());
#else
    (void)port;
#endif
}

// AdLib: the EXE-faithful driver on the vendored Nuked-OPL3 core (the OPL
// SFX's core too, for the right balance).  Always available.
void SdlAudio::start_opl_music() {
    opl_music_ = std::make_unique<OplMusicPlayer>(device_rate_);
    music_backend_ = "opl";
}

// An explicit backend that failed (no ROMs, no SoundFont, a typo), named by
// its actual cause: a broken package must not look like an unconfigured one.
void SdlAudio::report_music_failure(const std::string& device,
                                    const std::string& rom_dir) {
    if (device == "gm-builtin") {
        if (!g_fluid_lib_found)
            std::fprintf(stderr,
                         "audio: gm-builtin unavailable — the FluidSynth "
                         "library could not be loaded.  This build ships "
                         "one; if you see this, the package is incomplete "
                         "(set $OLDUVAI_FLUIDSYNTH to override).\n");
        else
            std::fprintf(stderr,
                         "audio: gm-builtin unavailable — FluidSynth "
                         "loaded, but no SoundFont was found.  Pass "
                         "--soundfont <file.sf2> or install one.\n");
    } else if (device == "mt32-builtin") {
        // The library is vendored and compiled in: only the ROMs can be the
        // cause.
        std::fprintf(stderr,
                     "audio: mt32-builtin unavailable — no MT-32/CM-32L "
                     "ROM pair found.  Searched: ");
        for (const auto& d : rom_search_dirs(rom_dir))
            std::fprintf(stderr, "%s ", d.c_str());
        std::fprintf(stderr,
                     "\n  Need CM32L_CONTROL.ROM + CM32L_PCM.ROM, or "
                     "MT32_CONTROL.ROM + MT32_PCM.ROM (any case).\n");
    } else {
        std::fprintf(stderr,
                     "audio: music device '%s' could not start.  Check "
                     "the device name for typos.\n", device.c_str());
    }
}

// Choose and load the music synth, first hit wins: host MIDI (opt-in), MT-32
// builtin, GM builtin, the auto host fallback, OPL.
void SdlAudio::select_music_backend(const std::string& music_device,
                                    const std::string& rom_dir,
                                    const std::string& soundfont,
                                    const std::string& midi_port,
                                    const std::string& mt32_model) {
    // "host-midi" (and the old alias "mt32") send raw MT-32 to a MIDI OUT
    // port, for real MT-32 hardware or MUNT; "gm-host" translates MT-32 -> GM
    // programs, for GM synths behind the OS mapper.  If no port opens, the
    // auto synth chain follows.  The builtin emulator is "mt32-builtin".
    std::string device = music_device;
    if ((device == "host-midi" || device == "mt32") &&
        !try_host_midi("host-midi", midi_port))
        device = "auto";
    if (device == "gm-host" && !try_host_midi("gm-host", midi_port))
        device = "auto";
    const auto wants = [&device](const char* a, const char* b) {
        return device == "auto" || device == a || device == b;
    };
    if (!host_midi_active_ && wants("mt32-builtin", "mt32")) {
        synth_ = Mt32Synth::create(rom_dir, device_rate_, mt32_model);
        if (synth_ != nullptr) music_backend_ = "mt32-builtin";
    }
    if (!host_midi_active_ && synth_ == nullptr &&
        wants("gm-builtin", "gm")) {
        const std::string sf = find_soundfont(soundfont);
        if (!sf.empty()) {
            std::fprintf(stderr, "gm-builtin: soundfont = %s\n", sf.c_str());
            synth_ = FluidSynth::create(sf, device_rate_);
            if (synth_ != nullptr) music_backend_ = "gm-builtin";
        }
    }
    if (!host_midi_active_ && synth_ == nullptr && device == "auto")
        try_windows_host_fallback(midi_port);
    if (host_midi_active_) return;
    if (device == "opl" || (device == "auto" && synth_ == nullptr)) {
        start_opl_music();
        return;
    }
    if (music_backend_ != "none" || device == "off" || device == "none") return;
    report_music_failure(device, rom_dir);
    // Then AdLib, so a saved MT-32 choice with moved ROMs is not a silent game.
    start_opl_music();
    music_fell_back_ = true;
    std::fprintf(stderr, "audio: falling back to AdLib FM music.\n");
}

// Resolve the SFX backend and pre-bake the banks.  Reads music_backend_: run
// after select_music_backend.
void SdlAudio::resolve_and_bake_sfx(const std::string& sfx_backend) {
    // "auto" pairs with the music device (as the reference): MT-32 music ->
    // MT-32 SFX, GM -> GM, OPL/none -> SB-DAC VOC.  Never "opl" (AdLib SFX is
    // opt-in, like the EXE's 'A' card).  Explicit choices pass through.
    std::string sfxb = sfx_backend;
    if (sfxb == "auto") {
        // music_backend_ tracks which synth (if any) create() installed:
        // mt32-builtin => MT-32 SFX, gm-builtin => GM SFX, else SB-DAC VOC.
        if (music_backend_ == "mt32-builtin") sfxb = "mt32-sfx";
        else if (music_backend_ == "gm-builtin") sfxb = "gm-sfx";
        else sfxb = "sb-dac";
    }
    midi_sfx_ = (sfxb == "midi" || sfxb == "mt32-sfx" || sfxb == "gm-sfx");
    opl_sfx_ = (sfxb == "opl");
    sfx_off_ = (sfxb == "none" || sfxb == "off");
    if (opl_sfx_) bake_opl_sfx();
    if (midi_sfx_) bake_midi_sfx();
}


std::vector<std::int16_t> SdlAudio::render_offline(
    const std::vector<std::uint8_t>& midi_stream, int frames) {
    if (frames < 0) frames = 0;
    // The OPL driver plays raw game MDI (FF 7F voice patches), not the
    // sequencer's stream: load it directly.  set_loop(false): a stream with
    // every event at tick 0 would spin forever looping.
    if (synth_ == nullptr && opl_music_ != nullptr) {
        opl_music_->set_loop(false);
        if (!opl_music_->open(midi_stream)) return {};
        std::vector<std::int16_t> out(static_cast<std::size_t>(frames) * 2, 0);
        constexpr int kChunk = 1024;
        for (int done = 0; done < frames; done += kChunk) {
            const int n = std::min(kChunk, frames - done);
            opl_music_->render(n,
                               out.data() + static_cast<std::size_t>(done) * 2);
        }
        return out;
    }
    // Sequencer-backed synths (mt32-builtin / gm) render from seq_; OPL plays
    // raw game-MDI via opl_music_ and is handled above.
    seq_.load(midi_stream);
    std::vector<std::int16_t> out(static_cast<std::size_t>(frames) * 2, 0);
    // Render in fixed chunks so the per-buffer event quantisation matches real
    // playback (mix() dispatches a whole chunk's due events, then renders it).
    constexpr int kChunk = 1024;
    for (int done = 0; done < frames; done += kChunk) {
        const int n = std::min(kChunk, frames - done);
        mix(out.data() + static_cast<std::size_t>(done) * 2, n);
    }
    return out;
}

// Bake phase, OPL: pre-render the AdLib voices at the device rate.
void SdlAudio::bake_opl_sfx() {
    if (device_ == 0) return;
    for (const auto& id : opl_sfx_ids()) {
        const auto stereo = render_adlib_sfx_by_id(id, device_rate_);
        if (stereo.empty()) continue;
        std::vector<std::int16_t> mono(stereo.size() / 2);
        for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = stereo[i * 2];
        sfx_[id] = std::move(mono);
    }
}

// Bake phase, MIDI (mt32-sfx / gm-sfx): render each catalog note event to PCM
// through the active synth once, before any music loads.  Played as
// independent voices, never live through the music synth.
void SdlAudio::bake_midi_sfx() {
    if (midi_sfx_ && synth_ != nullptr && device_ != 0) {
        const int tail_frames = 100 * device_rate_ / 1000;  // Python tail_ms=100
        for (const auto& s : kMidiSfx) {
            const int gate_frames = std::max(1, s.ms * device_rate_ / 1000);
            const int total = gate_frames + tail_frames;
            std::vector<std::int16_t> stereo(
                static_cast<std::size_t>(total) * 2, 0);
            // Note on through the active synth, then render the tail.
            if (s.prog >= 0) synth_->send(0xC0 | s.ch, s.prog, 0);
            synth_->send(0x90 | s.ch, s.note, s.vel);
            if (s.note2 >= 0) synth_->send(0x90 | s.ch, s.note2, s.vel);
            synth_->render(gate_frames, stereo.data());
            synth_->send(0x80 | s.ch, s.note, 0);
            if (s.note2 >= 0) synth_->send(0x80 | s.ch, s.note2, 0);
            synth_->render(tail_frames, stereo.data() + gate_frames * 2);
            // Mono by averaging L and R.  Left-only loses hard-panned patches:
            // SFX_GENERIC's BellSinger (prog 46, the food ding) is hard right,
            // its left ~17x quieter.
            std::vector<std::int16_t> mono(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
                mono[i] = static_cast<std::int16_t>(
                    (static_cast<int>(stereo[i * 2]) + stereo[i * 2 + 1]) / 2);
            // Peak-normalize up to ~-4 dBFS (peak 20000), never down:
            // libmt32emu renders SFX ~18 dB under the music.  Same as the
            // reference.
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
}

void SdlAudio::reopen_device() {
    // From the device-removed watch (main thread).  Reopen the default output
    // with the original spec (flags 0: SDL converts), so synths keep
    // device_rate_.
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
    // Teardown joins a thread, closes libraries and writes a capture: swallow
    // exceptions (a destructor must not throw), as ~HostMidiPlayer does.
    // stop() has its own try, so a throw there still closes the device and
    // removes the watch.
    try {
        // Join the host-MIDI pump thread first (it silences the port on stop).
        if (host_midi_active_) host_midi_.stop();
        // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (...) {
    }
    try {
        if (event_watch_installed_) SDL_DelEventWatch(audio_device_watch, this);
        if (device_ != 0) {
            SDL_PauseAudioDevice(device_, 1);
            SDL_CloseAudioDevice(device_);
        }
        if (!capture_path_.empty()) write_capture();   // callback is stopped now
        // OLDUVAI_AUDIO_STATS: print the real-time health summary.  overruns >
        // 0 or a worst_lock_wait near the budget = dropout risk on this host.
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
        // The synth tears itself down after the device closes.
        // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (...) {
    }
}

void SdlAudio::write_capture() {
    if (!write_wav16(capture_path_, capture_, device_rate_, 2)) {
        std::fprintf(stderr, "audio-capture: could not write %s\n",
                     capture_path_.c_str());
        return;
    }
    const std::string sync = capture_path_ + ".sync";
    if (std::FILE* f = std::fopen(sync.c_str(), "w")) {
        std::fprintf(f, "rate %d\nperf_freq %llu\nt0 %llu\n", device_rate_,
                     static_cast<unsigned long long>(SDL_GetPerformanceFrequency()),
                     static_cast<unsigned long long>(capture_t0_));
        std::fclose(f);
    }
    std::fprintf(stderr, "audio-capture: %.1f s at %d Hz -> %s\n",
                 static_cast<double>(capture_.size()) / 2.0 / device_rate_,
                 device_rate_, capture_path_.c_str());
}

void SdlAudio::load_sfx(const std::string& id,
                        const formats::VocAudio& voc) {
    // OPL: the AdLib voices are pre-rendered; a VOC must not overwrite them.
    // Ids without an AdLib record (SFX_WAIT_AND_PLAY) still take the VOC.
    if (opl_sfx_ && opl_sfx_lookup(id) != nullptr) return;
    // MIDI SFX backends: keep the bake; only SB-DAC plays the VOC samples.
    if (midi_sfx_ && sfx_.find(id) != sfx_.end()) return;
    if (voc.sample_rate <= 0 || voc.data.empty()) return;
    // Band-limited (windowed-sinc) upsampling + edge declick: the 4 kHz samples
    // reach their Nyquist and start at ~28% of full scale.  The SB's analog
    // stage smoothed both.  See resample.hpp.
    std::vector<std::int16_t> pcm =
        resample_sinc_u8(voc.data, voc.sample_rate, device_rate_);
    if (pcm.empty()) return;
    apply_edge_fade(pcm, device_rate_);
    std::lock_guard<std::mutex> lock(mu_);
    sfx_[id] = std::move(pcm);
}

bool SdlAudio::has_sfx(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return sfx_.find(id) != sfx_.end();
}

void SdlAudio::play_sfx(const std::string& id) {
    if (device_ == 0 || sfx_off_) return;
    std::lock_guard<std::mutex> lock(mu_);
    // Every SFX is pre-rendered PCM in sfx_, played as independent voices.
    const std::vector<std::int16_t>* buf = nullptr;
    {
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
    // synth_ is created in the ctor and only torn down with the object (main
    // thread owns both ends) — safe to branch on unlocked.
    if (synth_ != nullptr) {
        // Convert and parse the new track before taking the callback's lock
        // (both allocate and walk every event); under the lock only key off and
        // swap.
        MidiSequencer next;
        next.load(formats::build_gm_midi(raw_mdi, track_id,
                                         drop_runtime_modulation(),
                                         wants_gm_translation()));
        std::lock_guard<std::mutex> lock(mu_);
        // All notes off (CC 123) before the swap: fade_out_music() only ramps
        // the mix gain, so a sustained note would lose its note-off and hang.
        // The EXE's driver re-init at track start does the same after
        // MDI_FadeStop.
        for (int chn = 0; chn < 16; ++chn) {
            synth_->send(0xB0 | chn, 123, 0);   // all notes off
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
    // EXE MDI_FadeStop (1f75:00e4): master volume (DS:0x88d6) down 4 per ~111
    // ms step until silent.  Here: the music gain in 16 steps (~1.8 s).  No
    // lock while sleeping (the callback reads music_gain_ lock-free).
    if (!music_available()) return;
    // Host MIDI: no gain ramp on the external device; stop the track (all notes
    // off) before the next play_music().
    if (host_midi_active_) {
        host_midi_.stop();
        return;
    }
    constexpr int kSteps = 16;
    const Uint32 step_ms = 1000u * 2u / 18u;   // 2 BIOS ticks ≈ 111ms
    const float from = music_gain_.load(std::memory_order_relaxed);
    if (from <= 0.0f) return;   // already silent (set_music_fade)
    for (int s = kSteps - 1; s >= 0; --s) {
        music_gain_.store(from * static_cast<float>(s) / kSteps,
                          std::memory_order_relaxed);
        SDL_Delay(step_ms);
    }
    music_gain_.store(0.0f, std::memory_order_relaxed);
}

void SdlAudio::set_music_fade(float gain) {
    if (!music_available() || host_midi_active_) return;
    music_gain_.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed);
}

void SdlAudio::stop_music() {
    if (host_midi_active_) {
        host_midi_.stop();
        return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (synth_ != nullptr) {
        // Release everything: all-notes-off on every channel.
        for (int chn = 0; chn < 16; ++chn) {
            synth_->send(0xB0 | chn, 123, 0);
        }
        seq_ = MidiSequencer();
        return;
    }
    if (opl_music_ == nullptr) return;
    opl_music_->stop();
}

void SdlAudio::mix(std::int16_t* out, int frames) {
    // Health counters (audio.hpp): lock wait (main-thread contention) and the
    // whole callback against its buffer budget.  One audio thread: relaxed
    // stores.
    const std::uint64_t t0 = SDL_GetPerformanceCounter();
    std::unique_lock<std::mutex> lock(mu_, std::try_to_lock);
    if (!lock.owns_lock()) lock.lock();
    const std::uint64_t t_locked = SDL_GetPerformanceCounter();
    // Synth base layer (music + MIDI effects) or silence.  The synth
    // renders whenever it exists — effects must sound with no music.
    if (synth_ != nullptr) {
        // One event pump for both melodic backends; SFX still sound with no
        // music.
        if (seq_.loaded()) {
            seq_.advance(frames, device_rate_,
                         [&](std::uint8_t st, std::uint8_t d1, std::uint8_t d2) {
                             synth_->send(st, d1, d2);
                         });
        }
        synth_->render(frames, out);
    } else if (opl_music_ != nullptr) {
        opl_music_->render(frames, out);   // zero-fills past a stopped track
    } else {
        for (int i = 0; i < frames * 2; ++i) out[i] = 0;
    }
    // Music level = fade ramp x enhanced duck, applied to the synth layer
    // before the PCM SFX voices.
    const float mg =
        music_gain_.load(std::memory_order_relaxed) * music_balance_;
    if (mg < 0.999f) {
        for (int i = 0; i < frames * 2; ++i) {
            out[i] = static_cast<std::int16_t>(out[i] * mg);
        }
    }
    // Polyphonic PCM SFX voices; finished ones are pruned.
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
    if (!capture_path_.empty()) {   // OLDUVAI_AUDIO_CAPTURE (audio.hpp)
        if (capture_.empty()) capture_t0_ = t0;
        capture_.insert(capture_.end(), out, out + frames * 2);
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

SoundCardAvail probe_sound_cards(const std::string& rom_dir,
                                 const std::string& soundfont) {
    static std::map<std::string, SoundCardAvail> cache;
    const std::string key = rom_dir + '\n' + soundfont;
    if (const auto it = cache.find(key); it != cache.end()) return it->second;

    SoundCardAvail a;
    // MT-32: the library binds and some search dir holds a ROM pair (names
    // only, not read).
    {
        Mt32Api api;
        void* lib = nullptr;
        if (bind_mt32_api(api, lib)) {
            for (const auto& dir : rom_search_dirs(rom_dir)) {
                if (rom_pair_present(dir)) { a.mt32 = true; break; }
            }
        }
        dyn_close(lib);
    }
    // GM: FluidSynth binds, and a SoundFont is found (not loaded).
    {
        FsApi api;
        void* lib = nullptr;
        a.gm = bind_fluid_api(api, lib) && !find_soundfont(soundfont).empty();
        dyn_close(lib);
    }
    // External MIDI: host MIDI compiled in and at least one output port.
    a.midi = !host_midi_list_ports().empty();
    cache[key] = a;
    return a;
}

}  // namespace olduvai::presentation
