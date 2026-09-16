// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/parallel_rows.hpp"

#include <cstring>   // memcpy — see pack()/copy_px()
#include "enhance/pixel_scalers.hpp"

#include <cstddef>

// Classic palette-preserving pixel-art scalers.  Each output sub-pixel is
// copied verbatim from a source pixel — no blending — so the DOS palette and
// binary transparency survive untouched.  Ports of the reference engine's
// classical scalers (nearest / scale2x / scale3x, AdvanceMAME
// algorithm, https://www.scale2x.it/algorithm) and Eagle 2x (Dirk
// Stevens 1997, public domain;
// https://en.wikipedia.org/wiki/Pixel-art_scaling_algorithms#Eagle).

namespace olduvai::enhance {

namespace {

// Pack one RGBA pixel into a u32 for cheap whole-pixel equality.  Byte order
// is irrelevant (used only for == comparisons), as long as pack is consistent.
inline std::uint32_t pack(const std::vector<std::uint8_t>& px, std::size_t i) {
    // ONE 32-bit load, not four byte loads plus shifts and ORs.  The old form
    // compiled to exactly that on aarch64 — scale3x calls this NINE times per
    // source pixel, so it was ~36 ldrb where 9 ldr would do, and four A53s
    // contend for the same memory bandwidth.
    //
    // memcpy is the portable spelling of an unaligned load; every compiler
    // lowers it to a single instruction and it cannot trap on strict-alignment
    // targets the way a reinterpret_cast would.
    //
    // Byte ORDER changes from explicitly little-endian to host order.  That is
    // safe for the reason the original comment gives: this value is used ONLY
    // for whole-pixel equality, never decomposed, and both operands always come
    // from the same pack().  Consistency is the whole contract.
    std::uint32_t v;
    std::memcpy(&v, px.data() + i * 4, 4);
    return v;
}

// Copy the source pixel at (sx, sy) — clamped to edges — into dst[di].
inline void copy_px(const std::vector<std::uint8_t>& src, int w, int h,
                    int sx, int sy, std::vector<std::uint8_t>& dst,
                    std::size_t di) {
    // Branchless clamps: the compiler emits csel rather than two branches each.
    // These are called nine times per source pixel and are taken only at the
    // border, so the branch predictor was being asked to do nothing useful
    // millions of times per frame.
    sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
    sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);
    const std::size_t si =
        (static_cast<std::size_t>(sy) * w + sx) * 4;
    // ONE 32-bit move, not four byte moves.  Same reasoning as pack() above:
    // this is a pure copy, so byte order never enters into it.
    std::memcpy(dst.data() + di, src.data() + si, 4);
}

}  // namespace

std::vector<std::uint8_t> nearest_scale(const std::vector<std::uint8_t>& rgba,
                                        int w, int h, int scale) {
    if (scale <= 1) return rgba;
    const int ow = w * scale, oh = h * scale;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * oh * 4);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            const std::size_t si =
                (static_cast<std::size_t>(y / scale) * w + (x / scale)) * 4;
            const std::size_t di =
                (static_cast<std::size_t>(y) * ow + x) * 4;
            out[di] = rgba[si];
            out[di + 1] = rgba[si + 1];
            out[di + 2] = rgba[si + 2];
            out[di + 3] = rgba[si + 3];
        }
    return out;
}

std::vector<std::uint8_t> scale2x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h) {
    // Neighbourhood per source pixel P:
    //       A
    //     C P B
    //       D
    // E0 = (C==A && C!=D && A!=B) ? A : P
    // E1 = (A==B && A!=C && B!=D) ? B : P
    // E2 = (D==C && D!=B && C!=A) ? C : P
    // E3 = (B==D && B!=A && D!=C) ? D : P
    const int ow = w * 2;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * h * 2 * 4);
    auto at = [&](int x, int y) -> std::uint32_t {
        if (x < 0) x = 0; else if (x >= w) x = w - 1;
        if (y < 0) y = 0; else if (y >= h) y = h - 1;
        return pack(rgba, static_cast<std::size_t>(y) * w + x);
    };
    // §3.22: row-band split.  Writes are y-derived (base/di from y), reads go to the
    // read-only input via the clamping accessor, so bands never share an
    // output byte — bit-identical, and test_upscale_threading proves it.
    parallel_rows(h, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t A = at(x, y - 1), B = at(x + 1, y),
                                C = at(x - 1, y), D = at(x, y + 1);
            // E0
            const bool e0a = (C == A && C != D && A != B);
            const bool e1b = (A == B && A != C && B != D);
            const bool e2c = (D == C && D != B && C != A);
            const bool e3d = (B == D && B != A && D != C);
            const std::size_t base =
                (static_cast<std::size_t>(y * 2) * ow + x * 2) * 4;
            // Each sub-pixel takes its own side's neighbour when the rule
            // fires, else the centre — and the neighbour differs from the
            // centre in exactly ONE coordinate: E0/E3 in y, E1/E2 in x. The
            // invariant coordinate was previously also written as a ternary
            // (`e0a ? x : x`) for visual symmetry between the four lines.
            // Both arms were the same token, so the compiler folded it and the
            // output is unchanged; spelling it plainly says which axis moves.
            copy_px(rgba, w, h, x, e0a ? y - 1 : y, out, base);
            copy_px(rgba, w, h, e1b ? x + 1 : x, y, out, base + 4);
            copy_px(rgba, w, h, e2c ? x - 1 : x, y, out,
                    base + static_cast<std::size_t>(ow) * 4);
            copy_px(rgba, w, h, x, e3d ? y + 1 : y, out,
                    base + static_cast<std::size_t>(ow) * 4 + 4);
        }
    });
    return out;
}

