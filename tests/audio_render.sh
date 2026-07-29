#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Audio render gate (audio-harness Phase 2).  Renders a synthetic MIDI through
# each synth backend TWICE via the shipping --render-audio CLI (one fresh
# process each) and asserts the PCM hash is identical run-to-run — the
# determinism the audio DIP refactor's before/after check relies on — AND that
# the result is not silence.
#
# WHY THE NON-SILENCE CHECK EXISTS.  Determinism alone is a vacuous assertion:
# any two runs of silence hash the same.  This gate ran and PASSED for its
# whole life while certifying 44100 frames of pure digital silence — measured
# peak 0, rms 0, not one non-zero sample — because the fixture put its notes on
# MIDI channel 0 (unassigned on MT-32) and its only channel-1 notes at exactly
# t=1.000 s, one sample past the render window.  See tests/audio_fixture.py for
# both causes.  A hash check cannot notice that; a peak check cannot miss it.
#
# Deliberately NOT a checked-in golden hash: std::pow in the OPL pitch table
# feeds an integer quantisation and the frame accumulator is an FMA-contraction
# site under LTO, so a pinned hash would fail across libm/compiler versions for
# reasons unrelated to any regression.  Run-to-run equality plus "it actually
# sounds" is the honest pair.
#
# Data-gated: --render-audio exits 77 when a backend can't load its assets
# (MT-32 ROMs / a GM SoundFont).  If NO backend renders, the whole test exits
# 77 (CTest SKIP) so a runner without ROMs/SoundFont stays green.
#   usage: audio_render.sh <path-to-olduvai>
set -eu

BIN="${1:?usage: audio_render.sh <olduvai binary>}"
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
fixture="${TMPDIR:-/tmp}/olduvai_audio_fixture.mid"

# python3 drives the fixture and the analysis.  SKIP rather than fail when it
# is absent: the self-hosted Windows builder has no Python, and a hard failure
# there would break `ctest --preset release` in the packaging job for a
# missing TEST dependency, not a product defect.  Same convention as the
# data-gated tests.
if ! command -v python3 >/dev/null 2>&1; then
    echo "audio_render: SKIP — python3 not available"
    exit 77
fi
python3 "${here}/audio_fixture.py" "${fixture}" \
    || { echo "audio_render: could not generate the MIDI fixture" >&2; exit 1; }

# PIN THE ASSETS.  Both synths are asset-driven, so "the render changed" is
# only a useful signal if the inputs did not: a different SoundFont or a
# different ROM revision produces legitimately different audio, and comparing
# across machines without pinning would just measure which files each happens
# to have.  Roland SC-55 is the preferred face (find_soundfont ranks it first)
# and the one ScummVM ships, so it is the reproducible choice.
sha() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else shasum -a 256 "$1"; fi | cut -c1-12
}
SF=""
for d in "${OLDUVAI_SOUNDFONT_DIR:-}" /opt/homebrew/share/scummvm \
         /usr/local/share/scummvm /usr/share/scummvm /usr/share/sounds/sf2 \
         "$(dirname "${BIN}")/soundfonts" ./soundfonts; do
    [ -n "${d}" ] && [ -f "${d}/Roland_SC-55.sf2" ] && { SF="${d}/Roland_SC-55.sf2"; break; }
done
SF_ARGS=""
[ -n "${SF}" ] && SF_ARGS="--soundfont ${SF}"
# Same idea for the ROMs: whichever dir the engine will actually use.
ROM_DIR="${OLDUVAI_MT32_ROMS:-${HOME:-}/mt32-roms}"
[ -d "${ROM_DIR}" ] || ROM_DIR="./mt32-roms"

