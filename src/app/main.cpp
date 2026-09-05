// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Olduvai entry point.  Current state: game-file detection + the M2 asset
// viewer (--viewer).

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
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

#ifdef OLDUVAI_HAVE_SDL
#include "presentation/diag/bug_capture.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/game_app.hpp"
#include "presentation/audio/host_midi.hpp"
#include "presentation/diag/viewer.hpp"
#endif


namespace {

// ── Headless audio render (Phase 1 harness) ──────────────────────────
// Deterministic offline PCM render of a synthetic format-0 MIDI stream
// through a synth backend — the gate for the audio DIP refactor and the
// substrate for the mt32_gm instrument-matching scripts.  Needs no game
// files; data-gated on the backend's own assets (MT-32 ROMs / GM
// SoundFont) → exits 77 (SKIP) when the chosen synth can't load.
//
// Extracted verbatim from main() (§3.10b): a leaf command that only reads
// the parsed CLI state and returns an exit code — main keeps the dispatch,
// not the body.
int render_audio_command(const olduvai::app::CliArgs& args,
                         const olduvai::app::PlaySettings& ps) {
    std::vector<std::uint8_t> smf;
    bool read_failed = false;
    if (std::FILE* mf = std::fopen(args.render_audio.c_str(), "rb")) {
        // Stop AT the short read rather than looping back into fread: a
        // short read is EOF or error, and in both cases another fread is a
        // read on a stream whose position is already spent or
        // indeterminate.  Distinguishing them also matters — without the
        // ferror check an I/O failure produced a TRUNCATED buffer that
        // then went on to be parsed as MIDI.  --render-audio takes a
        // user-supplied path, the surface SECURITY.md calls out, so a
        // partial read must be an error rather than a shorter song.
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
    olduvai::presentation::SdlAudio audio(
        ps.music_device, ps.rom_dir, ps.soundfont, ps.sfx_backend, rate, 0,
        "", /*offline=*/true,
        ps.mt32_model.empty() ? "auto" : ps.mt32_model);
    if (!audio.music_available()) {
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

// ── AdLib SFX render ─────────────────────────────────────────────────
// `--render-sfx <id|all>` renders the OPL sound effects the same way the
// engine plays them, and prints "sha256  frames  id" per effect — or
// writes WAVs with --render-audio-out (as <stem>_<id>.wav for "all").
//
// The music side of this (--render-audio) has existed since the audio
// harness; the SFX side had NO offline path at all, so a change to the
// OPL render or the sample chain could not be compared before and after.
// scripts/metrics/audio_diff.sh is what uses both.
//
// Game-data-gated by construction: the patch bytes are read from the
// user's executable (`install_adlib_sfx_voices`) and this repo ships none.
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
                out = stem + "_" + id + ext;
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

// Print the full usage block to stdout.  Flags mirror the parsing loop in
// parse_args() one-for-one; keep the two in sync when adding options.
void print_usage() {
    std::printf(
        "olduvai " OLDUVAI_VERSION " — native engine recreation of Prehistorik (1991, Titus)\n"
        "\n"
        "Usage:\n"
        "  olduvai [options]\n"
        "\n"
        "Runs game-file detection in game_dir.  With no mode flag it reports\n"
        "whether the required files are present; --play launches the game and\n"
        "--viewer opens the asset browser.  Original Prehistorik game files are\n"
        "required (FILESA.CUR, FILESB.CUR, FILESA.VGA, FILESB.VGA, and\n"
        "HISTORIK.EXE — or PREH.SQZ as the GOG release ships it).  A GOG\n"
        "install root works directly as game_dir (data/PREH is found).\n"
        "\n"
        "General:\n"
        "  -h, --help              Show this help and exit.\n"
        "      --mt32-model M      MT-32 ROM set: auto|cm32l|mt32 (default auto:\n"
        "                          CM-32L when its ROMs are present).\n"
        "      --version           Print version and exit.\n"
        "      --game-dir <dir>    Directory with the game files (default: \".\",\n"
        "                          then the machine's GOG install if present).\n"
        "      --play              Launch the game.\n"
        "      --viewer            Open the asset/image browser.\n"
        "      --level <n>         Sequence position to start at: 0 = intro/\n"
        "                          title, 1-7 = play levels (display numbering),\n"
        "                          8 = win ending.  Default: the intro/title;\n"
        "                          an explicit level jumps straight in.\n"
        "\n"
        "\n"
        "Display:\n"
        "  -f, --fullscreen        Start in desktop-fullscreen (Alt+Enter toggles).\n"
        "      --vsync             Request display vsync (off by default; the\n"
        "                          driver may silently ignore it).\n"
        "      --vga-scan          Classic-mode VGA scanout: re-present the\n"
        "                          held frame every display refresh between\n"
        "                          18.2 Hz ticks (like real VGA scanning VRAM\n"
        "                          at 70 Hz).  DEFAULT ON for classic runs\n"
        "                          (implies vsync there); --no-vga-scan opts\n"
        "                          out.  No effect under enhanced/HD\n"
        "                          (smooth-motion covers it).\n"
        "      --display-mode <m>  Window scaling path: gpu|cpu (default: gpu).\n"
        "                          gpu = GPU (accelerated) scaling; cpu = software\n"
        "                          renderer (escape hatch on some setups).\n"
        "      --transitions <m>   Screen-transition mode: smooth|classic\n"
        "                          (default: smooth).  classic forces smooth-\n"
        "                          motion off; smooth keeps it.  Auto-classic\n"
        "                          under --trace; --replay keeps smooth.\n"
        "      --aspect <m>        Pixel aspect mode: keep|4:3|stretch|widescreen\n"
        "                          (default: keep).  keep = square pixels +\n"
        "                          black bars; 4:3 = CRT-like vertical stretch;\n"
        "                          stretch = fill window, no bars; widescreen\n"
        "                          (enhanced only) peeks adjacent screens into\n"
        "                          the side margins.\n"
        "      --autofire [speed]  Hold the attack key to keep swinging (no\n"
        "                          mashing): slow|medium|fast (bare = fast;\n"
        "                          fast matches the boss-fight feel).  Saved\n"
        "                          to config; --no-autofire turns it off.\n"
        "\n"
        "Audio:\n"
        "      --music-device <d>  Music backend: auto|mt32-builtin|gm-builtin|opl|\n"
        "                          none|host-midi|gm-host (default: auto).  host-midi\n"
        "                          (alias mt32) streams raw MT-32 MIDI to a real MIDI\n"
        "                          OUT port; gm-host streams GM-translated MIDI (for\n"
        "                          the Windows GS Wavetable synth).  On Windows, auto\n"
        "                          falls back to gm-host before OPL when no MT-32\n"
        "                          ROMs or SoundFont are found.\n"
        "      --midi-port <name>  Host MIDI OUT port for host-midi / gm-host\n"
        "                          (default: first port, preferring MT-32/MUNT).\n"
        "      --list-midi-ports   List available MIDI OUT ports and exit.\n"
        "      --sfx-backend <b>   SFX backend: auto|opl|sb-dac|mt32-sfx|gm-sfx|midi\n"
        "                          (default: auto — pairs to the music device).\n"
        "      --rom-dir <dir>     MT-32/CM-32L ROM directory (mt32-builtin).\n"
        "      --soundfont <file>  SoundFont (.sf2) for gm-builtin.\n"
        "      --audio-rate <hz>   Mixer/synth sample rate (default: device\n"
        "                          preference, else 48000).\n"
        "      --audio-buffer <n>  Mixer buffer in sample frames, power of two\n"
        "                          (default: 2048).\n"
        "\n"
        "Enhanced / HD:\n"
        "      --enhanced          Enable enhanced mode (all effects).\n"
        "      --enhance <list>    Deprecated: the per-feature names no longer\n"
        "                          select a subset.  Any listed name simply\n"
        "                          turns enhanced mode on.\n"
        "      --hd-profile <p>    HD upscaler profile: native|retro|smooth|\n"
        "                          eagle|xbr|mmpx|omniscale (default: omniscale).\n"
        "      --render-scale <n>  Integer render scale: 2 or 4 (default: 4).\n"
        "      --hd-font <f>       HD vector text face: freckle|noto\n"
        "                          (default: freckle).  Needs enhanced mode.\n"
        "      --banner-fx <e>     Enhanced banner colour effect: caveman|fire|\n"
        "                          rainbow|gold|pulse (default: caveman).\n"
        "      --window <WxH>      Force window pixel size, e.g. 1680x720 (~21:9)\n"
        "                          to simulate an ultrawide widescreen viewport.\n"
        "                          Use with --aspect widescreen.\n"
        "      --start-screen <n>  DEBUG: enter a surface level at screen n (e.g.\n"
        "                          the last screen) instead of 0.  Clamped to the\n"
        "                          level's screen count.\n"
        "\n"
        "Config:\n"
        "      --profile <name>    Built-in profile: dos|hd.  Overrides the\n"
        "                          saved config (CLI flags still win): dos =\n"
        "                          byte-faithful; hd = full enhanced +\n"
        "                          widescreen peeks (add --aspect 4:3 for the\n"
        "                          classic CRT look).\n"
        "      --no-config         Ignore the saved config file for this run.\n"
        "      --save-config       Persist the effective CLI settings to the config\n"
        "                          file, then continue.\n"
        "\n"
        "Dev / Headless:\n"
        "      --replay <file>     Replay recorded inputs (with --play).\n"
        "      --trace <file>      Write a per-frame trace (with --play).\n"
        "      --record-inputs <file>  Write live inputs as replay-schema JSONL\n"
        "                              (with --play; re-playable via --replay).\n"
        "      --cheats            Enable test cheats: number keys 1-6 grant a\n"
        "                          power-up (1=Spring..6=Axe). Off during replay.\n"
        "      --god               999 energy + never out of lives; normal\n"
        "                          ghost/respawn on a fall (debug).\n"
        "                          Off during replay.\n"
        "      --debug-collision   Tint solid collision cells (dev overlay).\n"
        "      --debug-entities    Box every active entity (dev overlay).\n"
        "      --debug-perf        Show FPS + frame time (dev overlay).\n"
        "      --play-frames <n>   Run the game for n frames then exit (default: -1,\n"
        "                          unlimited).\n"
        "      --play-shot <file>  Save a screenshot of the game to <file>.\n"
        "      --play-shot-frame <n>   Frame at which to capture --play-shot\n"
        "                              (default: 1).\n"
        "      --viewer-frames <n> Run the viewer for n frames then exit (default: -1,\n"
        "                          unlimited).\n"
        "      --viewer-shot <file>    Save a screenshot of the viewer to <file>.\n"
        "      --render-audio <smf>    Render a MIDI file through the synth and\n"
        "                              print a digest, or write --render-audio-out.\n"
        "      --render-audio-out <wav>    WAV destination for the two render\n"
        "                                  commands (default: print a digest).\n"
        "      --render-audio-secs <s>     Render duration (default: 2).\n"
        "      --render-sfx <id|all>   Render an AdLib sound effect the way the\n"
        "                              engine plays it; needs --game-dir.\n");
}

}  // namespace

int main(int argc, char** argv) {
    // One-shot migration: earlier versions left a cache directory behind that
    // nothing reads any more (see legacy_cache.hpp).  Silent and best-effort.
    olduvai::app::remove_legacy_cache_dir();

    olduvai::app::CliArgs args;
    olduvai::app::PlaySettings ps;
    {
        const auto pr = olduvai::app::parse_args(argc, argv, args, ps);
        if (pr.show_help) { print_usage(); return 0; }
        if (pr.show_version) {
            std::printf("olduvai %s\n", OLDUVAI_VERSION);
            return 0;
        }
        if (pr.should_exit) return pr.exit_code;
    }

#ifdef OLDUVAI_HAVE_SDL
    // A GUI launch (Finder / file-manager double-click) has no terminal:
    // the no-mode detection report would print to nowhere and the app
    // would appear to do nothing.  With no mode requested, default to
    // playing — the whole point of double-clicking the app.
    // INVARIANT: every standalone verb must be listed here.  launched_from_gui
    // is isatty(stdin)==0 && isatty(stderr)==0, not Finder detection — so a
    // verb missing from this list silently becomes --play in any piped, CI or
    // redirected shell.  A removed flag is a compile error; a FORGOTTEN one is
    // a hang.
    if (!args.play && !args.viewer && !args.do_list_midi_ports &&
        olduvai::app::launched_from_gui()) {
        args.play = true;
    }
#endif

    {
        olduvai::app::Config merged;
        if (!args.no_config) {
            for (const auto& [k, v] : olduvai::app::load_config_file()) {
                merged[k] = v;
            }
        }
        // An explicit --profile states INTENT — it must beat the saved
        // config, or "--profile dos" silently stays enhanced under a saved
        // hd play.json (the trap from the 2026-07-04 CLI review).  New
        // precedence: defaults < config < profile < CLI flags.
        if (!args.profile.empty()) {
            // Includes the dos-side clears (see apply_profile).
            olduvai::app::apply_profile(merged, args.profile);
        }
        // Pure per-key precedence resolution (options_resolve.cpp, CC3
        // phase 3 — unit-tested precedence matrix).  game_dir bridges
        // through the string mirror.
        ps.game_dir = args.game_dir.string();
        olduvai::app::merge_config(ps, merged);
        if (ps.config_game_dir) args.game_dir = ps.game_dir;
#ifdef OLDUVAI_HAVE_SDL
        // F5 bug-report destination (config-only; $OLDUVAI_BUG_DIR still
        // overrides).  Default without either: <home>/olduvai/bug_reports.
        if (!ps.bug_report_dir.empty())
            olduvai::presentation::set_bug_report_dir(ps.bug_report_dir);
#endif
        if (args.save_config) {
            olduvai::app::Config out = merged;
            if (ps.cli.enhanced) out["enhanced"] = ps.enhanced ? "true" : "false";
            if (ps.cli.enhanced && !ps.enhance_list.empty())
                out["enhance"] = ps.enhance_list;
            if (ps.cli.hd) out["hd_profile"] = ps.hd_profile;
            if (ps.cli.scale) out["render_scale"] = std::to_string(ps.render_scale);
            if (ps.cli.aspect) out["aspect"] = ps.aspect;
            if (ps.cli.game_dir) out["game_dir"] = args.game_dir.string();
            if (olduvai::app::save_config_file(out)) {
                std::printf("Saved settings to %s\n",
                            olduvai::app::config_path().c_str());
            }
        }
    }

#ifdef OLDUVAI_HAVE_SDL
    // Leaf commands, extracted verbatim above (§3.10b): main keeps the
    // dispatch and the ordering (audio render before SFX render before the
    // interactive paths), not their bodies.
    if (!args.render_audio.empty())
        return render_audio_command(args, ps);

    if (!args.render_sfx.empty()) return render_sfx_command(args, ps);
#endif

    // ── MIDI port enumeration ────────────────────────────────────────────
    // Standalone command: list the host MIDI OUT ports
    // the --music-device host-midi path can target, then exit.  Needs no game
    // files.  When this build has no RtMidi (Linux without ALSA, or the option
    // off), report the feature as unavailable rather than crashing.
    if (args.do_list_midi_ports) {
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

    // ── game directory resolution ────────────────────────────────────────
    // (After the standalone commands that need no game files.)  Map a GOG
    // install root (game files under data/PREH) to the directory actually
    // holding the files.  A no-op for plain directories.  Done after
    // --save-config so the config keeps the path the user gave.
    args.game_dir = olduvai::prepare::resolve_game_dir(args.game_dir);

    // With NO configured directory (no --game-dir, none in the config) and
    // no game files where we stand, probe the machine's GOG install —
    // a fresh GOG copy then plays with plain `olduvai --play`.  An explicit
    // directory, even a wrong one, is always respected (clear error beats
    // silently playing from somewhere else).
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

    // ── Cache commands ───────────────────────────────────────────────────
    // These run without launching the game (and exit when done).  Purge needs
    // no game files; prepare/verify detect+checksum the fileset themselves so
    // they can report missing/zero-byte files with a clear message.



    // Detection accepts PREH.SQZ in place of HISTORIK.EXE (GOG / CD
    // releases) — the manual per-name loop would wrongly report those
    // copies as incomplete.
    {
        olduvai::prepare::GameFiles gf =
            olduvai::prepare::detect_game_files(args.game_dir);
        if (!gf.complete()) {
            // Always emit the report to the console — the fallback trail
            // for terminals, logs and debugging even when the GUI dialog
            // below handles the user-facing side.
            std::printf("Olduvai needs your original Prehistorik game files.\n");
            std::printf("Missing in %s:\n%s",
                        args.game_dir.string().c_str(), gf.problems().c_str());
            std::printf("Copy them there (or pass --game-dir) and run again.\n");
            std::fflush(stdout);
#ifdef OLDUVAI_HAVE_SDL
            // GUI session: additionally raise the first-run dialog (folder
            // picker + GOG link).  A validated pick is persisted to
            // play.json and adopted for this run.
            if (olduvai::app::launched_from_gui()) {
                std::string chosen_preset;
                const auto picked = olduvai::app::first_run_dialog(
                    args.game_dir, gf.problems(), &chosen_preset);
                if (!picked) return 1;                 // user quit
                args.game_dir = *picked;
                // Adopt the dialog's presentation choice for THIS session
                // too — it is already persisted for the next launch, but the
                // config merge above ran before the dialog existed (the
                // "chose Enhanced HD, got classic DOS" first-run report).
                olduvai::app::adopt_preset(ps, args.profile, chosen_preset);
                ps.style_answered = true;   // the dialog always asks
                gf = olduvai::prepare::detect_game_files(args.game_dir);
                if (gf.complete()) {
                    std::printf("Using game folder %s (saved to settings).\n",
                                args.game_dir.string().c_str());
                }
            }
#endif
            if (!gf.complete()) return 1;
        }
    }

    if (args.play) {
#ifdef OLDUVAI_HAVE_SDL
        // Auto-discovered installs (a GOG copy found without the missing-
        // files dialog, or a pre-seeded game_dir) skip the dialog and with
        // it the one-time Classic/Enhanced question — ask it now on a GUI
        // launch whose config never answered it (2026-07-19 Windows field
        // report: GOG auto-find → silent classic DOS, no question asked).
        if (olduvai::app::launched_from_gui() && !ps.style_answered) {
            const std::string preset = olduvai::app::ask_preset_choice();
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
            // Terminal launch: the question box is GUI-only, so a config
            // that never answered it silently defaults to classic DOS —
            // leave a one-line pointer instead.
            std::printf("Tip: choose Classic or Enhanced with --profile "
                        "dos|hd (or in Options -> Style; saved for next "
                        "time).\n");
        }

        // Validate + assemble the GameOptions (app/options_build.cpp): the
        // --enhance parse, the six tuning-flag validations, the cross-field
        // derivations, and the field-by-field copy — the untested other half
        // of the parse_args/merge_config decomposition, now unit-testable
        // (audit A2).  The callee never prints: warnings go to stderr on the
        // success path, and a validation failure sets the exit code.
        olduvai::presentation::GameOptions go;
        const olduvai::app::BuildOutcome bo =
            olduvai::app::build_game_options(args, ps, go);
        for (const auto& w : bo.warnings)
            std::fprintf(stderr, "%s", w.c_str());
        if (!bo.ok) {
            std::fprintf(stderr, "%s", bo.error.c_str());
            return bo.exit_code;
        }
        // Any decoder can throw on a corrupt or truncated game file
        // (CurError, LzssError, Pc1Error, DurError, SqzError, ExeTableError —
        // all std::runtime_error).  load_level catches its own, but the audio
        // and asset paths do not, so a truncated archive reached std::terminate
        // and the user saw only "libc++abi: terminating due to uncaught
        // exception".  The decoders' own messages are good ("archive
        // truncated: data for entry BONUS.VOC") — they just needed to reach
        // stderr instead of an abort.  Catch here, at the boundary, rather
        // than threading error returns through every asset call.
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

    if (args.viewer) {
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

    std::printf("Game files found. Engine not yet implemented — "
                "run with --viewer to browse the game's images.\n");
    return 0;
}
