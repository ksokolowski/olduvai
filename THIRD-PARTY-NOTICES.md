# Third-party notices

Olduvai is GPL-3.0-or-later (see [LICENSE](LICENSE)). It vendors, bundles, or
optionally loads the third-party components below. Release binaries carry this
file plus the referenced license texts in a `licenses/` directory.

**Corresponding source:** the complete source for every release is available
from the same release page (the *Source code* archive GitHub attaches to each
release) and from the repository itself.

| Component | Upstream | License | How it ships |
|---|---|---|---|
| Nuked-OPL3 | [nukeykt/Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | LGPL-2.1-or-later | **Compiled into every binary** (`third_party/nuked_opl3/`). Conveyed as part of the GPL-3.0 work under LGPL-2.1 §3 (conversion to GPL). Full text: `third_party/nuked_opl3/LICENSE`. |
| SDL2 | [libsdl.org](https://libsdl.org) | zlib | Bundled shared library (Linux AppImage, macOS app bundle, Windows MSVC zip — `SDL2.dll`). |
| FluidSynth | [fluidsynth.org](https://www.fluidsynth.org) | LGPL-2.1 | **Linux AppImage only:** bundled as a *replaceable shared object* (`libfluidsynth.so.3` — swap it to exercise LGPL §6 relinking). Elsewhere loaded at runtime from the user's system if installed; never statically linked. |
| libmt32emu (munt) | [munt/munt](https://github.com/munt/munt) | LGPL-2.1 | **Never distributed.** Loaded at runtime (`dlopen`) from the user's system when present. |
| RtMidi | [thestk/rtmidi](https://github.com/thestk/rtmidi) | RtMidi license (MIT-style) | Compiled in when host-MIDI support is built (`third_party/rtmidi/`). Full text: `third_party/rtmidi/LICENSE`. |
| stb (`stb_image`, `stb_image_write`, `stb_truetype`) | [nothings/stb](https://github.com/nothings/stb) | MIT / public domain (dual) | Compiled in (`third_party/stb/`; license text embedded in each header). |
| Freckle Face | Google Fonts | SIL OFL 1.1 | Bundled font file; license ships beside it (`fonts/FreckleFace-LICENSE.txt`). |
| Noto Sans | Google Fonts | SIL OFL 1.1 | Bundled font file; license ships beside it (`fonts/NotoSans-LICENSE.txt`). |
| doctest | [doctest/doctest](https://github.com/doctest/doctest) | MIT ([license text](third_party/doctest/LICENSE.txt)) | Test framework only — **not part of any release binary**. |
| AppImage runtime | [AppImage project](https://github.com/AppImage/type2-runtime) | MIT | Embedded in the `.AppImage` file by `appimagetool` at packaging time. |

The Linux AppImage additionally bundles the shared libraries the above depend
on (resolved by `linuxdeploy` from the build host); each retains its own
upstream license — see the respective projects.

Olduvai contains no code, art, audio, or data from the original game; see the
README **Legal** section.
