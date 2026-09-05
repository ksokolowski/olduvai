// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/audio/audio.hpp"

#include "presentation/audio/opl_sfx.hpp"
#include "presentation/audio/resample.hpp"
#include "presentation/audio/rom_dirs.hpp"
#include "presentation/audio/soundfont_pick.hpp"

#include <SDL.h>

#include "presentation/audio/dynlib.hpp"

#ifdef OLDUVAI_VENDORED_MT32EMU
// Vendored Munt libmt32emu — see third_party/mt32emu/OLDUVAI-VENDORING.md.
// Only the C interface is used; the C++ API would couple us to munt's types.
#include <mt32emu/c_interface/c_interface.h>
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

// WHY THE @executable_path CANDIDATES COME FIRST ON macOS.  A LEAF-NAME
// dlopen cannot find a dylib shipped inside the .app: dyld searches
// DYLD_LIBRARY_PATH then DYLD_FALLBACK_LIBRARY_PATH (~/lib, /usr/local/lib,
// /usr/lib) and never looks beside the executable.  Measured on macOS 15:
// with the binary at App/Contents/MacOS and the dylib alongside in the
// bundle, "libfoo.dylib" is NOT FOUND while the @executable_path form
// resolves.  So bundling these libraries WITHOUT this list is inert: the
// download grows and the loader still never looks there.
//
// ../libs, not ../Frameworks, because that is where make_dmg_macos.sh
// already puts SDL2 (dylibbundler -d Contents/libs/ -p
// @executable_path/../libs/) — one bundle layout, not two.  Bundled copy
// first, so a shipped app uses the version it was tested against rather than
// whatever Homebrew happens to have.
//
// This is also why the gap went unnoticed for so long: on a dev machine the
// ABSOLUTE Homebrew path below is what resolves (the leaf names never do,
// /opt/homebrew is not a fallback directory), so developers always had MT-32
// and FluidSynth while shipped macOS users never did.
#ifdef OLDUVAI_VENDORED_MT32EMU
// Adapters from our void*-based Mt32Api to munt's real C signatures.
//
// The dlsym path has to reinterpret_cast, because dlsym only ever hands back
// void* — and that cast is, strictly, UB: it calls through a function pointer
// of a different type.  It happens to work everywhere we ship.  With the
// header actually visible we can do better than "happens to work", and the
// compiler agrees: casting these directly earns -Wcast-function-type-mismatch
// on create() and play_msg(), which under CI's -Werror is a build failure.
//
// So the vendored path converts explicitly instead.  The interesting one is
// the report handler: mt32emu_report_handler_i is a struct holding a single
// pointer, passed BY VALUE — which is exactly why passing our &struct as a
// void* has always worked in the dlsym path.  Here we build the real struct.
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

// Bind the seven C-API entry points MT-32 playback needs.  Returns false if
// any required one is missing.
//
// Vendored build (the default): libmt32emu is compiled in, so binding is a
// compile-time fact — no probe, no handle, and no way for a shipped package
// to arrive without it.  That last part is the point: as a dlopen'd library
// it reached Linux AppImage users only, and macOS/Windows users silently got
// OPL instead for four public releases.
//
// The dlopen path stays for -DOLDUVAI_WITH_MT32EMU=OFF builds (distros that
// prefer to link the system libmt32emu, and anyone using $OLDUVAI_MT32EMU).
// Both paths reinterpret_cast: the real create() takes the report handler as
// a one-pointer struct BY VALUE, which our void* typedef matches in every
// calling convention we target, and which is how the dlsym path has always
// worked.
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

