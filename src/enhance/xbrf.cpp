// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Built with -ffp-contract=off (CMakeLists.txt): differences are summed in a
// fixed order with no fused multiply-add, so every platform picks the same corners.
#include "enhance/xbrf.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include "enhance/parallel_rows.hpp"
#include "enhance/xbrf_patterns.hpp"

namespace olduvai::enhance {

namespace {

using namespace xbrf;  // the pattern tables and their shapes

// A pixel's four bytes, in memory order, as one word: "the same" is one compare.
using Word = std::uint32_t;
using Pixel = std::array<std::uint8_t, 4>;

constexpr int kPad = 2;
constexpr int kMinScale = 2;
constexpr int kMaxScale = 6;

Pixel to_pixel(Word w) {
    Pixel p{};
    std::memcpy(p.data(), &w, sizeof w);
    return p;
}

Word to_word(const Pixel& p) {
    Word w = 0;
    std::memcpy(&w, p.data(), sizeof w);
    return w;
}

// BT.2020 luma and chroma of the channel differences, then alpha.
class Metric {
public:
    explicit Metric(const XbrfTuning& t)
        : kr_(t.luma_red), kb_(t.luma_blue), kg_(1.0 - kb_ - kr_), cb_(0.5 / (1.0 - kb_)), cr_(0.5 / (1.0 - kr_)) {}

    double operator()(const Pixel& p, const Pixel& q) const {
        const double dr = static_cast<double>(p[0]) - static_cast<double>(q[0]);
        const double dg = static_cast<double>(p[1]) - static_cast<double>(q[1]);
        const double db = static_cast<double>(p[2]) - static_cast<double>(q[2]);
        const double y = kr_ * dr + kg_ * dg + kb_ * db;
        const double cb = cb_ * (db - y);
        const double cr = cr_ * (dr - y);
        const double d = std::sqrt(y * y + cb * cb + cr * cr);
        if (p[3] == 255 && q[3] == 255) return d;  // what the alpha step gives two opaque pixels
        const double ap = static_cast<double>(p[3]) / 255.0;
        const double aq = static_cast<double>(q[3]) / 255.0;
        const double lo = std::min(ap, aq);
        const double hi = std::max(ap, aq);
        return lo * d + 255.0 * (hi - lo);
    }

private:
    double kr_;
    double kb_;
    double kg_;
    double cb_;
    double cr_;
};

// The difference of two words, remembered: a page has few colours, so the same
// pairs come back again and again.  One per thread; the same pixels differ by exactly 0.
class Differences {
public:
    explicit Differences(const Metric& metric) : metric_(metric), slots_(kSize) {}

    double operator()(Word a, Word b) {
        if (a == b) return 0.0;
        // Order the pair: the difference is the same either way round, and key 0 (a == b) is never stored.
        const std::uint64_t key = a < b ? (std::uint64_t{a} << 32) | b : (std::uint64_t{b} << 32) | a;
        const std::size_t slot = static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> (64 - kBits));
        Slot& found = slots_[slot];
        if (found.key != key) found = {key, metric_(to_pixel(a), to_pixel(b))};
        return found.value;
    }

private:
    static constexpr unsigned kBits = 12;
    static constexpr std::size_t kSize = std::size_t{1} << kBits;
    struct Slot {
        std::uint64_t key = 0;
        double value = 0.0;
    };
    const Metric& metric_;
    std::vector<Slot> slots_;
};

std::size_t at(int x, int y, int stride) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x);
}

// The picture padded by kPad on every side with its edge pixels.  Only what
// survives the crop is worked out, and that never reads past the padding.
class Picture {
public:
    Picture(const std::vector<std::uint8_t>& rgba, int w, int h)
        : w_(w + 2 * kPad), h_(h + 2 * kPad), px_(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_)) {
        for (int y = 0; y < h_; ++y) {
            const int sy = std::clamp(y - kPad, 0, h - 1);
            for (int x = 0; x < w_; ++x) {
                const int sx = std::clamp(x - kPad, 0, w - 1);
                std::memcpy(&px_[at(x, y, w_)], &rgba[at(sx, sy, w) * 4], sizeof(Word));
            }
        }
    }

    Word word(int x, int y) const { return px_[at(x, y, w_)]; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_;
    int h_;
    std::vector<Word> px_;
};

