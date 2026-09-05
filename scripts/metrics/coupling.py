#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""File-level coupling over tracked src/ — the exact #include graph.

Reports, for every project file:
    Ce (efferent) = distinct project files it includes
    Ca (afferent) = distinct project files that include it
    I  = Ce/(Ce+Ca)   0 = maximally stable, 1 = maximally unstable

Also emits the layer-to-layer include matrix and re-checks two architecture
invariants independently of check_layers.sh:
  - no lower layer includes a higher one (CLAUDE.md's layering contract)
  - no SDL header below presentation

Cyclomatic complexity is NOT computed here — that is lizard's job; see
docs/internal/archive/2026-07-30-ck-metrics-audit.md §1.

Usage: python3 scripts/metrics/coupling.py   (from anywhere in the worktree)
"""
import collections
import os
import re
import subprocess
import sys

LAYER_ORDER = ["formats", "prepare", "core", "systems", "enhance",
               "presentation", "trace", "app"]


def repo_root():
    return subprocess.run(["git", "rev-parse", "--show-toplevel"],
                          capture_output=True, text=True, check=True).stdout.strip()


def layer(path):
    p = path.split("/")
    return p[1] if len(p) > 1 else "?"


def main():
    os.chdir(repo_root())
    files = [f for f in subprocess.run(["git", "ls-files", "src"],
                                       capture_output=True, text=True,
                                       check=True).stdout.split()
             if f.endswith((".cpp", ".hpp", ".h"))]
    if not files:
        sys.exit("no tracked sources under src/")

    inc_re = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
    sys_re = re.compile(r"^\s*#\s*include\s+<([^>]+)>", re.M)
    by_incpath = {f[len("src/"):]: f for f in files}

    text, edges, sysdeps = {}, collections.defaultdict(set), collections.defaultdict(set)
    for f in files:
        with open(f, encoding="utf-8", errors="replace") as fh:
            text[f] = fh.read()
        for m in inc_re.findall(text[f]):
            tgt = by_incpath.get(m)
            if tgt and tgt != f:
                edges[f].add(tgt)
        sysdeps[f] = set(sys_re.findall(text[f]))

    rev = collections.defaultdict(set)
    for a, bs in edges.items():
        for b in bs:
            rev[b].add(a)

    tot = sum(len(v) for v in edges.values())
    print(f"files={len(files)}  include edges={tot}  avg fan-out={tot/len(files):.1f}\n")

    rows = [(f, len(edges.get(f, ())), len(rev.get(f, ()))) for f in files]
    print("Most depended-upon (highest Ca) — the blast-radius set:")
    print(f"{'Ca':>4}{'Ce':>4}{'I':>6}  file")
    for f, ce, ca in sorted(rows, key=lambda r: -r[2])[:12]:
        print(f"{ca:>4}{ce:>4}{ce/(ce+ca) if ce+ca else 0:>6.2f}  {f}")
    print("\nHeaviest dependents (highest Ce) — hardest files to move:")
    print(f"{'Ce':>4}{'Ca':>4}{'I':>6}  file")
    for f, ce, ca in sorted(rows, key=lambda r: -r[1])[:12]:
        print(f"{ce:>4}{ca:>4}{ce/(ce+ca) if ce+ca else 0:>6.2f}  {f}")

    lyr = collections.Counter((layer(a), layer(b))
                              for a, bs in edges.items() for b in bs)
    present = [l for l in LAYER_ORDER if any(layer(f) == l for f in files)]
    print("\nLayer-to-layer include counts (row includes column):")
    print(f"{'':<14}" + "".join(f"{c[:6]:>9}" for c in present))
    for r in present:
        print(f"{r:<14}" + "".join(f"{lyr.get((r, c), 0) or '.':>9}" for c in present))

    print("\nLayer contract (lower layers never include higher):")
    viol = [(a, b, n) for (a, b), n in lyr.items()
            if a in LAYER_ORDER and b in LAYER_ORDER and a != b
            and LAYER_ORDER.index(b) > LAYER_ORDER.index(a)]
    if not viol:
        print("  CLEAN — no upward include found.")
    else:
        for a, b, n in sorted(viol, key=lambda v: -v[2]):
            print(f"  VIOLATION {a} -> {b} ({n} edges)")

    print("\nSDL containment (formats/core/systems/enhance must be SDL-free):")
    bad = [f for f, s in sysdeps.items()
           if layer(f) in ("formats", "core", "systems", "enhance")
           and any(h.startswith("SDL") for h in s)]
    print("  CLEAN — no SDL include below presentation." if not bad
          else "\n".join(f"  {f}" for f in bad))
    return 1 if (viol or bad) else 0


if __name__ == "__main__":
    sys.exit(main())