// ROM discovery: the search list lives in `rom_dirs.hpp` and is pinned per
// platform by tests/test_rom_dirs.cpp — deliberately NOT restated here, since
// this comment carried a copy that was already two entries stale by the time
// the Windows per-user location landed.  User-facing version in docs/AUDIO.md.
// Accepts the MT-32 or CM-32L control+PCM pair in either letter case.
// Resolve the pair against what is ACTUALLY on disk, compared
// case-insensitively.  The old code tried four fixed spellings — the two
// names all-upper, then all-lower — which silently required the control and
// PCM ROMs to share one case.  Real collections do not: combining the common
// "legacy" dump (MT32_CONTROL.ROM, upper) with a MAME-versioned one
// (mt32_pcm.rom, lower) yields a mixed-case set, and that set matched NONE of
// the four spellings.  It worked anyway on macOS and Windows because their
// filesystems are case-insensitive, so the lookup was corrected for us — and
// failed on Linux, where it is not.  Same silent, unexplained "no MT-32" a
// user with no ROMs at all would see.
// Which device's ROMs to prefer.  CM-32L is the later, superset machine (it
// adds 33 PCM samples over the MT-32), so it stays the default — but the
// choice was previously INVISIBLE: a box holding both pairs quietly ran
// CM-32L while one holding only MT-32 ROMs ran MT-32, the log said
// "mt32-builtin" either way, and the two legitimately sound different.  That
// cost real time during the 2026-07-26 cross-platform audio validation, where
// macOS and Linux disagreed for exactly this reason and nothing said so.
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

bool add_rom_pair(const Mt32Api& api, void* ctx, const std::string& dir,
                  Mt32Model model = Mt32Model::kAuto) {
    std::error_code ec;
    std::vector<std::string> entries;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec)) {
            entries.push_back(e.path().filename().string());
        }
    }
    if (entries.empty()) return false;

    const auto find_ci = [&entries](const char* want) {
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
    };
    // Only the two IDENTITIES are listed now; case is no longer part of the
    // key, so the four-spelling ladder collapses.
    const auto try_pair = [&](const char* ctl, const char* pcm,
                              const char* label) {
        const std::string a = find_ci(ctl);
        const std::string b = find_ci(pcm);
        if (a.empty() || b.empty()) return false;
        if (api.add_rom(ctx, (dir + "/" + a).c_str()) >= 0 &&
            api.add_rom(ctx, (dir + "/" + b).c_str()) >= 0) {
            g_mt32_loaded = std::string(label) + " (" + a + " + " + b + ")";
            return true;
        }
        return false;
    };
    // MUST short-circuit: try_pair() feeds ROMs into the SAME context, so
    // evaluating both would load a CM-32L control ROM and an MT-32 PCM ROM
    // into one synth.  (Written eagerly first, caught before it ran.)
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
    // Platform search list lives in soundfont_pick.hpp so a test can pin it —
    // the selection RULE was unit-tested while this list was not, and the bug
    // was entirely in the list.
    const std::vector<std::string> system_dirs = default_soundfont_dirs();
    // Roland SC-55 first: it is the Sound Canvas set (same lineage as the
    // Windows gm.dls) and the most faithful GM voice.  Debian/Ubuntu ship it as
    // scummvm-data's /usr/share/scummvm/Roland_SC-55.sf2 (GPLv3).  Then the
    // clean-provenance free faces.
    const std::vector<std::string> names = {
        "Roland_SC-55.sf2", "GeneralUser-GS.sf2", "GeneralUser GS.sf2",
        "FluidR3_GM.sf2", "default-GM.sf2"};
    return select_soundfont(config_dir, system_dirs, names, exists);
}