// ---- Step 1: how each corner is cut where four pixels meet ----

// How strongly a corner is cut, if at all.
enum class Strength : std::uint8_t { none = 0, weak, strong };

// A block's corners, clockwise from the top left: one clockwise turn takes
// corner c to corner (c + 1) mod 4.
constexpr std::size_t kTopLeft = 0;
constexpr std::size_t kTopRight = 1;
constexpr std::size_t kBottomRight = 2;
constexpr std::size_t kBottomLeft = 3;
using Corners = std::array<Strength, 4>;

// Whether any corner is cut: none is 0, so the four together are 0 only when none is.
bool any_cut(const Corners& c) {
    unsigned cut = 0;
    for (const Strength k : c) cut |= static_cast<unsigned>(k);
    return cut != 0;
}

// The corners as seen after `turns` clockwise turns: corner c then holds what was at (c - turns) mod 4.
class Turned {
public:
    Turned(const Corners& c, int turns) : c_(c), back_(4 - static_cast<std::size_t>(turns)) {}
    Strength operator[](std::size_t corner) const { return c_[(corner + back_) % 4]; }

private:
    const Corners& c_;
    std::size_t back_;
};

// `cut` unless the pixel is the same as either of the other two.
Strength unless_same(Strength cut, Word p, Word q, Word r) { return p == q || p == r ? Strength::none : cut; }

// The group whose top left, E, is at (x, y); F right of it, H below, I below
// right.  Each of the four pixels' corners at the point where they meet, by the
// corner it is of that pixel: E's bottom right, F's bottom left, H's top right, I's top left.
Corners classify(const Picture& pic, Differences& dif, const XbrfTuning& tune, int x, int y) {
    const Word e = pic.word(x, y);
    const Word f = pic.word(x + 1, y);
    const Word h = pic.word(x, y + 1);
    const Word i = pic.word(x + 1, y + 1);
    Corners c{};
    if ((e == f && h == i) || (e == h && f == i)) return c;
    // Pairs as offsets from E, in this order: (-1,+1)-(0,0), (0,0)-(+1,-1), (0,+2)-(+1,+1), (+1,+1)-(+2,0),
    // (0,+1)-(+1,0); then (-1,0)-(0,+1), (0,+1)-(+1,+2), (0,-1)-(+1,0), (+1,0)-(+2,+1), (0,0)-(+1,+1).
    const auto pair = [&](int ax, int ay, int bx, int by) { return dif(pic.word(x + ax, y + ay), pic.word(x + bx, y + by)); };
    const double along_hf = pair(-1, 1, 0, 0) + pair(0, 0, 1, -1) + pair(0, 2, 1, 1) + pair(1, 1, 2, 0) + tune.centre_weight * pair(0, 1, 1, 0);
    const double along_ei = pair(-1, 0, 0, 1) + pair(0, 1, 1, 2) + pair(0, -1, 1, 0) + pair(1, 0, 2, 1) + tune.centre_weight * pair(0, 0, 1, 1);
    if (along_hf < along_ei) {
        const Strength cut = tune.strong_ratio * along_hf < along_ei ? Strength::strong : Strength::weak;
        c[kBottomRight] = unless_same(cut, e, f, h);
        c[kTopLeft] = unless_same(cut, i, h, f);
    } else if (along_ei < along_hf) {
        const Strength cut = tune.strong_ratio * along_ei < along_hf ? Strength::strong : Strength::weak;
        c[kTopRight] = unless_same(cut, h, e, i);
        c[kBottomLeft] = unless_same(cut, f, e, i);
    }
    return c;
}

