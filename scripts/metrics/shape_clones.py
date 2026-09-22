#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""One concept, two bodies — the duplication clones.py cannot see.

WHY THIS EXISTS.  clones.py matches RUNS OF LINES, so it finds copy-paste and
nothing else.  The duplications this tree keeps producing are a different
shape: the same RITUAL performed around a call that differs.  The one that
prompted this (2026-09-20, owner's question) was four sites — the loading card
and the score tally, in each driver — spelling out

    TextScreenHd h;
    if (hd && hd_text.ok()) h = make_text_screen_hd(deps, ENV, tag);
    screen.begin_screen(ENV, tag);
    if (!show_SOMETHING(..., present, h)) quit = true;

where only `show_SOMETHING` differed.  clones.py scored it as nothing, because
no two of those blocks share enough consecutive TEXT.  Every §3.14 / §3.18 /
§3.31 find had this shape, and every one of them was found by a person reading
code, which does not scale.

WHAT IT DOES.  For every function it can delimit, it extracts the ORDERED
SEQUENCE OF CALLS — the identifiers followed by `(`, arguments discarded — and
looks for the same short sequence occurring in two or more places.  Then, the
part that matters: it also reports sequences that match EXCEPT AT ONE POSITION
("a skeleton with a hole"), which is precisely the shape above.  Arguments are
discarded on purpose: a ritual whose arguments differ is still a ritual.

REPORT-ONLY, NEVER A GATE — the same reasoning as clones.py.  A hole is often
legitimate (two SDL paths that clear, copy and present around a different
texture may want to stay apart), so every hit is a lead to read, not a defect.

VALIDATED AGAINST THE CASE IT WAS WRITTEN FOR, which is the only reason to
trust it: run on the commit BEFORE the text-screen refactor it reports

    2 sites | ok -> make_text_screen_hd -> begin_screen ->
              <show_loading_screen|show_score_tally>

and, across the two drivers, `begin_screen -> show_loading_screen ->
end_screen`.  Getting there cost three wrong versions, each of which
reported confidently on part of the tree:
  1. signatures matched line by line — every MULTI-LINE signature was
     skipped, i.e. both drivers and most sequences;
  2. function bodies looked for at brace depth 0 — every file wraps its code
     in `namespace { }`, so the count was ZERO;
  3. same-function hits filtered out as "a loop" — but two rituals 1200 lines
     apart inside one driver is exactly the case this exists for.
If you change the delimiter, re-run it on `HEAD~1` of the refactor above and
check that line still appears.

WHAT IT GETS WRONG.  Read this before believing a number.
  * BLIND TO A RITUAL SPREAD OUT.  The platform driver built its text-screen
    handle at level entry and used it 1200 lines later; between them lie
    hundreds of calls, so no window covers both.  Only the boss driver's
    adjacent copy was reported — enough to find it, but the tool measures
    ADJACENCY, not data flow.
  * It does not parse C++.  Functions are delimited by a brace scan from a
    heuristic signature match; a file it cannot delimit is skipped, not
    guessed at.
  * Control flow is FLATTENED.  Two calls in opposite arms of an `if` read as
    a sequence, so a "shared ritual" may be two paths that never both run.
  * Common noise dominates unfiltered: size/empty/push_back/begin/end, the
    SDL clear-copy-present triple, casts.  There is a noise list below and
    --raw disables it.
  * It is blind to duplication expressed with DIFFERENT call names — the same
    idea written twice by two people.  Nothing cheap finds that.
  * Two sites in one function are usually a loop the reader already sees;
    --same-function includes them, default is across functions only.

    scripts/metrics/shape_clones.py [--len N] [--top N] [--raw]
                                    [--same-function] [--holes 0|1]
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")

# Calls that say nothing about a ritual: containers, casts, trivia.  Hits made
# of these are how the first run buried its own findings.
NOISE = {
    "size", "empty", "push_back", "emplace_back", "begin", "end", "data",
    "clear", "resize", "reserve", "move", "max", "min", "abs", "swap",
    "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast",
    "sizeof", "count", "find", "at", "get", "set", "c_str", "printf",
    "fprintf", "snprintf", "strlen", "memcpy", "memset", "assert", "if",
    "for", "while", "switch", "return", "sizeof", "std", "lround", "round",
    "floor", "ceil", "sqrt", "sin", "cos", "pow", "fabs", "insert", "erase",
    "substr", "back", "front", "first", "second", "value", "has_value",
    "reset", "release", "make_unique", "make_shared", "to_string", "stoi",
}

# A function body is a `{` at depth 0 whose preceding text ends in a
# parameter list — found by scanning braces, NOT by matching a signature on
# one line.  The first version matched signatures line by line and therefore
# skipped every multi-line signature in the tree, which is to say both drivers
# and most of the sequences: it reported confidently on the code it could
# still see.  A tool that silently drops its most duplicated files is worse
# than no tool.
SIGTAIL = re.compile(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?"
                     r"(?:noexcept\s*)?(?:->[\w:<>,&*\s]+)?\s*$", re.S)
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def strip_code(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r'"(\\.|[^"\\])*"', '""', text)
    return text


def functions(path):
    """(name, first_line, calls[]) for each function body we can delimit."""
    text = strip_code(open(path, errors="replace").read())
    out, depth, i = [], 0, 0
    starts = []          # stack of (name, line, body_start) per open brace
    line = 1
    while i < len(text):
        c = text[i]
        if c == "\n":
            line += 1
        elif c == "{":
            # At ANY depth: every file wraps its code in `namespace { }`, so a
            # function is at depth 1, not 0.  (Checking only depth 0 delimited
            # exactly nothing and said so — the second way this delimiter
            # could have reported on a fraction of the tree in silence.)
            m = SIGTAIL.search(text[max(0, i - 600):i])
            name = None
            if m and m.group(1) not in ("if", "for", "while", "switch",
                                        "catch", "return", "namespace"):
                name = m.group(1)
            starts.append((name, line, i + 1))
            depth += 1
        elif c == "}":
            depth -= 1
            if starts:
                name, l0, b0 = starts.pop()
                if name is not None:
                    body = text[b0:i]
                    # Names of 1-2 characters are local math vocabulary (the
                    # upscalers' P / mix / w), not a ritual: they buried the
                    # first run's real findings under six variants of one
                    # pixel kernel.
                    calls = [x for x in CALL.findall(body)
                             if x not in NOISE and len(x) > 2]
                    if calls:
                        out.append((name, l0, calls))
        i += 1
    return out


def windows(calls, n):
    for k in range(len(calls) - n + 1):
        yield k, tuple(calls[k:k + n])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--len", type=int, default=4, help="calls per window")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--holes", type=int, default=1, choices=(0, 1),
                    help="1 = also group windows differing at ONE position")
    ap.add_argument("--same-function", action="store_true")
    ap.add_argument("--within-file", action="store_true",
                    help="also report groups confined to ONE file")
    ap.add_argument("--raw", action="store_true", help="no noise filter")
    args = ap.parse_args()
    if args.raw:
        NOISE.clear()

    sites = collections.defaultdict(list)   # window -> [(file, fn, line)]
    for dirpath, _dirs, files in os.walk(SRC):
        for f in sorted(files):
            if not f.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, ROOT)
            for fn, line, calls in functions(path):
                for off, w in windows(calls, args.len):
                    sites[w].append((rel, fn, line, off))

    groups = []
    seen = set()
    for w, hits in sites.items():
        if w in seen:
            continue
        members = {w: hits}
        if args.holes:
            # A skeleton with ONE hole: same window with one position free.
            for pos in range(len(w)):
                key = w[:pos] + ("*",) + w[pos + 1:]
                for w2, h2 in sites.items():
                    if w2 == w or w2 in seen:
                        continue
                    if w2[:pos] + ("*",) + w2[pos + 1:] == key:
                        members[w2] = h2
        allhits = [h for hh in members.values() for h in hh]
        # Distinct SITES, not distinct functions.  Two rituals 1200 lines
        # apart inside one driver are the case this tool was written for, and
        # the first version filtered them out as "same function" — it could
        # not see the very duplication that prompted it.  Two hits in one
        # function count as two sites when they are far apart in that
        # function's call sequence; adjacent ones are a loop the reader
        # already sees.
        FAR = 25
        sites_seen = []
        for f, fn, l, o in sorted(allhits, key=lambda h: (h[0], h[1], h[3])):
            if not any(pf == f and pfn == fn and abs(po - o) < FAR
                       for pf, pfn, po in sites_seen):
                sites_seen.append((f, fn, o))
        if len(sites_seen) < 2:
            continue
        nfiles = len({f for f, _fn, _o in sites_seen})
        # Default: a ritual worth naming crosses FILES.  A sequence repeated
        # inside one file is usually that file's own vocabulary (a scaler
        # kernel, a spawn table); --within-file includes them.
        if nfiles < 2 and not args.within_file:
            continue
        for k in members:
            seen.add(k)
        # Score: distinct sites, then how many variants share the skeleton.
        groups.append((nfiles, len(sites_seen), len(members), w, members,
                       sites_seen))

    groups.sort(key=lambda g: (-g[0], -g[1], -g[2]))
    print("shape_clones: same call sequence in %d+ places "
          "(window=%d calls, holes=%d)\n" % (2, args.len, args.holes))
    for nfiles, sitecount, nvar, w, members, sites_seen in groups[:args.top]:
        holes = [i for i in range(len(w))
                 if len({v[i] for v in members}) > 1]
        shape = " -> ".join("<%s>" % "|".join(sorted({v[i] for v in members}))
                            if i in holes else w[i] for i in range(len(w)))
        print("  %d sites in %d files | %s" % (sitecount, nfiles, shape))
        for f, fn, off in sites_seen:
            print("      %s  %s()  [call #%d]" % (f, fn, off))
        print()
    if not groups:
        print("  (nothing above the window size — try --len 3)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
