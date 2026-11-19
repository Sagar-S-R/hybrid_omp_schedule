#pragma once
/// @file fixed_ratio_executor.hpp
/// Static weighted split by a one-shot global P:E ratio (Beserra-style baseline).

#include "common/executor.hpp"
#include "hybrid_sched/hybrid_for.hpp"

#include <string>

namespace hs {
namespace bench {

class FixedRatioExecutor : public Executor {
public:
    /// @param ratio  P-slot weight / E-slot weight (default 2.0).
    ///               Pass 0 to auto-measure at first call.
    explicit FixedRatioExecutor(double ratio = 2.0);

    void parallel_for_range(const char* name, std::size_t begin, std::size_t end,
                             FunctionRef<void(std::size_t, std::size_t)> body) override;

    [[nodiscard]] std::string label() const override;

    void reset() override {}

private:
    Options opts_;
};

} // namespace bench
} // namespace hs
