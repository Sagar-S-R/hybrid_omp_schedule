#pragma once
/// @file timer.hpp
/// @brief Lightweight steady_clock helpers for the hybrid_sched library.

#include <chrono>
#include <cstdint>

namespace hs {

/// Returns elapsed nanoseconds since steady_clock epoch.
[[nodiscard]] inline std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Returns elapsed seconds since steady_clock epoch.
[[nodiscard]] inline double now_s() noexcept {
    return static_cast<double>(now_ns()) * 1e-9;
}

/// Simple RAII scoped timer — stores elapsed seconds in a user-supplied double.
struct ScopedTimer {
    double& out;
    std::int64_t t0;

    explicit ScopedTimer(double& out_seconds) noexcept : out(out_seconds), t0(now_ns()) {}

    ~ScopedTimer() noexcept { out = static_cast<double>(now_ns() - t0) * 1e-9; }

    // Non-copyable, non-movable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/// Non-RAII version: records start, elapsed() returns seconds since start.
struct Stopwatch {
    std::int64_t t0{now_ns()};

    void reset() noexcept { t0 = now_ns(); }

    [[nodiscard]] double elapsed_s() const noexcept {
        return static_cast<double>(now_ns() - t0) * 1e-9;
    }

    [[nodiscard]] std::int64_t elapsed_ns() const noexcept { return now_ns() - t0; }
};

} // namespace hs
