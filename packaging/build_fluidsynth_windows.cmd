@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Krzysztof Sokolowski
rem Build a MINIMAL, dependency-free FluidSynth DLL for the Windows package.
rem   usage: packaging\build_fluidsynth_windows.cmd <workdir> <out-dir>
rem
rem The cmd twin of build_fluidsynth.sh: same pinned version, same checksum,
rem same "-Dosal=embedded plus everything optional off" recipe.  It is a
rem separate script rather than a shared one because MSVC needs vcvars64
rem sourced into the environment first, exactly like the SDL2 step.
rem
rem MEASURED on a Windows builder (MSVC Build Tools 2022):
rem   default /MD build -> imports MSVCP140.dll + VCRUNTIME140.dll
rem   /MT build         -> imports ONLY KERNEL32.dll
rem The repo builds olduvai.exe with a static CRT so users need no VC++
rem redistributable (docs/BUILDING.md says so).  A /MD FluidSynth beside it
rem would quietly break that promise for anyone without the redistributable,
rem and the failure would look like "GM does not work" — indistinguishable
rem from a missing SoundFont.  Hence CMAKE_MSVC_RUNTIME_LIBRARY.
rem
rem Output DLL is named libfluidsynth-3.dll, which is already the first entry
rem in audio.cpp's load_fluidsynth() candidate list — no source change needed.
setlocal
set "WORK=%~1"
set "OUTDIR=%~2"
if "%WORK%"=="" set "WORK=%CD%\fsbuild-work"
if "%OUTDIR%"=="" set "OUTDIR=%CD%"

set FS_VER=2.5.7
set FS_SHA=CE27840221AB00DD59BF27E85ECBBA480C6C2A7C9FBEC4243658F68F59C07F4A

if not exist "%WORK%" mkdir "%WORK%"
pushd "%WORK%" || exit /b 1

if not exist fs.tar.gz (
  powershell -NoProfile -Command "Invoke-WebRequest -Uri https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v%FS_VER%.tar.gz -OutFile fs.tar.gz" || exit /b 1
)
powershell -NoProfile -Command "$h=(Get-FileHash fs.tar.gz -Algorithm SHA256).Hash; if ($h -ne '%FS_SHA%') { throw ('FluidSynth checksum mismatch: ' + $h) }" || exit /b 1
if exist fluidsynth-%FS_VER% rmdir /s /q fluidsynth-%FS_VER%
if exist build rmdir /s /q build
tar -xzf fs.tar.gz || exit /b 1

rem Everything optional OFF: each would be another DLL to ship, sign and
rem license, and none is reachable through the small C-API subset the engine
rem binds.  We drive the synth directly and do our own mixing/resampling, so
rem FluidSynth needs no audio or MIDI driver of its own.
rem ── WIN32 AND /w GO THROUGH CFLAGS, NOT -DCMAKE_C_FLAGS ─────────────────────
rem
rem THIS IS THE BUG.  FluidSynth's generated fluidsynth.h decides its export
rem visibility like this (include/fluidsynth.cmake:29-40):
rem
rem     #if (BUILD_SHARED_LIBS == 0)   -> FLUIDSYNTH_API empty
rem     #elif defined(WIN32)
rem         #if   defined(FLUIDSYNTH_NOT_A_DLL)  -> empty
rem         #elif defined(FLUIDSYNTH_DLL_EXPORTS) -> __declspec(dllexport)
rem         #else                                 -> __declspec(dllimport)
rem     #elif ... __GNUC__ -> visibility("default")
rem     #else  -> FLUIDSYNTH_API empty          <-- WHERE WE LANDED
rem
rem That tests the PREPROCESSOR macro WIN32, and MSVC does not define it — it
rem defines _WIN32.  Bare WIN32 comes from CMake's MSVC platform defaults
rem (/DWIN32 /D_WINDOWS).  Lose it and the chain falls past the WIN32 arm to
rem the final #else: FLUIDSYNTH_API expands to NOTHING, every symbol compiles
rem unexported, the DLL links with an EMPTY export table, MSVC emits no import
rem library, and fluidsynth.exe dies with LNK1181.  Note FLUIDSYNTH_DLL_EXPORTS
rem is defined the whole time and never consulted — which is why grepping
rem build.ninja for it says "present" and proves nothing.
rem
rem Passing -DCMAKE_C_FLAGS=... REPLACES CMAKE_C_FLAGS_INIT, and that is where
rem /DWIN32 /D_WINDOWS live.  So silencing warnings that way silently disarms
rem the export macro.  The CFLAGS/CXXFLAGS environment variables are COMBINED
rem with the platform defaults instead of overwriting them, so they are the
rem right lever — and /DWIN32 is repeated here explicitly so this build does
rem not depend on any particular CMake version still supplying it.
rem
rem /w itself: vendored code is not held to our warning bar (CMakeLists.txt,
rem olduvai_silence_vendored_target) — nine C4267/C4805/C5287 out of
rem fluid_dls.cpp and fluid_synth.c, upstream's to fix.
set "CFLAGS=/DWIN32 /D_WINDOWS /w"
set "CXXFLAGS=/DWIN32 /D_WINDOWS /w"
cmake -S fluidsynth-%FS_VER% -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -Dosal=embedded -DBUILD_SHARED_LIBS=ON ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -Denable-libsndfile=OFF -Denable-readline=OFF -Denable-network=OFF ^
  -Denable-aufile=OFF -Denable-portaudio=OFF -Denable-jack=OFF ^
  -Denable-pulseaudio=OFF -Denable-alsa=OFF -Denable-oss=OFF -Denable-dbus=OFF ^
  -Denable-libinstpatch=OFF -Denable-ipv6=OFF -Denable-openmp=OFF ^
  -Denable-sdl3=OFF -Denable-dsound=OFF -Denable-wasapi=OFF ^
  -Denable-waveout=OFF -Denable-winmidi=OFF || exit /b 1

