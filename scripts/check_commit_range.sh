#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# CI backstop for the commit-msg hook: reject AI-attribution trailers in
# any commit message of the inspected range (default: all of HEAD's
# history, which is cheap on a fresh-history repo).
set -eu

range="${1:-HEAD}"
bad=$(git log --format='%H %s%n%b' "$range" |
      grep -icE 'Co-Authored-By|Generated with.*Claude' || true)
if [ "$bad" -gt 0 ]; then
    echo "check_commit_range: forbidden AI-attribution trailer in commit messages." >&2
    git log --format='%H %s' "$range" --grep='Co-Authored-By' --regexp-ignore-case >&2
    git log --format='%H %s' "$range" --grep='Generated with' --regexp-ignore-case >&2
    exit 1
fi
echo "check_commit_range: OK ($range)"