// ---- Melodic synth backends (PcmMidiSynth impls) -------------------------
// Each owns its dlopen handle + backend objects and the bound C-API subset;
// create() runs the whole probe and returns null on any failure so the caller
// just checks the pointer.  send()/render() are the only live entry points —
// see the PcmMidiSynth doc in audio.hpp.

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
                    // WHICH device, not just "mt32-builtin".  CM-32L and MT-32
                    // are different machines and legitimately sound different;
                    // saying only the backend name is how two boxes disagreed
                    // for a whole afternoon during the cross-platform audio
                    // validation with nothing to explain it.
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
        void* lib = load_fluidsynth();
        // Distinguish "no FluidSynth" from "no SoundFont".  Both used to
        // report the same thing, which is exactly why a shipped package with
        // no synth in it looked identical to a user who had not installed a
        // SoundFont — and why the four-release packaging gap went unreported.
        g_fluid_lib_found = (lib != nullptr);
        if (lib == nullptr) return nullptr;
        FsApi api;
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
        api.cc = reinterpret_cast<FsCcFn>(
            dyn_sym(lib, "fluid_synth_cc"));
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
            return nullptr;
        }
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
        // DELIBERATELY NOT dyn_close(lib_) — this leaks one handle per
        // process and that is the fix, not an oversight.
        //
        // libfluidsynth is built with OpenMP, and its libgomp worker pool
        // OUTLIVES delete_fluid_synth: the threads are libgomp's, not the
        // synth's, so nothing we can call joins them.  dlclose then unmaps the
        // code those threads are about to return into, and they fault.  It is
        // a race, which is why it reads as flaky rather than broken: measured
        // 7 of 8 renders crashing here, 0 of 8 with OMP_NUM_THREADS=1, with
        // the fault always in libgomp on a worker thread and never in our
        // frames or FluidSynth's.
        //
        // Not closing is the standard remedy for a dlopen'd OpenMP library.
        // The cost is nil: dlopen refcounts, so re-selecting the device
        // returns the same handle rather than mapping a second copy, and the
        // mapping goes away at exit like every other one.
        //
        // libmt32emu keeps its dyn_close — it has no OpenMP and no such pool.
    }
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
    // Retained but deliberately never read: the destructor does NOT
    // dyn_close it (see there for why), so the handle is held only to
    // document the intentional leak.  [[maybe_unused]] because Clang's
    // -Wunused-private-field fires on a written-but-never-read member.
    [[maybe_unused]] void* lib_ = nullptr;
    void* settings_ = nullptr;
    void* synth_ = nullptr;
    FsApi api_;
};

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
                   const std::string& midi_port, bool offline,
                   const std::string& mt32_model) {
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
    // Offline (render harness): keep the negotiated device_rate_ (from
    // audio_rate) and set up the synths below, but open NO device and install
    // no unplug watch — mix() is driven by hand via render_offline().
    if (!offline) {
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
    }

    // Phases as methods over the members they set (§3.7's D shape, applied to
    // the one §3.12 row no instrument could verify until audio_diff.sh
    // existed).  Order is load-bearing: SFX resolution reads music_backend_
    // so "auto" can pair to whatever music synth actually loaded.
    select_music_backend(music_device, rom_dir, soundfont, midi_port,
                         mt32_model);
    resolve_and_bake_sfx(sfx_backend);
    if (device_ != 0) SDL_PauseAudioDevice(device_, 0);   // offline: no device
}

