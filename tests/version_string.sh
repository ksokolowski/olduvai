#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# `olduvai --version` names the exact build (src/core/build_id.hpp):
#   olduvai 0.9.7 (a97f2a7, 2026-09-13 10:41)             clean tree
#   olduvai 0.9.7 (a97f2a7-dirty, built 2026-09-13 11:02) uncommitted changes
#   olduvai 0.9.7 (unknown)                                built without git
# In a git checkout the hash must also be HEAD's.  A mismatch means the
# binary is STALE — the trap the build ID exists to expose, and the one that
# once cost a whole playtest round.  Needs no game files.
#
# usage: version_string.sh <olduvai binary> <source dir> <expected version>
set -eu
BIN="$1"
SRC="$2"
VER="$3"

out=$("$BIN" --version | tr -d '\r')
echo "version_string: $out"
case "$out" in
    "olduvai $VER ("*")") ;;
    *) echo "FAIL: expected 'olduvai $VER (<build id>)'"; exit 1 ;;
esac
id=${out#"olduvai $VER ("}
id=${id%")"}
if [ "$id" = "unknown" ]; then
    echo "version_string: OK (built without git)"
    exit 0
fi
stamp='[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}'
if ! printf '%s\n' "$id" |
        grep -Eq "^[0-9a-f]{7,}(, $stamp|-dirty, built $stamp)\$"; then
    echo "FAIL: malformed build id '$id'"
    exit 1
fi
if head=$(git -C "$SRC" rev-parse --short=7 HEAD 2>/dev/null); then
    case "$id" in
        "$head"*) ;;
        *) echo "FAIL: build id '$id' is not HEAD ($head) — stale binary?"
           exit 1 ;;
    esac
fi
echo "version_string: OK"
