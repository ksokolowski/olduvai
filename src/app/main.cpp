// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Olduvai entry point.  Current state: game-file detection + the M2 asset
// viewer (--viewer).

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "config.hpp"
#ifdef OLDUVAI_HAVE_SDL
#include "first_run.hpp"
#endif
#include "enhance/upscale.hpp"
#include "prepare/cache_paths.hpp"
#include "prepare/game_files.hpp"
#include "prepare/prepare.hpp"
#include "formats/cur.hpp"
#include "formats/voc.hpp"
#include "presentation/hd_sfx.hpp"
#include "presentation/wav_io.hpp"

#ifdef OLDUVAI_HAVE_SDL
#include "presentation/bug_capture.hpp"
#include "presentation/game_app.hpp"
#include "presentation/host_midi.hpp"
#include "presentation/viewer.hpp"
#endif

namespace fs = std::filesystem;

namespace {

// Print the full usage block to stdout.  Flags mirror the parsing loop in
// main() one-for-one; keep the two in sync when adding options.
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
        "Cache (first-run prepare-and-cache pipeline):\n"
        "      --prepare           Build the local cache for the detected game\n"
        "                          files, then exit.  No game files are copied —\n"
        "                          only checksums + decoded data live in the cache.\n"
        "      --verify-cache      Report whether the cache is present/valid/stale\n"
        "                          for the detected game files, then exit.\n"
        "      --purge-cache       Delete the entire local cache, then exit.\n"
        "                          The engine re-prepares on the next run.\n"
        "      --decode-sfx        Decode the sound-effect samples to WAV files\n"
        "                          in the cache (hd_sfx_src/) for the optional\n"
        "                          HD SFX bake (scripts/bake_hd_sfx.py), then exit.\n"
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
        "      --enhanced          Enable the full enhanced-feature bundle.\n"
        "      --enhance <list>    Enable a comma-separated feature subset. Known:\n"
        "                          smooth-motion, cinematic-cue, hud-overlay,\n"
        "                          fluid-bubbles, secret-slide, descent-pan, hd-text.\n"
        "      --hd-profile <p>    HD upscaler profile: native|retro|smooth|\n"
        "                          eagle|xbr|mmpx|omniscale (default: omniscale).\n"
        "      --render-scale <n>  Integer render scale: 2 or 4 (default: 4).\n"
        "      --hd-font <f>       HD vector text face: freckle|noto\n"
        "                          (default: freckle).  Needs hd-text + HD.\n"
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
        "      --profile <name>    Built-in profile: dos|hd|hd-43.  Overrides the\n"
        "                          saved config (CLI flags still win): dos =\n"
        "                          byte-faithful; hd = full enhanced +\n"
        "                          widescreen peeks; hd-43 = full enhanced at\n"
        "                          the CRT 4:3 look.\n"
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
        "      --viewer-shot <file>    Save a screenshot of the viewer to <file>.\n");
}

}  // namespace

