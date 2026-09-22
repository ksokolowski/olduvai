#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
# Keep the engine's stderr, and print it when a gate finds no shot.
#
# WHY THIS EXISTS.  Every shot gate sent the engine's stderr to /dev/null and
# then reported "no shot produced" — which is the symptom of a dozen different
# causes and the evidence for none of them.  It cost real time twice on
# 2026-09-20: a rare startup failure on the dev Mac (BACKLOG §6, five
# sightings) showed up as an empty shot directory and was briefly read as a
# regression in the commit under test, and the TrimUI crash was only diagnosed
# because the LAUNCHER happened to keep a log the gates do not.
#
# The engine already says why it fails — "game: window creation failed: <SDL
# error>", "cannot read the game files in <dir>" — so the fix is to keep the
# line and show it, not to add new reporting.
#
# BOTH STREAMS, into one file.  The first-run "needs your original game files"
# report goes to STDOUT, which the gates discarded just as thoroughly as
# stderr: capturing only stderr still produced "no shot produced" and silence
# (measured, while writing this).  The gates keep sending the run's output
# away from the terminal; the difference is that it now lands somewhere a
# failure can read it back.
#
# Usage, in a gate script:
#
#     . "$(dirname "$0")/lib/engine_err.sh"
#     engine_err_init                # sets ${ERR}, removed on any exit
#     ... "${BINARY}" --play ... >"${ERR}" 2>&1
#     if [ ! -s "${SHOT}" ]; then
#         echo "my_gate: FAIL — no shot produced"
#         engine_said "${ERR}"       # prints the tail, or says it was silent
#         exit 1
#     fi
#     engine_err_clean "${ERR}"      # optional; the EXIT trap also removes it
#
# Deliberately NOT a wrapper around the invocation: the gates differ in env,
# flags and redirection, and a wrapper would have to take all of it as
# arguments — the shape that makes a shared helper harder to read than the
# thing it replaces.

# Set ${ERR} and have it removed however the gate exits.  These scripts have
# five to seven exit paths each, and cleaning at every one of them is how a
# leak gets written in the first place (measured: seven files left in /tmp
# after one pass over the converted gates).
#
# It sets the variable rather than printing it, ON PURPOSE.  The first version
# was `ERR="$(engine_err_file)"`, which runs the function — and therefore its
# trap — inside the command-substitution SUBSHELL: the trap fired the instant
# that subshell exited, deleting the file before the gate had written a byte,
# and the redirect then made an untrapped one. Leaks measured again, which is
# how it was caught. A gate that needs its own EXIT trap must chain this one;
# none of the current callers has one.
engine_err_init() {
    ERR="$(mktemp /tmp/olduvai_err.XXXXXX)"
    trap 'rm -f "${ERR}"' EXIT
}

# Print the last lines of `$1` (both streams), indented, or say so when the
# run produced nothing at all.  Never fails the caller: it runs on a path
# that is already failing.
engine_said() {
    if [ -s "$1" ]; then
        echo "  the run said:"
        tail -6 "$1" 2>/dev/null | sed 's/^/    /'
    else
        echo "  (the run printed nothing — it died before it could)"
    fi
}

# Optional: remove it early.  The EXIT trap set by engine_err_file covers
# every path, so this is only for a gate that wants /tmp clean before it goes
# on to something long.
engine_err_clean() {
    rm -f "$1"
}
