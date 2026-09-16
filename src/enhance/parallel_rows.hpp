// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#pragma once

#include <functional>

namespace olduvai::enhance {

// Run `body(y0, y1)` over the half-open row range [0, h), split into
// CONTIGUOUS bands across a persistent worker pool.  The calling thread runs
// band 0 and joins the wait, so N participants means N-1 spawned workers.
//
// WHY THIS EXISTS (BACKLOG §3.22).  The HD upscalers are `for (int y …)` loops
// and omniscale x4 measured 57.7 ms of a 60.0 ms frame against a 54.9 ms
// budget — the shipped default profile, over budget on Apple silicon.
//
// WHY A BAND SPLIT IS BIT-IDENTICAL AND NOT MERELY EQUIVALENT.  In every
// scaler the output index is derived from the input row, `o = ((y*s + sy) * W
// + (x*s + sx)) * 4`, so input row y writes output rows [y*s, y*s+s) and no
// two bands touch the same byte.  Reads go to buffers that are fully computed
// before the loop and never written inside it.  Each output byte is therefore
// produced by the same code from the same inputs regardless of which thread
// runs it — no reassociation, no accumulation order, nothing float-sensitive.
// That is what lets mmpx's pixel hash gates prove the change EXACTLY.
//
// WHY NOT OpenMP.  This tree already carries a libgomp casualty: FluidSynth is
// built with OpenMP and its worker pool outlives the synth, so `dlclose` faults
// (measured 7 of 8 renders crashing; see audio.cpp's note).  Introducing a
// SECOND OpenMP pool next to that one is not a trade worth making for a row
// split we can write in eighty lines.
//
// Deterministic by construction: fixed contiguous bands, never work-stealing.
// Not because stealing would corrupt the output — it could not, the bytes are
// independent — but because a fixed split makes a divergence reproducible.
void parallel_rows(int h, const std::function<void(int y0, int y1)>& body);

// Effective participant count, including the calling thread.  1 means every
// call runs serially in the caller.  Honours OLDUVAI_UPSCALE_THREADS, which
// exists so a bisect can force 1 without a rebuild.
int parallel_row_threads();

// Force serial execution in the calling thread.  Two callers, both real:
//   * the always-green gate `test_upscale_threading` computes each scaler
//     BOTH ways in one process and byte-compares — the property claimed for
//     this split ("bit-identical, not merely equivalent") is only worth
//     anything if something checks it;
//   * a bisect that needs the threading out of the way without a rebuild.
// Cheap and global: an atomic read per parallel_rows() call, ~19 a frame.
void set_parallel_rows_enabled(bool on);
bool parallel_rows_enabled();

}  // namespace olduvai::enhance
