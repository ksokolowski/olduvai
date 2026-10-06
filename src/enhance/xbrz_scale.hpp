// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRZ (third_party/xbrz) over RGBA byte buffers: the `xbrz` HD profile.
#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// RGBA in, (W*s)x(H*s) RGBA out; s in 2..6 (the profile uses 2 to 4).  Alpha
// takes part in both the edge decision and the blend, so a sprite's silhouette
// rounds off like its interior and a transparent pixel's colour never leaks in.
std::vector<std::uint8_t> xbrz_scale(const std::vector<std::uint8_t>& rgba,
                                     int w, int h, int s);

// Which of xBRZ's two exact colour-distance paths runs: the CPU's default (the
// alpha table on armhf, upstream's on every other build), or one of them by
// name.  They give the same pixels bit for bit; the choice is speed alone, and
// a test holds the two equal on every host.
enum class XbrzDistance { Platform, Reference, AlphaTable };
std::vector<std::uint8_t> xbrz_scale_with(const std::vector<std::uint8_t>& rgba,
                                          int w, int h, int s, XbrzDistance path);

}  // namespace olduvai::enhance
