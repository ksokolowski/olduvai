// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Olduvai entry point.  Current state: game-file detection + the M2 asset
// viewer (--viewer).

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
#include "core/build_id.hpp"
#include "options_resolve.hpp"
#include "cli_args.hpp"
#include "legacy_cache.hpp"
#include "options_build.hpp"
#ifdef OLDUVAI_HAVE_SDL
#include "first_run.hpp"
#endif
#include "enhance/upscale.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_files.hpp"
#include "formats/cur.hpp"
#include "formats/voc.hpp"
#include "presentation/audio/opl_sfx.hpp"
#include "presentation/audio/wav_io.hpp"
#include "presentation/menu/profile_table.hpp"

#ifdef OLDUVAI_HAVE_SDL
#include "presentation/diag/bug_capture.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/game_app.hpp"
#include "presentation/audio/host_midi.hpp"
#include "presentation/diag/viewer.hpp"
#endif


namespace {

// --render-audio: deterministic offline PCM render of a format-0 MIDI stream
// through one synth backend.  No game files needed; exits 77 (SKIP) when the
// chosen synth cannot load (no ROMs / SoundFont).
int render_audio_command(const olduvai::app::CliArgs& args,
                         const olduvai::app::PlaySettings& ps) {
    std::vector<std::uint8_t> smf;
    bool read_failed = false;
    if (std::FILE* mf = std::fopen(args.render_audio.c_str(), "rb")) {
        // Stop at a short read: it is EOF or an error, and ferror tells them
        // apart, so an I/O failure is an error rather than a truncated song (a
        // user-supplied path, see SECURITY.md).
        std::uint8_t buf[8192];
        for (;;) {
            const std::size_t got = std::fread(buf, 1, sizeof buf, mf);
            if (got > 0) smf.insert(smf.end(), buf, buf + got);
            if (got < sizeof buf) {
                read_failed = std::ferror(mf) != 0;
                break;
            }
        }
        std::fclose(mf);
    }
    if (read_failed) {
        std::fprintf(stderr, "render-audio: error reading %s\n",
                     args.render_audio.c_str());
        return 1;
    }
    if (smf.empty()) {
        std::fprintf(stderr, "render-audio: cannot read %s\n",
                     args.render_audio.c_str());
        return 1;
    }
    const int rate = (ps.audio_rate >= 8000) ? ps.audio_rate : 44100;
    olduvai::presentation::AudioSetup setup =
        olduvai::presentation::audio_setup_of(ps);
    setup.rate = rate;
    setup.offline = true;
    olduvai::presentation::SdlAudio audio(setup);
    // A fallback is not the backend asked for: rendering AdLib for a missing GM
    // SoundFont would test the wrong synth.
    if (!audio.music_available() || audio.music_fell_back()) {
        std::fprintf(stderr,
            "render-audio: no synth backend for '%s' — SKIP "
            "(MT-32 needs ROMs; GM needs a SoundFont)\n",
            ps.music_device.c_str());
        return 77;   // CTest SKIP_RETURN_CODE
    }
    const int frames =
        static_cast<int>(rate * args.render_audio_secs);
    const std::vector<std::int16_t> pcm = audio.render_offline(smf, frames);
    if (!args.render_audio_out.empty()) {
        olduvai::presentation::write_wav16(args.render_audio_out, pcm, rate,
                                           2);
        std::printf("render-audio: %d frames @ %d Hz (%s) -> %s\n", frames,
                    rate, audio.active_music_backend().c_str(),
                    args.render_audio_out.c_str());
    } else {
        const std::vector<std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(pcm.data()),
            reinterpret_cast<const std::uint8_t*>(pcm.data()) +
                pcm.size() * sizeof(std::int16_t));
        std::printf("%s  %d  %s\n",
                    olduvai::presentation::sfx_digest_hex(bytes).c_str(),
                    frames, audio.active_music_backend().c_str());
    }
    return 0;
}

