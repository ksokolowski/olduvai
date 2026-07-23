# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski

cmake_minimum_required(VERSION 3.21)
project(olduvai
    VERSION 0.9.3
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

# Address + UB sanitizers for a verification build (local runs and the CI
# sanitize lane).  UB is made FATAL (-fno-sanitize-recover) so ctest fails on
# any hit instead of scrolling past a warning.  Forces LTO off (IPO+ASan
# interact badly) and keeps frame pointers for readable stacks.  Use a
# separate build tree: cmake --preset asan (build/asan)
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
    src/presentation/text_editor.cpp
    src/enhance/enhanced_hud.cpp
    src/prepare/exe_tables.cpp
    src/prepare/cache_paths.cpp
    src/prepare/game_files.cpp
    src/prepare/prepare.cpp
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
target_include_directories(olduvai_formats PUBLIC src third_party/stb)
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
        src/presentation/midi_seq.cpp)
    target_compile_options(fuzz_formats PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_formats PRIVATE -fsanitize=fuzzer)
    target_link_libraries(fuzz_formats PRIVATE olduvai_formats)
endif()

# ── presentation (SDL2; optional so headless format work still builds) ─────
find_package(SDL2 QUIET)
# Single point of truth for which SDL2 flavour every target links.
set(OLDUVAI_SDL2 SDL2::SDL2)
if(WIN32 AND OLDUVAI_STATIC_SDL AND TARGET SDL2::SDL2-static)
    set(OLDUVAI_SDL2 SDL2::SDL2-static)
endif()
if(SDL2_FOUND)
    add_library(olduvai_presentation
        src/presentation/banners.cpp
        src/presentation/viewer.cpp
        src/presentation/gamepad.cpp
        src/presentation/game_render.cpp
        src/presentation/sprite_blit.cpp
        src/presentation/bg_compose.cpp
        src/presentation/level_save.cpp
        src/presentation/level_setup.cpp
        src/presentation/pause_flow.cpp
        src/presentation/pause_service.cpp
    src/presentation/report_form.cpp
    src/presentation/tile_patterns.cpp
        src/presentation/screen_tiles.cpp
        src/presentation/game_app.cpp
        src/presentation/hud_render.cpp
        src/presentation/debug_overlay.cpp
        src/presentation/screens.cpp
        src/presentation/text_overlay.cpp
        src/presentation/title_menu_flow.cpp
        src/presentation/transition_players.cpp
        src/presentation/widescreen_presenter.cpp
        src/presentation/window_util.cpp
        src/presentation/boss_app.cpp
        src/presentation/boss_render.cpp
        src/presentation/audio.cpp
        src/presentation/host_midi.cpp
        src/presentation/opl_sfx.cpp
        src/presentation/midi_seq.cpp
        src/presentation/replay.cpp
        src/presentation/image_out.cpp
        src/presentation/bug_capture.cpp
        src/presentation/mini_json.cpp
        src/presentation/menu.cpp
        src/presentation/menu_model.cpp
        src/presentation/menu_render.cpp
        src/presentation/text_overlay_edit.cpp
        src/presentation/settings_apply.cpp
        src/presentation/settings_session.cpp
        src/presentation/settings_flow.cpp
        src/presentation/confirm_dialog.cpp
        src/presentation/save_state.cpp
        src/presentation/widescreen.cpp
        src/presentation/boss_widescreen.cpp
        src/presentation/l3_end_level.cpp
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
    target_sources(olduvai_presentation PRIVATE src/presentation/opl_music.cpp)

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
        target_include_directories(olduvai_presentation PRIVATE third_party)
        target_compile_definitions(olduvai_presentation PRIVATE
            OLDUVAI_HAVE_RTMIDI=1)
        # RtMidi's own source trips -Wall/-Wextra; keep our warnings clean by
        # not promoting them to errors there (we never error on warnings, but
        # silence the noise on the vendored TU).
        set_source_files_properties(third_party/rtmidi/RtMidi.cpp PROPERTIES
            COMPILE_OPTIONS "-w")
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
add_executable(olduvai src/app/main.cpp src/app/config.cpp
                       src/app/options_resolve.cpp src/app/cli_args.cpp)
target_link_libraries(olduvai PRIVATE olduvai_formats)
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
target_include_directories(olduvai_logo PRIVATE src third_party/stb)

add_executable(opl_music_dump EXCLUDE_FROM_ALL tools/opl_music_dump.cpp
    src/presentation/opl_music.cpp
    third_party/nuked_opl3/opl3.c)
target_include_directories(opl_music_dump PRIVATE third_party/nuked_opl3)
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
    tests/test_mdi.cpp
    tests/test_transitions.cpp
    tests/test_screen_topology.cpp
    tests/test_upscale.cpp
    tests/test_hd_asset_cache.cpp
    tests/test_hd_asset_cache_disk.cpp
    tests/test_fluid_bubbles.cpp
    tests/test_cache_paths.cpp
    tests/test_game_files.cpp
    tests/test_prepare.cpp
    tests/test_menu.cpp
    tests/test_menu_model.cpp
    tests/test_save_state.cpp
    tests/test_soundfont_pick.cpp
    tests/test_resample.cpp
    tests/test_wav_io.cpp
    tests/test_opl_music.cpp
    tests/test_options_resolve.cpp
    src/app/config.cpp
    src/app/options_resolve.cpp
    src/presentation/opl_music.cpp
    third_party/nuked_opl3/opl3.c
    src/presentation/mini_json.cpp
    src/presentation/menu.cpp
    src/presentation/menu_model.cpp
    src/presentation/save_state.cpp
)
target_compile_definitions(olduvai_tests PRIVATE
    OLDUVAI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Bundle the menu model next to the binary for runtime discovery.
# configure_file (not file(COPY)) so edits to assets/data/menus.json re-copy on the
# next build instead of going stale until a manual re-configure.
configure_file(${CMAKE_SOURCE_DIR}/assets/data/menus.json
               ${CMAKE_BINARY_DIR}/data/menus.json COPYONLY)

# ALSO compile the menu model into the binary (raw string literal) so a
# lone executable still has its pause menu — the on-disk data/menus.json
# (searched first at runtime) remains the override for customisation.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/menus_embedded.cpp
    COMMAND ${CMAKE_COMMAND}
            -DIN=${CMAKE_SOURCE_DIR}/assets/data/menus.json
            -DOUT=${CMAKE_BINARY_DIR}/generated/menus_embedded.cpp
            -DSYMBOL=embedded_menus_json
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_text.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/data/menus.json
            ${CMAKE_SOURCE_DIR}/cmake/embed_text.cmake
    COMMENT "Embedding data/menus.json")