// Choose and load the music synth: host-MIDI (opt-in), MT-32 builtin, GM
// builtin, the auto host fallback, OPL — first hit wins, later blocks are
// guarded on nothing having loaded yet.  Moved verbatim from the constructor.
void SdlAudio::select_music_backend(const std::string& music_device,
                                    const std::string& rom_dir,
                                    const std::string& soundfont,
                                    const std::string& midi_port,
                                    const std::string& mt32_model) {
    // ── Host-MIDI music path (opt-in) ───────────────────────────────────────
    // "host-midi" (and the Python back-compat alias "mt32") route MDI music to
    // a real MIDI OUT port via RtMidi instead of rendering audio.  When it
    // opens a port we mark host_midi_active_ and SKIP every synth-music setup
    // block below (neither synth_ nor opl_music_ ever binds), so the music
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
        synth_ = Mt32Synth::create(rom_dir, device_rate_, mt32_model);
        if (synth_ != nullptr) music_backend_ = "mt32-builtin";
    }
    // GM (FluidSynth + SoundFont) next in the auto chain.
    if (!host_midi_active_ && synth_ == nullptr &&
        (music_device_eff == "auto" || music_device_eff == "gm-builtin" ||
         music_device_eff == "gm")) {
        const std::string sf = find_soundfont(soundfont);
        if (!sf.empty()) {
            std::fprintf(stderr, "gm-builtin: soundfont = %s\n", sf.c_str());
            synth_ = FluidSynth::create(sf, device_rate_);
            if (synth_ != nullptr) music_backend_ = "gm-builtin";
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
    if (!host_midi_active_ && synth_ == nullptr &&
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
         ((music_device_eff == "auto") && synth_ == nullptr));
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
        // Name the ACTUAL cause.  "needs ROMs or a SoundFont or check your
        // spelling" made a broken package indistinguishable from an
        // unconfigured one, and users reasonably assumed the latter.
        if (music_device_eff == "gm-builtin") {
            if (!g_fluid_lib_found) {
                std::fprintf(stderr,
                             "audio: gm-builtin unavailable — the FluidSynth "
                             "library could not be loaded.  This build ships "
                             "one; if you see this, the package is incomplete "
                             "(set $OLDUVAI_FLUIDSYNTH to override).\n");
            } else {
                std::fprintf(stderr,
                             "audio: gm-builtin unavailable — FluidSynth "
                             "loaded, but no SoundFont was found.  Pass "
                             "--soundfont <file.sf2> or install one.\n");
            }
        } else if (music_device_eff == "mt32-builtin") {
            // The library is vendored and compiled in, so it is never the
            // cause here — only the ROMs can be.
            std::fprintf(stderr,
                         "audio: mt32-builtin unavailable — no MT-32/CM-32L "
                         "ROM pair found.  Searched: ");
            for (const auto& d : rom_search_dirs(rom_dir)) {
                std::fprintf(stderr, "%s ", d.c_str());
            }
            std::fprintf(stderr,
                         "\n  Need CM32L_CONTROL.ROM + CM32L_PCM.ROM, or "
                         "MT32_CONTROL.ROM + MT32_PCM.ROM (any case).\n");
        } else {
            std::fprintf(stderr,
                         "audio: music device '%s' could not start — no "
                         "music.  Check the device name for typos.\n",
                         music_device_eff.c_str());
        }
    }
}

// Resolve the SFX backend (music-aware "auto") and pre-bake the sample banks.
// Moved verbatim from the constructor; reads music_backend_, so it must run
// AFTER select_music_backend.
void SdlAudio::resolve_and_bake_sfx(const std::string& sfx_backend) {
    // Resolve the SFX backend, music-aware — mirrors Python's
    // _resolve_sfx_backend so "auto" pairs to the music device: MT-32 music →
    // MT-32 SFX, GM music → GM SFX, OPL/none → SB-DAC VOC samples.  "auto"
    // never selects "opl" (AdLib synth SFX stays opt-in, like the EXE 'A'
    // card).  Explicit choices pass through unchanged.
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

    midi_sfx_ = (sfxb == "midi" || sfxb == "mt32-sfx" || sfxb == "gm-sfx");
    opl_sfx_ = (sfxb == "opl");
    if (opl_sfx_) bake_opl_sfx();
    if (midi_sfx_) bake_midi_sfx();
}


