#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""comment_only.py -- prove a change touches comments only.

Strips // and /* */ comments (string and raw-string literals kept) from each
changed C/C++ file at BASE and in the working tree, collapses whitespace, and
compares.  Also collects the distinct evidence citations (FUN_xxxx, +0xNN)
in comments on both sides: a pass may reword or deduplicate them, never drop
one entirely.

  scripts/metrics/comment_only.py            # working tree vs HEAD
  scripts/metrics/comment_only.py HEAD~3     # vs another ref

Exit 1 if any file's code changed or a citation disappeared.  Blind to a
change inside a string literal that looks like a comment -- none exist today.
"""
import re
import subprocess
import sys

CITE = re.compile(r"FUN_[0-9a-fA-F]{4}_[0-9a-fA-F]{4}|\+0x[0-9a-fA-F]+")


def split(src):
    """Return (code, comments) with comments removed from code."""
    code, notes, i, n = [], [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            notes.append(src[i:j])
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            notes.append(src[i:j])
            code.append(" ")
            i = j
        elif src.startswith('R"', i) and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', src[i:])
            if not m:
                code.append(src[i])
                i += 1
                continue
            end = ")" + m.group(1) + '"'
            j = src.find(end, i)
            j = n if j < 0 else j + len(end)
            code.append(src[i:j])
            i = j
        elif src[i] in "\"'":
            q, j = src[i], i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            code.append(src[i:j + 1])
            i = j + 1
        else:
            code.append(src[i])
            i += 1
    return re.sub(r"\s+", " ", "".join(code)).strip(), "\n".join(notes)


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    files = [f for f in git("diff", "--name-only", base).split()
             if f.endswith((".cpp", ".hpp", ".h", ".c"))]
    bad = 0
    for f in files:
        old = git("show", f"{base}:{f}")
        try:
            new = open(f, encoding="utf-8").read()
        except FileNotFoundError:
            print(f"DELETED  {f}")
            bad += 1
            continue
        (oc, on), (nc, nn) = split(old), split(new)
        if oc != nc:
            print(f"CODE     {f}")
            bad += 1
        lost = sorted(set(CITE.findall(on)) - set(CITE.findall(nn)))
        if lost:
            print(f"CITES    {f}: {' '.join(lost)}")
            bad += 1
    print(f"comment_only: {len(files)} file(s) vs {base}, {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