// --render-sfx <id|all>: render the OPL effects as the engine plays them and
// print "sha256  frames  id", or write WAVs with --render-audio-out
// (<stem>_<id>.wav for "all").  Used by scripts/metrics/audio_diff.sh.  Needs
// game data: the patch bytes come from the user's executable.
int render_sfx_command(const olduvai::app::CliArgs& args,
                       const olduvai::app::PlaySettings& ps) {
    const std::string dir =
        olduvai::prepare::resolve_game_dir(ps.game_dir).string();
    std::vector<std::uint8_t> exe;
    try {
        exe = olduvai::prepare::load_game_executable(dir);
    } catch (...) {
        std::fprintf(stderr,
                     "render-sfx: no game executable under '%s' — SKIP\n",
                     dir.c_str());
        return 77;
    }
    olduvai::presentation::install_adlib_sfx_voices(
        olduvai::prepare::read_adlib_sfx_voices(exe));
    const int rate = (ps.audio_rate >= 8000) ? ps.audio_rate : 44100;
    std::vector<std::string> ids;
    if (args.render_sfx == "all") {
        ids = olduvai::presentation::opl_sfx_ids();
    } else {
        ids.push_back(args.render_sfx);
    }
    if (ids.empty()) {
        std::fprintf(stderr, "render-sfx: no SFX in the catalog — SKIP\n");
        return 77;
    }
    int rendered = 0;
    for (const std::string& id : ids) {
        const std::vector<std::int16_t> pcm =
            olduvai::presentation::render_adlib_sfx_by_id(id, rate);
        if (pcm.empty()) {
            std::fprintf(stderr, "render-sfx: '%s' produced nothing\n",
                         id.c_str());
            continue;
        }
        ++rendered;
        if (!args.render_audio_out.empty()) {
            std::string out = args.render_audio_out;
            if (ids.size() > 1) {
                const std::size_t dot = out.rfind('.');
                const std::string stem =
                    dot == std::string::npos ? out : out.substr(0, dot);
                const std::string ext =
                    dot == std::string::npos ? "" : out.substr(dot);
                out = stem;
                out += "_";
                out += id;
                out += ext;
            }
            olduvai::presentation::write_wav16(out, pcm, rate, 2);
            std::printf("render-sfx: %s -> %s\n", id.c_str(), out.c_str());
        } else {
            const std::vector<std::uint8_t> bytes(
                reinterpret_cast<const std::uint8_t*>(pcm.data()),
                reinterpret_cast<const std::uint8_t*>(pcm.data()) +
                    pcm.size() * sizeof(std::int16_t));
            std::printf("%s  %zu  %s\n",
                        olduvai::presentation::sfx_digest_hex(bytes).c_str(),
                        pcm.size(), id.c_str());
        }
    }
    return rendered > 0 ? 0 : 77;
}

// The --help text: the banner and what the program is, then the option
// sections, generated from the table parse_args reads.
void print_usage() {
    std::printf(
        "olduvai " OLDUVAI_VERSION " — native engine recreation of Prehistorik (1991, Titus)\n"
        "\n"
        "Usage:\n"
        "  olduvai [options]\n"
        "\n"
        "Runs game-file detection in game_dir.  With no mode flag it reports\n"
        "whether the required files are present and prints this help; --play\n"
        "launches the game and --viewer opens the asset browser.  Original\n"
        "Prehistorik game files are required (FILESA.CUR, FILESB.CUR,\n"
        "FILESA.VGA, FILESB.VGA, and HISTORIK.EXE — or PREH.SQZ as the GOG\n"
        "release ships it).  A GOG install root works directly as game_dir\n"
        "(data/PREH is found).\n"
        "\n"
        "%s",
        olduvai::app::flag_usage().c_str());
}

// List the host MIDI OUT ports and exit (no game files).  Without RtMidi,
// report the feature as unavailable.
int list_midi_ports_command() {
#ifdef OLDUVAI_HAVE_SDL
    if (!olduvai::presentation::host_midi_available()) {
        std::printf("Host MIDI is not available in this build.\n");
        return 0;
    }
    const std::vector<std::string> ports =
        olduvai::presentation::host_midi_list_ports();
    if (ports.empty()) {
        std::printf(
            "No MIDI output ports found.  Connect a MIDI device, run "
            "MUNT, or create a virtual MIDI port (CoreMIDI / ALSA seq).\n");
    } else {
        std::printf("Available MIDI output ports:\n");
        for (std::size_t i = 0; i < ports.size(); ++i) {
            std::printf("  %zu: %s\n", i, ports[i].c_str());
        }
    }
    return 0;
#else
    std::printf("Host MIDI is not available in this build "
                "(no presentation layer).\n");
    return 0;
#endif
}

// Game-directory resolution for the interactive launch.  Free-name
// probe: 2 (args, ps) — both stay parameters.
void resolve_launch_game_dir(olduvai::app::CliArgs& args,
                             const olduvai::app::PlaySettings& ps) {
    // Map a GOG install root (files under data/PREH) to the directory holding
    // the files.  After --save-config, so the config keeps the path the user
    // gave.
    args.game_dir = olduvai::prepare::resolve_game_dir(args.game_dir);

    // No configured directory and no game files here: probe for a GOG install.
    // An explicit directory, even a wrong one, is always respected.
    if (!ps.cli.game_dir && !ps.config_game_dir &&
        !olduvai::prepare::detect_game_files(args.game_dir).complete()) {
        for (const auto& cand :
             olduvai::prepare::default_game_dir_candidates()) {
            const olduvai::prepare::GameFiles gf =
                olduvai::prepare::detect_game_files(cand);
            if (gf.complete()) {
                args.game_dir = gf.dir;   // detection already resolved data/PREH
                std::printf("Using game files found at %s\n",
                            args.game_dir.string().c_str());
                break;
            }
        }
    }
}