// Every group a cropped pixel touches, worked out once: top left from kPad-1 to kPad+w-1 each way.
class Meetings {
public:
    Meetings(const Picture& pic, const Metric& metric, const XbrfTuning& tune, int w, int h)
        : cols_(w + 1), m_(static_cast<std::size_t>(w + 1) * static_cast<std::size_t>(h + 1)) {
        parallel_rows(h + 1, [&](int r0, int r1) {
            Differences dif(metric);
            for (int r = r0; r < r1; ++r)
                for (int c = 0; c < cols_; ++c) m_[at(c, r, cols_)] = classify(pic, dif, tune, c + kPad - 1, r + kPad - 1);
        });
    }

    // The groups whose top row is the padded picture's row y, by left column from kPad-1.
    const Corners* row(int y) const { return &m_[at(0, y - kPad + 1, cols_)]; }

private:
    int cols_;
    std::vector<Corners> m_;
};

// ---- Step 2: the block ----

// A pixel's 3x3, row by row, E in the middle.
namespace at3 {
enum : std::size_t { A, B, C, D, E, F, G, H, I };
}

// For 0 to 3 clockwise turns, the cell of the 3x3 that each turned position reads.
// One turn sends (r, c) to (c, 2 - r), so turned (r, c) reads (2 - c, r).
using Turn = std::array<std::size_t, 9>;
constexpr std::array<Turn, 4> make_turns() {
    std::array<Turn, 4> t{};
    for (std::size_t i = 0; i < 9; ++i) t[0][i] = i;
    for (std::size_t k = 1; k < 4; ++k)
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t c = 0; c < 3; ++c) t[k][r * 3 + c] = t[k - 1][(2 - c) * 3 + r];
    return t;
}
constexpr auto kTurns = make_turns();

// A pixel's 3x3 seen turned.
class Hood {
public:
    Hood(const Picture& pic, Differences& dif, const XbrfTuning& tune, int x, int y, const Turn& turn)
        : pic_(pic), dif_(dif), tune_(tune), x_(x), y_(y), turn_(turn) {}

    Word word(std::size_t p) const { return word_at(turn_[p]); }

    double dif(std::size_t p, std::size_t q) const { return dif_(word(p), word(q)); }

    bool alike(std::size_t p, std::size_t q) const { return dif(p, q) < tune_.alike_below; }
    const XbrfTuning& tuning() const { return tune_; }

private:
    static int col(std::size_t i) { return static_cast<int>(i % 3) - 1; }
    static int row(std::size_t i) { return static_cast<int>(i / 3) - 1; }
    Word word_at(std::size_t i) const { return pic_.word(x_ + col(i), y_ + row(i)); }

    const Picture& pic_;
    Differences& dif_;
    const XbrfTuning& tune_;
    int x_;
    int y_;
    const Turn& turn_;
};

bool rounds_alone(const Hood& t, const Turned& k) {
    using namespace at3;
    if (k[kTopRight] != Strength::none && !t.alike(E, G)) return true;
    if (k[kBottomLeft] != Strength::none && !t.alike(E, C)) return true;
    return !t.alike(E, I) && t.alike(G, H) && t.alike(H, I) && t.alike(I, F) && t.alike(F, C);
}

Shape line_shape(const Hood& t) {
    using namespace at3;
    const double fg = t.dif(F, G);
    const double hc = t.dif(H, C);
    const bool gentle = t.tuning().slope_ratio * fg <= hc && t.word(E) != t.word(G) && t.word(D) != t.word(G);
    const bool tall = t.tuning().slope_ratio * hc <= fg && t.word(E) != t.word(C) && t.word(B) != t.word(C);
    if (gentle && tall) return kBoth;
    if (gentle) return kGentle;
    if (tall) return kTall;
    return kDiagonal;
}

// The bottom-right corner's shape, in a frame turned so the corner is there.
Shape pick_shape(const Hood& t, const Turned& k) {
    if (k[kBottomRight] != Strength::strong && rounds_alone(t, k)) return kRound;
    return line_shape(t);
}

