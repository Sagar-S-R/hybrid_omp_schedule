/// @file hybrid_executor.cpp

#include "hybrid_executor.hpp"

#include <sstream>
#include <string>

namespace hs {
namespace bench {

HybridExecutor::HybridExecutor(Policy policy, Recalibrate recalib, bool use_smt) {
    opts_.policy = policy;
    opts_.recalibrate = recalib;
    opts_.use_smt = use_smt;
    opts_.min_parallel_seconds = 0.0;

    std::ostringstream ss;
    ss << "hybrid_";
    switch (policy) {
    case Policy::EDP:           ss << "edp";           break;
    case Policy::ENERGY:        ss << "energy";        break;
    case Policy::TIME_BALANCED: ss << "time_balanced"; break;
    case Policy::P_ONLY:        ss << "p_only";        break;
    case Policy::E_ONLY:        ss << "e_only";        break;
    case Policy::FIXED_RATIO:   ss << "fixed_ratio";   break;
    }
    switch (recalib) {
    case Recalibrate::EVERY_CALL: ss << ",calib=every";   break;
    case Recalibrate::EVERY_N:    ss << ",calib=every_n"; break;
    case Recalibrate::ON_DRIFT:   ss << ",calib=drift";   break;
    case Recalibrate::ONCE:       ss << ",calib=once";    break;
    }
    if (!use_smt) ss << ",no_smt";
    label_ = ss.str();
}

void HybridExecutor::parallel_for_range(const char* name, std::size_t begin, std::size_t end,
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

std::string HybridExecutor::label() const { return label_; }

} // namespace bench
} // namespace hs
