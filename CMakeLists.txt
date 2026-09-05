# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski

cmake_minimum_required(VERSION 3.21)
project(olduvai
    VERSION 0.9.6
    DESCRIPTION "Native recreation of the Prehistorik (1991) engine"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── MSVC (shipped Windows build) ────────────────────────────────────────────
# Self-contained exe: static CRT (/MT) so no VC++ redistributable is needed —
# same "ships without external deps" philosophy as the MinGW/Linux/macOS
# builds.  NOMINMAX/WIN32_LEAN_AND_MEAN keep windows.h from clobbering
# std::min/max; the runtime audio loader (dynlib.hpp) uses LoadLibrary here.
if(MSVC)
    add_compile_options(/W3 /permissive- /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX
                            WIN32_LEAN_AND_MEAN)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # /MT (/MTd for Debug)
else()
    add_compile_options(-Wall -Wextra)
    # §4b #9 extended set (2026-08-24): every flag BELOW measured 0 sites in
    # src/ at adoption, and the tree still builds clean with them.  MSVC
    # excluded — its /W3 surface is a separate ledger (see §4b #10's
    # boss-renderer rows).
    #
    # -Wdouble-promotion is NOT in this set, and the note here used to imply it
    # was ("6 sites, fixed same day").  Re-measured 2026-08-24: 8 warnings over
    # SIX locations, so nothing was fixed and nothing guards them —
    #   render/widescreen.cpp:192,193,218,219  (the margin edge gradient)
    #   render/lerp_snapshot.hpp:59            (the smooth-motion lerp; three
    #     of the eight, because an inline header function is reported per TU)
    #   presentation/game_app.cpp:2452
    # Deliberately deferred rather than adopted: silencing these means writing
    # `2.0f` where `2.0` is now, which CHANGES the arithmetic — and every one of
    # them is in float-sensitive code the hash gates pin (the widescreen margin
    # compose, the smooth-motion lerp).  That is a numerics change needing the
    # widescreen/transition goldens re-verified, not a warning cleanup, and it
    # does not belong in a release cut.  BACKLOG §4b #9.
    add_compile_options(-Wold-style-cast -Wcast-qual -Wformat=2
                        -Wnon-virtual-dtor -Wextra-semi -Wimplicit-fallthrough)
endif()

# Treat warnings as errors on demand (default OFF so a stray warning never
# blocks a local build).  CI runs with -DOLDUVAI_WERROR=ON — both toolchain
# surfaces verified clean (clang 2026-07-08, GCC 15 2026-07-09; see BO1).
# GCC/Clang only — MSVC's first build isn't warning-clean yet, and -Werror
# would block the shippable artifact on cosmetic warnings.
option(OLDUVAI_WERROR "Treat compiler warnings as errors (CI does)" OFF)
if(OLDUVAI_WERROR AND NOT MSVC)
    add_compile_options(-Werror)
endif()

# ── Vendored code is not held to our warning bar ────────────────────────────
#
# MEASURED on the 2026-07-26 pre-tag dry run: 193 compiler warnings across the
# four platform jobs, of which 90 came from code we did not write — SDL2's own
# ObjC (50, macOS deprecations), doctest (31, MSVC C5285), FluidSynth (9,
# MSVC).  Our own warnings were outnumbered on every platform but Linux, which
# is precisely the failure mode: a real warning in our code arrives in a wall
# of noise nobody reads, and the log looked from the outside like "FluidSynth
# throws hundreds of warnings" when FluidSynth throws nine.
#
# The line, and it is deliberate: **compiler warnings OFF for vendored code,
# sanitizers ON.**  A warning in upstream's source is upstream's business and
# we will not patch it — the same argument that keeps mt32emu verbatim below
# (a local patch is re-work at every re-sync and the LGPL source pointer has
# to stay honest).  A SANITIZER hit is a different thing: it is a real defect
# that will crash OUR binary, so the vendored tree stays instrumented, with
# only the one narrow, documented exception at mt32emu's shift-base.
#
# This generalises what RtMidi.cpp already did per-file since 2026-07.  Use it
# for every vendored target; do NOT hand-roll `-w` at a new call site.
function(olduvai_silence_vendored_target tgt)
    if(NOT TARGET ${tgt})
        return()
    endif()
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /w)
    else()
        target_compile_options(${tgt} PRIVATE -w)
    endif()
endfunction()

# Vendored HEADERS are a separate problem: doctest is not a target we own, it
# is #included into OUR translation units, so a target-level flag cannot reach
# it.  SYSTEM include directories are the mechanism that can — the compiler
# suppresses diagnostics from headers found there.  MSVC needs the /external
# switches turned on explicitly before /external:I means anything.
if(MSVC)
    add_compile_options(/external:W0)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.29)
        add_compile_options(/external:anglebrackets-)
    endif()
endif()

