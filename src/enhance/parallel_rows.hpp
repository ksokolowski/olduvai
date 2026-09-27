// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#pragma once

#include <functional>

namespace olduvai::enhance {

// Run `body(y0, y1)` over [0, h) split into contiguous bands on a persistent
// worker pool; the caller runs band 0 (N participants = N-1 workers).
// Bit-identical to serial: in every scaler input row y writes only output rows
// [y*s, y*s+s), and reads come from buffers complete before the loop, so each
// byte is computed the same way on any thread.  Fixed bands, no work stealing,
// so a divergence is reproducible.  Not OpenMP: libfluidsynth's libgomp pool
// already crashes on dlclose (audio.cpp).
void parallel_rows(int h, const std::function<void(int y0, int y1)>& body);

// Participants including the caller; 1 = serial.  OLDUVAI_UPSCALE_THREADS
// overrides (bisecting without a rebuild).
int parallel_row_threads();

// Force serial execution: test_upscale_threading byte-compares each scaler
// both ways, and a bisect can switch threading off.  One atomic read per
// parallel_rows() call.
void set_parallel_rows_enabled(bool on);
bool parallel_rows_enabled();

}  // namespace olduvai::enhance