std::vector<std::uint8_t> scale3x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h) {
    // 3x3 neighbourhood:
    //   A B C
    //   D E F
    //   G H I
    const int ow = w * 3;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * h * 3 * 4);
    auto at = [&](int x, int y) -> std::uint32_t {
        if (x < 0) x = 0; else if (x >= w) x = w - 1;
        if (y < 0) y = 0; else if (y >= h) y = h - 1;
        return pack(rgba, static_cast<std::size_t>(y) * w + x);
    };
    // §3.22: row-band split.  Writes are y-derived (base/di from y), reads go to the
    // read-only input via the clamping accessor, so bands never share an
    // output byte — bit-identical, and test_upscale_threading proves it.
    parallel_rows(h, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t A = at(x - 1, y - 1), B = at(x, y - 1),
                                C = at(x + 1, y - 1), D = at(x - 1, y),
                                E = at(x, y), F = at(x + 1, y),
                                G = at(x - 1, y + 1), H = at(x, y + 1),
                                I = at(x + 1, y + 1);
            const bool db_bf = (D == B && D != H && B != F);
            const bool bf_fh = (B == F && B != D && F != H);
            const bool hd_db = (H == D && H != F && D != B);
            const bool fh_hd = (F == H && F != B && H != D);

            // Each output sub-pixel selects a source neighbour or E.  We
            // record (dx, dy) source offsets to copy via copy_px.
            struct Sel { int dx, dy; };
            const Sel sels[9] = {
                db_bf ? Sel{-1, 0} : Sel{0, 0},                            // E0 -> D
                ((db_bf && E != C) || (bf_fh && E != A)) ? Sel{0, -1}      // E1 -> B
                                                         : Sel{0, 0},
                bf_fh ? Sel{1, 0} : Sel{0, 0},                             // E2 -> F
                ((hd_db && E != A) || (db_bf && E != G)) ? Sel{-1, 0}      // E3 -> D
                                                         : Sel{0, 0},
                Sel{0, 0},                                                 // E4 -> E
                ((bf_fh && E != I) || (fh_hd && E != C)) ? Sel{1, 0}       // E5 -> F
                                                         : Sel{0, 0},
                hd_db ? Sel{-1, 0} : Sel{0, 0},                            // E6 -> D
                ((fh_hd && E != G) || (hd_db && E != I)) ? Sel{0, 1}       // E7 -> H
                                                         : Sel{0, 0},
                fh_hd ? Sel{1, 0} : Sel{0, 0},                             // E8 -> F
            };
            const std::size_t base =
                (static_cast<std::size_t>(y * 3) * ow + x * 3) * 4;
            for (int sy = 0; sy < 3; ++sy)
                for (int sx = 0; sx < 3; ++sx) {
                    const Sel s = sels[sy * 3 + sx];
                    const std::size_t di =
                        base + (static_cast<std::size_t>(sy) * ow + sx) * 4;
                    copy_px(rgba, w, h, x + s.dx, y + s.dy, out, di);
                }
        }
    });
    return out;
}

std::vector<std::uint8_t> eagle_2x(const std::vector<std::uint8_t>& rgba,
                                   int w, int h) {
    // 3x3 neighbourhood:
    //   S T U
    //   V C W
    //   X Y Z
    // UL = S if (S==V && S==T) else C
    // UR = U if (T==U && U==W) else C
    // DL = X if (V==X && X==Y) else C
    // DR = Z if (W==Z && Y==Z) else C
    const int ow = w * 2;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * h * 2 * 4);
    auto at = [&](int x, int y) -> std::uint32_t {
        if (x < 0) x = 0; else if (x >= w) x = w - 1;
        if (y < 0) y = 0; else if (y >= h) y = h - 1;
        return pack(rgba, static_cast<std::size_t>(y) * w + x);
    };
    // §3.22: row-band split.  Writes are y-derived (base/di from y), reads go to the
    // read-only input via the clamping accessor, so bands never share an
    // output byte — bit-identical, and test_upscale_threading proves it.
    parallel_rows(h, [&](int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t S = at(x - 1, y - 1), T = at(x, y - 1),
                                U = at(x + 1, y - 1), V = at(x - 1, y),
                                W = at(x + 1, y), X = at(x - 1, y + 1),
                                Y = at(x, y + 1), Z = at(x + 1, y + 1);
            const bool ul = (S == V && S == T);
            const bool ur = (T == U && U == W);
            const bool dl = (V == X && X == Y);
            const bool dr = (W == Z && Y == Z);
            const std::size_t base =
                (static_cast<std::size_t>(y * 2) * ow + x * 2) * 4;
            copy_px(rgba, w, h, ul ? x - 1 : x, ul ? y - 1 : y, out, base);
            copy_px(rgba, w, h, ur ? x + 1 : x, ur ? y - 1 : y, out, base + 4);
            copy_px(rgba, w, h, dl ? x - 1 : x, dl ? y + 1 : y, out,
                    base + static_cast<std::size_t>(ow) * 4);
            copy_px(rgba, w, h, dr ? x + 1 : x, dr ? y + 1 : y, out,
                    base + static_cast<std::size_t>(ow) * 4 + 4);
        }
    });
    return out;
}

}  // namespace olduvai::enhance