int main(int argc, char** argv) {
    fs::path game_dir = ".";
    bool viewer = false;
    bool play = false;
    bool do_prepare = false;
    bool do_decode_sfx = false;
    bool do_verify_cache = false;
    bool do_purge_cache = false;
    bool do_list_midi_ports = false;
    int play_level = -1;   // -1 = not given → attract (sequence position 0)
    bool play_enhanced = false;
    std::string play_enhance_list;   // --enhance a,b,c  (granular feature subset)
    std::string play_hd_profile;
    int play_render_scale = 4;   // default 4 → omniscale 1280x800 in --enhanced
    std::string play_music_device = "auto";
    // Gamepad mapping (config-only: play.json pad_* keys, SDL button names).
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    std::string play_midi_port;
    std::string play_rom_dir;
    std::string play_soundfont;
    std::string play_sfx_backend = "auto";  // pair to music device (Python-style)
    std::string play_replay;
    std::string play_trace;
    std::string play_record_inputs;
    bool play_cheats = false;
    std::string play_autofire = "off";
    bool cli_autofire = false;
    bool play_god = false;
    bool play_debug_collision = false;
    bool play_debug_entities = false;
    bool play_debug_perf = false;
    bool play_vsync = false;
    // VGA hold-frame scanout: DEFAULT ON for the classic experience (owner
    // decision 2026-07-04 after playtest of the flag).  Inert under the HD
    // substrate (smooth-motion covers enhanced); --no-vga-scan opts out.
    bool play_vga_scan = true;
    bool cli_vga_scan = false;
    bool play_fullscreen = false;
    std::string play_display_mode = "gpu";   // gpu (GPU scale) | cpu (software)
    int play_audio_rate = 0;                  // 0 = device default / auto
    int play_audio_buffer = 0;                // 0 = 2048-frame default
    std::string play_transitions = "smooth";  // smooth | classic
    std::string play_aspect = "keep";         // keep | 4:3 | stretch
    std::string play_hd_font = "freckle";     // freckle | noto
    std::string play_banner_fx = "caveman";   // caveman|fire|rainbow|gold|pulse
    int play_window_w = 0, play_window_h = 0;  // --window WxH (0 = auto)
    int play_start_screen = 0;                 // --start-screen N (debug)
    std::string profile;
    bool no_config = false;
    bool save_config = false;
    bool cli_enhanced = false, cli_hd = false, cli_scale = false,
         cli_game_dir = false, cli_aspect = false;
    bool config_game_dir = false;   // game_dir came from the config file
    int play_frames = -1;
    std::string play_shot;
    int play_shot_frame = 1;
    int viewer_frames = -1;
    std::string viewer_shot;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        bool known = true;
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--version") {
            std::printf("olduvai %s\n", OLDUVAI_VERSION);
            return 0;
        } else if (arg == "--game-dir" && i + 1 < argc) {
            game_dir = argv[++i];
            cli_game_dir = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
            if (profile != "dos" && profile != "hd" && profile != "hd-43") {
                std::fprintf(stderr,
                    "olduvai: --profile must be 'dos', 'hd', or 'hd-43' "
                    "(got '%s')\n", profile.c_str());
                return 2;
            }
        } else if (arg == "--no-config") {
            no_config = true;
        } else if (arg == "--save-config") {
            save_config = true;
        } else if (arg == "--viewer") {
            viewer = true;
        } else if (arg == "--play") {
            play = true;
        } else if (arg == "--prepare") {
            do_prepare = true;
        } else if (arg == "--decode-sfx") {
            do_decode_sfx = true;
        } else if (arg == "--verify-cache") {
            do_verify_cache = true;
        } else if (arg == "--purge-cache") {
            do_purge_cache = true;
        } else if (arg == "--level" && i + 1 < argc) {
            play_level = std::atoi(argv[++i]);
            if (play_level < 0 || play_level > 8) {
                std::fprintf(stderr,
                    "olduvai: --level must be 0 (intro/title), 1-7 (play "
                    "levels, display numbering) or 8 (win ending) "
                    "(got '%s')\n", argv[i]);
                return 2;
            }
        } else if (arg == "--enhanced") {
            play_enhanced = true;
            cli_enhanced = true;
        } else if (arg == "--enhance" && i + 1 < argc) {
            play_enhance_list = argv[++i];
            cli_enhanced = true;   // an explicit subset also overrides config
        } else if (arg == "--hd-profile" && i + 1 < argc) {
            play_hd_profile = argv[++i];
            cli_hd = true;
        } else if (arg == "--music-device" && i + 1 < argc) {
            play_music_device = argv[++i];
        } else if (arg == "--midi-port" && i + 1 < argc) {
            play_midi_port = argv[++i];
        } else if (arg == "--list-midi-ports") {
            do_list_midi_ports = true;
        } else if (arg == "--replay" && i + 1 < argc) {
            play_replay = argv[++i];
        } else if (arg == "--trace" && i + 1 < argc) {
            play_trace = argv[++i];
        } else if (arg == "--record-inputs" && i + 1 < argc) {
            play_record_inputs = argv[++i];
        } else if (arg == "--cheats") {
            play_cheats = true;
        } else if (arg == "--god") {
            play_god = true;
        } else if (arg == "--autofire") {
            play_autofire = "fast";
            cli_autofire = true;
            if (i + 1 < argc) {
                const std::string s = argv[i + 1];
                if (s == "slow" || s == "medium" || s == "fast") {
                    play_autofire = s;
                    ++i;
                }
            }
        } else if (arg == "--no-autofire") {
            play_autofire = "off";
            cli_autofire = true;
        } else if (arg == "--debug-collision") {
            play_debug_collision = true;
        } else if (arg == "--debug-entities") {
            play_debug_entities = true;
        } else if (arg == "--debug-perf") {
            play_debug_perf = true;
        } else if (arg == "--vga-scan") {
            play_vga_scan = true;
            cli_vga_scan = true;
        } else if (arg == "--no-vga-scan") {
            play_vga_scan = false;
            cli_vga_scan = true;
        } else if (arg == "--vsync") {
            play_vsync = true;
        } else if (arg == "-f" || arg == "--fullscreen") {
            play_fullscreen = true;
        } else if (arg == "--display-mode" && i + 1 < argc) {
            play_display_mode = argv[++i];
        } else if (arg == "--audio-rate" && i + 1 < argc) {
            play_audio_rate = std::atoi(argv[++i]);
        } else if (arg == "--audio-buffer" && i + 1 < argc) {
            play_audio_buffer = std::atoi(argv[++i]);
        } else if (arg == "--transitions" && i + 1 < argc) {
            play_transitions = argv[++i];
        } else if (arg == "--aspect" && i + 1 < argc) {
            play_aspect = argv[++i];
            cli_aspect = true;
        } else if (arg == "--hd-font" && i + 1 < argc) {
            play_hd_font = argv[++i];
        } else if (arg == "--banner-fx" && i + 1 < argc) {
            play_banner_fx = argv[++i];
        } else if (arg == "--start-screen" && i + 1 < argc) {
            play_start_screen = std::atoi(argv[++i]);
        } else if (arg == "--window" && i + 1 < argc) {
            const std::string wh = argv[++i];
            const auto xpos = wh.find_first_of("xX");
            if (xpos != std::string::npos) {
                play_window_w = std::atoi(wh.substr(0, xpos).c_str());
                play_window_h = std::atoi(wh.substr(xpos + 1).c_str());
            }
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            play_rom_dir = argv[++i];
        } else if (arg == "--soundfont" && i + 1 < argc) {
            play_soundfont = argv[++i];
        } else if (arg == "--sfx-backend" && i + 1 < argc) {
            play_sfx_backend = argv[++i];
        } else if (arg == "--render-scale" && i + 1 < argc) {
            play_render_scale = std::atoi(argv[++i]);
            cli_scale = true;
        } else if (arg == "--play-frames" && i + 1 < argc) {
            play_frames = std::atoi(argv[++i]);
        } else if (arg == "--play-shot" && i + 1 < argc) {
            play_shot = argv[++i];
        } else if (arg == "--play-shot-frame" && i + 1 < argc) {
            play_shot_frame = std::atoi(argv[++i]);
        } else if (arg == "--viewer-frames" && i + 1 < argc) {
            viewer_frames = std::atoi(argv[++i]);
        } else if (arg == "--viewer-shot" && i + 1 < argc) {
            viewer_shot = argv[++i];
        } else {
            known = false;
        }
        if (!known) {
            std::fprintf(stderr, "olduvai: unrecognized argument '%s'\n",
                         arg.c_str());
            std::fprintf(stderr, "Try 'olduvai --help' for usage.\n");
            return 2;
        }
    }

