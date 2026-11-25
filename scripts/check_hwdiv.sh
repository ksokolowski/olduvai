#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# An armhf binary's hardware divides are all in the copy picked at run time
# (src/enhance/xbrf_hwdiv.cpp, section olduvai_hwdiv): one anywhere else stops
# a Cortex-A8 or A9 with an illegal instruction.  And some are there, or the
# copy fell out of the build; and, where the binary has symbols, the copy does
# not call into the first.  Run by packaging/build_knulli_release.sh on the
# unstripped binary.
#   check_hwdiv.sh BINARY      OBJDUMP names the disassembler, llvm-objdump by default
set -u
BIN=${1:?usage: check_hwdiv.sh BINARY}
OBJDUMP=${OBJDUMP:-}
if [ -z "$OBJDUMP" ]; then
    for o in llvm-objdump /opt/homebrew/opt/llvm/bin/llvm-objdump /usr/local/opt/llvm/bin/llvm-objdump; do
        command -v "$o" > /dev/null 2>&1 && { OBJDUMP=$o; break; }
    done
fi
[ -n "$OBJDUMP" ] || { echo "check_hwdiv: no llvm-objdump; set OBJDUMP" >&2; exit 2; }
listing=$("$OBJDUMP" -d -C --no-show-raw-insn "$BIN") || { echo "check_hwdiv: cannot read $BIN" >&2; exit 2; }
divs=$(printf '%s\n' "$listing" | awk '
    /^Disassembly of section/ { sec = $4 }
    /\t(sdiv|udiv)\t/ { if (sec == "olduvai_hwdiv:") inside++; else outside++ }
    END { printf "%d %d", inside, outside }')
# With symbols, the copy's calls are its own: one from the section to the
# first copy's xbrf_scale or its file's own functions runs the first's code.
# The helpers both share, parallel_rows and the pattern tables, it may call.
back=$(printf '%s\n' "$listing" | awk '
    /^Disassembly of section/ { sec = $4 }
    sec == "olduvai_hwdiv:" && /\tbl[a-z]*\t/ && /<olduvai::enhance::(xbrf_scale|\(anonymous namespace\))/ { n++ }
    END { printf "%d", n }')
[ "$back" = 0 ] || { echo "check_hwdiv: $BIN's hardware-divide copy calls the first copy $back times" >&2; exit 1; }
inside=${divs% *}
outside=${divs#* }
[ "$outside" = 0 ] || { echo "check_hwdiv: $BIN has $outside hardware divides outside olduvai_hwdiv" >&2; exit 1; }
[ "$inside" -gt 0 ] || { echo "check_hwdiv: $BIN has no hardware-divide copy" >&2; exit 1; }
echo "check_hwdiv: OK, $inside hardware divides, all in olduvai_hwdiv"
