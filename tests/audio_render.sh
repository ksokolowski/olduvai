#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Audio render gate (audio-harness Phase 2).  Renders a synthetic MIDI through
# each synth backend TWICE via the shipping --render-audio CLI (one fresh
# process each) and asserts the PCM hash is identical run-to-run — the
# determinism the audio DIP refactor's before/after check relies on.
#
# Data-gated: --render-audio exits 77 when a backend can't load its assets
# (MT-32 ROMs / a GM SoundFont).  If NO backend renders, the whole test exits
# 77 (CTest SKIP) so a runner without ROMs/SoundFont stays green.
#   usage: audio_render.sh <path-to-olduvai>
set -eu

BIN="${1:?usage: audio_render.sh <olduvai binary>}"
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
fixture="${TMPDIR:-/tmp}/olduvai_audio_fixture.mid"

python3 "${here}/audio_fixture.py" "${fixture}" \
    || { echo "audio_render: could not generate the MIDI fixture" >&2; exit 1; }

ran=0
for dev in gm-builtin mt32-builtin; do
    if h1="$("${BIN}" --render-audio "${fixture}" --music-device "${dev}" \
                      --audio-rate 44100 --render-audio-secs 1 2>/dev/null)"; then
        h2="$("${BIN}" --render-audio "${fixture}" --music-device "${dev}" \
                       --audio-rate 44100 --render-audio-secs 1 2>/dev/null)"
        if [ "${h1}" != "${h2}" ]; then
            echo "audio_render: ${dev} is NON-DETERMINISTIC:" >&2
            echo "  run1: ${h1}" >&2
            echo "  run2: ${h2}" >&2
            exit 1
        fi
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
