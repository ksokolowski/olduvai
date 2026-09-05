#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""LOC + Halstead volume + Maintainability Index, per file and per layer.

Inputs, each from the tool that measures it best:
  Halstead V   multimetric   (pass its JSON via --mm)
  CC           lizard --csv  (pass via --cc)
  LOC          computed here, C++-aware, so the definition is explicit

Do NOT substitute multimetric's own cyclomatic_complexity or
maintainability_index.  Its CC is a weak file-level count (257 for game_app.cpp
against lizard's 452 summed over the file), and MI consumes CC, so using it
corrupts every MI silently.  Its MI clamps at 0; this recomputes unclamped so
the sign stays visible.

READ docs/internal/archive/2026-07-30-cognitive-mi-audit.md SECTION 5 BEFORE
ACTING ON THE MI COLUMN.  MI saturates at 0 on the 14 files that matter, its
size terms are ~78% of the penalty, and the base formula is blind to the 9286
rationale comment lines that BACKLOG section 4 forbids deleting.  It is
measured here to justify NOT tracking it.  Instrumentation, not a gate.

Usage:
  multimetric $(git ls-files 'src/**/*.cpp' 'src/**/*.hpp') > mm.json
  lizard src -l cpp --csv > cc.csv
  python3 scripts/metrics/loc_mi.py --mm mm.json --cc cc.csv
"""
import argparse
import csv
import collections
import json
import math
import os
import re
import subprocess
import sys

_ap = argparse.ArgumentParser()
_ap.add_argument("--mm", default="mm.json", help="multimetric JSON")
_ap.add_argument("--cc", default="cc.csv", help="lizard --csv output")
ARGS = _ap.parse_args()
ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                      text=True, check=True).stdout.strip()
os.chdir(ROOT)


def loc_counts(path):
    """raw, blank, comment-only, code. A line with code AND a trailing comment
    counts as code — the conservative choice, since it never inflates comments."""
    raw = blank = comment = code = 0
    in_block = False
    for line in open(path, encoding="utf-8", errors="replace"):
        raw += 1
        s = line.strip()
        if not s:
            blank += 1
            continue
        if in_block:
            comment += 1
            if "*/" in s:
                in_block = False
                after = s.split("*/", 1)[1].strip()
                if after:
                    comment -= 1
                    code += 1
            continue
        if s.startswith("//"):
            comment += 1
        elif s.startswith("/*"):
            comment += 1
            if "*/" not in s:
                in_block = True
        else:
            code += 1
            if "/*" in s and "*/" not in s.split("/*", 1)[1]:
                in_block = True
    return raw, blank, comment, code


files = [f for f in subprocess.run(["git", "ls-files", "src"], capture_output=True,
                                   text=True, check=True).stdout.split()
         if f.endswith((".cpp", ".hpp"))]

mm = json.load(open(ARGS.mm))["files"]
mm = {os.path.relpath(k, ROOT): v for k, v in mm.items()}

cc_sum, cc_max, cc_fns = collections.Counter(), collections.Counter(), collections.Counter()
for r in csv.reader(open(ARGS.cc)):
    if len(r) < 9:
        continue
    f, c = r[6], int(r[1])
    cc_sum[f] += c
    cc_max[f] = max(cc_max[f], c)
    cc_fns[f] += 1


def layer(p):
    q = p.split("/")
    return q[1] if len(q) > 1 else "?"


rows = []
for f in files:
    raw, blank, comm, code = loc_counts(f)
    V = mm.get(f, {}).get("halstead_volume", 0.0)
    G = cc_sum.get(f, 0)
    L = max(code, 1)
    if V <= 0:
        continue
    mi = 171 - 5.2 * math.log(V) - 0.23 * G - 16.2 * math.log(L)
    cm = comm / raw if raw else 0.0
    mi_cw = mi + 50 * math.sin(math.sqrt(2.4 * cm))
    rows.append(dict(f=f, raw=raw, blank=blank, comment=comm, code=code, V=V, G=G,
                     mi=mi, mi_n=max(0.0, mi) * 100 / 171, mi_cw=mi_cw,
                     fns=cc_fns.get(f, 0), maxcc=cc_max.get(f, 0)))

T = lambda k: sum(r[k] for r in rows)
print("=" * 76)
print("LINES OF CODE (tracked src/, C++-aware)")
print("=" * 76)
print(f"  raw lines      {T('raw'):>7}")
print(f"  blank          {T('blank'):>7}  ({100*T('blank')/T('raw'):.1f}%)")
print(f"  comment-only   {T('comment'):>7}  ({100*T('comment')/T('raw'):.1f}%)")
print(f"  code           {T('code'):>7}  ({100*T('code')/T('raw'):.1f}%)")
print(f"  comment:code ratio  1 : {T('code')/T('comment'):.2f}")
print()
print(f"{'layer':<14}{'files':>6}{'raw':>8}{'code':>8}{'comment':>9}{'cmt%':>7}")
agg = collections.defaultdict(lambda: collections.Counter())
for r in rows:
    a = agg[layer(r["f"])]
    for k in ("raw", "code", "comment"):
        a[k] += r[k]
    a["files"] += 1
for k, a in sorted(agg.items(), key=lambda kv: -kv[1]["raw"]):
    print(f"{k:<14}{a['files']:>6}{a['raw']:>8}{a['code']:>8}{a['comment']:>9}"
          f"{100*a['comment']/a['raw']:>6.1f}%")

print()
print("=" * 76)
print("HALSTEAD VOLUME")
print("=" * 76)
vs = sorted(r["V"] for r in rows)
print(f"  total V = {T('V'):,.0f}   median file V = {vs[len(vs)//2]:,.0f}   "
      f"max = {vs[-1]:,.0f}")
print(f"\n{'V':>12}{'code':>7}{'V/code':>8}  file")
for r in sorted(rows, key=lambda r: -r["V"])[:12]:
    print(f"{r['V']:>12,.0f}{r['code']:>7}{r['V']/r['code']:>8.1f}  {r['f']}")
print("\n  Densest by V per code line (>=200 code lines):")
for r in sorted([r for r in rows if r["code"] >= 200], key=lambda r: -r["V"]/r["code"])[:8]:
    print(f"{r['V']:>12,.0f}{r['code']:>7}{r['V']/r['code']:>8.1f}  {r['f']}")

print()
print("=" * 76)
print("MAINTAINABILITY INDEX")
print("=" * 76)
mis = sorted(r["mi_n"] for r in rows)
n = len(mis)
print(f"  files={n}  median MI(norm)={mis[n//2]:.1f}  mean={sum(mis)/n:.1f}  "
      f"min={mis[0]:.1f}  max={mis[-1]:.1f}")
bands = [(20, 101, "good      (>=20)"), (10, 20, "moderate  (10-20)"), (0, 10, "poor      (<10)")]
print()
for lo, hi, lbl in bands:
    sel = [r for r in rows if lo <= r["mi_n"] < hi]
    print(f"  {lbl}  {len(sel):>4} files ({100*len(sel)/n:>4.1f}%)  "
          f"{sum(r['code'] for r in sel):>6} code lines")
print(f"\n  clamped at 0 (formula went negative): "
      f"{sum(1 for r in rows if r['mi'] < 0)} files")
print(f"\n{'MI':>6}{'MIcw':>7}{'code':>7}{'V':>11}{'CC':>6}  file   [worst 15]")
for r in sorted(rows, key=lambda r: r["mi"])[:15]:
    print(f"{r['mi_n']:>6.1f}{max(0,r['mi_cw'])*100/171:>7.1f}{r['code']:>7}"
          f"{r['V']:>11,.0f}{r['G']:>6}  {r['f']}")
print()
print("  Per layer (median normalized MI):")
per = collections.defaultdict(list)
for r in rows:
    per[layer(r["f"])].append(r["mi_n"])
for k, v in sorted(per.items(), key=lambda kv: sorted(kv[1])[len(kv[1])//2]):
    v = sorted(v)
    print(f"    {k:<14} median {v[len(v)//2]:>5.1f}   worst {v[0]:>5.1f}   files {len(v)}")
