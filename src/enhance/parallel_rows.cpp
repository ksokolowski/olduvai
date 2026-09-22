// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/parallel_rows.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace olduvai::enhance {
namespace {

// Rows below this are not worth a wakeup: the barrier costs a few microseconds
// and a 200-row source at x4 is the smallest thing we ever scale.  Measured
// rather than guessed would be better; this is deliberately conservative so a
// small buffer can never be made SLOWER by threading it.
constexpr int kMinRowsToSplit = 32;

int env_threads() {
    const char* e = std::getenv("OLDUVAI_UPSCALE_THREADS");
    if (e == nullptr || *e == '\0') return 0;
    const long v = std::strtol(e, nullptr, 10);
    return v > 0 ? static_cast<int>(v) : 0;
}

int decide_threads() {
    if (const int forced = env_threads(); forced > 0) return forced;
    const unsigned hc = std::thread::hardware_concurrency();
    if (hc <= 1) return 1;
    // Capped at 8: this is memory-bandwidth bound well before it is core
    // bound, and an unbounded count on a many-core host spends more on the
    // barrier than it saves.  Raise it only with a measurement.
    return static_cast<int>(std::min(hc, 8u));
}

// Persistent workers waiting on a generation counter.  Deliberately plain: a
// pool is where threading bugs live, so this one has a single task shape, no
// queue, no stealing and no lifetime subtleties beyond join-on-destroy.
class RowPool {
public:
    RowPool() : n_(decide_threads()) {
        for (int i = 1; i < n_; ++i)
            workers_.emplace_back([this, i] { worker(i); });
    }

    ~RowPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        start_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    int size() const { return n_; }

    void run(int h, const std::function<void(int, int)>& body) {
        if (n_ <= 1 || h < kMinRowsToSplit) { body(0, h); return; }
        {
            std::lock_guard<std::mutex> lk(m_);
            body_ = &body;
            h_ = h;
            pending_ = n_ - 1;
            ++gen_;
        }
        start_.notify_all();
        const auto [y0, y1] = band(0, h);
        body(y0, y1);                      // the caller is participant 0
        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this] { return pending_ == 0; });
        body_ = nullptr;
    }

private:
    std::pair<int, int> band(int i, int h) const {
        const int base = h / n_, extra = h % n_;
        const int y0 = i * base + std::min(i, extra);
        const int y1 = y0 + base + (i < extra ? 1 : 0);
        return {y0, y1};
    }

    void worker(int idx) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(int, int)>* body = nullptr;
            int h = 0;
            {
                std::unique_lock<std::mutex> lk(m_);
                start_.wait(lk, [this, seen] { return stop_ || gen_ != seen; });
                if (stop_) return;
                seen = gen_;
                body = body_;
                h = h_;
            }
            if (body != nullptr) {
                const auto [y0, y1] = band(idx, h);
                if (y1 > y0) (*body)(y0, y1);
            }
            {
                std::lock_guard<std::mutex> lk(m_);
                if (--pending_ == 0) done_.notify_one();
            }
        }
    }

    const int n_;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable start_, done_;
    const std::function<void(int, int)>* body_ = nullptr;
    int h_ = 0;
    int pending_ = 0;
    std::uint64_t gen_ = 0;
    bool stop_ = false;
};

RowPool& pool() {
    // Function-local static: constructed on first use (so a process that never
    // upscales spawns nothing) and joined at exit.
    static RowPool p;
    return p;
}

}  // namespace

namespace {
std::atomic<bool> g_enabled{true};
}  // namespace

void set_parallel_rows_enabled(bool on) {
    g_enabled.store(on, std::memory_order_relaxed);
}

bool parallel_rows_enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

int parallel_row_threads() { return pool().size(); }

void parallel_rows(int h, const std::function<void(int, int)>& body) {
    if (h <= 0) return;
    if (!g_enabled.load(std::memory_order_relaxed)) { body(0, h); return; }
    pool().run(h, body);
}

}  // namespace olduvai::enhance
