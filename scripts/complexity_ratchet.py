#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""The complexity ratchet: every function over a clang-tidy size or
cognitive-complexity threshold is on a list, with its numbers and the reason it
may be there.  Driven by `scripts/check_tidy.sh --ratchet[-update]`, which runs
the pinned clang-tidy and hands its output here.

    complexity_ratchet.py <tidy-output> <baseline> [--update]

Check: fails on a function not on the list, a number that moved (either way:
a gain is locked in by updating the list, so the list stays the truth), an
entry no longer reported, or an entry without a reason.  Update: rewrites the
numbers and keeps the reasons; a new entry's reason is "?", which the check
refuses until someone writes one.

Keys are file + function (+ an ordinal when a file has two of the name, and
for lambdas, which clang-tidy does not name); line numbers are not part of the
key, so an edit above a function does not churn the list.
"""

import re
import sys

WARN = re.compile(r"^(?P<path>\S+?):(?P<line>\d+):\d+: warning: (?P<msg>.*) "
                  r"\[(?P<check>readability-function-(?:size|cognitive-complexity))\]$")
NOTE = re.compile(r"^(?P<path>\S+?):(?P<line>\d+):\d+: note: (?P<msg>.*)$")
SIZE_NOTES = [
    (re.compile(r"^(\d+) lines including whitespace"), "lines"),
    (re.compile(r"^(\d+) statements"), "statements"),
    (re.compile(r"^(\d+) branches"), "branches"),
    (re.compile(r"^(\d+) parameters"), "parameters"),
    (re.compile(r"^(\d+) variables"), "variables"),
    (re.compile(r"^nesting level (\d+)"), "nesting"),
]
FUNC = re.compile(r"^function '([^']+)'")
COGNITIVE = re.compile(r"cognitive complexity of (\d+)")


def relpath(path):
    i = path.find("/src/")
    return path[i + 1:] if i >= 0 else path


def measure(lines):
    """{(file, line, name): {metric: value}} from clang-tidy's output.  A size
    warning's numbers are in notes at the warning's own location; they are
    matched by that location, not by order (source excerpts sit between)."""
    found = {}
    size_at = {}   # (file, line) -> the size warning's metrics
    for raw in lines:
        raw = raw.rstrip("\n")
        w = WARN.match(raw)
        if w:
            msg = w.group("msg")
            f = FUNC.match(msg)
            name = f.group(1) if f else "lambda"
            where = (relpath(w.group("path")), int(w.group("line")))
            entry = found.setdefault((*where, name), {})
            if w.group("check").endswith("cognitive-complexity"):
                entry["cognitive"] = int(COGNITIVE.search(msg).group(1))
            else:
                size_at[where] = entry
            continue
        n = NOTE.match(raw)
        if not n:
            continue
        entry = size_at.get((relpath(n.group("path")), int(n.group("line"))))
        if entry is None:
            continue
        for pattern, metric in SIZE_NOTES:
            m = pattern.match(n.group("msg"))
            if m:
                entry[metric] = int(m.group(1))
    return found


def keyed(found):
    """{"file function[#n]": {metric: value}}, ordinals by line."""
    out = {}
    seen = {}
    for (path, _line, name), metrics in sorted(found.items()):
        base = f"{path} {name}"
        seen[base] = seen.get(base, 0) + 1
        out[f"{base}#{seen[base]}"] = metrics
    # Only an ambiguous name keeps its ordinal.
    return {(k[:-2] if k.endswith("#1") and seen[k[:-2]] == 1 else k): v
            for k, v in out.items()}


def load(path):
    """{key: ({metric: value}, reason)}"""
    entries = {}
    try:
        with open(path) as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                body, _, reason = line.partition("#")
                parts = body.split()
                key = f"{parts[0]} {parts[1]}"
                metrics = dict((k, int(v)) for k, v in
                               (p.split("=") for p in parts[2:]))
                entries[key] = (metrics, reason.strip())
    except FileNotFoundError:
        pass
    return entries


HEADER = """\
# The complexity ratchet (scripts/check_tidy.sh --ratchet).  Every function
# over a .clang-tidy size or cognitive-complexity threshold, with its numbers
# and why it may be there.  A new entry, a moved number or a missing reason
# fails the gate; `scripts/check_tidy.sh --ratchet-update` rewrites the
# numbers and keeps the reasons.  A gain is locked in the same way.
#
# file function metric=value ...  # reason
"""


def write(path, measured, old):
    with open(path, "w") as f:
        f.write(HEADER)
        for key in sorted(measured):
            metrics = " ".join(f"{k}={v}" for k, v in sorted(measured[key].items()))
            reason = old[key][1] if key in old and old[key][1] else "?"
            f.write(f"{key} {metrics}  # {reason}\n")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    with open(argv[1]) as f:
        measured = keyed(measure(f))
    baseline = load(argv[2])
    if "--update" in argv[3:]:
        write(argv[2], measured, baseline)
        unjustified = [k for k in measured
                       if k not in baseline or not baseline[k][1]
                       or baseline[k][1] == "?"]
        print(f"complexity_ratchet: {len(measured)} entries written to {argv[2]}")
        for k in unjustified:
            print(f"  needs a reason: {k}")
        return 0

    def fmt(m):
        return " ".join(f"{k}={v}" for k, v in sorted(m.items()))

    problems = []
    for key, metrics in sorted(measured.items()):
        if key not in baseline:
            problems.append(f"new      {key} {fmt(metrics)}")
            continue
        was, reason = baseline[key]
        if metrics != was:
            moved = sorted(set(was) | set(metrics))
            problems.append(f"moved    {key} " + ", ".join(
                f"{k} {was.get(k, '-')} -> {metrics.get(k, '-')}"
                for k in moved if was.get(k) != metrics.get(k)))
        if not reason or reason == "?":
            problems.append(f"no reason {key}")
    for key in sorted(set(baseline) - set(measured)):
        problems.append(f"gone     {key} (was {fmt(baseline[key][0])})")
    if not problems:
        print(f"complexity_ratchet: OK — {len(measured)} functions over "
              "threshold, all on the list with a reason")
        return 0
    print("complexity_ratchet: FAIL — the list and the code disagree:")
    for p in problems:
        print(f"  {p}")
    print("A function that grew: shrink it, or justify the growth.  A gain:")
    print("lock it in.  Either way: scripts/check_tidy.sh --ratchet-update,")
    print("then give every '?' a reason, in scripts/complexity_baseline.txt.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