// A pattern cell placed in the real block, turned back.
struct Placed {
    std::size_t row;
    std::size_t col;
    unsigned m;
    unsigned n;
};
using PlacedPattern = std::vector<Placed>;
using PlacedSet = std::array<std::array<PlacedPattern, 4>, kShapeCount>;

// Each turn back sends (i, j) to (s-1-j, i).
Placed place(const Cell& cell, int turns, int s) {
    int row = cell.row;
    int col = cell.col;
    for (int k = 0; k < turns; ++k) {
        const int r = s - 1 - col;
        col = row;
        row = r;
    }
    return {static_cast<std::size_t>(row), static_cast<std::size_t>(col), cell.m, cell.n};
}

PlacedSet make_placed(int s) {
    PlacedSet out{};
    for (std::size_t shape = 0; shape < kShapeCount; ++shape)
        for (int turns = 0; turns < 4; ++turns)
            for (const Cell& cell : patterns_for(s)[shape])
                out[shape][static_cast<std::size_t>(turns)].push_back(place(cell, turns, s));
    return out;
}

const PlacedPattern& placed(int s, Shape shape, int turns) {
    static const std::array<PlacedSet, 5> kAll = {make_placed(2), make_placed(3), make_placed(4), make_placed(5), make_placed(6)};
    return kAll[static_cast<std::size_t>(s - kMinScale)][shape][static_cast<std::size_t>(turns)];
}

// f blended into b by m/n, each counted by its alpha too; rounded half up.
Word blend(Word bw, Word fw, unsigned m, unsigned n) {
    const Pixel b = to_pixel(bw);
    const Pixel f = to_pixel(fw);
    const unsigned wf = static_cast<unsigned>(f[3]) * m;
    const unsigned wb = static_cast<unsigned>(b[3]) * (n - m);
    const unsigned total = wf + wb;
    if (total == 0) return 0;
    Pixel out{};
    for (std::size_t c = 0; c < 3; ++c)
        out[c] = static_cast<std::uint8_t>((static_cast<unsigned>(f[c]) * wf + static_cast<unsigned>(b[c]) * wb + total / 2) / total);
    out[3] = static_cast<std::uint8_t>((total + n / 2) / n);
    return to_word(out);
}

void store(std::uint8_t* dst, Word w) { std::memcpy(dst, &w, sizeof w); }

// A block where it stands in the output, already filled with its pixel.
class Block {
public:
    Block(std::uint8_t* top_left, std::size_t stride, int s) : base_(top_left), stride_(stride), s_(s) {}

    void mix_in(const Placed& p, Word colour) const {
        std::uint8_t* const cell = base_ + p.row * stride_ + p.col * sizeof(Word);
        Word w = 0;
        std::memcpy(&w, cell, sizeof w);
        store(cell, p.n == 0 ? colour : blend(w, colour, p.m, p.n));
    }
    int scale() const { return s_; }

private:
    std::uint8_t* base_;
    std::size_t stride_;  // bytes per output row
    int s_;
};

// One corner, turned clockwise `turns` times to stand at the bottom right.
void soften_corner(const Block& block, const Hood& t, const Turned& k, int turns) {
    using namespace at3;
    const Word colour = t.dif(E, F) <= t.dif(E, H) ? t.word(F) : t.word(H);
    for (const Placed& p : placed(block.scale(), pick_shape(t, k), turns)) block.mix_in(p, colour);
}

// The four corners of source pixel x, one from each group it's in, from the
// groups above it (`up`) and level with it (`level`).
Corners corners_of(const Corners* up, const Corners* level, int x) {
    const auto c = static_cast<std::size_t>(x);
    Corners k{};
    k[kTopLeft] = up[c][kTopLeft];
    k[kTopRight] = up[c + 1][kTopRight];
    k[kBottomRight] = level[c + 1][kBottomRight];
    k[kBottomLeft] = level[c][kBottomLeft];
    return k;
}

