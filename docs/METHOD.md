# How this engine was built — the reverse-engineering method

Olduvai was not written by watching the game and guessing. It is the
end product of a deliberate, evidence-driven pipeline that turns a
1991 real-mode DOS executable into a modern, validated engine — twice
(a reference implementation first, then this one). This document
describes that method. It contains no game content and no material
derived from game assets; it describes *process*.

The engine is an independent reimplementation written from scratch — not a
decompilation, disassembly dump, or reconstruction of the original source.
The analysis described below was performed on a legitimately owned copy to
learn how the engine must behave to interoperate with the original data
files, for interoperability purposes (cf. EU Directive 2009/24/EC, arts.
5(3) and 6). The analysis artifacts themselves are not part of this project. The Python
reference implementation and the research archive it belongs to are kept
private by design — the same rule that keeps reverse-engineering artifacts
out of this repository keeps them out of publication entirely.

## The prime directive

**The EXE's bytes are the only truth.** Not any decompiler's output,
not memory of how the game "should" behave, not other ports. Every
concrete value in the engine — a sprite index, a damage number, a
timer, a table entry, an animation step — must trace to a specific
instruction at a specific offset in the original executable, and that
citation ships in the source code and the commit message.

The corollary: **the engine reproduces the original's bugs.** A
faithful port that "fixes" the original is a different game. Known
original bugs are preserved and annotated in-code; the handful of
deliberate deviations (hardware-specific rendering internals,
quality-of-life options ruled in by the project owner) are each
individually documented with the evidence for what the original
actually does.

## Multi-angle triangulation

Every decompiler hallucinates. The method treats decompiler output as
*hypothesis*, never evidence, and requires agreement across
independent tools — with a strict arbitration order:

1. **Ghidra** (headless, scripted) — structure: control flow, function
   boundaries, cross-references, a first-pass decompilation of the
   whole binary. Good for orientation; wrong often enough that nothing
   ships on its word alone.
2. **Reko** — a second, independent decompiler with strong DOS
   real-mode support. Its call-graph annotations are frequently
   cleaner than Ghidra's. Disagreement between the two is a signal,
   not a tiebreak.
3. **Rizin** — a third opinion on function boundaries; catches
   entry points the others skip (e.g. targets reached only through
   jump tables).
4. **Capstone** (as a library, 16-bit x86 mode) — the **byte-level
   arbitrator**. When tools disagree, or whenever a concrete numeric
   value is about to enter the engine, the raw instruction bytes are
   disassembled and read directly. Capstone wins every dispute.
5. **Raw file bytes** — for data tables, seeds, palette indices:
   computed file offsets, read with a hex viewer, cross-checked
   against the segment arithmetic. (One recurring trap this catches:
   Capstone prints 16-bit displacements as signed, so tables in the
   upper half of the data segment must always be verified from raw
   bytes.)
6. **Runtime tracing** (instrumented DOSBox-X) — last resort, for
   self-modifying code, indirect dispatch, and timing questions that
   static reading cannot settle.

Experience across hundreds of verified functions: **two tools agreeing
is not enough.** Values that passed two decompilers and a full test
suite have still been wrong; only the byte-level read caught them.
Roughly 80% of fidelity bugs found in play-testing traced back to a
single-tool reading that triangulation would have caught.

## Findings: the knowledge base

Reverse-engineering work evaporates unless it is written down at the
moment of proof. Every triangulated value, every re-walked function,
every closed bug, and — just as important — **every refuted
hypothesis** becomes a *Finding*: a short document with the claim, the
evidence (tool outputs, byte offsets, raw bytes), a confidence grade,
and its resolution. Several hundred Findings accumulated over the
project; they are the project's long-term memory, and they outrank
fresh analysis by default (a prior high-confidence Finding has more
than once prevented a plausible-looking wrong "correction").

Function and data-segment registries (name, size, evidence, audit
status per entry) are machine-checked: linters enforce that verified
entries carry evidence dates, that engine constants alias verified
catalog entries, and that fidelity commits cite their evidence.

## Play-testing as evidence

Human gameplay memory of the original is treated as a first-class
triangulation signal. "The original doesn't do that" from someone who
played the game for years has repeatedly outranked tool consensus —
several bugs that passed multiple decompilers *and* the automated test
suites were caught only by a player's eye. The workflow answers such
reports with a byte-level re-walk, never with a defense of the
existing code. An in-game capture key (screenshot + full state dump +
suggested EXE function) turns each observation into an actionable,
reproducible report.

## Prior art: useful, then dangerous

Gregory Montoir's Amiga-version reimplementation was a valuable
*orientation* aid early on — mechanic shapes, sprite encoding ideas,
a rough map of the first level. But the Amiga and DOS versions differ
in concrete values everywhere, and the reimplementation simplifies
further. Past the orientation phase it produced more false directions
than help: sprite indices, thresholds, hitboxes and state-machine
shapes taken from it as working hypotheses caused real bugs — each
was disproven against the DOS evidence and re-derived independently. The project's standing rule became: *prior art may
suggest where to look, never what is true* — every value must come
from the DOS executable itself. (A related codebase for a different
Titus engine family proved so misleading it was banned as a reference
outright.)

## Pseudo-code → reference engine → this engine

The port happened twice, deliberately:

1. **Python reference engine.** Findings were first expressed as
   executable pseudo-code — Python that mirrors the EXE's structure
   function-by-function, with the byte citations inline. Python's
   ergonomics make it the ideal medium for *proving understanding*:
   fast to write, trivially testable (≈2,000 unit + contract tests),
   and readable enough that a mismatch against a Finding is obvious.
   The reference engine became fully playable end-to-end.
2. **Olduvai.** With behaviour proven, the reference was mapped into
   modern C++/SDL2 for performance and features. The mapping is kept
   honest by construction: both engines share one deterministic RNG
   lineage and are run in **lockstep** — a cross-engine differ
   compares hundreds of frames field-by-field (positions, velocities,
   counters, RNG state) with zero tolerance, and a 12-scenario
   behavioural corpus plus a golden-trace CI gate pin the agreement
   permanently. When the two engines disagree, one of them is wrong
   about the EXE, and the Findings decide which.

This two-stage shape is the method's biggest practical win: the
expensive part (proving what the original does) is done in the medium
best suited to proof, and the performant part inherits that proof
mechanically instead of re-deriving it.

## Validation, always on

- Cross-engine frame diff (reference ↔ Olduvai) in shared-RNG lockstep.
- Golden trace: a scripted 300-frame run pinned against an
  oracle-validated fixture, run in CI on every change.
- Characterization tests freeze behaviour before refactors; large
  presentation refactors are proven by pixel-identical screenshot
  comparison against the pre-refactor build.
- Every fidelity fix adds or refreshes evidence in the registries the
  same day it lands.

---

*The pipeline described here is general: nothing in it is specific to
this game beyond the file formats. The same method — multi-tool
triangulation, Findings discipline, executable pseudo-code, reference
engine, lockstep port — applies to any DOS-era binary.*
