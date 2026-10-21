#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# apt-get for CI jobs: update and install with a time limit and retries.
#
#   sh scripts/ci_apt.sh [apt-get install option...] <package>...
#
# A mirror that drops its connections can leave apt-get waiting forever: on
# 2026-10-06 sdl-floor sat three hours in `apt-get update` with every
# connection in CLOSE_WAIT, until the runner's job limit killed it.  Here a
# run that hangs is killed after APT_TIMEOUT seconds (default 300) and tried
# again, three times.  Uses sudo when not root.
set -eu
[ $# -gt 0 ] || { echo "usage: ci_apt.sh [option...] <package>..." >&2; exit 2; }
SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO=sudo
LIMIT="${APT_TIMEOUT:-300}"

attempt() {   # attempt <label> <command...>
    label=$1
    shift
    for n in 1 2 3; do
        # shellcheck disable=SC2086  # $SUDO is empty or one word
        $SUDO timeout -k 10 "$LIMIT" "$@" && return 0
        echo "ci_apt: $label failed or timed out (attempt $n of 3)" >&2
        [ "$n" -eq 3 ] || sleep 10
    done
    return 1
}

attempt "apt-get update" apt-get update -qq
attempt "apt-get install" env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y -qq "$@"