rem Does the export macro actually reach the compiler?  This is the root
rem question behind an empty export table, and it is answerable from the
rem generated build graph without linking anything.
rem
rem FluidSynth defines FLUIDSYNTH_DLL_EXPORTS (top-level CMakeLists, inside
rem `if(WIN32)`) and applies it to the libfluidsynth-OBJ target via the legacy
rem COMPILE_FLAGS property.  The sources compile in that OBJECT library, not in
rem the shared target — so if that property does not land, every TU compiles
rem with no __declspec(dllexport), the DLL links with an EMPTY export table,
rem MSVC emits no import library, and the exe link dies with LNK1181.  That is
rem the whole suspected chain, and this findstr tests its first link.
echo === FLUIDSYNTH CONFIGURE CHECK ==============================================
rem WIN32 is the load-bearing one — see the autopsy above.  FLUIDSYNTH_DLL_EXPORTS
rem was present in build.ninja throughout the failure and never consulted, so
rem checking it proves nothing; this checks the gate that actually decided.
findstr /c:"/DWIN32" build\build.ninja >nul
if errorlevel 1 (
  echo   WIN32: ABSENT from build.ninja  ^<-- exports will be EMPTY, expect LNK1181
) else (
  echo   WIN32: defined - the export arm of fluidsynth.h is reachable
)
rem The GENERATED header is the ground truth, and it distinguishes the two ways
rem FLUIDSYNTH_API can end up empty: `#cmakedefine01 BUILD_SHARED_LIBS` landing
rem as 0 (first arm), versus WIN32 undefined (falls to the final #else).  Print
rem it rather than infer it — the whole reason this took a day is that
rem FLUIDSYNTH_DLL_EXPORTS looked right while the gate above it decided.
if exist "build\include\fluidsynth.h" (
  echo   --- generated fluidsynth.h, export gate ---
  findstr /n /c:"BUILD_SHARED_LIBS" /c:"FLUIDSYNTH_API" "build\include\fluidsynth.h"
) else (
  echo   --- generated fluidsynth.h NOT FOUND under build\include ---
  dir /s /b build\fluidsynth.h 2>nul
)
echo === END FLUIDSYNTH CONFIGURE CHECK =========================================
rem Build ONLY the library target, not `all`.
rem
rem FluidSynth's `all` also builds its CLI program, `fluidsynth.exe`, which is a
rem separate target that LINKS AGAINST the DLL's import library.  We have never
rem needed either one: this engine dlopen's libfluidsynth-3.dll at runtime
rem (audio.cpp), our CMakeLists does not link fluidsynth at all, and the copy
rem step below takes the DLL and nothing else.  So the exe and the .lib were
rem always pure waste, and building them means a link step that can fail for
rem reasons that cannot affect us.
rem
rem It does exactly that on the self-hosted runner, where the exe link dies with
rem LNK1181 "cannot open input file 'src\libfluidsynth-3.lib'".  Not building the
rem exe removes the failing edge outright.
rem
rem NOT A COMPILER-VERSION PROBLEM.  That was the first theory and a toolset
rem matrix killed it on its first run: 14.44.35207 (MSVC 19.44 — the very
rem toolset GitHub's windows-latest ships, where this script is green) and
rem 14.51.36231 (VS 2026) fail IDENTICALLY here.  Same compiler, different
rem result, so the variable is this environment, not the toolchain.  Do not
rem re-derive that; it costs a matrix run to disprove.
rem
rem NOT A DIAGNOSIS EITHER.  If the .lib is missing because nothing was
rem exported, the DLL is exportless and useless to a runtime dlopen, and the
rem export assertion further down is what must catch it.
cmake --build build --parallel --target libfluidsynth
set FS_BUILD_RC=%ERRORLEVEL%

rem ── Diagnostics, printed ALWAYS, including when the build just failed ──────
rem The runner wipes its workspace after every job, so the CI log is the only
rem place these can be read — that is why they are here and not in a runbook.
rem Two questions, and between them they discriminate the whole failure:
rem   1. Is the export table empty?  (the standing hypothesis)
rem   2. Was an import library produced ANYWHERE?  (FluidSynth sets
rem      IMPORT_PREFIX "lib" + ARCHIVE_OUTPUT_NAME, and where CMake puts an
rem      import lib has moved across versions — a .lib in the wrong directory
rem      gives this identical LNK1181 with exports perfectly intact.)
echo === FLUIDSYNTH DIAGNOSTICS (build rc=%FS_BUILD_RC%) ==========================
echo --- every .lib under build\ ---
dir /s /b build\*.lib 2>nul || echo   (no .lib anywhere)
echo --- every .dll under build\ ---
dir /s /b build\*.dll 2>nul || echo   (no .dll anywhere)
if exist "build\src\libfluidsynth-3.dll" (
  echo --- dumpbin /EXPORTS ---
  dumpbin /EXPORTS "build\src\libfluidsynth-3.dll"
) else (
  echo --- no DLL to inspect ---
)
echo === END FLUIDSYNTH DIAGNOSTICS ==============================================

if not "%FS_BUILD_RC%"=="0" exit /b %FS_BUILD_RC%

if not exist "build\src\libfluidsynth-3.dll" (
  echo build_fluidsynth_windows: expected build\src\libfluidsynth-3.dll, got: >&2
  dir /b /s build\*.dll >&2
  exit /b 1
)

rem Assert the point of the exercise before shipping it: a static-CRT,
rem dependency-free DLL imports nothing but system libraries.  glib,
rem MSVCP140 or VCRUNTIME140 appearing here means the recipe regressed.
dumpbin /dependents "build\src\libfluidsynth-3.dll" | findstr /i /c:"glib" /c:"MSVCP140" /c:"VCRUNTIME140" >nul
if not errorlevel 1 (
  echo build_fluidsynth_windows: DLL has non-system dependencies: >&2
  dumpbin /dependents "build\src\libfluidsynth-3.dll" >&2
  exit /b 1
)

rem Assert the DLL actually EXPORTS the C API we bind.  A dlopen'd library with
rem an empty export table loads fine and then fails every symbol lookup — the
rem silent-capability-loss shape that hid the four-release packaging gap, and
rem the exact failure mode suspected behind the MSVC 2026 missing-import-library
rem error above (no exports produces no .lib).  /dependents cannot see it.
rem
rem These twelve are the whole surface: audio.cpp's dyn_sym list.  If FluidSynth
rem renames one, this fails loudly at build time rather than as "GM is silent".
for %%S in (new_fluid_settings fluid_settings_setnum new_fluid_synth ^
            fluid_synth_sfload fluid_synth_noteon fluid_synth_noteoff ^
            fluid_synth_program_change fluid_synth_cc fluid_synth_pitch_bend ^
            fluid_synth_write_s16 delete_fluid_synth delete_fluid_settings) do (
  rem Leading space, no end-anchor: dumpbin indents the name, and anchoring on
  rem $ is fragile against its CRLF/trailing whitespace.  A loose match is the
  rem right trade here — the failure being guarded is an EMPTY export table, in
  rem which case nothing matches either way, and a spurious FAIL would block the
  rem build for no reason.
  dumpbin /exports "build\src\libfluidsynth-3.dll" | findstr /c:" %%S" >nul
  if errorlevel 1 (
    echo build_fluidsynth_windows: DLL does not export %%S >&2
    echo   the export table is empty or incomplete - GM would load and be silent >&2
    exit /b 1
  )
)

copy /y "build\src\libfluidsynth-3.dll" "%OUTDIR%\" >nul || exit /b 1
popd
echo minimal FluidSynth %FS_VER% ready: %OUTDIR%\libfluidsynth-3.dll
