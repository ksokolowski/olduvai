#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# The shipped ARM binaries, run: every golden trace in an arm64 container.
#
#   scripts/arm_check.sh [--bins DIR] [--out DIR]
#
# Builds the handheld binaries with packaging/build_knulli_release.sh (zig,
# the toolchain that ships; or takes DIR/olduvai.aarch64 and olduvai.armhf
# from --bins), then in a Debian arm64 container (Podman, or Docker):
#   - armhf `--version` on QEMU's Cortex-A7 and Cortex-A9 models: the A9 has
#     no hardware divide, so an A7-only binary dies there;
#   - all golden-trace scenarios of tests/golden_trace_*.sh: aarch64 native,
#     armhf under qemu-arm as a Cortex-A9, each trace diffed against its
#     golden (zero tolerance, as the desktop corpus).
# Exits non-zero on any failure.  A release-cut step (RELEASE_CUT_RUNBOOK),
# not a CI job: CI builds with another compiler, and the bugs this exists for
# (2026-10-07: an A7-only armhf binary; an aarch64 LTO build whose exported
# memset looped) were the zig builds' own.
#
# Needs: the release script's cross setup (zig, ~/olduvai_cross) unless
# --bins; podman (its VM running on macOS) or docker; the game files
# (OLDUVAI_GAME_DATA, else <repo>/game_data).  QEMU's CPU models stand in for
# the chips: a crash or a pass there is strong evidence, not proof.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd -P)
BINS="" OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --bins) BINS="$2"; shift 2 ;;
        --out)  OUT="$2"; shift 2 ;;
        -h|--help) sed -n '4,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "arm_check: unknown option '$1'" >&2; exit 2 ;;
    esac
done
ENGINE=$(command -v podman || command -v docker || true)
[ -n "$ENGINE" ] || { echo "arm_check: needs podman or docker" >&2; exit 2; }
"$ENGINE" info >/dev/null 2>&1 || {
    echo "arm_check: $ENGINE is not running (macOS: podman machine start)" >&2; exit 2; }
GAME="${OLDUVAI_GAME_DATA:-$REPO/game_data}"
[ -f "$GAME/FILESA.VGA" ] || { echo "arm_check: no game files at $GAME" >&2; exit 2; }
[ -n "$OUT" ] || OUT=$(mktemp -d "${TMPDIR:-/tmp}/arm_check.XXXXXX")
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd -P)
case "$OUT/" in "$REPO/"*) echo "arm_check: --out must be outside the repo" >&2; exit 2 ;; esac

# 1. The binaries.
if [ -z "$BINS" ]; then
    bash "$REPO/packaging/build_knulli_release.sh" --src "$REPO" --out "$OUT/build" >/dev/null
    BINS="$OUT/build"
fi
mkdir -p "$OUT/bins"
for a in aarch64 armhf; do
    [ -f "$BINS/olduvai.$a" ] || { echo "arm_check: no $BINS/olduvai.$a" >&2; exit 2; }
    cp "$BINS/olduvai.$a" "$OUT/bins/"
done
# The Enhanced HD text needs fonts/ beside the binary.
cp -R "$REPO/assets/fonts" "$OUT/bins/fonts"

# 2. The game files, links resolved (a container cannot follow a link out of
#    its mount), and the scenario list from the test scripts themselves.
rm -rf "$OUT/game"; mkdir -p "$OUT/game"
cp -L "$GAME"/* "$OUT/game/" 2>/dev/null || true
python3 - "$REPO" > "$OUT/traces.tsv" <<'PY'
import glob, re, shlex, sys
for f in sorted(glob.glob(sys.argv[1] + "/tests/golden_trace_*.sh")):
    src = open(f).read().replace("\\\n", " ")
    m = re.search(r'lib/trace_gate\.sh"\s+(.*)', src)
    if not m:
        continue
    name, level, ss, script, golden, frames = shlex.split(m.group(1).split("\n")[0])[:6]
    flags = re.search(r'TRACE_GATE_FLAGS="([^"]*)"', src)
    envs = re.findall(r'(?:exec\s+env\s+|^export\s+)((?:OLDUVAI_[A-Z_]+=\S+\s*)+)', src, re.M)
    print("\t".join([name, level, ss, script, golden, frames,
                     flags.group(1) if flags else "", " ".join(envs).strip()]))
PY
n=$(wc -l < "$OUT/traces.tsv" | tr -d ' ')
[ "$n" -gt 0 ] || { echo "arm_check: no golden-trace scenarios found" >&2; exit 1; }

# 3. In the container.
cat > "$OUT/run.sh" <<'RUN'
#!/bin/bash
set -u
export DEBIAN_FRONTEND=noninteractive SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# The replays run unthrottled: the same traces, without the 18.2 Hz waits.
export OLDUVAI_UNTHROTTLED=1
dpkg --add-architecture armhf
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends qemu-user libsdl2-2.0-0 \
    libc6:armhf libsdl2-2.0-0:armhf >/dev/null 2>&1 || { echo "apt failed"; exit 2; }
fail=0
for cpu in cortex-a7 cortex-a9; do
    if v=$(timeout 60 qemu-arm -cpu "$cpu" /w/bins/olduvai.armhf --version 2>&1); then
        echo "  armhf on $cpu: $v"
    else
        echo "FAIL: armhf --version on $cpu: $v"; fail=1
    fi
done
one() {   # <scenario line> <runner...>
    IFS=$'\t' read -r name level ss script golden frames flags envs <<< "$1"; shift
    sst=""; [ "$ss" != "-" ] && sst="--start-screen $ss"
    out=$(mktemp); cfg=$(mktemp -d)
    # shellcheck disable=SC2086  # flags, envs and sst are word lists
    env XDG_CONFIG_HOME="$cfg" $envs timeout 1800 "$@" --play --level "$level" \
        $sst $flags --replay "/w/fx/$script" --trace "$out" --play-frames "$frames" \
        --game-dir /w/game >/dev/null 2>&1
    if diff -q --strip-trailing-cr "/w/fx/$golden" "$out" >/dev/null 2>&1; then
        echo "PASS $name"; else echo "FAIL $name"; fi
    rm -rf "$out" "$cfg"
}
export -f one
for arch in aarch64 armhf; do
    if [ "$arch" = aarch64 ]; then run="/w/bins/olduvai.aarch64"
    else run="qemu-arm -cpu cortex-a9 /w/bins/olduvai.armhf"; fi
    res=$(tr '\n' '\0' < /w/traces.tsv |
          xargs -0 -P 8 -I{} bash -c "one \"\$1\" $run" _ {} | sort)
    p=$(grep -c '^PASS' <<< "$res"); f=$(grep -c '^FAIL' <<< "$res")
    grep '^FAIL' <<< "$res" | sed "s/^/  $arch: /"
    echo "  $arch: $p passed, $f failed"
    [ "$f" -eq 0 ] || fail=1
done
exit $fail
RUN
echo "arm_check: $n scenarios, aarch64 native and armhf as a Cortex-A9"
rc=0
"$ENGINE" run --rm --arch arm64 -v "$OUT":/w -v "$REPO/tests/fixtures":/w/fx:ro \
    docker.io/library/debian:bookworm bash /w/run.sh || rc=$?
rm -rf "$OUT/game"
[ "$rc" -eq 0 ] && echo "arm_check: OK" || echo "arm_check: FAILED" >&2
exit "$rc"
