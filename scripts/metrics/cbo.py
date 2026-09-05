#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""Per-type CBO (coupling between objects) over tracked src/.

For each class/struct declared in a project header, counts the DISTINCT other
project-declared types referenced by that type's own body plus its out-of-line
`X::` member definitions in the matching .cpp.

APPROXIMATE, and deliberately so: there is no AST here (the dev box has no
clang), so this is token matching over brace-matched regions with comments
stripped.  It is an upper bound.

Do NOT "simplify" this by blobbing whole files.  That variant was written first
and discarded as invalid: it scored the three Boss* structs sharing
boss_app.hpp at 46 apiece by crediting each with the others' declarations.

Read docs/internal/archive/2026-07-30-ck-metrics-audit.md §5 BEFORE using the
output to justify a refactor.  This metric cannot see closure-scoped coupling,
which is where this codebase's real coupling lives — giving the frame loop an
owner will RAISE this number while LOWERING actual coupling.

Usage: python3 scripts/metrics/cbo.py   (from anywhere in the worktree)
"""
import collections
import os
import re
import subprocess
import sys

DECL_RE = re.compile(r"^[ \t]*(class|struct)\s+([A-Z][A-Za-z0-9_]*)\s*(?:final)?\s*(:[^{;]*)?\{", re.M)


def brace_match(t, start):
    """Index of the '}' closing the first '{' at or after start."""
    i = t.index("{", start)
    depth = 0
    for j in range(i, len(t)):
        if t[j] == "{":
            depth += 1
        elif t[j] == "}":
            depth -= 1
            if depth == 0:
                return i, j
    return i, len(t) - 1


def member_defs(cpp_text, name):
    """Regions of a .cpp defining `name::member` — signature to closing brace."""
    out = []
    for m in re.finditer(r"^[^\s/].*\b" + re.escape(name) + r"::", cpp_text, re.M):
        end = cpp_text.find("\n}", m.start())
        out.append(cpp_text[m.start():end + 2] if end > 0 else cpp_text[m.start():m.start() + 400])
    return out


def main():
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                          capture_output=True, text=True, check=True).stdout.strip()
    os.chdir(root)
    files = [f for f in subprocess.run(["git", "ls-files", "src"], capture_output=True,
                                       text=True, check=True).stdout.split()
             if f.endswith((".cpp", ".hpp"))]
    text = {f: open(f, encoding="utf-8", errors="replace").read() for f in files}

    types = {}
    for f in files:
        if not f.endswith(".hpp"):
            continue
        for m in DECL_RE.finditer(text[f]):
            name = m.group(2)
            if name not in types:
                types[name] = (f,) + brace_match(text[f], m.start())
    if not types:
        sys.exit("no types found")

    cbo = {}
    for name, (hdr, i, j) in types.items():
        parts = [text[hdr][i:j + 1]]
        cpp = hdr[:-4] + ".cpp"
        if cpp in text:
            parts += member_defs(text[cpp], name)
        blob = "\n".join(parts)
        blob = re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", blob), flags=re.S)
        refs = {t for t in re.findall(r"\b[A-Z][A-Za-z0-9_]*\b", blob)
                if t in types and t != name}
        cbo[name] = (len(refs), sorted(refs), hdr)

    vals = sorted(v[0] for v in cbo.values())
    n = len(vals)
    print(f"types={n}  mean CBO={sum(vals)/n:.1f}  median={vals[n//2]}  "
          f"p90={vals[int(.9*n)]}  max={vals[-1]}\n")
    print("CBO distribution:")
    for lo, hi in [(0, 0), (1, 3), (4, 7), (8, 14), (15, 24), (25, 10**9)]:
        k = sum(1 for v in vals if lo <= v <= hi)
        print(f"  {f'{lo}-{"inf" if hi > 10**8 else hi}':>7}  {k:>4} types ({100*k/n:>4.1f}%)")

    print("\nTop 18 by CBO:")
    print(f"{'CBO':>4}  {'type':<32} declared in")
    for name, (c, _, hdr) in sorted(cbo.items(), key=lambda kv: -kv[1][0])[:18]:
        print(f"{c:>4}  {name:<32} {hdr}")

    used_by = collections.Counter()
    for _, (_, refs, _) in cbo.items():
        used_by.update(refs)
    print("\nMost-referenced types (afferent — changing these ripples widest):")
    for t, k in used_by.most_common(12):
        print(f"  {k:>3}  {t:<28} ({types[t][0]})")


if __name__ == "__main__":
    main()
