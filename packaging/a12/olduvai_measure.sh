#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Runs ON the device, installed at /userdata/system/olduvai_measure.sh.
# Usage: OLDUVAI_MEASURE_FLAGS="..." olduvai_measure.sh [extra olduvai args]
# Olduvai measurement service - clean DRM handoff for SSH-driven runs.
# 2026-09-12 chain: SSH shell has no VT, so SDL KMSDRM cannot get DRM master
# (pageflip -22). SIGSTOPping ES keeps ES holding master. Full stop +
# openvt -s on a free console VT lets KMS master + present succeed.
# NOTE: renderer:/frame-stats lines only print when OLDUVAI_FRAME_STATS=1,
# and olduvai.env does not set it - so export it here.
LOG=/userdata/system/logs/olduvai_measure.log
FLAGS="${OLDUVAI_MEASURE_FLAGS:---no-config --default-profile hd-handheld}"
VT=2
while ps -o tty= | grep -q " tty$VT"; do VT=$((VT+1)); done
/etc/init.d/S31emulationstation stop
sleep 2
cd /userdata/roms/ports/olduvai || exit 2
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
export OLDUVAI_FRAME_STATS=1
. ./olduvai.env
INNER="./olduvai --game-dir ./game --rom-dir ./mt32-roms $FLAGS $* > \"$LOG\" 2>&1"
echo "=== measure run: $(date) on vt$VT ==="
openvt -s -c "$VT" -- sh -c "$INNER" olduvai_measure
for i in $(seq 1 15); do
  pgrep -x olduvai >/dev/null && break
  sleep 1
done
for i in $(seq 1 300); do
  pgrep -x olduvai >/dev/null || break
  sleep 1
done
chvt 1
/etc/init.d/S31emulationstation start >/dev/null 2>&1
echo "=== done; ES restarted ==="