if(SDL2_FOUND)
    target_sources(olduvai_presentation PRIVATE
        ${CMAKE_BINARY_DIR}/generated/menus_embedded.cpp)
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
# presentation lib), so it needs the generated embedded-model TU too.  Without
# it the load_menus_embedded reference only linked because LTO dead-stripped
# it — a works-by-accident graph the OLDUVAI_SANITIZE lane (LTO off) caught.
target_sources(olduvai_tests PRIVATE
    ${CMAKE_BINARY_DIR}/generated/menus_embedded.cpp)
target_include_directories(olduvai_tests PRIVATE third_party
                                                 third_party/nuked_opl3)
target_link_libraries(olduvai_tests PRIVATE olduvai_formats)
add_test(NAME unit COMMAND olduvai_tests)

# Pure (no-SDL) settings-apply tier classifier test.
add_executable(test_settings_apply EXCLUDE_FROM_ALL
    tests/test_settings_apply.cpp
    src/presentation/settings_apply.cpp)
target_include_directories(test_settings_apply PRIVATE src)
add_test(NAME settings_apply COMMAND test_settings_apply)

add_executable(test_settings_session EXCLUDE_FROM_ALL
    tests/test_settings_session.cpp
    src/presentation/settings_session.cpp)
target_include_directories(test_settings_session PRIVATE src)
add_test(NAME settings_session COMMAND test_settings_session)

# Pure (no-SDL) SettingsFlow controller test (OL-B1): options-subtree
# membership derived from a synthetic MenuModel + confirm-dialog lifecycle +
# apply/discard resolution through the injected hooks.
add_executable(test_settings_flow EXCLUDE_FROM_ALL
    tests/test_settings_flow.cpp
    src/presentation/settings_flow.cpp
    src/presentation/settings_apply.cpp
    src/presentation/settings_session.cpp
    src/presentation/confirm_dialog.cpp)
