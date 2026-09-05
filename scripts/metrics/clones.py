#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""Copy-paste detector for the olduvai tree — BACKLOG section 4b item 8.

WHY THIS AND NOT jscpd.  Section 4b specced jscpd 5 (Node) for this job.  This
does the same work with no toolchain to install and no lockfile to keep current,
which matters for a report nobody runs on a schedule.  It found, in one run on
2026-08-02: the 33-line sprite_blit HD-blit clone section 4b had already
predicted, three copies of an input policy, two identical structs under
different names, a nine-argument list written six times, and a resampler that
had outlived its feature by a month and was pure dead code.

REPORT-ONLY, NEVER A GATE.  Section 4b's reasoning stands: abstraction pushes
measured duplication UP (1.67% -> 11.28% on the reference), so a threshold here
would punish the refactors it exists to prompt.  Read it, judge each hit, act on
the ones that are real.

HOW IT WORKS.  Each line is normalised (comments stripped, whitespace collapsed;
literals KEPT, since a differing constant is usually a real difference in this
codebase).  Windows of N normalised lines are hashed, identical windows grouped,
and overlapping windows merged so one 40-line clone reports once.

WHAT IT GETS WRONG, AND WHY THE FILTERS EXIST.  On the first run, half the top
ten were noise of two kinds:
  * include blocks — three files opening with the same 20 #includes
  * declaration/definition pairs — a long signature in the .hpp and again in
    the .cpp, which is C++, not duplication
Both are filtered by default.  --raw disables the filtering when you want to
audit what is being hidden.  There is one filter this deliberately does NOT
have: "the same function in two files", because that is exactly what it is for.

    scripts/metrics/clones.py [--window N] [--raw] [--top N]
"""
import argparse
import hashlib
import re
import subprocess
import sys
from collections import defaultdict

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//.*")
# Lines that carry no logic: a clone made only of these is a formatting artefact.
NOISE = {"{", "}", "};", "});", "return;", "break;", "continue;", "else", "};"}


def normalise(path):
    """[(original_lineno, normalised_text)] for code lines only."""
    src = open(path, encoding="utf-8", errors="replace").read()
    src = BLOCK_COMMENT.sub("", src)
    out = []
    for i, raw in enumerate(src.splitlines(), 1):
        line = LINE_COMMENT.sub("", raw).strip()
        if not line or line in NOISE:
            continue
        out.append((i, re.sub(r"\s+", " ", line)))
    return out


def is_include_run(lines):
    """A clone that is nothing but #include / #pragma / using lines."""
    return all(
        t.startswith(("#include", "#pragma", "using ", "namespace "))
        for _, t in lines
    )


def is_decl_def_pair(locs, files):
    """Same basename, one .hpp and one .cpp — a signature and its definition."""
    if len(locs) != 2:
        return False
    a, b = (f.rsplit(".", 1) for f in (locs[0][0], locs[1][0]))
    return a[0] == b[0] and {a[1], b[1]} == {"hpp", "cpp"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--window", type=int, default=8,
                    help="minimum clone length in normalised lines (default 8)")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--raw", action="store_true",
                    help="do not filter include blocks / decl-def pairs")
    args = ap.parse_args()

    files = subprocess.run(
        ["git", "ls-files", "src/*.cpp", "src/**/*.cpp",
         "src/*.hpp", "src/**/*.hpp"],
        capture_output=True, text=True, check=True).stdout.split()
    if not files:
        print("clones: no sources found (run from the repo root)", file=sys.stderr)
        return 1

    norm = {f: normalise(f) for f in files}
    windows = defaultdict(list)
    for f, n in norm.items():
        for i in range(len(n) - args.window + 1):
            body = "\n".join(t for _, t in n[i:i + args.window])
            h = hashlib.blake2b(body.encode(), digest_size=12).hexdigest()
            windows[h].append((f, i))

    seen, clones = set(), []
    for locs in (v for v in windows.values() if len(v) > 1):
        key = tuple(sorted(locs))
        if key not in seen:
            seen.add(key)
            clones.append(locs)

    # Merge: a clone whose every location is the previous one shifted by 1
    # extends it, so a long clone reports once instead of once per offset.
    clones.sort(key=lambda l: (l[0][0], l[0][1]))
    merged = []
    for locs in clones:
        for m in merged:
            if len(m["locs"]) == len(locs) and all(
                    a[0] == b[0] and b[1] == a[1] + m["len"] - args.window + 1
                    for a, b in zip(m["locs"], locs)):
                m["len"] += 1
                break
        else:
            merged.append({"locs": locs, "len": args.window})

    rows, hidden = [], 0
    for m in merged:
        f, i = m["locs"][0]
        body = norm[f][i:i + m["len"]]
        if not args.raw and (is_include_run(body) or
                             is_decl_def_pair(m["locs"], norm)):
            hidden += 1
            continue
        rows.append(m)

    rows.sort(key=lambda m: -(m["len"] * (len(m["locs"]) - 1)))
    print(f"clones: window={args.window} "
          f"{'(unfiltered)' if args.raw else f'({hidden} noise groups hidden)'}")
    for m in rows[:args.top]:
        dup = m["len"] * (len(m["locs"]) - 1)
        where = ", ".join(f"{f}:{norm[f][i][0]}" for f, i in m["locs"][:5])
        extra = "" if len(m["locs"]) <= 5 else f" (+{len(m['locs']) - 5} more)"
        print(f"{dup:5d} dup-lines | {m['len']:3d} x {len(m['locs'])} sites | "
              f"{where}{extra}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