#ifdef OLDUVAI_HAVE_SDL
    // A GUI launch (Finder / file-manager double-click) has no terminal:
    // the no-mode detection report would print to nowhere and the app
    // would appear to do nothing.  With no mode requested, default to
    // playing — the whole point of double-clicking the app.
    if (!play && !viewer && !do_prepare && !do_decode_sfx &&
        !do_verify_cache && !do_purge_cache && !do_list_midi_ports &&
        olduvai::app::launched_from_gui()) {
        play = true;
    }
#endif

    bool style_answered = false;   // config/profile ever chose Classic/HD?
    {
        olduvai::app::Config merged;
        if (!no_config) {
            for (const auto& [k, v] : olduvai::app::load_config_file()) {
                merged[k] = v;
            }
        }
        // An explicit --profile states INTENT — it must beat the saved
        // config, or "--profile dos" silently stays enhanced under a saved
        // hd play.json (the trap from the 2026-07-04 CLI review).  New
        // precedence: defaults < config < profile < CLI flags.
        if (!profile.empty()) {
            // Includes the dos-side clears (see apply_profile).
            olduvai::app::apply_profile(merged, profile);
        }
        // The one-time Classic/Enhanced question is considered answered once
        // any source (saved config, profile, an explicit CLI flag) states
        // the master flag.
        style_answered = merged.count("enhanced") != 0 || cli_enhanced;
        if (auto it = merged.find("vga_scan");
            it != merged.end() && !cli_vga_scan) {
            play_vga_scan = it->second == "true";
        }
        if (auto it = merged.find("autofire");
            it != merged.end() && !cli_autofire) {
            play_autofire = it->second;
        }
        if (auto it = merged.find("enhanced");
            it != merged.end() && !cli_enhanced) {
            play_enhanced = it->second == "true";
        }
        if (auto it = merged.find("enhance");
            it != merged.end() && !cli_enhanced && play_enhance_list.empty()) {
            play_enhance_list = it->second;
        }
        if (auto it = merged.find("hd_profile");
            it != merged.end() && !cli_hd) {
            play_hd_profile = it->second;
        }
        if (auto it = merged.find("render_scale");
            it != merged.end() && !cli_scale) {
            play_render_scale = std::atoi(it->second.c_str());
        }
        if (auto it = merged.find("game_dir");
            it != merged.end() && !cli_game_dir) {
            game_dir = it->second;
            config_game_dir = true;
        }
        // Audio keys (CLI defaults are sentinels; config fills them).
        // Gamepad mapping keys (config-only; SDL button names).
        if (auto it = merged.find("pad_jump"); it != merged.end())
            pad_jump = it->second;
        if (auto it = merged.find("pad_attack"); it != merged.end())
            pad_attack = it->second;
        if (auto it = merged.find("pad_pause"); it != merged.end())
            pad_pause = it->second;
        if (auto it = merged.find("pad_confirm"); it != merged.end())
            pad_confirm = it->second;
        if (auto it = merged.find("pad_back"); it != merged.end())
            pad_back = it->second;
        if (auto it = merged.find("pad_deadzone"); it != merged.end())
            pad_deadzone = std::atoi(it->second.c_str());
        if (auto it = merged.find("music_device");
            it != merged.end() && play_music_device == "auto") {
            play_music_device = it->second;
        }
        if (auto it = merged.find("rom_dir");
            it != merged.end() && play_rom_dir.empty()) {
            play_rom_dir = it->second;
        }
        if (auto it = merged.find("soundfont");
            it != merged.end() && play_soundfont.empty()) {
            play_soundfont = it->second;
        }
        if (auto it = merged.find("sfx_backend");
            it != merged.end() && play_sfx_backend == "auto") {
            play_sfx_backend = it->second;
        }
        // Tuning keys (sentinel defaults; config fills them when the CLI left
        // the default).  Read-only from config, like the audio keys above —
        // bool flags can't tell "default off" from "explicitly off", so they
        // (vsync/fullscreen) are CLI-only by design.
        if (auto it = merged.find("display_mode");
            it != merged.end() && play_display_mode == "gpu") {
            play_display_mode = it->second;
        }
        if (auto it = merged.find("audio_rate");
            it != merged.end() && play_audio_rate == 0) {
            play_audio_rate = std::atoi(it->second.c_str());
        }
        if (auto it = merged.find("audio_buffer");
            it != merged.end() && play_audio_buffer == 0) {
            play_audio_buffer = std::atoi(it->second.c_str());
        }
        if (auto it = merged.find("transitions");
            it != merged.end() && play_transitions == "smooth") {
            play_transitions = it->second;
        }
        if (auto it = merged.find("aspect");
            it != merged.end() && !cli_aspect) {
            // Guard on the FLAG, not the sentinel value: an explicit
            // "--aspect keep" must beat a saved widescreen config.
            play_aspect = it->second;
        }
        if (auto it = merged.find("hd_font");
            it != merged.end() && play_hd_font == "freckle") {
            play_hd_font = it->second;
        }
        if (auto it = merged.find("banner_fx");
            it != merged.end() && play_banner_fx == "caveman") {
            play_banner_fx = it->second;
        }
#ifdef OLDUVAI_HAVE_SDL
        // F5 bug-report destination (config-only; $OLDUVAI_BUG_DIR still
        // overrides).  Default without either: <home>/olduvai/bug_reports.
        if (auto it = merged.find("bug_report_dir");
            it != merged.end() && !it->second.empty()) {
            olduvai::presentation::set_bug_report_dir(it->second);
        }
#endif
        if (save_config) {
            olduvai::app::Config out = merged;
            if (cli_enhanced) out["enhanced"] = play_enhanced ? "true" : "false";
            if (cli_enhanced && !play_enhance_list.empty())
                out["enhance"] = play_enhance_list;
            if (cli_hd) out["hd_profile"] = play_hd_profile;
            if (cli_scale) out["render_scale"] = std::to_string(play_render_scale);
            if (cli_aspect) out["aspect"] = play_aspect;
            if (cli_game_dir) out["game_dir"] = game_dir.string();
            if (olduvai::app::save_config_file(out)) {
                std::printf("Saved settings to %s\n",
                            olduvai::app::config_path().c_str());
            }
        }
    }

    // ── MIDI port enumeration ────────────────────────────────────────────
    // Standalone command (like --verify-cache): list the host MIDI OUT ports
    // the --music-device host-midi path can target, then exit.  Needs no game
    // files.  When this build has no RtMidi (Linux without ALSA, or the option
    // off), report the feature as unavailable rather than crashing.
    if (do_list_midi_ports) {
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
    game_dir = olduvai::prepare::resolve_game_dir(game_dir);

    // With NO configured directory (no --game-dir, none in the config) and
    // no game files where we stand, probe the machine's GOG install —
    // a fresh GOG copy then plays with plain `olduvai --play`.  An explicit
    // directory, even a wrong one, is always respected (clear error beats
    // silently playing from somewhere else).
    if (!cli_game_dir && !config_game_dir &&
        !olduvai::prepare::detect_game_files(game_dir).complete()) {
        for (const auto& cand :
             olduvai::prepare::default_game_dir_candidates()) {
            const olduvai::prepare::GameFiles gf =
                olduvai::prepare::detect_game_files(cand);
            if (gf.complete()) {
                game_dir = gf.dir;   // detection already resolved data/PREH
                std::printf("Using game files found at %s\n",
                            game_dir.string().c_str());
                break;
            }
        }
    }

    // ── Cache commands ───────────────────────────────────────────────────
    // These run without launching the game (and exit when done).  Purge needs
    // no game files; prepare/verify detect+checksum the fileset themselves so
    // they can report missing/zero-byte files with a clear message.
    if (do_purge_cache) {
        const std::string root = olduvai::prepare::cache_root().string();
        if (olduvai::prepare::purge_cache()) {
            std::printf("Purged cache at %s\n", root.c_str());
            return 0;
        }
        std::fprintf(stderr, "olduvai: failed to purge cache at %s\n",
                     root.c_str());
        return 1;
    }

    if (do_decode_sfx) {
        // Decode the game's digital sound effects to WAV in the user's cache
        // (hd_sfx_src/<digest>.wav) so the optional offline HD SFX bake
        // (scripts/bake_hd_sfx.py) can enhance them into hd_sfx/.  Decoded
        // data derived from the user's own files, in the user's cache.
        namespace fs2 = std::filesystem;
        const auto slurp = [](const fs2::path& p) {
            std::vector<std::uint8_t> d;
            std::FILE* f = std::fopen(p.string().c_str(), "rb");
            if (f == nullptr) return d;
            std::uint8_t buf[65536];
            std::size_t got;
            while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0)
                d.insert(d.end(), buf, buf + got);
            std::fclose(f);
            return d;
        };
        const fs2::path out_dir = olduvai::presentation::hd_sfx_src_dir();
        if (!olduvai::prepare::ensure_cache_dir(out_dir)) {
            std::fprintf(stderr, "olduvai: cannot create %s\n",
                         out_dir.string().c_str());
            return 1;
        }
        int exported = 0;
        for (const char* archive :
             {"FILESA.CUR", "FILESB.CUR", "FILESA.VGA", "FILESB.VGA"}) {
            const auto bytes = slurp(game_dir / archive);
            if (bytes.empty()) continue;
            try {
                const olduvai::formats::CurArchive ar(bytes);
                for (const auto& entry : ar.entries()) {
                    if (entry.name.size() < 4 ||
                        entry.name.substr(entry.name.size() - 4) != ".VOC") {
                        continue;
                    }
                    const auto voc = olduvai::formats::parse_voc(entry.data);
                    const auto* audio = voc.audio();
                    if (audio == nullptr || audio->data.empty() ||
                        audio->sample_rate <= 0) {
                        continue;
                    }
                    std::vector<std::int16_t> pcm(audio->data.size());
                    for (std::size_t i = 0; i < pcm.size(); ++i) {
                        pcm[i] = static_cast<std::int16_t>(
                            (audio->data[i] - 128) << 8);
                    }
                    const fs2::path out =
                        out_dir / (olduvai::presentation::sfx_digest_hex(
                                       audio->data) +
                                   ".wav");
                    if (olduvai::presentation::write_wav16(
                            out, pcm, audio->sample_rate, 1)) {
                        std::printf("decoded %-12s -> %s\n",
                                    entry.name.c_str(),
                                    out.string().c_str());
                        ++exported;
                    }
                }
            } catch (const std::exception&) {
                continue;   // unreadable archive: the play path reports it
            }
        }
        if (exported == 0) {
            std::fprintf(stderr,
                         "olduvai: no sound-effect samples found in %s\n",
                         game_dir.string().c_str());
            return 1;
        }
        std::printf(
            "Decoded %d sample(s).  Optional next step:\n"
            "  python3 scripts/bake_hd_sfx.py   (writes the enhanced set to "
            "hd_sfx/)\n",
            exported);
        return 0;
    }

    if (do_verify_cache || do_prepare) {
        const olduvai::prepare::GameFiles gf =
            olduvai::prepare::detect_game_files(game_dir);
        if (!gf.complete()) {
            std::fprintf(stderr,
                "olduvai: cannot %s — game files incomplete in %s:\n%s",
                do_prepare ? "prepare cache" : "verify cache",
                game_dir.string().c_str(), gf.problems().c_str());
            return 1;
        }
        if (do_verify_cache) {
            const olduvai::prepare::CacheStatus st =
                olduvai::prepare::inspect_cache(gf);
            const char* word = "missing";
            int rc = 1;
            switch (st.state) {
                case olduvai::prepare::CacheState::kValid:
                    word = "valid"; rc = 0; break;
                case olduvai::prepare::CacheState::kStale:
                    word = "stale"; rc = 1; break;
                case olduvai::prepare::CacheState::kMissing:
                    word = "missing"; rc = 1; break;
                case olduvai::prepare::CacheState::kNoFiles:
                    word = "no-files"; rc = 1; break;
            }
            std::printf("Cache: %s\n", word);
            std::printf("  key:    %s\n", st.key.c_str());
            std::printf("  bucket: %s\n", st.bucket.string().c_str());
            std::printf("  %s\n", st.message.c_str());
            return rc;
        }
        // do_prepare: force a (re)build.
        return olduvai::prepare::run_prepare(gf, /*verbose=*/true) ? 0 : 1;
    }

    // Fold a first-run presentation choice into THIS session's locals — the
    // saved config only helps the NEXT launch (the config merge above ran
    // before any first-run UI).  cli_*/--profile keep the documented
    // precedence: an explicit flag or profile always wins.
    auto adopt_preset = [&](const std::string& preset) {
        if (preset.empty() || !profile.empty()) return;
        olduvai::app::Config pc;
        olduvai::app::apply_profile(pc, preset);
        if (auto it = pc.find("enhanced"); it != pc.end() && !cli_enhanced)
            play_enhanced = it->second == "true";
        if (auto it = pc.find("enhance"); it != pc.end() && !cli_enhanced)
            play_enhance_list = it->second;
        if (auto it = pc.find("hd_profile"); it != pc.end() && !cli_hd)
            play_hd_profile = it->second;
        if (auto it = pc.find("render_scale"); it != pc.end() && !cli_scale)
            play_render_scale = std::atoi(it->second.c_str());
        if (auto it = pc.find("aspect"); it != pc.end() && !cli_aspect)
            play_aspect = it->second;
    };

    // Detection accepts PREH.SQZ in place of HISTORIK.EXE (GOG / CD
    // releases) — the manual per-name loop would wrongly report those
    // copies as incomplete.
    {
        olduvai::prepare::GameFiles gf =
            olduvai::prepare::detect_game_files(game_dir);
        if (!gf.complete()) {
            // Always emit the report to the console — the fallback trail
            // for terminals, logs and debugging even when the GUI dialog
            // below handles the user-facing side.
            std::printf("Olduvai needs your original Prehistorik game files.\n");
            std::printf("Missing in %s:\n%s",
                        game_dir.string().c_str(), gf.problems().c_str());
            std::printf("Copy them there (or pass --game-dir) and run again.\n");
            std::fflush(stdout);
#ifdef OLDUVAI_HAVE_SDL
            // GUI session: additionally raise the first-run dialog (folder
            // picker + GOG link).  A validated pick is persisted to
            // play.json and adopted for this run.
            if (olduvai::app::launched_from_gui()) {
                std::string chosen_preset;
                const auto picked = olduvai::app::first_run_dialog(
                    game_dir, gf.problems(), &chosen_preset);
                if (!picked) return 1;                 // user quit
                game_dir = *picked;
                // Adopt the dialog's presentation choice for THIS session
                // too — it is already persisted for the next launch, but the
                // config merge above ran before the dialog existed (the
                // "chose Enhanced HD, got classic DOS" first-run report).
                adopt_preset(chosen_preset);
                style_answered = true;   // the dialog always asks
                gf = olduvai::prepare::detect_game_files(game_dir);
                if (gf.complete()) {
                    std::printf("Using game folder %s (saved to settings).\n",
                                game_dir.string().c_str());
                }
            }
#endif
            if (!gf.complete()) return 1;
        }
    }

    if (play) {
#ifdef OLDUVAI_HAVE_SDL
        // Auto-discovered installs (a GOG copy found without the missing-
        // files dialog, or a pre-seeded game_dir) skip the dialog and with
        // it the one-time Classic/Enhanced question — ask it now on a GUI
        // launch whose config never answered it (2026-07-19 Windows field
        // report: GOG auto-find → silent classic DOS, no question asked).
        if (olduvai::app::launched_from_gui() && !style_answered) {
            const std::string preset = olduvai::app::ask_preset_choice();
            if (!preset.empty()) {   // "" = box unavailable; ask again later
                olduvai::app::Config c = olduvai::app::load_config_file();
                olduvai::app::apply_profile(c, preset);
                if (olduvai::app::save_config_file(c)) {
                    std::printf("Style choice (%s) saved to %s\n",
                                preset.c_str(),
                                olduvai::app::config_path().c_str());
                }
                adopt_preset(preset);
            }
        }

        // First-run UX: make sure the local cache is prepared for these game
        // files before the window opens.  On a hit this is silent; on a miss
        // or key-mismatch it prints "Preparing game data…" and (re)builds.
        // A prepare failure is non-fatal (the engine decodes on the fly) —
        // it just means stage-2 HD persistence can't be keyed yet.
        {
            const olduvai::prepare::GameFiles gf =
                olduvai::prepare::detect_game_files(game_dir);
            olduvai::prepare::ensure_prepared(gf);
        }

        // Build the enhanced-feature set.  --enhanced enables the full bundle;
        // --enhance a,b enables a named subset (union with --enhanced).  Mirrors
        // reference tools/play.py _ENHANCEMENT_NAMES.  An unknown name is a hard
        // error (exit 2), matching the reference.
        olduvai::presentation::EnhanceFlags enhance_flags;
        if (play_enhanced)
            enhance_flags = olduvai::presentation::EnhanceFlags::all();
        {
            std::stringstream ss(play_enhance_list);
            std::string item;
            while (std::getline(ss, item, ',')) {
                const auto b = item.find_first_not_of(" \t");
                if (b == std::string::npos) continue;
                const auto e = item.find_last_not_of(" \t");
                const std::string name = item.substr(b, e - b + 1);
                if      (name == "smooth-motion") enhance_flags.smooth_motion = true;
                else if (name == "cinematic-cue") enhance_flags.cinematic_cue = true;
                else if (name == "hud-overlay")   enhance_flags.hud_overlay   = true;
                else if (name == "fluid-bubbles") enhance_flags.fluid_bubbles = true;
                else if (name == "secret-slide")  enhance_flags.secret_slide  = true;
                else if (name == "descent-pan")   enhance_flags.descent_pan   = true;
                else if (name == "hd-text")       enhance_flags.hd_text       = true;
                else {
                    std::fprintf(stderr,
                        "olduvai: unknown --enhance feature '%s'.  Known: "
                        "smooth-motion, cinematic-cue, hud-overlay, "
                        "fluid-bubbles, secret-slide, descent-pan, hd-text\n",
                        name.c_str());
                    return 2;
                }
            }
        }
        // Umbrella `enhanced` = the HD-render substrate; on if any feature is
        // requested (so a single --enhance gives the enhanced render path).
        play_enhanced = enhance_flags.any();

        // ── Tuning-flag validation (reject typos like the reference does).
        if (play_display_mode != "gpu" && play_display_mode != "cpu") {
            std::fprintf(stderr,
                "olduvai: --display-mode must be 'gpu' or 'cpu' (got '%s')\n",
                play_display_mode.c_str());
            return 2;
        }
        if (play_transitions != "smooth" && play_transitions != "classic") {
            std::fprintf(stderr,
                "olduvai: --transitions must be 'smooth' or 'classic' "
                "(got '%s')\n", play_transitions.c_str());
            return 2;
        }
        if (play_aspect != "keep" && play_aspect != "4:3" &&
            play_aspect != "stretch" && play_aspect != "widescreen") {
            std::fprintf(stderr,
                "olduvai: --aspect must be 'keep', '4:3', 'stretch', or "
                "'widescreen' (got '%s')\n", play_aspect.c_str());
            return 2;
        }
        // --hd-font: map the friendly name to the bundled TTF file.  Unknown
        // names are a hard error (matches the reference choices=[...]).
        std::string hd_font_file;
        if (play_hd_font == "freckle") {
            hd_font_file = "FreckleFace-Regular.ttf";
        } else if (play_hd_font == "noto") {
            hd_font_file = "NotoSans-Regular.ttf";
        } else {
            std::fprintf(stderr,
                "olduvai: --hd-font must be 'freckle' or 'noto' (got '%s')\n",
                play_hd_font.c_str());
            return 2;
        }
        // --banner-fx: reject unknown effect names (the shader would silently
        // fall back to caveman otherwise).
        if (play_banner_fx != "caveman" && play_banner_fx != "fire" &&
            play_banner_fx != "rainbow" && play_banner_fx != "gold" &&
            play_banner_fx != "pulse") {
            std::fprintf(stderr,
                "olduvai: --banner-fx must be caveman|fire|rainbow|gold|pulse "
                "(got '%s')\n", play_banner_fx.c_str());
            return 2;
        }
        // --hd-profile: empty means "use the default" (omniscale, the
        // historical owner default).  Any non-empty name must be one olduvai
        // actually renders — reject unknown/unimplemented names with exit 2
        // and the supported list (matches how the reference raises KeyError;
        // NO silent fall-through to omniscale for a name we don't implement).
        if (play_hd_profile.empty()) {
            play_hd_profile = "omniscale";
        } else if (!olduvai::enhance::is_supported_hd_profile(play_hd_profile)) {
            std::string list;
            for (const auto& p : olduvai::enhance::supported_hd_profiles()) {
                if (!list.empty()) list += ", ";
                list += p;
            }
            std::fprintf(stderr,
                "olduvai: --hd-profile '%s' is not supported.  Supported "
                "profiles: %s.\n",
                play_hd_profile.c_str(), list.c_str());
            return 2;
        }

        // --transitions classic: force smooth-motion off (a convenience
        // override of the enhance flag, applied AFTER enhance resolution so
        // it wins).
        //
        // --trace needs exactly ONE presented state per logic frame, but
        // smooth-motion inserts sub-frames whose count comes from the display
        // refresh (non-deterministic) — so tracing forces classic.  --replay
        // ALONE does NOT: it only injects inputs once per logic tick, which
        // sub-frame render interpolation never touches, so a recorded session
        // replays with full smooth motion (demo-movie friendly).  --replay
        // WITH --trace still forces classic via the trace branch.
        if (play_transitions == "classic") {
            enhance_flags.smooth_motion = false;
        } else if (!play_trace.empty()) {
            if (enhance_flags.smooth_motion) {
                std::fprintf(stderr,
                    "olduvai: --trace set → forcing transitions classic "
                    "(smooth-motion off) for deterministic frames\n");
            }
            enhance_flags.smooth_motion = false;
        }

        // widescreen_active requires the HD substrate (enhanced AND a non-
        // native hd profile) — anything else silently pillarboxed before;
        // say so instead.
        if (play_aspect == "widescreen" &&
            (!play_enhanced || play_hd_profile == "native")) {
            std::fprintf(stderr,
                "olduvai: --aspect widescreen needs the enhanced HD substrate "
                "(--enhanced or an --enhance subset, plus a non-native "
                "--hd-profile) — falling back to a plain pillarbox\n");
        }
        olduvai::presentation::GameOptions go;
        go.game_dir = game_dir;
        // Sequencer mapping (EXE FUN_2bd7_04be slots): no --level → position 0
        // (the attract: intro cards + title + main menu); an explicit 1-7
        // jumps straight into that level (no logo/title/menu — --level 1 is
        // explicit too); 8 = the win ending.  Headless/replay runs remap 0→1
        // inside run_game for deterministic gameplay frame 0.
        go.level = play_level < 0 ? 0 : play_level;
        go.pad_jump = pad_jump;
        go.pad_attack = pad_attack;
        go.pad_pause = pad_pause;
        go.pad_confirm = pad_confirm;
        go.pad_back = pad_back;
        go.pad_deadzone = pad_deadzone;
        go.enhanced = play_enhanced;
        go.enhance = enhance_flags;
        go.hd_profile = play_hd_profile;
        go.banner_fx = play_banner_fx;
        go.window_w = play_window_w;
        go.window_h = play_window_h;
        go.start_screen = play_start_screen;
        go.render_scale = play_render_scale;
        go.music_device = play_music_device;
        go.midi_port = play_midi_port;
        go.rom_dir = play_rom_dir;
        go.soundfont = play_soundfont;
        go.sfx_backend = play_sfx_backend;
        go.replay = play_replay;
        go.trace = play_trace;
        go.record_inputs = play_record_inputs;
        go.cheats = play_cheats;
        go.god = play_god;
        go.autofire = play_autofire;
        go.debug_collision = play_debug_collision;
        go.debug_entities = play_debug_entities;
        go.debug_perf = play_debug_perf;
        go.frames = play_frames;
        go.screenshot = play_shot;
        go.screenshot_frame = play_shot_frame;
        // Scanout needs vblank pacing, but only CLASSIC uses it — the default-
        // on flag must not silently enable vsync for enhanced/HD runs.
        const bool hd_substrate =
            play_enhanced && play_hd_profile != "native";
        go.vsync = play_vsync || (play_vga_scan && !hd_substrate);
        go.fullscreen = play_fullscreen;
        go.display_mode = play_display_mode;
        go.audio_rate = play_audio_rate;
        go.audio_buffer = play_audio_buffer;
        go.transitions = play_transitions;
        go.aspect = play_aspect;
        go.vga_scan = play_vga_scan;
        go.hd_font = hd_font_file;
        // In-game Options menu → play.json.  Load-modify-save keeps any keys
        // the menu doesn't touch; the app layer owns config I/O.
        go.persist = [](const std::string& key, const std::string& value) {
            olduvai::app::Config c = olduvai::app::load_config_file();
            c[key] = value;
            olduvai::app::save_config_file(c);
        };
        // Quicksave alongside the config (…/olduvai/saves/quicksave.json).
        go.save_path = std::filesystem::path(olduvai::app::config_path())
                           .parent_path()
                           .append("saves")
                           .append("quicksave.sav")
                           .string();
        return olduvai::presentation::run_game(go);
#else
        std::printf("This build has no presentation layer (SDL2 missing).\n");
        return 1;
#endif
    }

    if (viewer) {
#ifdef OLDUVAI_HAVE_SDL
        olduvai::presentation::ViewerOptions vo;
        vo.game_dir = game_dir;
        vo.frames = viewer_frames;
        vo.screenshot = viewer_shot;
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