# Fingerprint ONLY the two ROMs the engine will actually load, and print them
# under their canonical lowercase identity — so a machine holding
# MT32_CONTROL.ROM and one holding mt32_control.rom produce the SAME line for
# the same bytes.  Fingerprinting the whole directory instead would compare
# "how many spare ROM dumps does this box have", which is not the question.
find_rom() {   # $1 = canonical lowercase identity -> path, or empty
    # MUST always exit 0: this runs inside `c="$(find_rom ...)"` under set -e,
    # and a "not found" status would abort the whole script rather than leave
    # ${c} empty.  (It did — the CM-32L probe killed the run before the MT-32
    # pair was ever considered.)
    for f in "${ROM_DIR}"/*; do
        [ -f "${f}" ] || continue
        if [ "$(basename "${f}" | tr 'A-Z' 'a-z')" = "$1" ]; then
            echo "${f}"
            return 0
        fi
    done
    return 0
}
asset_fingerprint() {
    [ -n "${SF}" ] && echo "soundfont $(sha "${SF}") $(basename "${SF}")"
    # Mirror add_rom_pair(): CM-32L first, then MT-32, and only a COMPLETE
    # pair counts.  Listing every ROM identity present would fingerprint the
    # spare dumps a machine happens to keep, so two boxes that load the same
    # pair would look different — the fingerprint must describe what the
    # engine LOADS, not what is lying around.
    for pair in "cm32l_control.rom cm32l_pcm.rom" "mt32_control.rom mt32_pcm.rom"; do
        set -- ${pair}
        c="$(find_rom "$1")"; m="$(find_rom "$2")"
        if [ -n "${c}" ] && [ -n "${m}" ]; then
            echo "rom $(sha "${c}") $1"
            echo "rom $(sha "${m}") $2"
            return
        fi
    done
}

ran=0
for dev in gm-builtin mt32-builtin; do
    # shellcheck disable=SC2086  # SF_ARGS is deliberately word-split
    if h1="$("${BIN}" --render-audio "${fixture}" --music-device "${dev}" ${SF_ARGS} \
                      --audio-rate 44100 --render-audio-secs 1 2>/dev/null)"; then
        # shellcheck disable=SC2086
        h2="$("${BIN}" --render-audio "${fixture}" --music-device "${dev}" ${SF_ARGS} \
                       --audio-rate 44100 --render-audio-secs 1 2>/dev/null)"
        if [ "${h1}" != "${h2}" ]; then
            echo "audio_render: ${dev} is NON-DETERMINISTIC:" >&2
            echo "  run1: ${h1}" >&2
            echo "  run2: ${h2}" >&2
            exit 1
        fi

        # Determinism is only half the assertion — prove it is not silence.
        wav="${TMPDIR:-/tmp}/olduvai_audio_${dev}.wav"
        # shellcheck disable=SC2086
        "${BIN}" --render-audio "${fixture}" --music-device "${dev}" ${SF_ARGS} \
                 --audio-rate 44100 --render-audio-secs 1 \
                 --render-audio-out "${wav}" >/dev/null 2>&1
        # Invariants any correct render holds, on any platform: audible, both
        # channels alive, and the energy sitting at the FIXTURE's own pitches
        # rather than somewhere else.  A hash cannot name which of those broke.
        if ! python3 "${here}/audio_analyze.py" check "${wav}"; then
            echo "audio_render: ${dev} failed the render invariants" >&2
            echo "  Check that the fixture's notes are on a channel this" >&2
            echo "  backend assigns (MT-32 leaves channel 0 unassigned), that" >&2
            echo "  they start inside the --render-audio-secs window, and that" >&2
            echo "  the SoundFont/ROMs actually loaded." >&2
            rm -f "${wav}"
            exit 1
        fi

        # Optional CROSS-PLATFORM check (internal).  Render on one machine,
        # copy the WAVs over, point this at them on the other:
        #   OLDUVAI_AUDIO_REF=/path/to/refs ctest --preset release -R audio_render
        # Correlation, not equality: measured macOS/arm64 vs Windows/x86-64,
        # MT-32 differs in 97.6% of SAMPLES (float LA32 + IIR reverb, and it is
        # the synth core — rendering at MT-32's native 32 kHz with no resampler
        # is no better) yet correlates at 0.9989.  GM correlates at 1.000000.
        # A genuinely wrong render — other SoundFont, other ROM set, wrong
        # rate — lands far below the 0.99 bar.
        if [ -n "${OLDUVAI_AUDIO_REF:-}" ] && [ -f "${OLDUVAI_AUDIO_REF}/${dev}.assets" ]; then
            here_assets="$(asset_fingerprint 2>/dev/null)"
            if [ "${here_assets}" != "$(cat "${OLDUVAI_AUDIO_REF}/${dev}.assets")" ]; then
                echo "audio_render: ${dev} — reference was built from DIFFERENT assets;" >&2
                echo "  comparing would measure the assets, not the engine. Refusing." >&2
                exit 1
            fi
        fi
        if [ -n "${OLDUVAI_AUDIO_REF:-}" ] && [ -f "${OLDUVAI_AUDIO_REF}/${dev}.wav" ]; then
            if ! python3 "${here}/audio_analyze.py" compare \
                    "${OLDUVAI_AUDIO_REF}/${dev}.wav" "${wav}"; then
                echo "audio_render: ${dev} does not match the reference render" >&2
                rm -f "${wav}"
                exit 1
            fi
        fi

        # Keep the render when asked, so it can become the other side of the
        # comparison above.
        if [ -n "${OLDUVAI_AUDIO_OUT:-}" ]; then
            mkdir -p "${OLDUVAI_AUDIO_OUT}"
            cp "${wav}" "${OLDUVAI_AUDIO_OUT}/${dev}.wav"
            # The asset fingerprint travels WITH the reference: a correlation
            # failure then means "the render changed", not "this machine has a
            # different SoundFont", which would be a useless signal.
            asset_fingerprint > "${OLDUVAI_AUDIO_OUT}/${dev}.assets" 2>/dev/null || true
        fi
        rm -f "${wav}"
        echo "audio_render: ${dev} OK — deterministic (${h1})"
        ran=$((ran + 1))
    else
        rc=$?
        if [ "${rc}" -eq 77 ]; then
            echo "audio_render: ${dev} — SKIP (assets absent)"
        else
            echo "audio_render: ${dev} render failed (exit ${rc})" >&2
            exit 1
        fi
    fi
done

if [ "${ran}" -eq 0 ]; then
    echo "audio_render: SKIP — no synth backend loaded" \
         "(set \$OLDUVAI_SOUNDFONT and/or \$OLDUVAI_MT32_ROMS)" >&2
    exit 77
fi
