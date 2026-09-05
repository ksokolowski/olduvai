#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""Which prologue names does a driver's frame loop actually use, and how hot?

BACKLOG section 3.7 is "give the loop an owner".  Its measurement — 97 names
declared, 85 referenced by the loop, 62 of them mutable — was taken by hand on
2026-07-27 and the item says to re-measure before starting, because the numbers
move.  This takes it.

probe_slice.sh answers the complementary question: for a candidate SLICE, how
many names would have to cross the new boundary.  That is the right instrument
for deciding whether to extract.  This one is for deciding what to OWN: it lists
every name the prologue declares, classifies it, and counts how often the loop
body reaches for it.  A cluster of hot mutable names that belong to one subject
is an owner waiting to be written.

Heuristics, and they are heuristics: declarations are recognised by pattern, not
by parsing C++.  Read the output as a ranked lead list, not as truth.  The
`const`/lambda/mutable split is what matters and it is the part the regex gets
right; an occasional miscount of a nested declaration does not move a cluster.

    scripts/metrics/loop_owners.py <file> <fn-first-line> <loop-line> <fn-last-line>

e.g. scripts/metrics/loop_owners.py src/presentation/game_app.cpp 338 1220 2768
"""
import re
import sys
from collections import Counter

DECL = re.compile(
    r"^\s*(?P<const>const\s+|constexpr\s+)?"
    r"(?:auto|bool|int|float|double|char|std::\w+|[A-Z]\w*|unsigned|Uint\d+|"
    r"std::vector<[^>]+>|std::function<.*>)"
    r"[\w:<>,\s\*&\[\]]*?\b(?P<name>[a-z_]\w*)\s*(?P<init>[=({;])"
)
KEYWORDS = {"if", "for", "while", "switch", "return", "else", "do", "case",
            "const", "auto", "static", "using", "namespace", "struct", "class"}


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    path, first, loop, last = (sys.argv[1], int(sys.argv[2]),
                               int(sys.argv[3]), int(sys.argv[4]))
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    prologue = lines[first - 1:loop - 1]
    body = "\n".join(lines[loop - 1:last])

    decls = {}
    for raw in prologue:
        line = re.sub(r"//.*", "", raw)
        m = DECL.match(line)
        if not m:
            continue
        name = m.group("name")
        if name in KEYWORDS or name in decls:
            continue
        is_lambda = "[" in line and "]" in line.split("=", 1)[-1][:6] \
            if "=" in line else False
        kind = ("lambda" if re.search(r"=\s*\[", line)
                else "const" if m.group("const") else "mutable")
        decls[name] = kind

    refs = Counter()
    for name in decls:
        refs[name] = len(re.findall(r"\b" + re.escape(name) + r"\b", body))

    used = {n: k for n, k in decls.items() if refs[n] > 0}
    kinds = Counter(used.values())
    print(f"{path}  prologue {first}-{loop - 1}, loop {loop}-{last}")
    print(f"  declared {len(decls)} | referenced by the loop {len(used)}")
    for k in ("mutable", "const", "lambda"):
        print(f"    {k:<8} {kinds.get(k, 0)}")
    print()
    print("  name                             kind      loop refs")
    for name, k in sorted(used.items(), key=lambda kv: -refs[kv[0]]):
        print(f"  {name:<32} {k:<9} {refs[name]:5d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