# Address + UB sanitizers for a verification build (local runs and the CI
# sanitize lane).  UB is made FATAL (-fno-sanitize-recover) so ctest fails on
# any hit instead of scrolling past a warning.  Forces LTO off (IPO+ASan
# interact badly) and keeps frame pointers for readable stacks.  Use a
# separate build tree: cmake --preset asan (build/asan)
#
# Half the lane lives in CMakePresets.json, not here: the RUNTIME options
# (ASAN_OPTIONS/UBSAN_OPTIONS) are set on the asan *test* preset, because two
# defaults are off and silently cost detection —
# detect_stack_use_after_return=1 (without it a real stack-use-after-return is
# simply not reported, on clang as well as g++) and UBSAN print_stacktrace=1
# (without it a fatal UB report is one line with no frames, which is what
# -fno-sanitize-recover gets you today).  Running an asan-built test binary
# directly, rather than through `ctest --preset asan`, gets neither.
# Deliberately NOT set: symbolize (already default), halt_on_error/
# abort_on_error (already default), print_stacktrace in ASAN_OPTIONS (not an
# ASan flag — silently ignored), and detect_leaks (hard-aborts on macOS/arm64).
option(OLDUVAI_SANITIZE "Build with -fsanitize=address,undefined" OFF)
if(OLDUVAI_SANITIZE)
    add_compile_options(-fsanitize=address,undefined
                        -fno-sanitize-recover=undefined
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    set(OLDUVAI_LTO OFF CACHE BOOL "sanitize build: LTO forced off" FORCE)
endif()

# ── Link-time optimisation (shared, opt-out) ────────────────────────────────
# IPO/LTO for Release on any toolchain that supports it.  Verified not to
# perturb the deterministic gameplay traces (golden_trace) on Windows; the
# Linux Release build re-verifies via ctest.
option(OLDUVAI_LTO "Enable IPO/LTO for Release" ON)
if(OLDUVAI_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _olduvai_ipo LANGUAGES CXX C)
    if(_olduvai_ipo)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# ── Windows (MinGW) release shape ───────────────────────────────────────────
# Self-contained binaries + modern loader hardening.  All opt-out, all
# WIN32-only — the macOS/Linux builds are untouched.
#
#  * OLDUVAI_STATIC_RUNTIME — statically link libgcc/libstdc++/winpthread
#    (and prefer static dlfcn).  Without it the exe dies pre-main with
#    STATUS_DLL_NOT_FOUND outside an MSYS2 shell unless the MinGW runtime
#    DLLs are deployed next to it (packaging/deploy_dlls_windows.sh).
#  * OLDUVAI_STATIC_SDL — link SDL2 statically (SDL2::SDL2-static) for a
#    single-file olduvai.exe.  Trade-off: users can't swap in their own
#    SDL2.dll; updates ship with the binary (same story as bundling).
#  * ASLR/DEP: --dynamicbase, --nxcompat, --high-entropy-va (64-bit ASLR
#    needs the explicit flag with binutils).
#  * OLDUVAI_LTO lives in the shared LTO block above (all platforms).
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    option(OLDUVAI_STATIC_RUNTIME "Static libgcc/libstdc++/winpthread" ON)
    option(OLDUVAI_STATIC_SDL "Link SDL2 statically" ON)

    if(OLDUVAI_STATIC_RUNTIME)
        add_link_options(-static-libgcc -static-libstdc++
                         -Wl,-Bstatic,--whole-archive -lwinpthread
                         -Wl,-Bdynamic,--no-whole-archive)
    endif()
    add_link_options(-Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
    add_compile_options(-fstack-protector-strong)
endif()

# ── Linux (GCC) release shape ───────────────────────────────────────────────
# Modern loader hardening.  Self-containment on Linux is the AppImage's job
# (bundled .so's), not static-glibc — so no static-runtime/static-SDL options
# here.  Opt-out via -DOLDUVAI_HARDEN=OFF; Linux-only, other platforms
# untouched.  _FORTIFY_SOURCE needs an optimised build, so it is gated to
# non-Debug and the distro's predefined level is undefined first to avoid a
# redefinition warning.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    option(OLDUVAI_HARDEN "RELRO, NX, PIE, stack-protector, FORTIFY" ON)
    if(OLDUVAI_HARDEN)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        add_compile_options(-fstack-protector-strong
            $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
        add_link_options(-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    endif()
endif()

# ── engine layers (SDL-free) ────────────────────────────────────────────────
add_library(olduvai_formats
    src/core/game_tables.cpp
    src/core/rng.cpp
    src/formats/lzss.cpp
    src/formats/unsqz.cpp
    src/formats/cur.cpp
    src/formats/packbits.cpp
    src/formats/mat.cpp
    src/formats/dur.cpp
    src/formats/pc1.cpp
    src/formats/voc.cpp
    src/formats/mdi.cpp
    src/enhance/mmpx.cpp
    src/enhance/omniscale.cpp
    src/enhance/pixel_scalers.cpp
    src/enhance/xbr.cpp
    src/enhance/upscale.cpp
    src/enhance/hd_asset_cache.cpp
    src/enhance/hd_text.cpp
    src/presentation/menu/text_editor.cpp
    src/enhance/enhanced_hud.cpp
    src/prepare/exe_tables.cpp
    src/prepare/game_files.cpp
    src/prepare/game_archives.cpp
    src/systems/player.cpp
    src/systems/monster_ai.cpp
    src/systems/spawning.cpp
    src/systems/collisions.cpp
    src/systems/collision_dispatch.cpp
    src/systems/score.cpp
    src/systems/frame_runner.cpp
    src/systems/transitions.cpp
    src/systems/cave_logic.cpp
    src/systems/secret.cpp
    src/systems/boss.cpp
    src/systems/boss_l2.cpp
    src/systems/boss_l4.cpp
    src/systems/boss_l6.cpp
    src/systems/fluid_bubbles.cpp
)
# -Wconversion on the SDL-free engine library only (§4b item 10).  The
# presentation layer is full of deliberate int<->float pixel math and SDL's own
# integer types, where the warning is noise; the byte-decoder and gameplay
# layers are exactly where a silent narrowing is a real bug.
#
# -Wno-sign-conversion, measured not assumed: §4b's "~7 sites" estimate
# predates clang folding -Wsign-conversion into -Wconversion.  With it on, the
# library reports ~30 warnings and they are ALL the `buf[static index]` int ->
# size_t idiom — pervasive, deliberate, and not the narrowing bug class this
# flag is here to catch.  Without it: the value-changing conversions below,
# each now explicit.
#
# GUARDED FOR MSVC, and it was not: `cl` rejects the GCC/Clang spelling outright
# ("D8021: invalid numeric argument '/Wconversion'") and the whole Windows build
# died at Configure->Build.  Landed unguarded 2026-08-02, i.e. AFTER v0.9.5 was
# tagged, so no release had exercised it — the 0.9.6 pre-tag dry run is what
# surfaced it, which is precisely the failure mode that runbook step exists for
# (0.9.4 learned it the expensive way, by failing the release itself).  MSVC's
# nearest equivalent is /W4's C4244/C4267 family and is a separate ledger
# (§4b #10); do not try to map the flags across.
if(NOT MSVC)
    target_compile_options(olduvai_formats PRIVATE -Wconversion -Wno-sign-conversion)
endif()

target_include_directories(olduvai_formats PUBLIC src)
target_include_directories(olduvai_formats SYSTEM PUBLIC third_party/stb)

# HD bake compression (OPTIONAL).  The upscaled cache stores palette art as
# 32-bit RGBA — ~17 distinct colours in a 4 MB frame — so deflate takes the
# on-disk bake from hundreds of MB to ~10.  zlib is in the macOS SDK, universal
# on Linux and present in MSYS2; MSVC has none, so this is strictly optional:
# without it the cache simply stores raw as before.  Entries carry a distinct
# magic, and an unreadable entry is already treated as a miss and re-baked, so
# builds with and without zlib interoperate on the same cache directory.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(olduvai_formats PRIVATE ZLIB::ZLIB)
    target_compile_definitions(olduvai_formats PRIVATE OLDUVAI_HAVE_ZLIB)
    message(STATUS "HD cache compression: zlib ${ZLIB_VERSION_STRING}")
else()
    message(STATUS "HD cache compression: OFF (zlib not found) — cache stores raw")
endif()
if(WIN32)
    # RegGetValueA in prepare/game_files.cpp (GOG install discovery).
    # MinGW links advapi32 by default; MSVC needs it explicit.
    target_link_libraries(olduvai_formats PUBLIC advapi32)
endif()

# ── format-parser fuzzer (clang libFuzzer; opt-in) ──────────────────────────
# Pre-emptive hardening for user-supplied game files — see
# tests/fuzz/fuzz_formats.cpp.  Requires OLDUVAI_SANITIZE so the library under
# test is ASan/UBSan-instrumented (a fuzzer over an uninstrumented library
# misses the bugs it exists to find).  Corpus dirs are local-only (content
# policy: no game-derived bytes in the tree).
option(OLDUVAI_FUZZ "Build the libFuzzer format-parser harness" OFF)
if(OLDUVAI_FUZZ)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        message(FATAL_ERROR
            "OLDUVAI_FUZZ needs LLVM clang — Apple's clang ships no libFuzzer "
            "runtime (brew install llvm, then set CC/CXX to its clang/clang++).")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "OLDUVAI_FUZZ requires clang (libFuzzer)")
    endif()
    if(NOT OLDUVAI_SANITIZE)
        message(FATAL_ERROR
            "OLDUVAI_FUZZ requires -DOLDUVAI_SANITIZE=ON so the parsers are instrumented")
    endif()
    add_executable(fuzz_formats tests/fuzz/fuzz_formats.cpp
        src/presentation/audio/midi_seq.cpp)
    target_compile_options(fuzz_formats PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_formats PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_formats PRIVATE olduvai_formats)
endif()

# ── presentation (SDL2; optional so headless format work still builds) ─────
find_package(SDL2 QUIET)
# Single point of truth for which SDL2 flavour every target links.
#
# OLDUVAI_STATIC_SDL used to be Windows-only.  It is now honoured wherever a
# static SDL2 is available, because the macOS release wants it too: SDL2 was
# the LAST third-party dylib in the .app (libmt32emu is vendored, FluidSynth
# is dlopen'd and staged by hand), and linking it in removes the bundling step
# altogether — along with dylibbundler, whose install-name rewriting produces
# a binary that SIGKILLs or hangs on macOS 26.  Measured: byte-identical audio
# and pixel output, zero third-party dylibs, and a bundle with no
# Contents/libs at all.  SDL2 is zlib-licensed, so static linking carries no
# relinking obligation (FluidSynth stays dynamic precisely because LGPL §6
# does).
set(OLDUVAI_SDL2 SDL2::SDL2)
if(OLDUVAI_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OLDUVAI_SDL2 SDL2::SDL2-static)
endif()
if(SDL2_FOUND)
    add_library(olduvai_presentation
        src/presentation/render/banners.cpp
        src/presentation/diag/viewer.cpp
        src/presentation/input/gamepad.cpp
        src/presentation/render/game_render.cpp
        src/presentation/render/sprite_blit.cpp
        src/presentation/render/bg_compose.cpp
        src/presentation/level/level_save.cpp
        src/presentation/level/level_setup.cpp
        src/presentation/menu/pause_flow.cpp
        src/presentation/menu/pause_service.cpp
    src/presentation/diag/report_form.cpp
    src/presentation/render/tile_patterns.cpp
        src/presentation/render/screen_tiles.cpp
        src/presentation/game_app.cpp
        src/presentation/render/hud_render.cpp
        src/presentation/diag/debug_overlay.cpp
        src/presentation/sequence/screens.cpp
        src/presentation/menu/cheat_picker.cpp
        src/presentation/render/level_surface.cpp
        src/presentation/render/text_overlay.cpp
        src/presentation/title_menu_flow.cpp
        src/presentation/sequence/transition_players.cpp
        src/presentation/render/widescreen_presenter.cpp
        src/presentation/window_util.cpp
        src/presentation/boss_app.cpp
        src/presentation/render/boss_render.cpp
        src/presentation/audio/audio.cpp
        src/presentation/audio/host_midi.cpp
        src/presentation/audio/opl_sfx.cpp
        src/presentation/audio/midi_seq.cpp
        src/presentation/input/replay.cpp
        src/presentation/image_out.cpp
        src/presentation/diag/draw_log.cpp
        src/presentation/sequence/end_sequence.cpp
        src/presentation/input/frame_input.cpp
        src/presentation/render/frame_presenter.cpp
        src/presentation/diag/reinit_test_hook.cpp
        src/presentation/diag/bug_capture.cpp
        src/presentation/menu/menu.cpp
        src/presentation/menu/menu_render.cpp
        src/presentation/menu/text_overlay_edit.cpp
        src/presentation/menu/settings_apply.cpp
        src/presentation/menu/settings_session.cpp
        src/presentation/menu/settings_flow.cpp
        src/presentation/menu/confirm_dialog.cpp
        src/presentation/level/save_state.cpp
        src/presentation/render/widescreen.cpp
        src/presentation/render/boss_widescreen.cpp
        src/presentation/render/boss_hud.cpp
        src/presentation/render/boss_arena.cpp
        src/presentation/sequence/l3_end_level.cpp
        third_party/nuked_opl3/opl3.c)
    target_include_directories(olduvai_presentation
        PUBLIC src third_party/stb
        PRIVATE third_party/nuked_opl3)
    target_compile_definitions(olduvai_presentation PRIVATE
        OLDUVAI_VERSION="${PROJECT_VERSION}")
    # std::thread (host-MIDI pump thread) needs pthreads on libstdc++/Linux,
    # even in the RtMidi-disabled stub build (the thread member is compiled
    # regardless).  Link it unconditionally so the stub case still links.
    find_package(Threads QUIET)
    target_link_libraries(olduvai_presentation
        PUBLIC olduvai_formats
        PRIVATE ${OLDUVAI_SDL2} ${CMAKE_DL_LIBS})
    if(WIN32)
        # CMAKE_DL_LIBS is empty on Windows; the runtime-loaded synth
        # backends (audio.cpp) use dlopen via dlfcn-win32 when available.
        # Under OLDUVAI_STATIC_RUNTIME prefer the static archive so the
        # binary doesn't pull in libdl.dll.
        if(OLDUVAI_STATIC_RUNTIME)
            find_library(OLDUVAI_DLFCN_LIB NAMES libdl.a dl)
        else()
            find_library(OLDUVAI_DLFCN_LIB dl)
        endif()
        if(OLDUVAI_DLFCN_LIB)
            target_link_libraries(olduvai_presentation PRIVATE ${OLDUVAI_DLFCN_LIB})
        endif()
    endif()
    if(Threads_FOUND)
        target_link_libraries(olduvai_presentation PRIVATE Threads::Threads)
    endif()

    # ── OPL music: EXE-faithful AdLib driver on the vendored Nuked-OPL3 ──
    # opl_music.cpp replaces the former libADLMIDI path: the driver plays
    # the container's authored FF 7F voice patches (byte-parity with the
    # reference renderer via tools/opl_music_dump), always available with
    # no external dependency.  (The former libADLMIDI submodule is removed.)
    target_sources(olduvai_presentation PRIVATE src/presentation/audio/opl_music.cpp)

    # ── Host MIDI output: vendored RtMidi (CoreMIDI / ALSA / WinMM) ────
    # Opt-in (--music-device host-midi / --list-midi-ports).  Default ON, but
    # designed so a MISSING dependency can never fail the build:
    #   * Apple — CoreMIDI is always present (no package); link the frameworks.
    #   * Windows — WinMM is always present; link winmm.  The Windows MIDI
    #     mapper's default device is the built-in Microsoft GS Wavetable
    #     Synth, so host MIDI gives every Windows machine a General MIDI
    #     sink with no extra software.
    #   * Linux — needs ALSA (libasound2-dev).  If ALSA is NOT found we
    #     silently DISABLE the feature (no error) and compile host_midi.cpp as
    #     a stub, so the engine is unaffected and --list-midi-ports / --midi-port
    #     report "host MIDI not available in this build".
    # OLDUVAI_HAVE_RTMIDI is defined only when RtMidi is actually compiled in;
    # host_midi.cpp keys its real-vs-stub paths off it.
    option(OLDUVAI_WITH_RTMIDI
           "Build vendored RtMidi for host MIDI output (CoreMIDI/ALSA)" ON)
    set(_olduvai_rtmidi_on OFF)
    if(OLDUVAI_WITH_RTMIDI AND
       EXISTS ${CMAKE_SOURCE_DIR}/third_party/rtmidi/RtMidi.cpp)
        if(APPLE)
            set(_olduvai_rtmidi_on ON)
            target_compile_definitions(olduvai_presentation PRIVATE __MACOSX_CORE__)
            target_link_libraries(olduvai_presentation PRIVATE
                "-framework CoreMIDI" "-framework CoreAudio"
                "-framework CoreFoundation")
        elseif(WIN32)
            set(_olduvai_rtmidi_on ON)
            target_compile_definitions(olduvai_presentation PRIVATE __WINDOWS_MM__)
            target_link_libraries(olduvai_presentation PRIVATE winmm)
        else()
            find_package(ALSA QUIET)
            if(ALSA_FOUND)
                set(_olduvai_rtmidi_on ON)
                target_compile_definitions(olduvai_presentation PRIVATE __LINUX_ALSA__)
                target_link_libraries(olduvai_presentation PRIVATE ALSA::ALSA)
            else()
                message(STATUS
                    "ALSA not found — host MIDI output disabled "
                    "(install libasound2-dev to enable it).  The build "
                    "continues; --list-midi-ports reports it as unavailable.")
            endif()
        endif()
    endif()
    if(_olduvai_rtmidi_on)
        target_sources(olduvai_presentation PRIVATE
            third_party/rtmidi/RtMidi.cpp)
        target_include_directories(olduvai_presentation SYSTEM PRIVATE third_party)
        target_compile_definitions(olduvai_presentation PRIVATE
            OLDUVAI_HAVE_RTMIDI=1)
        # RtMidi's own source trips -Wall/-Wextra.  This is the per-SOURCE
        # form of olduvai_silence_vendored_target: RtMidi.cpp is compiled
        # straight into olduvai_presentation rather than as its own target, so
        # a target-level flag would silence OUR sources too.  Same policy, and
        # `/w` is spelled for MSVC because that build compiles it as well.
        if(MSVC)
            set_source_files_properties(third_party/rtmidi/RtMidi.cpp
                PROPERTIES COMPILE_OPTIONS "/w")
        else()
            set_source_files_properties(third_party/rtmidi/RtMidi.cpp
                PROPERTIES COMPILE_OPTIONS "-w")
        endif()
    endif()

    # ── libmt32emu (Roland MT-32 / CM-32L), vendored and compiled in ────────
    #
    # It used to be dlopen'd, and that shipped a real defect for four public
    # releases: it reached Linux AppImage users only.  macOS and Windows
    # packages carried nothing, so those users silently fell back to OPL —
    # music still played, so nobody reported it.  Fixing that by PACKAGING ran
    # into a different hazard per platform (a leaf-name dlopen cannot see
    # inside a .app; rewriting a Homebrew dylib's install name invalidates its
    # ad-hoc signature and makes the next dlopen a SIGKILL rather than a
    # fallback; dylibbundler -od erases the directory you stage into; munt
    # publishes no Windows DLL at all).  Compiled in, none of that exists.
    # ScummVM has carried this same Munt source for over a decade for the same
    # reason.  Provenance, licence and re-sync steps:
    # third_party/mt32emu/OLDUVAI-VENDORING.md
    #
    # Off-switch kept because it is a real user-facing capability, not a
    # detail: -DOLDUVAI_WITH_MT32EMU=OFF drops it and mt32-builtin then
    # reports unavailable, exactly as an absent dlopen'd library used to.
    option(OLDUVAI_WITH_MT32EMU
           "Build vendored libmt32emu (MT-32/CM-32L emulation)" ON)
    if(OLDUVAI_WITH_MT32EMU AND
       EXISTS ${CMAKE_SOURCE_DIR}/third_party/mt32emu/CMakeLists.txt)
        # Static: this is an implementation detail of our binary, not a
        # shared library we ship.  EXCLUDE_FROM_ALL so `--target all` does not
        # build munt's install/package targets.
        set(libmt32emu_SHARED OFF CACHE BOOL "" FORCE)
        # And OFF for its tests, because `src/test/` is PRUNED from the vendored
        # copy (OLDUVAI-VENDORING.md) while the CMake that builds it is not.
        #
        # This is not defensive tidying — the block is LIVE on every configure.
        # third_party/mt32emu/CMakeLists.txt:26 does `if(NOT DEFINED
        # BUILD_TESTING) set(BUILD_TESTING TRUE)`, and we define it nowhere
        # before this line (our own enable_testing() is ~150 lines below), so
        # munt's test branch is entered every single time. The ONLY thing that
        # has been stopping it is `find_package(doctest 1.2.9 MODULE)` failing
        # at :33 and self-disabling at :36 — visible in our own configure log as
        # "doctest not found, testing unavailable".
        #
        # So a host with doctest installed builds a test runner whose sources do
        # not exist. Found 2026-07-29 on a new macOS Intel box where installing
        # doctest was enough to break configure; nothing about it is
        # macOS-specific, and any lane that acquires doctest hits it.
        #
        # Forcing the project's OWN option rather than BUILD_TESTING: the latter
        # is the standard CTest variable, and turning it off wholesale to fix a
        # vendored subdirectory would be a side effect aimed at the wrong scope.
        set(libmt32emu_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/mt32emu EXCLUDE_FROM_ALL)
        # munt's LA32 mixer doubles samples with `IntSampleEx(x) << 1` on
        # values that are routinely negative (src/Synth.cpp, DACInputMode_NICE
        # in produceLA32Output).  Left-shifting a negative is UB in C++17 —
        # and DEFINED in C++20 as exactly the wraparound every compiler
        # already emits, which is why this has shipped in Munt for years.
        # Our asan lane makes UB fatal (-fno-sanitize-recover), so the process
        # aborted the moment MT-32 rendered a sample; the ending_shot gate
        # added the same day caught it, because it opens audio.
        #
        # ONLY shift-base is disabled, and only for this target: every other
        # UBSan check still instruments the vendored code, so a genuine bug in
        # it — one that would be our problem too — still fails the lane.  The
        # blunter -fno-sanitize=undefined would have hidden those.
        #
        # NOT patched locally: the source here is verbatim upstream, which is
        # what makes re-syncing a directory copy and keeps the LGPL source
        # pointer honest.  A local patch is permitted (LGPL-2.1 §2, with
        # notices) but every one is re-work at each re-sync, and editing
        # sample arithmetic risks moving the MT-32 output we gate on.
        # Reporting it upstream is the fix that costs us nothing to keep.
        if(OLDUVAI_SANITIZE)
            target_compile_options(mt32emu PRIVATE -fno-sanitize=shift-base)
        endif()
        # Vendored: its warnings are upstream's, and we do not patch this tree
        # (see the verbatim-copy argument above).  The sanitizer instrumenting
        # it is NOT relaxed by this — only the one shift-base exception is.
        olduvai_silence_vendored_target(mt32emu)
        target_link_libraries(olduvai_presentation PRIVATE mt32emu)
        target_compile_definitions(olduvai_presentation PRIVATE
            OLDUVAI_VENDORED_MT32EMU=1)
        message(STATUS "MT-32 emulation: vendored libmt32emu (compiled in)")
    else()
        message(STATUS "MT-32 emulation: OFF")
    endif()

    # Scenario-authoring atlas (uses the presentation compositor).
    add_executable(screen_atlas EXCLUDE_FROM_ALL tools/screen_atlas.cpp)
    target_link_libraries(screen_atlas PRIVATE olduvai_presentation)

    # Widescreen adjacent-screen peek — kept headless composite-shot diagnostic
    # (per-level / per-margin) for the enhanced peek feature.
    add_executable(widescreen_peek_shot EXCLUDE_FROM_ALL tools/widescreen_peek_shot.cpp)
    target_link_libraries(widescreen_peek_shot PRIVATE olduvai_presentation ${OLDUVAI_SDL2})

    # Secret-room composer — direct render of the L1 secret screen for
    # trampoline z-order / bubble diagnosis (no gameplay).
    add_executable(secret_shot EXCLUDE_FROM_ALL tools/secret_shot.cpp)
    target_link_libraries(secret_shot PRIVATE olduvai_presentation)

    # OPL-SFX PCM dump — A/B validation vs the Python reference renderer.
    add_executable(opl_sfx_dump EXCLUDE_FROM_ALL tools/opl_sfx_dump.cpp)
    target_link_libraries(opl_sfx_dump PRIVATE olduvai_presentation)

    # L3 descent-pan composer — direct render of the Dark Woods trunk-descent
    # Phase 1 + camera pan for the enhanced "descent-pan" feature (no gameplay;
    # reaching screen 17→18 in game needs a full level run).
    add_executable(descent_pan_shot EXCLUDE_FROM_ALL tools/descent_pan_shot.cpp)
    target_link_libraries(descent_pan_shot PRIVATE olduvai_presentation)
else()
    message(WARNING
        "SDL2 not found — building headless (engine library + tests only; "
        "the game will not run).  Install libsdl2-dev (Debian/Ubuntu), "
        "`brew install sdl2` (macOS), or see docs/BUILDING.md for Windows.")
endif()

# ── app ─────────────────────────────────────────────────────────────────────
add_executable(olduvai src/app/main.cpp src/app/config.cpp src/app/legacy_cache.cpp
                       src/app/options_resolve.cpp src/app/cli_args.cpp
                       src/app/options_build.cpp)
# olduvai_formats is linked here for the SDL-less build.  When SDL2 is found,
# olduvai_presentation is linked below and already carries olduvai_formats
# PUBLIC, so naming it here too puts the same archive on the link line twice
# and Apple's ld says so on every single build: "ignoring duplicate libraries:
# 'libolduvai_formats.a'".  Harmless, and exactly the kind of permanent noise
# that trains people to skim link output.
if(NOT SDL2_FOUND)
    target_link_libraries(olduvai PRIVATE olduvai_formats)
endif()
target_compile_definitions(olduvai PRIVATE
    OLDUVAI_VERSION="${PROJECT_VERSION}")
if(WIN32 AND OLDUVAI_SDL2 STREQUAL "SDL2::SDL2" AND TARGET SDL2::SDL2)
    # Dynamic SDL2 (MSVC / official VC package ships a DLL, not a static lib):
    # copy SDL2.dll next to olduvai.exe so it runs in place and the packaged
    # zip is self-contained.  The MinGW build links SDL2 statically instead
    # (OLDUVAI_STATIC_SDL) and needs no DLL.
    add_custom_command(TARGET olduvai POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:olduvai>)
endif()
if(WIN32)
    # Project icon resource (the bone logo): Explorer, taskbar and title bar
    # pick up icon id 1 from the exe automatically.  windres via CMake's RC.
    enable_language(RC)
    set(OLDUVAI_ICO_PATH ${CMAKE_SOURCE_DIR}/assets/icon/olduvai.ico)
    configure_file(${CMAKE_SOURCE_DIR}/packaging/windows/olduvai.rc.in
                   ${CMAKE_BINARY_DIR}/olduvai.rc @ONLY)
    target_sources(olduvai PRIVATE ${CMAKE_BINARY_DIR}/olduvai.rc)
endif()
if(APPLE)
    # Embed bundle metadata into the bare CLI binary so macOS presents the
    # app menu / Dock as "Olduvai" (CFBundleName) instead of the lowercase
    # executable filename.  Standard single-file-executable mechanism:
    # an __info_plist section read by LaunchServices/AppKit at launch.
    configure_file(${CMAKE_SOURCE_DIR}/packaging/macos/Info-cli.plist.in
                   ${CMAKE_BINARY_DIR}/Info-cli.plist @ONLY)
    target_link_options(olduvai PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_BINARY_DIR}/Info-cli.plist")
endif()
if(SDL2_FOUND)
    target_sources(olduvai PRIVATE src/app/first_run.cpp)
    target_link_libraries(olduvai PRIVATE olduvai_presentation
                                          ${OLDUVAI_SDL2})
    target_compile_definitions(olduvai PRIVATE OLDUVAI_HAVE_SDL=1)
endif()


# ── trace harness (oracle-diff client) ──────────────────────────────────────
add_executable(olduvai_trace EXCLUDE_FROM_ALL src/app/trace_main.cpp)
target_link_libraries(olduvai_trace PRIVATE olduvai_formats)

# ── L2 dino spawn-side diagnostic ───────────────────────────────────────────
add_executable(l2_spawn_count EXCLUDE_FROM_ALL tools/l2_spawn_count.cpp)
target_link_libraries(l2_spawn_count PRIVATE olduvai_formats)

# ── MDI→MIDI dump — A/B the mt32 music conversion vs the Python reference ────
add_executable(mdi_dump EXCLUDE_FROM_ALL tools/mdi_dump.cpp)
target_link_libraries(mdi_dump PRIVATE olduvai_formats)

# Byte-parity harness for the authentic AdLib driver (headless, no SDL) —
# render a user-supplied music file to WAV and diff against the reference
# renderer's output.  PCM of game music is game content: compare locally.
# Logo generator (dev-only, not shipped): renders assets/logo/* with the
# main menu's exact title styling.  `cmake --build --preset release --target olduvai_logo`
add_executable(olduvai_logo EXCLUDE_FROM_ALL tools/logo_render.cpp)
target_link_libraries(olduvai_logo PRIVATE olduvai_formats)
target_include_directories(olduvai_logo PRIVATE src)
target_include_directories(olduvai_logo SYSTEM PRIVATE third_party/stb)

add_executable(opl_music_dump EXCLUDE_FROM_ALL tools/opl_music_dump.cpp
    src/presentation/audio/opl_music.cpp
    third_party/nuked_opl3/opl3.c)
target_include_directories(opl_music_dump SYSTEM PRIVATE third_party/nuked_opl3)
target_link_libraries(opl_music_dump PRIVATE olduvai_formats)

# ── tests (doctest, vendored single header) ─────────────────────────────────
enable_testing()
add_executable(olduvai_tests EXCLUDE_FROM_ALL
    tests/main.cpp
    tests/test_lzss.cpp
    tests/test_unsqz.cpp
    tests/test_cur.cpp
    tests/test_packbits.cpp
    tests/test_mat.cpp
    tests/test_dur.cpp
    tests/test_pc1.cpp
    tests/test_core.cpp
    tests/test_exe_tables.cpp
    tests/test_constants.cpp
    tests/test_player.cpp
    tests/test_autofire.cpp
    tests/test_monster_ai.cpp
    tests/test_monster_ai_batch2.cpp
    tests/test_monster_ai_batch3.cpp
    tests/test_spawning.cpp
    tests/test_collisions.cpp
    tests/test_collision_dispatch.cpp
    tests/test_boss.cpp
    tests/test_boss_l2.cpp
    tests/test_boss_l4.cpp
    tests/test_boss_l6.cpp
    tests/test_snap_lerp.cpp
    tests/test_mdi.cpp
    tests/test_transitions.cpp
    tests/test_post_frame_steps.cpp
    tests/test_screen_topology.cpp
    tests/test_upscale.cpp
    tests/test_hd_asset_cache.cpp
    tests/test_hd_asset_cache_disk.cpp
    tests/test_fluid_bubbles.cpp
    tests/test_game_files.cpp
    tests/test_menu.cpp
    tests/test_menu_model.cpp
    tests/test_save_state.cpp
    tests/test_soundfont_pick.cpp
    tests/test_rom_dirs.cpp
    tests/test_resample.cpp
    tests/test_wav_io.cpp
    tests/test_opl_music.cpp
    tests/test_options_resolve.cpp
    tests/test_options_build.cpp
    tests/test_cli_args.cpp
    tests/test_env_num.cpp
    src/app/config.cpp src/app/legacy_cache.cpp
    src/app/cli_args.cpp
    src/app/options_resolve.cpp
    src/app/options_build.cpp
    src/presentation/audio/opl_music.cpp
    third_party/nuked_opl3/opl3.c
    src/presentation/menu/menu.cpp
    src/presentation/level/save_state.cpp
)
target_compile_definitions(olduvai_tests PRIVATE
    OLDUVAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Build the MenuModel from assets/data/menus.json AT BUILD TIME.  The JSON
# stays the authoring format — a far better place to edit menus than C++
# initialisers, and where translations would live — it is simply read here
# instead of by a JSON parser inside the shipped binary.  Nothing is copied
# next to the executable any more: there is no runtime load to feed.
#
# A malformed menus.json now fails the BUILD, which is where a typo in a menu
# definition belongs, rather than degrading to a menu-less executable.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/menu_model_data.cpp
    COMMAND ${CMAKE_COMMAND}
            -DIN=${CMAKE_SOURCE_DIR}/assets/data/menus.json
            -DOUT=${CMAKE_BINARY_DIR}/generated/menu_model_data.cpp
            -P ${CMAKE_SOURCE_DIR}/cmake/gen_menu_model.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/data/menus.json
            ${CMAKE_SOURCE_DIR}/cmake/gen_menu_model.cmake
    COMMENT "Generating the menu model from data/menus.json")
if(SDL2_FOUND)
    target_sources(olduvai_presentation PRIVATE
        ${CMAKE_BINARY_DIR}/generated/menu_model_data.cpp)
endif()

# Embed the project icon (the bone logo PNG) so every window can carry it at
# runtime via SDL_SetWindowIcon — the .desktop/.ico/.icns files cover
# launchers, but Linux window managers take the running window's icon from
# the window itself (alt-tab / taskbar were blank without this).
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/icon_embedded.cpp
    COMMAND ${CMAKE_COMMAND}
            -DIN=${CMAKE_SOURCE_DIR}/assets/icon/icon_src.png
            -DOUT=${CMAKE_BINARY_DIR}/generated/icon_embedded.cpp
            -DSYMBOL=embedded_icon_png
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/icon/icon_src.png
            ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding app icon")
if(SDL2_FOUND)
    target_sources(olduvai_presentation PRIVATE
        ${CMAKE_BINARY_DIR}/generated/icon_embedded.cpp)
endif()
# olduvai_tests compiles menu_model.cpp directly (it does not link the
# presentation lib), so it needs the generated model TU too.  Without it the
# built_in_menu_model reference only linked because LTO dead-stripped it — a
# works-by-accident graph the OLDUVAI_SANITIZE lane (LTO off) caught.
target_sources(olduvai_tests PRIVATE
    ${CMAKE_BINARY_DIR}/generated/menu_model_data.cpp)
target_include_directories(olduvai_tests SYSTEM PRIVATE third_party
                                                        third_party/nuked_opl3)
target_link_libraries(olduvai_tests PRIVATE olduvai_formats)
add_test(NAME unit COMMAND olduvai_tests)

# Pure (no-SDL) settings-apply tier classifier test.
add_executable(test_settings_apply EXCLUDE_FROM_ALL
    tests/test_settings_apply.cpp
    src/presentation/menu/settings_apply.cpp)
target_include_directories(test_settings_apply PRIVATE src)
add_test(NAME settings_apply COMMAND test_settings_apply)

add_executable(test_settings_session EXCLUDE_FROM_ALL
    tests/test_settings_session.cpp
    src/presentation/menu/settings_session.cpp)
target_include_directories(test_settings_session PRIVATE src)
add_test(NAME settings_session COMMAND test_settings_session)

# Pure (no-SDL) SettingsFlow controller test (OL-B1): options-subtree
# membership derived from a synthetic MenuModel + confirm-dialog lifecycle +
# apply/discard resolution through the injected hooks.
add_executable(test_settings_flow EXCLUDE_FROM_ALL
    tests/test_settings_flow.cpp
    src/presentation/menu/settings_flow.cpp
    src/presentation/menu/settings_apply.cpp
    src/presentation/menu/settings_session.cpp
    src/presentation/menu/confirm_dialog.cpp)
target_include_directories(test_settings_flow PRIVATE src)
add_test(NAME settings_flow COMMAND test_settings_flow)

# Pure (no-SDL) widescreen neighbor-resolution decision-table test.
# widescreen.cpp's transitive includes pull no SDL, so this links SDL-free.
add_executable(test_widescreen EXCLUDE_FROM_ALL
    tests/test_widescreen.cpp
    src/presentation/render/widescreen.cpp)
target_include_directories(test_widescreen PRIVATE src)
add_test(NAME widescreen COMMAND test_widescreen)

# Pure (no-SDL) boss_widescreen helpers test (margin + surgical HUD inpaint +
# compose_widescreen reflect_pure invariants — Task 6; plus the §3.18 HUD-bar
# capture, which is the other half of the same HUD-strip erase).
add_executable(test_boss_widescreen EXCLUDE_FROM_ALL
    tests/test_boss_widescreen.cpp
    src/presentation/render/boss_widescreen.cpp
    src/presentation/render/boss_hud.cpp
    src/presentation/render/hud_render.cpp
    src/presentation/render/sprite_blit.cpp
    src/presentation/render/widescreen.cpp)
# LINK olduvai_formats rather than hand-listing mat/packbits/hd_text: MSVC
# resolves every undefined symbol in a pulled-in object, even one the test never
# calls, so hand-listing means chasing the whole transitive closure —
# hud_render -> blit_sprite -> HdAssetCache::get -> hd_asset_cache.cpp -> zlib.
# ld64 and GNU ld dead-strip that chain, which is why this target linked on
# macOS and Linux for a fortnight while `link.exe` failed with LNK2001/LNK1120
# (found by the 0.9.6 pre-tag dry run, after §3.18/§3.19 put hud_render.cpp and
# mat.cpp in here).  olduvai_formats is SDL-free, so the no-SDL property this
# target exists to hold is preserved — same shape as test_screen_tiles.
target_include_directories(test_boss_widescreen PRIVATE src third_party/stb)
target_link_libraries(test_boss_widescreen PRIVATE olduvai_formats)
add_test(NAME boss_widescreen COMMAND test_boss_widescreen)

add_executable(test_confirm_dialog EXCLUDE_FROM_ALL
    tests/test_confirm_dialog.cpp
    src/presentation/menu/confirm_dialog.cpp
    src/presentation/menu/settings_session.cpp)
target_include_directories(test_confirm_dialog PRIVATE src)
add_test(NAME confirm_dialog COMMAND test_confirm_dialog)

# Pure (no-SDL) shared surface-tile constructor identity gate (OL-B2):
# build_screen_tiles vs the frozen pre-refactor bind/peek construction, on
# real game data, every surface screen of levels 1/3/5/7 × enhanced flags.
# Skips (77) when game data is absent — golden_trace.sh pattern.
add_executable(test_screen_tiles EXCLUDE_FROM_ALL
    tests/test_screen_tiles_unify.cpp
    src/presentation/render/screen_tiles.cpp
    src/presentation/render/tile_patterns.cpp)
target_include_directories(test_screen_tiles PRIVATE src)
target_link_libraries(test_screen_tiles PRIVATE olduvai_formats)
target_compile_definitions(test_screen_tiles PRIVATE
    OLDUVAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME screen_tiles COMMAND test_screen_tiles)
set_tests_properties(screen_tiles PROPERTIES SKIP_RETURN_CODE 77)

# SQZ-executable parity gate: with real game data providing BOTH
# HISTORIK.EXE and PREH.SQZ, every prepared table must decode identically
# from either source.  Skips (77) when either file is absent.
add_executable(test_sqz_parity EXCLUDE_FROM_ALL tests/test_sqz_parity.cpp)
target_include_directories(test_sqz_parity PRIVATE src)
target_link_libraries(test_sqz_parity PRIVATE olduvai_formats)
target_compile_definitions(test_sqz_parity PRIVATE
    OLDUVAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME sqz_parity COMMAND test_sqz_parity)
set_tests_properties(sqz_parity PROPERTIES SKIP_RETURN_CODE 77)

add_executable(test_text_editor EXCLUDE_FROM_ALL tests/test_text_editor.cpp)
target_link_libraries(test_text_editor PRIVATE olduvai_formats)
target_include_directories(test_text_editor PRIVATE src)
target_include_directories(test_text_editor SYSTEM PRIVATE third_party)
add_test(NAME text_editor COMMAND test_text_editor)

add_executable(test_report_templates EXCLUDE_FROM_ALL tests/test_report_templates.cpp)
target_link_libraries(test_report_templates PRIVATE olduvai_formats)
target_include_directories(test_report_templates PRIVATE src)
target_include_directories(test_report_templates SYSTEM PRIVATE third_party)
add_test(NAME report_templates COMMAND test_report_templates)

# SDL-dependent presentation tests (headless via the dummy video driver).
if(SDL2_FOUND)
    add_executable(olduvai_sdl_tests EXCLUDE_FROM_ALL
        tests/test_window_util.cpp
        tests/test_screens.cpp
        tests/test_game_render_hd.cpp
        tests/test_widescreen_compose.cpp
        tests/test_tile_patterns.cpp
        tests/test_midi_seq.cpp
        tests/test_opl_sfx.cpp
        tests/test_sdl_audio.cpp
        tests/test_pause_actions.cpp
        tests/test_hud_render_stride.cpp
        tests/test_smooth_subpixel.cpp)
    target_include_directories(olduvai_sdl_tests SYSTEM PRIVATE third_party)
    target_link_libraries(olduvai_sdl_tests PRIVATE olduvai_presentation
                                                    ${OLDUVAI_SDL2})
    add_test(NAME sdl_unit COMMAND olduvai_sdl_tests)
    # Belt and braces with test_sdl_audio.cpp's own SDL_setenv: the SdlAudio
    # cases need a driver that always opens.  Inheriting the shell's passed on
    # macOS (CoreAudio) and failed on the Linux CI container, where there is no
    # sound device — found by the 0.9.6 pre-tag dry run.
    set_tests_properties(sdl_unit PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    add_executable(test_text_overlay_edit EXCLUDE_FROM_ALL
        tests/test_text_overlay_edit.cpp)
    target_include_directories(test_text_overlay_edit PRIVATE src)
    target_include_directories(test_text_overlay_edit SYSTEM PRIVATE third_party)
    target_link_libraries(test_text_overlay_edit PRIVATE olduvai_presentation
                                                         ${OLDUVAI_SDL2})
    add_test(NAME text_overlay_edit COMMAND test_text_overlay_edit)

    # Audio render gate (audio-harness Phase 2): deterministic offline PCM
    # render of a SYNTHETIC MIDI (no game content) through each synth backend,
    # one fresh process per backend via the shipping --render-audio CLI.
    # Data-gated — each backend exits 77 without its assets (MT-32 ROMs / a GM
    # SoundFont), so the whole test skips (77) on a runner without them.
    # SDL_AUDIODRIVER=dummy: the offline render opens no device, but SDL audio
    # init still needs a driver on a headless runner.
    # Windows cannot exec a #! script directly — run the .sh wrappers through
    # sh (present in MSYS2 and Git for Windows).  Empty prefix elsewhere.
    # MUST be defined BEFORE the first add_test that uses it: it used to sit
    # further down, so audio_render — the first .sh test — expanded an EMPTY
    # prefix and ctest tried to exec the script directly.  On Windows that is
    # BAD_COMMAND ("inappropriate file type or format"), reported as *Not Run*,
    # which ctest counts as a FAILURE and which failed the v0.9.4 release job.
    set(_olduvai_shell_tests ON)
    if(WIN32)
        find_program(OLDUVAI_TEST_SH NAMES sh bash)
        # ...and finding NOTHING is that same bug one layer further down.
        # find_program leaves the literal OLDUVAI_TEST_SH-NOTFOUND in the
        # variable, all 18 .sh tests register with THAT as their command, and
        # ctest reports every one as *Not Run* — which it counts as a FAILURE.
        # So the tests specifically designed to skip cleanly (77) on a runner
        # with no game files instead failed the whole job.  A test that cannot
        # launch must not be registered: no interpreter, no registration.
        if(NOT OLDUVAI_TEST_SH)
            set(_olduvai_shell_tests OFF)
        endif()
    endif()
endif()

# The shell-driven half of the suite.  Split out of the SDL2 block above (same
# condition, plus an interpreter) so a host without sh drops it as a unit
# instead of registering 18 unlaunchable tests.
if(SDL2_FOUND AND _olduvai_shell_tests)
    add_test(NAME audio_render
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/audio_render.sh"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(audio_render PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai
        ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    # Headless reinit integration smoke test: drives OLDUVAI_REINIT_TEST to
    # trigger a render_scale 2→4 reinit on frame 5 and asserts the logical
    # window size is 1280×800 and player position is preserved across the
    # rebuild.  The shell wrapper skips (exit 77) when game data is absent so
    # CI without assets stays green.
    add_test(NAME first_run
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/first_run.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(first_run PROPERTIES
        SKIP_RETURN_CODE 77)
    add_test(NAME reinit_smoke
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/reinit_smoke.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(reinit_smoke PROPERTIES
        SKIP_RETURN_CODE 77)
    # reinit_smoke needs the binary to be up-to-date.
    set_tests_properties(reinit_smoke PROPERTIES
        DEPENDS olduvai)

    # Golden-trace regression gate: the canonical 300-frame L1 scripted run
    # diffed against the oracle-validated fixture (tests/fixtures/).  Catches
    # gameplay-logic and RNG-stream regressions in systems/ without a manual
    # cross-engine oracle-diff.  Skips (77) when game data is absent.
    add_test(NAME golden_trace
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai_trace>")
    set_tests_properties(golden_trace PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai_trace)

    # Main-menu compose regression gate: renders one deterministic menu frame
    # via OLDUVAI_MAINMENU_SHOT and byte-compares to the committed golden.
    # Covers run_game's intro/attract/menu block, which golden_trace does not
    # (the trace starts in-level). Gates the TitleMenuFlow (CC1) extraction.
    # Skips (77) when game data is absent. See tests/mainmenu_shot.sh for the
    # cross-toolchain golden caveat.
    add_test(NAME mainmenu_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/mainmenu_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(mainmenu_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Level-complete fade gate: the 8b intercept on BOTH present paths, hashed
    # pre-upscale. Added with §3.7 slice 2 because that path had NO coverage of
    # any kind — it is reachable only by finishing a level — and 58 lines of it
    # were about to move. The goldens were captured from the PRE-extraction
    # binary, so the gate proves the move byte-identical rather than merely
    # agreeing with itself. Skips (77) when game data is absent.
    add_test(NAME level_fade
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/level_fade.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(level_fade PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Title-screen Style Apply equivalence: drives the title menu via the
    # OLDUVAI_MENU_SCRIPT walk — apply Enhanced HD at the title, then reboot
    # on the persisted config — and byte-compares the two menu shots (no
    # golden: post-apply frames use the float upscaler; both runs share one
    # binary, so equivalence is exact). Catches stale render state surviving
    # the in-place settings reinit. Skips (77) without game data.
    add_test(NAME title_style_apply
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/title_style_apply.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(title_style_apply PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # In-game Pause-overlay compose gate: force-opens the pause overlay on L1
    # frame 1 via OLDUVAI_PAUSE_SHOT and byte-compares to the golden. Covers the
    # pause block run_platform_level owns — the CC2c extraction target — which
    # golden_trace does not reach. Skips (77) without game data.
    add_test(NAME pause_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/pause_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(pause_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Cave transition margin symmetry: a cave has no neighbours, so its
    # widescreen margins must be a black bezel on the way OUT as well as IN.
    # The exit direction regressed (ws_old read post-exit_cave state) and no
    # existing test saw it — wide_transition covers cave ENTRY only.
    add_test(NAME cave_exit_margins
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/cave_exit_margins.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(cave_exit_margins PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Boss-arena per-frame trace: the boss-side counterpart of golden_trace.
    # run_boss_level had NO behavioural gate (boss_replay_record only compares a
    # recorded INPUT file), which is why every boss-side backlog item was
    # blocked.  Additive; skips (77) without game data.
    add_test(NAME boss_golden_trace
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/boss_golden_trace.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(boss_golden_trace PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 walk-across: the corpus scenario that LEAVES screen 0 (BACKLOG §3.15).
    # golden_trace runs screen 0 and boss_golden_trace runs the L2 arena, so
    # until this existed nothing in the corpus crossed a screen boundary and
    # transitions.cpp sat at 0.00%.  Recorded from this engine, NOT validated
    # against the oracle — a regression gate, not a parity one; see the script
    # header.  Additive; skips (77) without game data.
    add_test(NAME golden_trace_walk
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_walk.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_walk PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Display level 3 walk = internal L5 — reaches check_l5_transition, which
    # nothing executed because the corpus only ran L1 and the L2 arena (§3.15).
    # Shares walk_in.jsonl with golden_trace_walk; only --level differs.
    add_test(NAME golden_trace_walk_l3
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_walk_l3.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_walk_l3 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Display level 7 walk = internal L7 — reaches check_l7_transition, which
    # nothing executed because the corpus only ran L1 and the L2 arena (§3.15).
    # Shares walk_in.jsonl with golden_trace_walk; only --level differs.
    add_test(NAME golden_trace_walk_l7
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_walk_l7.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_walk_l7 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Clamp scenario: display 5, screen 10 (§3.16).  These pin the per-screen
    # walk clamps, nine of which no gate executed before this — display 5 was
    # not run at all.  Not traversals: the clamps hold the player in place.
    add_test(NAME golden_trace_l5s10
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l5s10.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l5s10 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Clamp scenario: display 5, screen 11 (§3.16).  These pin the per-screen
    # walk clamps, nine of which no gate executed before this — display 5 was
    # not run at all.  Not traversals: the clamps hold the player in place.
    add_test(NAME golden_trace_l5s11
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l5s11.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l5s11 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Clamp scenario: display 7, screen 4 (§3.16).  These pin the per-screen
    # walk clamps, nine of which no gate executed before this — display 5 was
    # not run at all.  Not traversals: the clamps hold the player in place.
    add_test(NAME golden_trace_l7s4
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l7s4.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l7s4 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 secret room: the corpus scenario that goes UNDERWATER (§3.15 step 2).
    # secret.cpp was 17.9% with three functions nothing executed.  Recorded
    # from this engine, NOT oracle-validated — see the script header.
    add_test(NAME golden_trace_secret
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_secret.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_secret PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 cave WITH A BAT (§3.15).  The screen-2 caves hold a spider; only the
    # screen-6 cave covers update_cave_bat — the last function nothing ran.
    add_test(NAME golden_trace_cavebat
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_cavebat.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_cavebat PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 fight + deep-run: the first native gates whose input presses ATTACK.
    # Both inputs adopted verbatim from the oracle repo's scenario corpus
    # (hand-authored key presses).  deep_run alone takes monster_ai 28.7->44.4%
    # and collisions 31.8->58.0%; at ~79 s it stays under boss_pause_shot's
    # 90 s, so the suite's critical path is unchanged.
    add_test(NAME golden_trace_fight
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_fight.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_fight PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)
    add_test(NAME golden_trace_deep_run
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_deep_run.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_deep_run PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 climbing: the corpus scenario that goes UP A LADDER (§3.15).
    # update_climbing needed Stairs plus the UP key held; no scenario held UP.
    # Same golden caveat as the others — recorded from this engine.
    add_test(NAME golden_trace_climb
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_climb.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_climb PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L4 triceratops fight (§3.15 / RECORDING.md): only L2 had a boss
    # scenario.  Patrol, tail-pocket club hits, kill zones, respawn i-frames.
    # Same golden caveat as the others — recorded from this engine.
    add_test(NAME golden_trace_l4_fight
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l4_fight.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l4_fight PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L6 giant fight (§3.15): the plate-launch/head-window fight.  Same
    # golden caveat as the others — recorded from this engine.
    add_test(NAME golden_trace_l6_fight
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l6_fight.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l6_fight PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L6 slam kills UNDER SMOOTH MOTION (§3.3c): the only gate that reaches
    # the pose-hold's logic timing — OLDUVAI_FORCE_SMOOTH=1 overrides the
    # max_frames and --trace guards for this run shape alone.
    add_test(NAME golden_trace_l6_slam_hd
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_l6_slam_hd.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_l6_slam_hd PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L1 cave: the corpus scenario that goes UNDERGROUND (§3.15 step 3).
    # cave_logic.cpp was 3.1%; entry needs DOWN held on the entrance, which no
    # scenario had ever pressed.  Same golden caveat as above.
    add_test(NAME golden_trace_cave
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/golden_trace_cave.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(golden_trace_cave PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # L4 ride-off victory compose (§3.7 cluster 3).  Until this existed the
    # branch could be neither reached (FORCE_WIN leaves win_flag at 0) nor
    # captured (the REAL_SHOT path fell through to the pillarbox present), so a
    # refactor of it had no gate at all.
    add_test(NAME boss_l4_victory
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/boss_l4_victory.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(boss_l4_victory PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # HD + WIDESCREEN pause overlay compose. pause_shot above pins the CLASSIC
    # path only (--render-scale 1, no --aspect widescreen → margin 0), so the
    # wide/HD pause frame — where the overlay dropped the margins and the HUD —
    # had no gate at all. Skips (77) without game data.
    add_test(NAME pause_shot_wide
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/pause_shot_wide.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(pause_shot_wide PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # §3.20's two empty display-mode cells: --aspect 4:3 (the only mode with a
    # different logical HEIGHT, 240) and --aspect stretch (the only mode that
    # disables logical scaling). Each gate asserts its own output size — proof
    # the mode took effect — plus a hash distinct from both keep-mode goldens.
    add_test(NAME pause_shot_43
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/pause_shot_43.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(pause_shot_43 PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    add_test(NAME pause_shot_stretch
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/pause_shot_stretch.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(pause_shot_stretch PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # BOSS-arena pause overlay compose. The boss pause is a bespoke present
    # block that bypasses the boss present helpers; nothing gated it, and it
    # skipped the HD upscale + widescreen compose entirely. Skips (77) without
    # game data.
    add_test(NAME boss_pause_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/boss_pause_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(boss_pause_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # CLASSIC (non-HD) boss HUD — BACKLOG §3.19. Every other boss gate passes
    # --enhanced, so the bitmap HUD had NO coverage: §3.18 stopped it drawing
    # entirely and the whole suite stayed green. Asserts behaviour, not a hash
    # — two runs at different lives counts must differ, and only in the digits.
    # Skips (77) without game data or python3.
    add_test(NAME boss_classic_hud
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/boss_classic_hud.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(boss_classic_hud PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Loading card + score tally, across 2 screens x 2 stacks x 2 MODES — the
    # only coverage the classic (dos) text presenter has, on either stack
    # (`tally_pause` is reachable from nowhere else), and the gate CLAUDE.md and
    # BACKLOG §3.14a/§3.16 cite as theirs.
    #
    # LABELLED `slow`, AND THAT IS THE POINT. It runs ~256 s, so the `release`
    # test preset filters label `slow` out and the everyday suite stays ~550 s —
    # the runtime argument scripts/gate_local.sh has always made. What changes
    # is that it is now a REGISTERED test rather than a loose script gate_local
    # invoked by name: there is one registry to read instead of two. A sweep
    # asking "what covers this?" read `ctest -N`, did not find it, and reported
    # it as never run — it had been running in gate_local all along. Anything
    # too slow for the default suite belongs HERE with the label, not outside
    # the registry (§3.20).
    #
    # Run it with:  ctest --preset release -L slow      (gate_local does)
    #               ctest --preset release-full         (everything)
    add_test(NAME hd_text_screens
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/hd_text_screens.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(hd_text_screens PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS slow
        DEPENDS olduvai)

    # In-game menu behaviour baseline: drives the pause menu via OLDUVAI_MENU_SCRIPT
    # (synthetic key events) through a nav/Options/Cheats/back walk and byte-
    # compares each frame to a committed golden. The interactive-menu regression
    # net that makes the CC2c pause-controller extraction testable without a
    # manual playtest. Skips (77) without game data.
    add_test(NAME menu_script
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/menu_script.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(menu_script PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Smooth-motion cave-enter snap: replays a walk into the L3 S4 cave with
    # OLDUVAI_DRAW_LOG and asserts no sub-frame glide across the teleport
    # (bug report 2026-07-17_165051_L3_S4 — the (9,11)px cave hop sat under
    # the 16-px distance snap guard). Skips (77) without game data.
    add_test(NAME cave_lerp
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/cave_lerp.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(cave_lerp PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    add_test(NAME report_form
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/report_form.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(report_form PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Wide-transition present gate (CC2d): replays two corpus scripts under
    # forced widescreen and hash-compares every wide-present frame (integer
    # mmpx profile — never omniscale, float LTO instability). Gates the
    # present_wide_transition extraction. Skips (77) without game data.
    add_test(NAME wide_transition
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/wide_transition.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(wide_transition PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Boss-loop input record+replay round-trip: replays a synthetic corpus into
    # the L2 boss WHILE recording and byte-compares the recorded file to the
    # input. Gates the boss --record-inputs wiring + its at(frame) record
    # convention. Skips (77) without game data.
    add_test(NAME boss_replay_record
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/boss_replay_record.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(boss_replay_record PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Win-ending compose gate.  end_sequence.cpp shipped with its own written
    # precondition unmet ("GATE FIRST: game-over/ending have no goldens"), so
    # the last thing a player ever sees was rendered by untested code.  Covers
    # the ending; the game-over half is gameover_shot below.
    add_test(NAME ending_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/ending_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(ending_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Game-over picture golden (BACKLOG §6): the uncapped slam replay reaches
    # game_over (any --play-frames run sets `single`, which SKIPS the screen),
    # and OLDUVAI_GAMEOVER_SHOT captures instead of waiting out the hold.
    add_test(NAME gameover_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/gameover_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(gameover_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)

    # Asset-viewer compose gate.  viewer.cpp was the only file in the tree
    # measuring 0.0% coverage BOTH with and without assets — a shipped,
    # user-reachable feature no test executed.  The hooks it needs
    # (--viewer-frames/--viewer-shot) already existed, unwired.
    add_test(NAME viewer_shot
        COMMAND ${OLDUVAI_TEST_SH} "${CMAKE_SOURCE_DIR}/tests/viewer_shot.sh"
                "${CMAKE_SOURCE_DIR}/game_data"
                "$<TARGET_FILE:olduvai>")
    set_tests_properties(viewer_shot PROPERTIES
        SKIP_RETURN_CODE 77
        DEPENDS olduvai)
endif()

# ── Test-gate honesty: label the data-gated tests ──────────────────────────
#
# Most of this suite needs the user's own game files and skips (77) without
# them, and NO CI runner has them.  That split was invisible: `ctest` prints
# "100% tests passed out of N" counting every skip as a pass, so the suite
# reads as fully green on a machine where most of it never executed.  This is
# the measured reason duplication keeps recurring — the helpers that get
# copied all live on asset-load and present paths, which no always-green test
# touches (docs/internal/BACKLOG.md §1).
#
#   ctest -L assets    → only the data-gated tests (needs game files)
#   ctest -LE assets   → only the always-green tests, i.e. what CI really runs
#   scripts/gate_local.sh → the full local gate; a SKIP there is a FAILURE
#
# The label is DERIVED from SKIP_RETURN_CODE, never hand-listed: a hand-kept
# list of 18 names is precisely the thing that drifts, and a new gated test
# would silently join the invisible set.  Must run after the last add_test —
# DIRECTORY PROPERTY TESTS only sees what has been registered so far.
get_property(_olduvai_tests DIRECTORY PROPERTY TESTS)
set(_olduvai_gated 0)
foreach(_test IN LISTS _olduvai_tests)
    get_test_property(${_test} SKIP_RETURN_CODE _olduvai_skip_rc)
    if(_olduvai_skip_rc STREQUAL "77")
        set_property(TEST ${_test} APPEND PROPERTY LABELS assets)
        math(EXPR _olduvai_gated "${_olduvai_gated} + 1")
    endif()
endforeach()
list(LENGTH _olduvai_tests _olduvai_total)

# Ratchet.  Not decoration: it converts "quietly added another test CI cannot
# see" into a configure error someone has to answer for.  SDL2-less builds
# register only the two non-SDL gated tests (screen_tiles, sqz_parity) — as do
# builds with no shell interpreter, since the other 18 are all .sh-driven.
if(SDL2_FOUND AND _olduvai_shell_tests)
    set(_olduvai_gated_expected 42)   # +1: hd_text_screens, +2: L4/L6 fights, +1: L6 slam-hd, +2: pause 4:3/stretch, +1: gameover_shot
else()
    set(_olduvai_gated_expected 2)
endif()
if(NOT _olduvai_gated EQUAL _olduvai_gated_expected)
    message(FATAL_ERROR
        "Data-gated test count changed: ${_olduvai_gated}, expected "
        "${_olduvai_gated_expected}.  If that is intended, update "
        "_olduvai_gated_expected in CMakeLists.txt AND the KPI row in "
        "docs/internal/BACKLOG.md §5.  If it is not, the new test needs to "
        "run without the user's game files.")
endif()
math(EXPR _olduvai_always_green "${_olduvai_total} - ${_olduvai_gated}")
message(STATUS
    "Olduvai tests: ${_olduvai_total} registered, ${_olduvai_gated} need game "
    "files, ${_olduvai_always_green} run anywhere (what CI sees)")
if(SDL2_FOUND AND NOT _olduvai_shell_tests)
    # Printed, never silent: dropping 18 tests is exactly the kind of thing
    # that should be visible in a build log rather than inferred from a count.
    message(STATUS
        "Olduvai tests: the 18 shell-driven tests are NOT registered — no "
        "sh/bash on PATH.  Install Git for Windows or MSYS2 to run them.")
endif()

# Bundle the HD font (OFL) next to the binary for runtime discovery.
# configure_file (not file(COPY)) — same stale-copy hazard class that bit
# the former build/data/menus.json: file(COPY) only runs at configure time.
configure_file(${CMAKE_SOURCE_DIR}/assets/fonts/FreckleFace-Regular.ttf
               ${CMAKE_BINARY_DIR}/fonts/FreckleFace-Regular.ttf COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/assets/fonts/FreckleFace-LICENSE.txt
               ${CMAKE_BINARY_DIR}/fonts/FreckleFace-LICENSE.txt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf
               ${CMAKE_BINARY_DIR}/fonts/NotoSans-Regular.ttf COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-LICENSE.txt
               ${CMAKE_BINARY_DIR}/fonts/NotoSans-LICENSE.txt COPYONLY)

# ── macOS app bundle (opt-in; `cmake --build --preset release --target olduvai_app`) ──
if(APPLE AND SDL2_FOUND)
    add_executable(olduvai_app MACOSX_BUNDLE EXCLUDE_FROM_ALL
        src/app/main.cpp src/app/config.cpp src/app/legacy_cache.cpp src/app/options_resolve.cpp
        src/app/cli_args.cpp src/app/first_run.cpp src/app/options_build.cpp)
    set_target_properties(olduvai_app PROPERTIES
        OUTPUT_NAME "Olduvai"
        MACOSX_BUNDLE_BUNDLE_NAME "Olduvai"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.olduvai.engine"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Olduvai.icns")
    target_compile_definitions(olduvai_app PRIVATE
        OLDUVAI_VERSION="${PROJECT_VERSION}" OLDUVAI_HAVE_SDL=1)
    target_link_libraries(olduvai_app PRIVATE olduvai_formats
                                              olduvai_presentation
                                              ${OLDUVAI_SDL2})
    add_custom_command(TARGET olduvai_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/assets/icon/Olduvai.icns
            $<TARGET_BUNDLE_CONTENT_DIR:olduvai_app>/Resources/Olduvai.icns
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/fonts
            $<TARGET_BUNDLE_CONTENT_DIR:olduvai_app>/Resources/fonts
        )
endif()

# ── developer build aggregates ──────────────────────────────────────────────
# The DEFAULT build produces the game only (target `olduvai`; plus the opt-in
# `olduvai_app` bundle on macOS).  Everything else — test binaries and dev
# tools — is walled behind explicit targets:
#   cmake --build --preset release --target tests   # then: ctest --preset release
#   cmake --build --preset release --target tools   # dev/debug utilities
# and lands in build/tests/ and build/tools/ respectively, so the build root
# holds only the game itself and its runtime data (data/, fonts/, generated/).
set(_olduvai_test_targets olduvai_tests test_settings_apply
    test_settings_session test_settings_flow test_widescreen
    test_boss_widescreen test_confirm_dialog test_screen_tiles
    test_sqz_parity test_text_editor test_report_templates)
if(TARGET olduvai_sdl_tests)
    list(APPEND _olduvai_test_targets olduvai_sdl_tests test_text_overlay_edit)
endif()
# The script-driven suites (golden_trace, the shot/scenario walks) execute
# the game and trace binaries, so `tests` builds those too.
add_custom_target(tests DEPENDS ${_olduvai_test_targets} olduvai olduvai_trace)
set_target_properties(${_olduvai_test_targets} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
if(TARGET fuzz_formats)
    set_target_properties(fuzz_formats PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endif()

set(_olduvai_tool_targets l2_spawn_count mdi_dump opl_music_dump olduvai_logo
    olduvai_trace)
foreach(_t screen_atlas widescreen_peek_shot secret_shot opl_sfx_dump
        descent_pan_shot)
    if(TARGET ${_t})
        list(APPEND _olduvai_tool_targets ${_t})
    endif()
endforeach()
add_custom_target(tools DEPENDS ${_olduvai_tool_targets})
set_target_properties(${_olduvai_tool_targets} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
