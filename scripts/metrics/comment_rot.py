#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""comment_rot.py -- comments that narrate HISTORY instead of explaining CODE.

Report-only, not a gate (BACKLOG §3.35).  Counts, per file under src/, the
signals of a comment that will rot the moment the history it tells moves on:

  backlog   BACKLOG / § references -- docs/internal/ is export-ignored, so in
            the PUBLIC tree each one points at a document nobody can read
  date      2026-09-21-style dates
  hash      commit hashes (7-9 hex chars with at least one letter and digit)
  owner     "owner" -- a ruling is a fine WHY, the person and the day are not
  history   used to / was wrong / this session / carrying this line / ...

Plus the comment:code ratio and every full-line comment block >= --block
lines.  The record of how the code got here belongs in commit messages and
archive/DONE-*.md; a comment should say what the code does and why.

    python3 scripts/metrics/comment_rot.py            # summary + top files
    python3 scripts/metrics/comment_rot.py --top 40 --block 20

WHERE IT IS WRONG:
- Evidence citations (`// FUN_xxxx_yyyy +0xNN`, EXE offsets) are REQUIRED by
  CLAUDE.md and deliberately not counted -- but a citation that ALSO carries
  a date or "owner-verified" is counted for those words.
- `hash` can hit a hex constant written without 0x; `owner` hits
  "owner-drawn" style prose.  Read the lines before acting on a file's count.
- Trailing `//` comments on code lines are scanned for signals but not
  counted as comment LINES (the ratio uses full-line comments only).
- It measures narration, not quality: a long block that explains a subtle
  invariant is not slop, and a short one that says "fixed 2026-09-01" is.
"""
import argparse
import collections
import pathlib
import re
import sys

SIGNALS = {
    "backlog": re.compile(r"BACKLOG|§\s?\d"),
    "date": re.compile(r"\b20\d\d-\d\d-\d\d\b"),
    "hash": re.compile(r"\b[0-9a-f]{7,9}\b"),
    "owner": re.compile(r"\bowner\b", re.I),
    "history": re.compile(
        r"\b(used to|until \d|was (?:WRONG|wrong|missing)|this session|"
        r"carrying this line|found (?:by|on|while)|first version|"
        r"no longer|previously)\b"),
}


def count(text):
    out = collections.Counter()
    for name, rx in SIGNALS.items():
        hits = rx.findall(text)
        if name == "hash":
            hits = [h for h in hits
                    if re.search(r"[a-f]", h) and re.search(r"\d", h)]
        out[name] += len(hits)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default="src")
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--block", type=int, default=25)
    a = ap.parse_args()

    root = pathlib.Path(a.root)
    if not root.is_dir():
        sys.exit(f"comment_rot: no such directory: {root}")
    files = sorted(p for p in root.rglob("*") if p.suffix in (".cpp", ".hpp", ".h"))

    total = collections.Counter()
    per = []
    blocks = []
    code_lines = comment_lines = 0
    for f in files:
        lines = f.read_text(errors="replace").splitlines()
        fc = collections.Counter()
        run = start = c_lines = 0
        for i, line in enumerate(lines, 1):
            s = line.strip()
            full = s.startswith(("//", "/*", "*"))
            m = re.search(r"//(.*)$", line)
            text = m.group(1) if m else (s if full else "")
            if text:
                fc += count(text)
            if full:
                c_lines += 1
                run += 1
                if run == 1:
                    start = i
            else:
                if run >= a.block:
                    blocks.append((run, f"{f}:{start}"))
                run = 0
                if s:
                    code_lines += 1
        if run >= a.block:
            blocks.append((run, f"{f}:{start}"))
        comment_lines += c_lines
        total += fc
        per.append((sum(fc.values()), c_lines, len(lines), str(f), fc))

    ratio = comment_lines / code_lines if code_lines else 0.0
    print(f"comment_rot: {len(files)} files under {root}/ -- {code_lines} code "
          f"lines, {comment_lines} full-line comment lines (ratio {ratio:.2f})")
    print("signals:", "  ".join(f"{k}={total[k]}" for k in SIGNALS),
          f" (total {sum(total.values())})")
    print(f"\ntop {a.top} files (signals | comment lines / file lines):")
    for n, c, total_lines, name, fc in sorted(per, key=lambda x: -x[0])[: a.top]:
        if n == 0:
            break
        detail = " ".join(f"{k}={v}" for k, v in fc.items() if v)
        print(f"  {n:4d} | {c:5d}/{total_lines:5d}  {name}  [{detail}]")
    print(f"\ncomment blocks >= {a.block} lines: {len(blocks)}")
    for n, loc in sorted(blocks, reverse=True)[: a.top]:
        print(f"  {n:3d}  {loc}")


if __name__ == "__main__":
    main()
