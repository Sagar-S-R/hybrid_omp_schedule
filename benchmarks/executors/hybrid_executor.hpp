#pragma once
/// @file hybrid_executor.hpp
/// Executor wrapping hs::hybrid_for_range for benchmark use.

#include "common/executor.hpp"
#include "hybrid_sched/hybrid_for.hpp"

#include <string>

namespace hs {
namespace bench {

class HybridExecutor : public Executor {
public:
    explicit HybridExecutor(Policy policy = Policy::EDP,
                             Recalibrate recalib = Recalibrate::EVERY_CALL,
                             bool use_smt = true);

    void parallel_for_range(const char* name, std::size_t begin, std::size_t end,
                             FunctionRef<void(std::size_t, std::size_t)> body) override;

    [[nodiscard]] std::string label() const override;

    void reset() override { reset_loop_state(); }

private:
    Options opts_;
    std::string label_;
};

} // namespace bench
} // namespace hs
