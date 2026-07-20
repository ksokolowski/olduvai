#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Release gate: the pushed tag must match the CMake project VERSION, so a
# forgotten CMakeLists bump fails the workflow loudly instead of shipping a
# binary whose --version contradicts the release name.
#   usage: scripts/check_release_version.sh v1.0.0[-rc1]
# A -suffix (rc/beta) is allowed on the tag and ignored for the comparison.
set -eu
cd "$(dirname "$0")/.."
tag="${1:?usage: check_release_version.sh <tag>}"
ver=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
[ -n "$ver" ] || { echo "check_release_version: no VERSION in CMakeLists.txt" >&2; exit 2; }
case "$tag" in
    v[0-9]*) ;;
    *) echo "check_release_version: tag '$tag' must look like v1.2.3" >&2; exit 1 ;;
esac
base="${tag#v}"; base="${base%%-*}"
if [ "$base" != "$ver" ]; then
    echo "check_release_version: tag $tag (base $base) != CMake VERSION $ver" >&2
    exit 1
fi
echo "check_release_version: OK — $tag matches CMake VERSION $ver"
