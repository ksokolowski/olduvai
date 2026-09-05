#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""Which DISPLAY MODES does the shell-test corpus actually exercise?

BACKLOG section 3.19 was found the expensive way: every boss gate passed
--enhanced, so the classic boss HUD had no coverage at all, a refactor stopped
it drawing entirely, and the whole suite stayed green.  The question "which
modes does the corpus actually run" had never been asked, and asking it by hand
gives a dated answer that rots the moment a test is added -- the same failure
the KPI table had five times before kpi_row.sh took over the typing.

So this takes it.  It reads every tests/*.sh, resolves the olduvai invocations,
and reports the display-mode flags each one passes, plus a coverage summary of
the cells that matter: classic vs enhanced, and which aspect / upscaler.

    scripts/metrics/mode_coverage.py            # the per-test table
    scripts/metrics/mode_coverage.py --summary  # just the cell counts

WHERE THIS IS WRONG, and it matters more than the numbers:

 1. It reads FLAGS, not BEHAVIOUR.  A test that passes --enhanced still renders
    classic if the binary decides hd_active() is false (enhanced with
    hd_profile "native" is exactly that).  Treat a covered cell as "the flag
    was passed", never as "the path ran".  The only honest check of the path is
    the OUTPUT -- boss_classic_hud asserts its shot is 320x200 for this reason.

 2. Shell variables are resolved only when they expand to a literal in the same
    file.  MODE_ARGS / FLAGS style indirection is picked up by scanning
    assignments, so a test that builds flags from a data table (hd_text_screens)
    is reported as covering every mode its table names -- which is right -- but
    a test that computes them some other way will read as bare.

 3. It cannot see modes set through play.json or a --profile preset.  first_run
    drives the `hd` PRESET and never passes --enhanced; it shows as classic
    here and is not.  A config-driven mode is invisible to a flag scan.

 4. A test appearing in this table is NOT evidence it gates pixels.  Most of
    the trace corpus runs classic and asserts nothing about the screen.  Cross
    against what the test actually compares before concluding anything.

 5. This lists FILES in tests/, which is not by itself proof anything RUNS
    them.  Every test is now a registered ctest, but the slow ones carry the
    label `slow` and the default `release` preset filters that label out, so
    `ctest --preset release -N` does not list them -- use `release-full`.
    The first version of this sweep read the default `ctest -N`, saw
    hd_text_screens missing, and reported it as never run when gate_local.sh
    had been running it the whole time.  Ask `ctest --preset release-full -N`
    before concluding anything is uncovered.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"

# Display-relevant flags, in the order they are reported.
FLAGS = ("--enhanced", "--hd-profile", "--aspect", "--render-scale",
         "--profile", "--transitions", "--vga-scan", "--fullscreen")


def join_continuations(text):
    return re.sub(r"\\\n\s*", " ", text)


def literal_assignments(text):
    """VAR="literal" pairs, so MODE_ARGS-style indirection resolves.

    NOT anchored to the start of a line: hd_text_screens sets MODE_ARGS inside
    `case` arms (`hd)  MODE_ARGS="--enhanced ..." ;;`), and an anchored regex
    reported the one test that deliberately covers BOTH stacks as classic-only
    — the exact blind spot this script exists to find.  Comment lines are
    dropped first so prose in a header cannot invent coverage.
    """
    out = {}
    body = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
    for m in re.finditer(r'(\w+)=("([^"]*)"|\'([^\']*)\')', body):
        val = m.group(3) if m.group(3) is not None else m.group(4)
        if "--" in val:
            out.setdefault(m.group(1), []).append(val)
    return out


def invocations(text):
    """Lines that actually run the binary (not the -x guard)."""
    for line in join_continuations(text).splitlines():
        if not re.search(r'\$\{?(BINARY|BIN|OLDUVAI_BIN)\}?', line):
            continue
        if "! -x" in line or "-x \"" in line:
            continue
        if not re.search(r"--play|--viewer|--trace|--render-audio", line):
            continue
        yield line


def modes_for(path):
    text = path.read_text(errors="replace")
    assigns = literal_assignments(text)
    seen = {f: set() for f in FLAGS}
    runs = 0
    for line in invocations(text):
        runs += 1
        # Splice in any literal assignment whose variable this line references.
        expanded = line
        for var, vals in assigns.items():
            if re.search(r'\$\{?%s\}?' % re.escape(var), line):
                expanded += " " + " ".join(vals)
        for f in FLAGS:
            if f in ("--enhanced", "--vga-scan", "--fullscreen"):
                if re.search(re.escape(f) + r"\b", expanded):
                    seen[f].add("yes")
            else:
                for m in re.finditer(re.escape(f) + r"\s+([A-Za-z0-9:.]+)", expanded):
                    seen[f].add(m.group(1))
    return runs, seen


def main():
    summary_only = "--summary" in sys.argv
    rows = []
    for path in sorted(TESTS.glob("*.sh")):
        runs, seen = modes_for(path)
        # Shared runners live in tests/lib and are counted through their callers.
        rows.append((path.stem, runs, seen))
    for extra in sorted((TESTS / "lib").glob("*.sh")):
        runs, seen = modes_for(extra)
        rows.append(("lib/" + extra.stem, runs, seen))

    if not summary_only:
        print("%-24s %-4s %-9s %-11s %-12s %s"
              % ("test", "runs", "enhanced", "hd-profile", "aspect", "other"))
        print("-" * 88)
        for name, runs, seen in rows:
            if runs == 0:
                print("%-24s %-4s %s" % (name, "-", "(no direct invocation)"))
                continue
            other = []
            for f in ("--render-scale", "--profile", "--transitions",
                      "--vga-scan", "--fullscreen"):
                if seen[f]:
                    other.append("%s=%s" % (f[2:], ",".join(sorted(seen[f]))))
            print("%-24s %-4d %-9s %-11s %-12s %s"
                  % (name, runs,
                     "yes" if seen["--enhanced"] else "classic",
                     ",".join(sorted(seen["--hd-profile"])) or "-",
                     ",".join(sorted(seen["--aspect"])) or "-",
                     " ".join(other)))
        print()

    # Coverage cells.  A cell with zero tests is the thing worth looking at.
    enhanced = [r for r in rows if r[1] and r[2]["--enhanced"]]
    classic = [r for r in rows if r[1] and not r[2]["--enhanced"]]
    print("enhanced-flag runs: %d   classic (no --enhanced): %d"
          % (len(enhanced), len(classic)))
    # DEFAULTS MATTER HERE.  A value with zero explicit uses is only a gap if
    # it is not what the engine picks when the flag is absent: aspect defaults
    # to "keep" and transitions to "smooth", so those run in every test that
    # says nothing.  Reporting them as uncovered would be the instrument
    # lying in the direction that generates busywork.
    for label, key, known, default in (
            ("aspect", "--aspect", ("keep", "4:3", "stretch", "widescreen"),
             "keep"),
            ("hd-profile", "--hd-profile", ("native", "mmpx", "omniscale",
                                            "xbr", "smooth"), "native"),
            ("transitions", "--transitions", ("smooth", "classic"), "smooth")):
        used = {}
        for _, runs, seen in rows:
            for v in seen[key]:
                used[v] = used.get(v, 0) + 1
        print("\n%s:" % label)
        for v in known:
            n = used.get(v, 0)
            if n:
                mark = ""
            elif v == default:
                mark = "  (the DEFAULT — runs whenever the flag is absent)"
            else:
                mark = "  <-- NO TEST PASSES THIS, AND IT IS NOT THE DEFAULT"
            print("    %-12s %d%s" % (v, n, mark))
        stray = sorted(set(used) - set(known))
        if stray:
            print("    (unrecognised values seen: %s)" % ", ".join(stray))


if __name__ == "__main__":
    main()