// First-run gate: report missing files (and on a GUI launch show the folder
// picker) until a complete set is found.  True once complete.
bool ensure_launch_game_files(olduvai::app::CliArgs& args,
                              olduvai::app::PlaySettings& ps) {
    olduvai::prepare::GameFiles gf =
        olduvai::prepare::detect_game_files(args.game_dir);
    if (!gf.complete()) {
        // The report always goes to the console too (terminals, logs).
        std::printf("Olduvai needs your original Prehistorik game files.\n");
        std::printf("Missing in %s:\n%s",
                    args.game_dir.string().c_str(), gf.problems().c_str());
        std::printf("Copy them there (or pass --game-dir) and run again.\n");
        std::fflush(stdout);
#ifdef OLDUVAI_HAVE_SDL
        // GUI session: also show the first-run dialog (folder picker + GOG
        // link); a validated pick is saved to play.json and used now.
        if (olduvai::app::launched_from_gui()) {
            std::string chosen_preset;
            const auto picked = olduvai::app::first_run_dialog(
                args.game_dir, gf.problems(), &chosen_preset,
                ps.profile_family);
            if (!picked) return false;            // user quit
            args.game_dir = *picked;
            // Adopt the dialog's style choice for this session too: the config
            // merge ran before the dialog.
            olduvai::app::adopt_preset(ps, args.profile, chosen_preset);
            ps.style_answered = true;   // the dialog always asks
            gf = olduvai::prepare::detect_game_files(args.game_dir);
            if (gf.complete()) {
                std::printf("Using game folder %s (saved to settings).\n",
                            args.game_dir.string().c_str());
            }
        }
#endif
        if (!gf.complete()) return false;
    }
    return true;
}

// The settings as the precedence rules layer them, into `ps` (and the game
// directory into `args`); --save-config writes the CLI-stated keys.
void apply_settings(olduvai::app::CliArgs& args,
                    olduvai::app::PlaySettings& ps) {
    const olduvai::app::Config file_cfg =
        args.no_config ? olduvai::app::Config{}
                       : olduvai::app::load_config_file();
    // Precedence: defaults < --default-profile < play.json < --profile <
    // CLI flags (options_resolve.hpp).  --profile states intent and beats
    // the saved config; --default-profile (a launcher's device defaults)
    // yields to it.
    const olduvai::app::LayeredConfig lc = olduvai::app::layer_config(
        file_cfg, args.profile, args.default_profile);
    for (const auto& w : lc.warnings) std::fputs(w.c_str(), stderr);
    ps.profile_family = lc.family;
    // Per-key precedence (options_resolve.cpp); game_dir goes through the
    // string mirror.
    ps.game_dir = args.game_dir.string();
    olduvai::app::merge_config(ps, lc.merged);
    if (ps.config_game_dir) args.game_dir = ps.game_dir;
#ifdef OLDUVAI_HAVE_SDL
    // F5 bug-report destination (config-only; $OLDUVAI_BUG_DIR still
    // overrides).  Default without either: <home>/olduvai/bug_reports.
    if (!ps.bug_report_dir.empty())
        olduvai::presentation::set_bug_report_dir(ps.bug_report_dir);
#endif
    if (args.save_config) {
        const olduvai::app::Config out = olduvai::app::config_to_save(
            file_cfg, args.profile, ps, args.game_dir.string());
        if (olduvai::app::save_config_file(out)) {
            std::printf("Saved settings to %s\n",
                        olduvai::app::config_path().c_str());
        }
    }
}

#ifdef OLDUVAI_HAVE_SDL
// The Classic/Enhanced question, once: an auto-discovered install skipped
// the first-run dialog that asks it.
void ask_style_once(const olduvai::app::CliArgs& args,
                    olduvai::app::PlaySettings& ps) {
    // A GUI launch that never answered is asked now.
    if (olduvai::app::launched_from_gui() && !ps.style_answered) {
        // The answer is a ROLE; the session's family decides which
        // profile plays it (presentation/menu/profile_table.hpp).
        const std::string choice = olduvai::app::ask_preset_choice();
        const std::string preset =
            choice.empty()
                ? std::string()
                : std::string(olduvai::presentation::resolve_preset(
                                  ps.profile_family, choice).name);
        if (!preset.empty()) {   // "" = box unavailable; ask again later
            olduvai::app::Config c = olduvai::app::load_config_file();
            olduvai::app::apply_profile(c, preset);
            if (olduvai::app::save_config_file(c)) {
                std::printf("Style choice (%s) saved to %s\n",
                            preset.c_str(),
                            olduvai::app::config_path().c_str());
            }
            olduvai::app::adopt_preset(ps, args.profile, preset);
        }
    } else if (!ps.style_answered) {
        // Terminal launch: the question is GUI-only, so print a pointer
        // instead.
        std::printf("Tip: choose Classic or Enhanced with --profile "
                    "dos|hd (or in Options -> Style; saved for next "
                    "time).\n");
    }
}
#endif

