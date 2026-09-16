// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The exact build this binary came from, next to OLDUVAI_VERSION:
//   "a97f2a7, 2026-09-13 10:41"             clean tree: hash + commit time
//   "a97f2a7-dirty, built 2026-09-13 11:02" uncommitted changes: + build time
//   "unknown"                                built without git
// Generated at BUILD time by cmake/build_id.cmake (target olduvai_build_id),
// linked into olduvai_formats so every binary carries it.  Shown by
// --version, the startup line on stderr (so a handheld's log names its
// build) and the F5 bug report.

#pragma once

namespace olduvai {

const char* build_id();

}  // namespace olduvai
