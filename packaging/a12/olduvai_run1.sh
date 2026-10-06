#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Runs ON the A12 or the TrimUI (KNULLI, /userdata/system/olduvai_run1.sh) or
# the R36S (ArkOS, as root) (docs/internal/HANDHELD_BENCHMARK.md): ONE measured
# run for a SERIES taken with EmulationStation already stopped
# (KNULLI /etc/init.d/S31emulationstation stop, ArkOS systemctl stop
# emulationstation, once; start it again after the series).  Unlike
# olduvai_measure.sh it never touches ES, and it refuses to
# run unless the panel is on and ES is down (es=0),
# logging panel and ES state before, every 5 s during, and after the run — a run whose
# log shows anything but "On/0/es=0" is not a measurement.
# Owner, 2026-09-17: the first series ran with ES back up and the panel off.
# usage: run1.sh <bin> <log> "<env assignments>" <olduvai args...>
BIN=$1; LOG=$2; ENVS=$3; shift 3
cd /userdata/roms/ports/olduvai 2>/dev/null || cd /roms/ports/olduvai || exit 2
# "On/0/es=0" when the panel is lit and ES is down.  The A12 (DPI) and the
# R36S (DSI) have DRM connector state and a backlight class; the TrimUI's disp
# driver reports "unblank" and backlight(N) in one sysfs file instead.
lit() {
  conn=$(ls -d /sys/class/drm/card0-DPI-1 /sys/class/drm/card0-DSI-1 2>/dev/null | head -1)
  if [ -n "$conn" ]; then
    echo "$(cat "$conn/dpms")/$(cat /sys/class/backlight/backlight/bl_power)"
  else
    case "$(cat /sys/devices/virtual/disp/disp/attr/sys 2>/dev/null)" in
      *unblank*"backlight(0)"*) echo "Off/1" ;;
      *unblank*backlight*) echo "On/0" ;;
      *) echo "Off/1" ;;
    esac
  fi
}
# KNULLI's espid is 0 when ES is down; ArkOS has no swissknife.
es() {
  if command -v knulli-es-swissknife >/dev/null; then knulli-es-swissknife --espid
  else pidof emulationstation || echo 0; fi
}
panel() { echo "$(lit)/es=$(es)"; }
echo "PRE $(panel)" > "$LOG.state"
case "$(panel)" in On/0/es=0) ;; *) echo "ABORT: not ready $(panel)"; exit 3 ;; esac
VT=2; while ps -o tty= | grep -q " tty$VT"; do VT=$((VT+1)); done
openvt -s -c "$VT" -- sh -c "export XDG_RUNTIME_DIR=/var/run OLDUVAI_FRAME_STATS=1 $ENVS; [ -f ./olduvai.env ] && . ./olduvai.env; ./$BIN --game-dir ./game --rom-dir ./mt32-roms $* > $LOG 2>&1; times >> $LOG"
for i in $(seq 1 15); do pgrep -f -- "--rom-dir ./mt32-roms" >/dev/null && break; sleep 1; done
MID=""
for i in $(seq 1 300); do
  pgrep -f -- "--rom-dir ./mt32-roms" >/dev/null || break
  # Panel AND ES every 5 s: a pre/post check alone cannot see ES coming
  # back mid-run (the stop can take >30 s to settle).
  [ $((i % 5)) -eq 0 ] && MID="$MID $(panel)"
  sleep 1
done
echo "MID$MID" >> "$LOG.state"
echo "POST $(panel)" >> "$LOG.state"
chvt 1; deallocvt "$VT" 2>/dev/null
cat "$LOG.state"