target_include_directories(test_settings_flow PRIVATE src)
add_test(NAME settings_flow COMMAND test_settings_flow)

# Pure (no-SDL) widescreen neighbor-resolution decision-table test.
# widescreen.cpp's transitive includes pull no SDL, so this links SDL-free.
add_executable(test_widescreen EXCLUDE_FROM_ALL
    tests/test_widescreen.cpp
    src/presentation/widescreen.cpp)
target_include_directories(test_widescreen PRIVATE src)
add_test(NAME widescreen COMMAND test_widescreen)

# Pure (no-SDL) boss_widescreen helpers test (margin + surgical HUD inpaint +
# compose_widescreen reflect_pure invariants — Task 6).
add_executable(test_boss_widescreen EXCLUDE_FROM_ALL
    tests/test_boss_widescreen.cpp
    src/presentation/boss_widescreen.cpp
    src/presentation/widescreen.cpp)
target_include_directories(test_boss_widescreen PRIVATE src)
add_test(NAME boss_widescreen COMMAND test_boss_widescreen)

add_executable(test_confirm_dialog EXCLUDE_FROM_ALL
    tests/test_confirm_dialog.cpp
    src/presentation/confirm_dialog.cpp
    src/presentation/settings_session.cpp)
target_include_directories(test_confirm_dialog PRIVATE src)
add_test(NAME confirm_dialog COMMAND test_confirm_dialog)

# Pure (no-SDL) shared surface-tile constructor identity gate (OL-B2):
# build_screen_tiles vs the frozen pre-refactor bind/peek construction, on
# real game data, every surface screen of levels 1/3/5/7 × enhanced flags.
# Skips (77) when game data is absent — golden_trace.sh pattern.
add_executable(test_screen_tiles EXCLUDE_FROM_ALL
    tests/test_screen_tiles_unify.cpp
    src/presentation/screen_tiles.cpp
    src/presentation/tile_patterns.cpp)
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
target_include_directories(test_text_editor PRIVATE third_party src)
add_test(NAME text_editor COMMAND test_text_editor)

add_executable(test_report_templates EXCLUDE_FROM_ALL tests/test_report_templates.cpp)
target_link_libraries(test_report_templates PRIVATE olduvai_formats)
target_include_directories(test_report_templates PRIVATE third_party src)
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
        tests/test_pause_actions.cpp)
    target_include_directories(olduvai_sdl_tests PRIVATE third_party)
    target_link_libraries(olduvai_sdl_tests PRIVATE olduvai_presentation
                                                    ${OLDUVAI_SDL2})
    add_test(NAME sdl_unit COMMAND olduvai_sdl_tests)

    add_executable(test_text_overlay_edit EXCLUDE_FROM_ALL
        tests/test_text_overlay_edit.cpp)
    target_include_directories(test_text_overlay_edit PRIVATE third_party src)
    target_link_libraries(test_text_overlay_edit PRIVATE olduvai_presentation
                                                         ${OLDUVAI_SDL2})
    add_test(NAME text_overlay_edit COMMAND test_text_overlay_edit)

    # Headless reinit integration smoke test: drives OLDUVAI_REINIT_TEST to
    # trigger a render_scale 2→4 reinit on frame 5 and asserts the logical
    # window size is 1280×800 and player position is preserved across the
    # rebuild.  The shell wrapper skips (exit 77) when game data is absent so
    # CI without assets stays green.
    # Windows cannot exec a #! script directly — run the .sh wrappers through
    # sh (present in MSYS2 and Git for Windows).  Empty prefix elsewhere.
    if(WIN32)
        find_program(OLDUVAI_TEST_SH NAMES sh bash)
    endif()
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
endif()

# Bundle the HD font (OFL) next to the binary for runtime discovery.
# configure_file (not file(COPY)) — same stale-copy hazard class that bit
# build/data/menus.json: file(COPY) only runs at configure time.
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
        src/app/main.cpp src/app/config.cpp src/app/options_resolve.cpp
        src/app/cli_args.cpp src/app/first_run.cpp)
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
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/assets/data/menus.json
            $<TARGET_BUNDLE_CONTENT_DIR:olduvai_app>/Resources/data/menus.json)
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
