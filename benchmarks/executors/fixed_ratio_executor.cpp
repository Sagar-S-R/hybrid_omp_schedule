/// @file fixed_ratio_executor.cpp

#include "fixed_ratio_executor.hpp"

#include <sstream>

namespace hs {
namespace bench {

FixedRatioExecutor::FixedRatioExecutor(double ratio) {
    opts_.policy              = Policy::FIXED_RATIO;
    opts_.recalibrate         = Recalibrate::ONCE;
    opts_.fixed_ratio_p_over_e = ratio > 0.0 ? ratio : 2.0;
    opts_.min_parallel_seconds = 0.0;
}

void FixedRatioExecutor::parallel_for_range(const char* name,
                                              std::size_t begin, std::size_t end,
                                              FunctionRef<void(std::size_t, std::size_t)> body) {
    LoopStats stats;
    Options opts = opts_;
    opts.stats_out = &stats;

    hybrid_for_range(name, begin, end,
                     [&body](std::size_t lo, std::size_t hi, int /*slot*/) {
                         body(lo, hi);
                     },
                     opts);

    last_imbalance_pct_ = stats.imbalance_percent;
}

std::string FixedRatioExecutor::label() const {
    std::ostringstream ss;
    ss << "fixed_ratio,r=" << opts_.fixed_ratio_p_over_e;
    return ss.str();
}

} // namespace bench
} // namespace hs