class Scaler {
public:
    Scaler(const std::vector<std::uint8_t>& rgba, int w, int h, int s, const XbrfTuning& tune)
        : tune_(tune), metric_(tune), pic_(rgba, w, h), meetings_(pic_, metric_, tune, w, h), w_(w), s_(s),
          stride_(static_cast<std::size_t>(w) * static_cast<std::size_t>(s) * sizeof(Word)) {}

    // Source row y into output rows [y*s, y*s+s): every block filled with its
    // pixel, then the blocks with a corner to soften worked out over that.
    const Metric& metric() const { return metric_; }

    // Not inlined into parallel_rows' std::function, a library template, whose code stays out of xbrf_hwdiv.cpp's section.
    [[gnu::noinline]] void row(std::vector<std::uint8_t>& out, int y, Differences& dif) const {
        std::uint8_t* const top = out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(s_) * stride_;
        std::uint8_t* dst = top;
        for (int x = 0; x < w_; ++x) {
            const Word e = pic_.word(x + kPad, y + kPad);
            for (int k = 0; k < s_; ++k, dst += sizeof(Word)) store(dst, e);
        }
        for (int r = 1; r < s_; ++r) std::memcpy(top + static_cast<std::size_t>(r) * stride_, top, stride_);
        const Corners* up = meetings_.row(y + kPad - 1);
        const Corners* level = meetings_.row(y + kPad);
        for (int x = 0; x < w_; ++x) {
            const Corners corners = corners_of(up, level, x);
            if (!any_cut(corners)) continue;
            const Block block(top + static_cast<std::size_t>(x) * static_cast<std::size_t>(s_) * sizeof(Word), stride_, s_);
            soften(block, corners, x + kPad, y + kPad, dif);
        }
    }

private:
    void soften(const Block& block, const Corners& corners, int x, int y, Differences& dif) const {
        // Bottom right, top right, top left, bottom left: 0 to 3 clockwise turns.
        for (int turns = 0; turns < 4; ++turns) {
            const Turned k(corners, turns);
            if (k[kBottomRight] == Strength::none) continue;
            soften_corner(block, Hood(pic_, dif, tune_, x, y, kTurns[static_cast<std::size_t>(turns)]), k, turns);
        }
    }

    const XbrfTuning& tune_;
    Metric metric_;
    Picture pic_;
    Meetings meetings_;
    int w_;
    int s_;
    std::size_t stride_;  // bytes per output row
};

std::vector<std::uint8_t> work_out(const std::vector<std::uint8_t>& rgba, int w, int h, int scale, const XbrfTuning& tuning) {
    const Scaler scaler(rgba, w, h, scale, tuning);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(scale) *
                                  static_cast<std::size_t>(scale) * 4);
    // Source row y writes output rows [y*scale, y*scale+scale) only.
    parallel_rows(h, [&](int y0, int y1) {
        Differences dif(scaler.metric());
        for (int y = y0; y < y1; ++y) scaler.row(out, y, dif);
    });
    return out;
}

}  // namespace

std::vector<std::uint8_t> xbrf_scale(const std::vector<std::uint8_t>& rgba, int w, int h, int scale) {
    return xbrf_scale(rgba, w, h, scale, XbrfTuning{});
}

std::vector<std::uint8_t> xbrf_scale(const std::vector<std::uint8_t>& rgba, int w, int h, int scale, const XbrfTuning& tuning) {
    if (scale == 1) return rgba;
    if (scale < kMinScale || scale > kMaxScale) throw std::invalid_argument("xbrf_scale: scale must be 1 to 6");
    if (w < 0 || h < 0 || rgba.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4)
        throw std::invalid_argument("xbrf_scale: buffer isn't w*h*4 bytes");
    if (w == 0 || h == 0) return {};

    return work_out(rgba, w, h, scale, tuning);
}

}  // namespace olduvai::enhance