std::vector<std::int16_t> SdlAudio::render_offline(
    const std::vector<std::uint8_t>& midi_stream, int frames) {
    if (frames < 0) frames = 0;
    // The OPL driver plays RAW game-MDI (FF 7F voice patches), not the
    // sequencer's channel-voice stream — load it directly.  Without this arm,
    // `--render-audio` on the OPL backend printed a digest of silence:
    // mix() rendered a player that had never been handed a track.
    // set_loop(false): a stream whose events all sit at tick 0 spins forever
    // under loop (the cursor restarts and active_ never clears).
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

// Bake phase, OPL: pre-render the AdLib FM voices now (at the device rate)
// so they're available even when a SFX has no VOC asset to trigger
// load_sfx().  The renderer emits interleaved stereo (L==R in OPL2 mode);
// playback is mono (one sample per frame, duplicated to both channels), so
// down-mix by taking the L channel.  play_sfx() then plays from sfx_.
void SdlAudio::bake_opl_sfx() {
}

// Bake phase, MIDI (mt32-sfx / gm-sfx): pre-render each catalog note event
// to PCM through the active synth ONCE, here at construction before any
// music loads — mirrors the Python build, which renders each SFX to a
// pygame.mixer.Sound.  Stored in sfx_ and played as independent polyphonic
// waves via the voice pool, NEVER injected live into the music synth (so
// they never steal music voices and get their own balance gain).
void SdlAudio::bake_midi_sfx() {
    // MIDI SFX backends (mt32-sfx / gm-sfx): pre-render each catalog note event
    // to PCM through the active synth ONCE, here at construction before any
    // music loads — mirrors the Python build, which renders each SFX to a
    // pygame.mixer.Sound.  Stored in sfx_ and played as independent polyphonic
    // waves via the voice pool, NEVER injected live into the music synth (so
    // they never steal music voices and get their own balance gain).
    if (midi_sfx_ && synth_ != nullptr && device_ != 0) {
        const int tail_frames = 100 * device_rate_ / 1000;  // Python tail_ms=100
        for (const auto& s : kMidiSfx) {
            const int gate_frames = std::max(1, s.ms * device_rate_ / 1000);
            const int total = gate_frames + tail_frames;
            std::vector<std::int16_t> stereo(
                static_cast<std::size_t>(total) * 2, 0);
            // Gate the note through the active synth, then render its tail:
            // the synth demuxes each channel-voice message exactly as live
            // music does (MT-32 packs it raw; GM routes to the typed calls).
            if (s.prog >= 0) synth_->send(0xC0 | s.ch, s.prog, 0);
            synth_->send(0x90 | s.ch, s.note, s.vel);
            if (s.note2 >= 0) synth_->send(0x90 | s.ch, s.note2, s.vel);
            synth_->render(gate_frames, stereo.data());
            synth_->send(0x80 | s.ch, s.note, 0);
            if (s.note2 >= 0) synth_->send(0x80 | s.ch, s.note2, 0);
            synth_->render(tail_frames, stereo.data() + gate_frames * 2);
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
    // The melodic synth (its context/handles + dlopen'd lib) tears itself
    // down when synth_ destructs, after the SDL device is closed above.
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
    std::lock_guard<std::mutex> lock(mu_);
    sfx_[id] = std::move(pcm);
}

void SdlAudio::play_sfx(const std::string& id) {
    if (device_ == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    // All SFX (OPL, SB-DAC VOC, and the MIDI backends baked at construction)
    // are pre-rendered PCM in sfx_ — play them as independent polyphonic waves,
    // never live through the music synth.
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
    // EXE MDI_FadeStop (1f75:00e4): ramp the MIDI master volume (DS:0x88d6)
    // down by 4 every delay(4)→2-BIOS-tick (~111ms) step until silent, then
    // the caller starts the next track at full.  We ramp the music mix gain
    // the same way: 16 steps (≈ master-vol/4) x ~111ms ≈ 1.8s.  No lock held
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
    // Real-time health counters (see audio.hpp): time the lock acquisition
    // (main-thread contention = the RB1 hazard) and the whole callback
    // against its buffer budget.  Single audio thread → relaxed stores.
    const std::uint64_t t0 = SDL_GetPerformanceCounter();
    std::unique_lock<std::mutex> lock(mu_, std::try_to_lock);
    if (!lock.owns_lock()) lock.lock();
    const std::uint64_t t_locked = SDL_GetPerformanceCounter();
    // Synth base layer (music + MIDI effects) or silence.  The synth
    // renders whenever it exists — effects must sound with no music.
    if (synth_ != nullptr) {
        // One event pump for both melodic backends: the sequencer emits parsed
        // channel-voice messages, the active synth demuxes each (MT-32 raw, GM
        // typed).  Effects still sound with no music (seq_ not loaded).
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
