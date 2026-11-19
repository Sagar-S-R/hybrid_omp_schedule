#pragma once
/// @file executor.hpp
/// @brief Abstract Executor interface used by all kernels.

#include "hybrid_sched/function_ref.hpp"

#include <cstddef>
#include <string>

namespace hs {
namespace bench {

class Executor {
public:
    virtual ~Executor() = default;

    /// Execute iterations [begin, end) of a loop identified by `name`.
    /// body(lo, hi) processes the contiguous range [lo, hi).
    virtual void parallel_for_range(const char* name, std::size_t begin, std::size_t end,
                                     hs::FunctionRef<void(std::size_t, std::size_t)> body) = 0;

    /// Human-readable label for CSV output.
    [[nodiscard]] virtual std::string label() const = 0;

    /// Reset any per-loop state (called between repetitions for hybrid executors).
    virtual void reset() {}

    /// Imbalance percentage from the last call to parallel_for_range.
    /// (Tmax - Tmean) / Tmax * 100 over participating threads.
    [[nodiscard]] double last_imbalance_pct() const noexcept { return last_imbalance_pct_; }

protected:
    double last_imbalance_pct_{0.0};
};

} // namespace bench
} // namespace hs
