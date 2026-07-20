#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Copy the MinGW/MSYS2 runtime DLLs an executable needs next to it, so it
# can be launched from PowerShell/cmd/Explorer where C:\msys64\ucrt64\bin
# is not on PATH (without this the loader dies with STATUS_DLL_NOT_FOUND,
# 0xC0000135, before main — no console output at all).
#
# NOTE: with the default Windows build shape (OLDUVAI_STATIC_RUNTIME +
# OLDUVAI_STATIC_SDL, see CMakeLists.txt) the binaries are self-contained
# and this script is a no-op.  It remains useful for builds configured
# with -DOLDUVAI_STATIC_RUNTIME=OFF / -DOLDUVAI_STATIC_SDL=OFF.
#
# Usage (from an MSYS2 UCRT64 shell):
#   scripts/deploy_dlls_windows.sh [build/release/olduvai.exe ...]
#
# Defaults to build/release/olduvai.exe and build/release/tools/olduvai_trace.exe.  Uses ldd to
# resolve exactly what the binaries link against, and copies only the
# non-system (/ucrt64 or /mingw64) DLLs.

set -e

BINARIES="${*:-build/release/olduvai.exe build/release/tools/olduvai_trace.exe}"

for bin in ${BINARIES}; do
    [ -f "${bin}" ] || { echo "skip (not found): ${bin}"; continue; }
    dir=$(dirname "${bin}")
    ldd "${bin}" | awk '/\/(ucrt64|mingw64)\//{print $3}' | sort -u |
    while read -r dll; do
        cp -u "${dll}" "${dir}/" && echo "  -> ${dir}/$(basename "${dll}")"
    done
done
echo "done."
