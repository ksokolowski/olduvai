#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Krzysztof Sokołowski
"""prose_tells.py: constructions that make prose read machine-written.

    scripts/metrics/prose_tells.py [FILE...]     # default: the public docs

Per file: em dashes per 100 words, the "not X, but Y" family, stock
phrases (inflation, sales words, sayings, run-ups, hedges, chat residue),
-ing riders, runs of bold-lead bullets, three sentences in a row opening
with the same word, curly quotes; then every hit with its line.

The catalogue follows the humanizer agent skill's patterns
(github.com/blader/humanizer, MIT), reduced to what a regex can see and
to this repo's vocabulary: "gate", "key", "enhance" and "highlight" are
ordinary terms here and are not counted.

WHERE THIS IS WRONG.  Each tell is also ordinary English: an em dash or a
"not only" is often the right choice.  The signal is density and repetition,
so read the hits and judge; a clean report is not good writing and a hit is
not a defect.  Code blocks and tables are skipped, headings and link targets
are not.  Report-only, never a gate.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT = ["README.md", "CONTRIBUTING.md", "LEGAL.md", "SECURITY.md",
           "SIGNING.md", "CHANGELOG.md"] + sorted(
               str(p.relative_to(ROOT)) for p in (ROOT / "docs").glob("*.md"))

# Stock words and phrases: the vocabulary of generated text, rare in the
# prose of people describing their own project.  Game and tool terms that
# collide ("next level", a fuzz "harness") are left out.
STOCK = [
    "delve", "seamless", "seamlessly", "robust", "leverage", "elevate",
    "tapestry", "testament", "love letter", "whether you're", "at its core",
    "it's worth noting", "worth noting", "crucially", "boasts", "unlock",
    "empower", "dive in", "dive into", "game-changer", "vibrant",
    "meticulous", "meticulously", "effortless", "effortlessly",
    "embark", "realm", "journey", "in today's", "rest assured",
    "look no further", "the best of both worlds", "stands as", "a nod to",
    "breathe new life", "timeless", "cutting-edge", "state-of-the-art",
    "in a nutshell", "simply put", "here's the thing",
    "the result:", "the catch:", "the upshot", "truly", "beloved",
    "iconic", "nostalgic journey", "trip down memory lane",
    # inflation and sales words
    "pivotal", "crucial", "intricate", "interplay", "landscape",
    "showcase", "showcases", "underscore", "underscores", "fostering",
    "garner", "bolstered", "enduring", "align with", "deep dive",
    "plays a key role", "setting the stage", "indelible", "profound",
    "nestled", "in the heart of", "groundbreaking", "renowned", "stunning",
    "breathtaking", "must-visit", "serves as", "stands as", "functions as",
    # sayings, run-ups, arguing with no one, hedges
    "the real question", "what really matters", "the heart of the matter",
    "let's be honest", "real talk", "here's what you need to know",
    "to be clear", "don't get me wrong", "i'm not saying",
    "could potentially", "might arguably", "to be fair",
    # chat residue
    "i hope this helps", "great question", "let me know",
]
STOCK_RE = re.compile(r"\b(" + "|".join(re.escape(s) for s in STOCK) + r")\b",
                      re.IGNORECASE)
# "not X, but Y", "not just", "isn't only", "more than just": the reveal.
REVEAL_RE = re.compile(
    r"\b(not (just|only|merely|simply)\b|isn't (just|only)\b|"
    r"more than just\b|not\b[^.;:]{1,40}\bbut\b)", re.IGNORECASE)
BOLD_BULLET_RE = re.compile(r"^\s*[-*] \*\*[^*]+\*\*")
# A participle bolted on after a comma: ", highlighting how ...".
RIDER_RE = re.compile(r", (highlighting|underscoring|emphasizing|reflecting|"
                      r"symbolizing|showcasing|fostering|contributing to|"
                      r"cementing|signalling|signaling)\b", re.IGNORECASE)
CURLY_RE = re.compile("[\u201c\u201d\u2018\u2019]")
SENTENCE_RE = re.compile(r"(?:^|(?<=[.!?])\s+)([A-Z][a-z']+)")


def prose_lines(text):
    """(line number, line) outside fenced code and tables."""
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced or line.lstrip().startswith("|"):
            continue
        yield n, line


def scan(path):
    lines = list(prose_lines(path.read_text(encoding="utf-8")))
    words = sum(len(l.split()) for _, l in lines)
    dashes = sum(l.count("—") for _, l in lines)
    hits = []
    run = best_run = 0
    openers = []   # (line, first word) of each sentence, in order
    for n, l in lines:
        for m in STOCK_RE.finditer(l):
            hits.append((n, "stock", m.group(0)))
        for m in REVEAL_RE.finditer(l):
            hits.append((n, "reveal", m.group(0)))
        for m in RIDER_RE.finditer(l):
            hits.append((n, "rider", m.group(0)))
        if CURLY_RE.search(l):
            hits.append((n, "curly", "curly quote"))
        if not l.lstrip().startswith(("#", "-", "*", ">")):
            openers += [(n, w) for w in SENTENCE_RE.findall(l)]
        if BOLD_BULLET_RE.match(l):
            run += 1
        elif l.strip() and not l.startswith("  "):   # a wrapped bullet continues
            run = 0
        best_run = max(best_run, run)
    for i in range(len(openers) - 2):
        w = openers[i][1]
        if w not in ("The", "A") and openers[i + 1][1] == w == openers[i + 2][1]:
            hits.append((openers[i][0], "opener", f"3x '{w}'"))
    per100 = 100.0 * dashes / words if words else 0.0
    return words, dashes, per100, best_run, hits


def main():
    files = sys.argv[1:] or DEFAULT
    print(f"{'file':32} {'words':>6} {'dashes':>6} {'/100w':>6} "
          f"{'bold-run':>8} {'stock':>5} {'reveal':>6} {'other':>5}")
    details = []
    for f in files:
        p = ROOT / f if not pathlib.Path(f).is_absolute() else pathlib.Path(f)
        if not p.is_file():
            continue
        words, dashes, per100, run, hits = scan(p)
        stock = sum(1 for h in hits if h[1] == "stock")
        reveal = sum(1 for h in hits if h[1] == "reveal")
        other = len(hits) - stock - reveal
        print(f"{f:32} {words:6} {dashes:6} {per100:6.2f} {run:8} "
              f"{stock:5} {reveal:6} {other:5}")
        details += [(f, n, kind, text) for n, kind, text in hits]
    if details:
        print()
        for f, n, kind, text in details:
            print(f"  {f}:{n}  {kind:6}  {text}")


if __name__ == "__main__":
    main()
