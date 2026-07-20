// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/mmpx.hpp"

#include <algorithm>

namespace olduvai::enhance {

namespace {

// Packed ABGR (A high byte) so the reference luma formula reads bytes
// position-compatibly.
inline std::uint32_t luma(std::uint32_t c) {
    const std::uint32_t alpha = (c >> 24) & 0xFF;
    return (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF) + 1) *
           (256 - alpha);
}

inline bool any_eq3(std::uint32_t x, std::uint32_t a, std::uint32_t b,
                    std::uint32_t c) {
    return x == a || x == b || x == c;
}
inline bool all_eq2(std::uint32_t x, std::uint32_t a, std::uint32_t b) {
    return x == a && x == b;
}
inline bool all_eq3(std::uint32_t x, std::uint32_t a, std::uint32_t b,
                    std::uint32_t c) {
    return x == a && x == b && x == c;
}
inline bool all_eq4(std::uint32_t x, std::uint32_t a, std::uint32_t b,
                    std::uint32_t c, std::uint32_t d) {
    return x == a && x == b && x == c && x == d;
}
inline bool none_eq2(std::uint32_t x, std::uint32_t a, std::uint32_t b) {
    return x != a && x != b;
}
inline bool none_eq4(std::uint32_t x, std::uint32_t a, std::uint32_t b,
                     std::uint32_t c, std::uint32_t d) {
    return x != a && x != b && x != c && x != d;
}

}  // namespace

std::vector<std::uint8_t> mmpx_2x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h) {
    // Pack to ABGR words.
    std::vector<std::uint32_t> src(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<std::uint32_t>(rgba[i * 4]) |
                 (static_cast<std::uint32_t>(rgba[i * 4 + 1]) << 8) |
                 (static_cast<std::uint32_t>(rgba[i * 4 + 2]) << 16) |
                 (static_cast<std::uint32_t>(rgba[i * 4 + 3]) << 24);
    }
    auto at = [&](int x, int y) -> std::uint32_t {
        x = std::clamp(x, 0, w - 1);   // replicate edges
        y = std::clamp(y, 0, h - 1);
        return src[static_cast<std::size_t>(y) * w + x];
    };

    std::vector<std::uint32_t> dst(static_cast<std::size_t>(w) * h * 4);
    const int W2 = w * 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t A = at(x - 1, y - 1), B = at(x, y - 1),
                                C = at(x + 1, y - 1), D = at(x - 1, y),
                                E = at(x, y), F = at(x + 1, y),
                                G = at(x - 1, y + 1), H = at(x, y + 1),
                                I = at(x + 1, y + 1);
            std::uint32_t J = E, K = E, L = E, M = E;
            if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) |
                 (G ^ E) | (H ^ E) | (I ^ E)) != 0) {
                const std::uint32_t P = at(x, y - 2), S = at(x, y + 2);
                const std::uint32_t Q = at(x - 2, y), R = at(x + 2, y);
                const std::uint32_t Bl = luma(B), Dl = luma(D),
                                    El = luma(E), Fl = luma(F),
                                    Hl = luma(H);

                // 1:1 slope rules.
                if (D == B && D != H && D != F && (El >= Dl || E == A) &&
                    any_eq3(E, A, C, G) &&
                    (El < Dl || A != D || E != P || E != Q)) {
                    J = D;
                }
                if (B == F && B != D && B != H && (El >= Bl || E == C) &&
                    any_eq3(E, A, C, I) &&
                    (El < Bl || C != B || E != P || E != R)) {
                    K = B;
                }
                if (H == D && H != F && H != B && (El >= Hl || E == G) &&
                    any_eq3(E, A, G, I) &&
                    (El < Hl || G != H || E != S || E != Q)) {
                    L = H;
                }
                if (F == H && F != B && F != D && (El >= Fl || E == I) &&
                    any_eq3(E, C, G, I) &&
                    (El < Fl || I != H || E != R || E != S)) {
                    M = F;
                }

                // Intersection rules.
                if (E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H) &&
                    F != at(x + 3, y)) {
                    K = F;
                    M = F;
                }
                if (E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H) &&
                    D != at(x - 3, y)) {
                    J = D;
                    L = D;
                }
                if (E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F) &&
                    H != at(x, y + 3)) {
                    L = H;
                    M = H;
                }
                if (E != B && all_eq4(E, A, C, H, S) && all_eq2(B, D, F) &&
                    B != at(x, y - 3)) {
                    J = B;
                    K = B;
                }
                if (Bl < El && all_eq4(E, G, H, I, S) &&
                    none_eq4(E, A, D, C, F)) {
                    J = B;
                    K = B;
                }
                if (Hl < El && all_eq4(E, A, B, C, P) &&
                    none_eq4(E, D, G, I, F)) {
                    L = H;
                    M = H;
                }
                if (Fl < El && all_eq4(E, A, D, G, Q) &&
                    none_eq4(E, B, C, I, H)) {
                    K = F;
                    M = F;
                }
                if (Dl < El && all_eq4(E, C, F, I, R) &&
                    none_eq4(E, B, A, G, H)) {
                    J = D;
                    L = D;
                }

                // 2:1 slope rules (sequential — later rules read the
                // updated corners).
                if (H != B) {
                    if (H != A && H != E && H != C) {
                        if (all_eq3(H, G, F, R) &&
                            none_eq2(H, D, at(x + 2, y - 1))) {
                            L = M;
                        }
                        if (all_eq3(H, I, D, Q) &&
                            none_eq2(H, F, at(x - 2, y - 1))) {
                            M = L;
                        }
                    }
                    if (B != I && B != G && B != E) {
                        if (all_eq3(B, A, F, R) &&
                            none_eq2(B, D, at(x + 2, y + 1))) {
                            J = K;
                        }
                        if (all_eq3(B, C, D, Q) &&
                            none_eq2(B, F, at(x - 2, y + 1))) {
                            K = J;
                        }
                    }
                }
                if (F != D) {
                    if (D != I && D != E && D != C) {
                        if (all_eq3(D, A, H, S) &&
                            none_eq2(D, B, at(x + 1, y + 2))) {
                            J = L;
                        }
                        if (all_eq3(D, G, B, P) &&
                            none_eq2(D, H, at(x + 1, y - 2))) {
                            L = J;
                        }
                    }
                    if (F != E && F != A && F != G) {
                        if (all_eq3(F, C, H, S) &&
                            none_eq2(F, B, at(x - 1, y + 2))) {
                            K = M;
                        }
                        if (all_eq3(F, I, B, P) &&
                            none_eq2(F, H, at(x - 1, y - 2))) {
                            M = K;
                        }
                    }
                }
            }
            const std::size_t o =
                static_cast<std::size_t>(y * 2) * W2 + x * 2;
            dst[o] = J;
            dst[o + 1] = K;
            dst[o + W2] = L;
            dst[o + W2 + 1] = M;
        }
    }
    // Unpack.
    std::vector<std::uint8_t> out(dst.size() * 4);
    for (std::size_t i = 0; i < dst.size(); ++i) {
        out[i * 4] = dst[i] & 0xFF;
        out[i * 4 + 1] = (dst[i] >> 8) & 0xFF;
        out[i * 4 + 2] = (dst[i] >> 16) & 0xFF;
        out[i * 4 + 3] = (dst[i] >> 24) & 0xFF;
    }
    return out;
}

}  // namespace olduvai::enhance