// --play: the question if unanswered, the options validated, the game.
int play_command(const olduvai::app::CliArgs& args,
                 olduvai::app::PlaySettings& ps) {
#ifdef OLDUVAI_HAVE_SDL
    ask_style_once(args, ps);

    // Validate and assemble GameOptions (options_build.cpp).  It never
    // prints: warnings go to stderr here, a validation failure sets the
    // exit code.
    olduvai::presentation::GameOptions go;
    const olduvai::app::BuildOutcome bo =
        olduvai::app::build_game_options(args, ps, go);
    for (const auto& w : bo.warnings)
        std::fprintf(stderr, "%s", w.c_str());
    if (!bo.ok) {
        std::fprintf(stderr, "%s", bo.error.c_str());
        return bo.exit_code;
    }
    // Name the build in every play log: a handheld's olduvai.log is often
    // the only record of which binary actually ran.
    std::fprintf(stderr, "olduvai %s (%s)\n", OLDUVAI_VERSION,
                 olduvai::build_id());
    // Decoders throw std::runtime_error on corrupt or truncated game files,
    // and the audio/asset paths do not catch; report the decoder's message
    // here instead of terminating.
    try {
        return olduvai::presentation::run_game(go);
    } catch (const std::exception& e) {
        std::fprintf(stderr,
                     "olduvai: cannot read the game files in %s\n"
                     "  %s\n"
                     "The file is present but its contents are not "
                     "readable — most likely truncated or corrupt.\n"
                     "Re-copy it from your original media or reinstall.\n",
                     args.game_dir.string().c_str(), e.what());
        return 1;
    }
#else
    std::printf("This build has no presentation layer (SDL2 missing).\n");
    return 1;
#endif
}

// --viewer: the asset viewer.
int viewer_command(const olduvai::app::CliArgs& args) {
#ifdef OLDUVAI_HAVE_SDL
    olduvai::presentation::ViewerOptions vo;
    vo.game_dir = args.game_dir;
    vo.frames = args.viewer_frames;
    vo.screenshot = args.viewer_shot;
    return olduvai::presentation::run_viewer(vo);
#else
    std::printf("This build has no presentation layer (SDL2 missing).\n");
    return 1;
#endif
}

int run(int argc, char** argv) {
    // One-shot migration: earlier versions left a cache directory behind that
    // nothing reads any more (see legacy_cache.hpp).  Silent and best-effort.
    olduvai::app::remove_legacy_cache_dir();

    olduvai::app::CliArgs args;
    olduvai::app::PlaySettings ps;
    {
        const auto pr = olduvai::app::parse_args(argc, argv, args, ps);
        if (pr.show_help) { print_usage(); return 0; }
        if (pr.show_version) {
            std::printf("olduvai %s (%s)\n", OLDUVAI_VERSION,
                        olduvai::build_id());
            return 0;
        }
        if (pr.should_exit) return pr.exit_code;
    }

#ifdef OLDUVAI_HAVE_SDL
    // A GUI launch has no terminal, so with no mode requested, play.  Every
    // standalone verb must be listed here: launched_from_gui is just "no tty on
    // stdin and stderr", so a missing verb becomes --play in any piped or CI
    // shell.
    if (!args.play && !args.viewer && !args.do_list_midi_ports &&
        olduvai::app::launched_from_gui()) {
        args.play = true;
    }
#endif

    apply_settings(args, ps);

#ifdef OLDUVAI_HAVE_SDL
    // Leaf commands; the order matters (audio render, SFX render, then the
    // interactive paths).
    if (!args.render_audio.empty()) return render_audio_command(args, ps);
    if (!args.render_sfx.empty()) return render_sfx_command(args, ps);
#endif

    if (args.do_list_midi_ports) return list_midi_ports_command();

    // ── game directory resolution ────────────────────────────────────────
    // (After the standalone commands that need no game files.)
    resolve_launch_game_dir(args, ps);

    // Detection accepts PREH.SQZ in place of HISTORIK.EXE (GOG / CD releases).
    if (!ensure_launch_game_files(args, ps)) return 1;

    if (args.play) return play_command(args, ps);
    if (args.viewer) return viewer_command(args);

    // No mode and the files were found: say where, then print the help.  No
    // trailing period (a path ending "/" would read "/.").
    std::printf("Game files found in %s\n\n",
                args.game_dir.string().c_str());
    print_usage();
    return 0;
}

}  // namespace

// Anything that escapes run() is reported, not a silent abort.
int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "olduvai: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "olduvai: an unknown error ended the run\n");
    }
    return 1;
}
