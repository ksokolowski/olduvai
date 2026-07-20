#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Bundle helper: builds Olduvai.app and rewrites hardcoded Homebrew
# install paths in the binary's load commands (dylibbundler).
set -e
cd "$(dirname "$0")/.."
cmake --build build/release --target olduvai_app
APP=build/release/Olduvai.app
command -v dylibbundler >/dev/null || {
    echo "need: brew install dylibbundler" >&2; exit 1; }
dylibbundler -od -b -x "$APP/Contents/MacOS/Olduvai" \
    -d "$APP/Contents/libs/" -p "@executable_path/../libs/"
echo "bundle ready: $APP"
