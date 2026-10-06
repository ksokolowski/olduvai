// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "cli_args.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "parse_num.hpp"

#include "presentation/audio/sound_card.hpp"
#include "presentation/menu/profile_table.hpp"

namespace olduvai::app {


namespace {

constexpr ParseOutcome kContinue{false, 0, false, false};
constexpr ParseOutcome kBadArgument{true, 2, false, false};

// argv numbers are explicit user intent: report and fail rather than let
// atoi's silent 0 through.
bool num(const char* flag, const char* text, int& dst) {
    if (parse_int(text, dst)) return true;
    std::fprintf(stderr, "olduvai: %s expects a whole number (got '%s')\n",
                 flag, text);
    return false;
}

template <class Table>
auto find_flag(const Table& table, const std::string& arg) -> decltype(&table[0]) {
    for (const auto& f : table)
        if (arg == f.name) return &f;
    return nullptr;
}

// --profile NAME; hd-43 (hd with a 4:3 aspect) stays accepted.
bool parse_profile(const char* text, CliArgs& args, PlaySettings& ps) {
    args.profile = text;
    if (args.profile == "hd-43") {
        args.profile = "hd";
        ps.aspect = "4:3";
        ps.cli.aspect = true;   // explicit: must beat a saved config
    }
    if (olduvai::presentation::find_profile(args.profile) != nullptr)
        return true;
    std::fprintf(stderr, "olduvai: --profile must be one of %s (got '%s')\n",
                 olduvai::presentation::profile_names().c_str(),
                 args.profile.c_str());
    return false;
}

bool parse_level(const char* text, int& level) {
    if (!num("--level", text, level)) return false;
    if (level >= 0 && level <= 8) return true;
    std::fprintf(stderr,
                 "olduvai: --level must be 0 (intro/title), 1-7 (play "
                 "levels, display numbering) or 8 (win ending) (got '%s')\n",
                 text);
    return false;
}

bool parse_window(const std::string& wh, int& w, int& h) {
    const auto xpos = wh.find_first_of("xX");
    if (xpos != std::string::npos && parse_int(wh.substr(0, xpos), w) &&
        parse_int(wh.substr(xpos + 1), h))
        return true;
    std::fprintf(stderr,
                 "olduvai: --window expects WxH, e.g. 896x400 (got '%s')\n",
                 wh.c_str());
    return false;
}

// --autofire [slow|medium|fast]: the rate is optional, fast by default.
// Takes the next argument only when it is a rate.
int parse_autofire(const char* next, PlaySettings& ps) {
    ps.autofire = "fast";
    ps.cli.autofire = true;
    if (next == nullptr) return 0;
    const std::string s = next;
    if (s != "slow" && s != "medium" && s != "fast") return 0;
    ps.autofire = s;
    return 1;
}

bool parse_sound_card(const char* text, std::string& card) {
    card = text;
    if (olduvai::presentation::find_sound_card(card) != nullptr) return true;
    std::fprintf(stderr,
                 "olduvai: --sound-card must be one of auto|sb|adlib|mt32|"
                 "gm|midi|off (got '%s')\n", text);
    return false;
}

bool parse_secs(const char* text, double& secs) {
    if (parse_double(text, secs)) return true;
    std::fprintf(stderr,
                 "olduvai: --render-audio-secs expects a number (got '%s')\n",
                 text);
    return false;
}

// --sound-card sets both audio flags after the loop, so an explicit
// --music-device / --sfx-backend wins in any order; marked CLI-given like
// them.
void apply_sound_card(const std::string& name, PlaySettings& ps) {
    const auto* card = olduvai::presentation::find_sound_card(name);
    if (card == nullptr) return;
    if (!ps.cli.music_device) {
        ps.music_device = card->music;
        ps.cli.music_device = true;
    }
    if (!ps.cli.sfx_backend) {
        ps.sfx_backend = card->sfx;
        ps.cli.sfx_backend = true;
    }
}

// The --help headings, in order.
enum Section { kGeneral, kDisplay, kAudio, kEnhanced, kConfig, kDev };
constexpr const char* kSectionTitle[] = {"General", "Display", "Audio",
                                         "Enhanced / HD", "Config",
                                         "Dev / Headless"};

// What a flag does with the argument after it (`next`, null at the end of
// argv): how many it took (0 or 1), or -1 for bad input, already reported
// (`flag` names it).
struct Action {
    bool needs_value;   // without one, the flag is an unknown argument
    std::function<int(const char* flag, const char* next)> apply;
};

// One command-line flag: its spellings, its --help entry and its action.
// parse_args and flag_usage read the same table, so a flag cannot be
// parsed and undocumented, or documented and not parsed.
struct Flag {
    const char* name;
    const char* alias;    // "-f", or null
    Section section;
    const char* usage;    // null: documented inside another entry
    Action action;
};

// A whole number (strict: `2x` is an error naming the flag); `cli` records
// that the command line stated it, where a saved config would otherwise win.
Action number(int& dst, bool* cli = nullptr) {
    return {true, [&dst, cli](const char* flag, const char* v) {
                if (!num(flag, v, dst)) return -1;
                if (cli != nullptr) *cli = true;
                return 1;
            }};
}
// A settings string the saved config also carries: the flag always wins.
Action setting(std::string& dst, bool& cli) {
    return {true, [&dst, &cli](const char*, const char* v) {
                dst = v;
                cli = true;
                return 1;
            }};
}
Action text(std::string& dst) {
    return {true, [&dst](const char*, const char* v) {
                dst = v;
                return 1;
            }};
}
Action toggle(bool& dst, bool value, bool* cli = nullptr) {
    return {false, [&dst, value, cli](const char*, const char*) {
                dst = value;
                if (cli != nullptr) *cli = true;
                return 0;
            }};
}
Action special(bool needs_value,
               std::function<int(const char* flag, const char* next)> apply) {
    return {needs_value, std::move(apply)};
}
// --help and --version: parse_args answers them before the table.
Action handled() { return {false, nullptr}; }

std::vector<Flag> flag_table(CliArgs& a, PlaySettings& ps,
                             std::string& sound_card) {
    return {
        {"--help", "-h", kGeneral,
         "  -h, --help              Show this help and exit.\n",
         handled()},
        {"--version", nullptr, kGeneral,
         "      --version           Print version and exit.\n",
         handled()},
        {"--game-dir", nullptr, kGeneral,
         "      --game-dir <dir>    Directory with the game files (default: \".\",\n"
         "                          then the machine's GOG install if present).\n",
         special(true, [&](const char*, const char* v) { a.game_dir = v; ps.cli.game_dir = true; return 1; })},
        {"--play", nullptr, kGeneral,
         "      --play              Launch the game.\n",
         toggle(a.play, true)},
        {"--viewer", nullptr, kGeneral,
         "      --viewer            Open the asset/image browser.\n",
         toggle(a.viewer, true)},
        {"--level", nullptr, kGeneral,
         "      --level <n>         Sequence position to start at: 0 = intro/\n"
         "                          title, 1-7 = play levels (display numbering),\n"
         "                          8 = win ending.  Default: the intro/title;\n"
         "                          an explicit level jumps straight in.\n",
         special(true, [&](const char*, const char* v) { return parse_level(v, a.play_level) ? 1 : -1; })},
        {"--fullscreen", "-f", kDisplay,
         "  -f, --fullscreen        Start in desktop-fullscreen (Alt+Enter toggles).\n",
         toggle(a.play_fullscreen, true)},
        {"--vsync", nullptr, kDisplay,
         "      --vsync             Request display vsync (off by default; the\n"
         "                          driver may silently ignore it).\n",
         toggle(a.play_vsync, true)},
        {"--vga-scan", nullptr, kDisplay,
         "      --vga-scan          Classic-mode VGA scanout: re-present the\n"
         "                          held frame every display refresh between\n"
         "                          18.2 Hz ticks (like real VGA scanning VRAM\n"
         "                          at 70 Hz).  DEFAULT ON for classic runs\n"
         "                          (implies vsync there); --no-vga-scan opts\n"
         "                          out.  No effect under enhanced/HD\n"
         "                          (smooth-motion covers it).\n",
         toggle(ps.vga_scan, true, &ps.cli.vga_scan)},
        {"--no-vga-scan", nullptr, kDisplay, nullptr,
         toggle(ps.vga_scan, false, &ps.cli.vga_scan)},
        {"--display-mode", nullptr, kDisplay,
         "      --display-mode <m>  Window scaling path: gpu|cpu (default: gpu).\n"
         "                          gpu = GPU (accelerated) scaling; cpu = software\n"
         "                          renderer (escape hatch on some setups).\n",
         setting(ps.display_mode, ps.cli.display_mode)},
        {"--transitions", nullptr, kDisplay,
         "      --transitions <m>   Screen-transition mode: smooth|classic\n"
         "                          (default: smooth).  classic forces smooth-\n"
         "                          motion off; smooth keeps it.  Auto-classic\n"
         "                          under --trace; --replay keeps smooth.\n",
         setting(ps.transitions, ps.cli.transitions)},
        {"--aspect", nullptr, kDisplay,
         "      --aspect <m>        Pixel aspect mode: keep|4:3|stretch|widescreen\n"
         "                          (default: keep).  keep = square pixels +\n"
         "                          black bars; 4:3 = CRT-like vertical stretch;\n"
         "                          stretch = fill window, no bars; widescreen\n"
         "                          (enhanced only) peeks adjacent screens into\n"
         "                          the side margins — its default window takes\n"
         "                          the desktop's shape, since a 16:10 one has\n"
         "                          no margins to show.\n",
         setting(ps.aspect, ps.cli.aspect)},
        {"--autofire", nullptr, kDisplay,
         "      --autofire [speed]  Hold the attack key to keep swinging (no\n"
         "                          mashing): slow|medium|fast (bare = fast;\n"
         "                          fast matches the boss-fight feel).  Saved\n"
         "                          to config; --no-autofire turns it off.\n",
         special(false, [&](const char*, const char* v) { return parse_autofire(v, ps); })},
        {"--no-autofire", nullptr, kDisplay, nullptr,
         special(false, [&](const char*, const char*) {
             ps.autofire = "off";
             ps.cli.autofire = true;
             return 0;
         })},
        {"--sound-card", nullptr, kAudio,
         "      --sound-card <c>    The sound setup, by the card a 1991 player\n"
         "                          knew: auto|sb (Sound Blaster: FM music,\n"
         "                          digital effects)|adlib (FM music and\n"
         "                          effects)|mt32|gm|midi (external)|off.  Sets\n"
         "                          --music-device and --sfx-backend; either\n"
         "                          one given as well wins.\n",
         special(true, [&](const char*, const char* v) { return parse_sound_card(v, sound_card) ? 1 : -1; })},
        {"--music-device", nullptr, kAudio,
         "      --music-device <d>  Music backend: auto|mt32-builtin|gm-builtin|opl|\n"
         "                          none|host-midi|gm-host (default: auto).  host-midi\n"
         "                          (alias mt32) streams raw MT-32 MIDI to a real MIDI\n"
         "                          OUT port; gm-host streams GM-translated MIDI (for\n"
         "                          the Windows GS Wavetable synth).  On Windows, auto\n"
         "                          falls back to gm-host before OPL when no MT-32\n"
         "                          ROMs or SoundFont are found.\n",
         setting(ps.music_device, ps.cli.music_device)},
        {"--midi-port", nullptr, kAudio,
         "      --midi-port <name>  Host MIDI OUT port for host-midi / gm-host\n"
         "                          (default: first port, preferring MT-32/MUNT).\n",
         text(a.play_midi_port)},
        {"--list-midi-ports", nullptr, kAudio,
         "      --list-midi-ports   List available MIDI OUT ports and exit.\n",
         toggle(a.do_list_midi_ports, true)},
        {"--sfx-backend", nullptr, kAudio,
         "      --sfx-backend <b>   SFX backend: auto|opl|sb-dac|mt32-sfx|gm-sfx|midi|\n"
         "                          none\n"
         "                          (default: auto — pairs to the music device).\n",
         setting(ps.sfx_backend, ps.cli.sfx_backend)},
        {"--rom-dir", nullptr, kAudio,
         "      --rom-dir <dir>     MT-32/CM-32L ROM directory (mt32-builtin).\n",
         text(ps.rom_dir)},
        {"--mt32-model", nullptr, kAudio,
         "      --mt32-model M      MT-32 ROM set: auto|cm32l|mt32 (default auto:\n"
         "                          CM-32L when its ROMs are present).\n",
         text(ps.mt32_model)},
        {"--soundfont", nullptr, kAudio,
         "      --soundfont <file>  SoundFont (.sf2) for gm-builtin.\n",
         text(ps.soundfont)},
        {"--audio-rate", nullptr, kAudio,
         "      --audio-rate <hz>   Mixer/synth sample rate (default: device\n"
         "                          preference, else 48000).\n",
         number(ps.audio_rate)},
        {"--audio-buffer", nullptr, kAudio,
         "      --audio-buffer <n>  Mixer buffer in sample frames, power of two\n"
         "                          (default: 2048).\n",
         number(ps.audio_buffer)},
        {"--enhanced", nullptr, kEnhanced,
         "      --enhanced          Enable enhanced mode (all effects).\n",
         toggle(ps.enhanced, true, &ps.cli.enhanced)},
        {"--enhance", nullptr, kEnhanced,
         "      --enhance <list>    Deprecated: the per-feature names no longer\n"
         "                          select a subset.  Any listed name simply\n"
         "                          turns enhanced mode on.\n",
         setting(ps.enhance_list, ps.cli.enhanced)},
        {"--hd-profile", nullptr, kEnhanced,
         "      --hd-profile <p>    HD upscaler profile: native|retro|smooth|\n"
         "                          eagle|xbrz|mmpx|omniscale (default: omniscale).\n"
         "                          At x3, smooth, eagle and mmpx all run Scale3x;\n"
         "                          at x4 they run twice.  xbr is now xbrz.\n",
         setting(ps.hd_profile, ps.cli.hd)},
        {"--render-scale", nullptr, kEnhanced,
         "      --render-scale <n>  Integer render scale: 2 or 4 (default: 4).\n",
         number(ps.render_scale, &ps.cli.scale)},
        {"--hd-font", nullptr, kEnhanced,
         "      --hd-font <f>       HD vector text face: freckle|noto\n"
         "                          (default: freckle).  Needs enhanced mode.\n",
         setting(ps.hd_font, ps.cli.hd_font)},
        {"--banner-fx", nullptr, kEnhanced,
         "      --banner-fx <e>     Enhanced banner colour effect: caveman|fire|\n"
         "                          rainbow|gold|pulse (default: caveman).\n",
         setting(ps.banner_fx, ps.cli.banner_fx)},
        {"--window", nullptr, kEnhanced,
         "      --window <WxH>      Force window pixel size, e.g. 1680x720 (~21:9)\n"
         "                          to simulate an ultrawide widescreen viewport\n"
         "                          on a narrower display.\n",
         special(true, [&](const char*, const char* v) { return parse_window(v, a.play_window_w, a.play_window_h) ? 1 : -1; })},
        {"--start-screen", nullptr, kEnhanced,
         "      --start-screen <n>  DEBUG: enter a surface level at screen n (e.g.\n"
         "                          the last screen) instead of 0.  Clamped to the\n"
         "                          level's screen count.\n",
         number(a.play_start_screen)},
        {"--profile", nullptr, kConfig,
         "      --profile <name>    Built-in profile: dos|hd (handhelds:\n"
         "                          dos-handheld[-43|-x2|-x4]|\n"
         "                          hd-handheld[-43|-x2|-x4]).  Overrides the\n"
         "                          saved config (CLI flags still win): dos =\n"
         "                          byte-faithful; hd = full enhanced +\n"
         "                          widescreen peeks (add --aspect 4:3 for the\n"
         "                          classic CRT look).\n",
         special(true, [&](const char*, const char* v) { return parse_profile(v, a, ps) ? 1 : -1; })},
        {"--default-profile", nullptr, kConfig,
         "      --default-profile <name>\n"
         "                          A launcher's device defaults: applied BELOW\n"
         "                          the saved config, so the player's own\n"
         "                          choices win.  Also decides what Classic and\n"
         "                          Enhanced mean in the menu.\n",
         text(a.default_profile)},
        {"--no-config", nullptr, kConfig,
         "      --no-config         Ignore the saved config file for this run.\n",
         toggle(a.no_config, true)},
        {"--save-config", nullptr, kConfig,
         "      --save-config       Persist the effective CLI settings to the config\n"
         "                          file, then continue.\n",
         toggle(a.save_config, true)},
        {"--replay", nullptr, kDev,
         "      --replay <file>     Replay recorded inputs (with --play).\n",
         text(a.play_replay)},
        {"--trace", nullptr, kDev,
         "      --trace <file>      Write a per-frame trace (with --play).\n",
         text(a.play_trace)},
        {"--record-inputs", nullptr, kDev,
         "      --record-inputs <file>  Write live inputs as replay-schema JSONL\n"
         "                              (with --play; re-playable via --replay).\n",
         text(a.play_record_inputs)},
        {"--cheats", nullptr, kDev,
         "      --cheats            Enable test cheats: number keys 1-6 grant a\n"
         "                          power-up (1=Spring..6=Axe). Off during replay.\n",
         toggle(a.play_cheats, true)},
        {"--god", nullptr, kDev,
         "      --god               999 energy + never out of lives; normal\n"
         "                          ghost/respawn on a fall (debug).\n"
         "                          Off during replay.\n",
         toggle(a.play_god, true)},
        {"--debug-collision", nullptr, kDev,
         "      --debug-collision   Tint solid collision cells (dev overlay).\n",
         toggle(a.play_debug_collision, true)},
        {"--debug-entities", nullptr, kDev,
         "      --debug-entities    Box every active entity (dev overlay).\n",
         toggle(a.play_debug_entities, true)},
        {"--debug-perf", nullptr, kDev,
         "      --debug-perf        Show FPS + frame time (dev overlay).\n",
         toggle(a.play_debug_perf, true)},
        {"--play-frames", nullptr, kDev,
         "      --play-frames <n>   Run the game for n frames then exit (default: -1,\n"
         "                          unlimited).\n",
         number(a.play_frames)},
        {"--play-shot", nullptr, kDev,
         "      --play-shot <file>  Save a screenshot of the game to <file>.\n",
         text(a.play_shot)},
        {"--play-shot-frame", nullptr, kDev,
         "      --play-shot-frame <n>   Frame at which to capture --play-shot\n"
         "                              (default: 1).\n",
         number(a.play_shot_frame)},
        {"--viewer-frames", nullptr, kDev,
         "      --viewer-frames <n> Run the viewer for n frames then exit (default: -1,\n"
         "                          unlimited).\n",
         number(a.viewer_frames)},
        {"--viewer-shot", nullptr, kDev,
         "      --viewer-shot <file>    Save a screenshot of the viewer to <file>.\n",
         text(a.viewer_shot)},
        {"--render-audio", nullptr, kDev,
         "      --render-audio <smf>    Render a MIDI file through the synth and\n"
         "                              print a digest, or write --render-audio-out.\n",
         text(a.render_audio)},
        {"--render-audio-out", nullptr, kDev,
         "      --render-audio-out <wav>    WAV destination for the two render\n"
         "                                  commands (default: print a digest).\n",
         text(a.render_audio_out)},
        {"--render-audio-secs", nullptr, kDev,
         "      --render-audio-secs <s>     Render duration (default: 2).\n",
         special(true, [&](const char*, const char* v) { return parse_secs(v, a.render_audio_secs) ? 1 : -1; })},
        {"--render-sfx", nullptr, kDev,
         "      --render-sfx <id|all>   Render an AdLib sound effect the way the\n"
         "                              engine plays it; needs --game-dir.\n",
         text(a.render_sfx)},

    };
}

const Flag* find(const std::vector<Flag>& table, const std::string& arg) {
    for (const auto& f : table)
        if (arg == f.name || (f.alias != nullptr && arg == f.alias)) return &f;
    return nullptr;
}

}  // namespace

ParseOutcome parse_args(int argc, char** argv, CliArgs& args, PlaySettings& ps) {
    std::string sound_card;   // applied after the loop
    const std::vector<Flag> table = flag_table(args, ps, sound_card);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") return {true, 0, true, false};
        if (arg == "--version") return {true, 0, false, true};
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        const Flag* f = find(table, arg);
        if (f == nullptr || !f->action.apply ||
            (f->action.needs_value && next == nullptr)) {
            std::fprintf(stderr, "olduvai: unrecognized argument '%s'\n",
                         arg.c_str());
            std::fprintf(stderr, "Try 'olduvai --help' for usage.\n");
            return kBadArgument;
        }
        const int took = f->action.apply(f->name, next);
        if (took < 0) return kBadArgument;
        i += took;
    }
    apply_sound_card(sound_card, ps);
    return kContinue;
}

std::string flag_usage() {
    CliArgs a;
    PlaySettings ps;
    std::string card;
    const std::vector<Flag> table = flag_table(a, ps, card);
    std::string out;
    for (int s = kGeneral; s <= kDev; ++s) {
        if (s != kGeneral) out += "\n";
        out += kSectionTitle[s];
        out += ":\n";
        for (const auto& f : table)
            if (f.section == s && f.usage != nullptr) out += f.usage;
    }
    return out;
}

}  // namespace olduvai::app
