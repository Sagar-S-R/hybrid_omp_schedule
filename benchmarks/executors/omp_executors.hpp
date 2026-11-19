#pragma once
/// @file omp_executors.hpp
/// OpenMP static/dynamic/guided executors with configurable thread count and affinity.

#include "common/executor.hpp"
#include "hybrid_sched/topology.hpp"

#include <string>
#include <vector>

namespace hs {
namespace bench {

enum class OmpKind { Static, Dynamic, Guided };
enum class AffinityMode { OS, Ordered, P_Only, E_Only, Mixed };

class OmpExecutor : public Executor {
public:
    OmpExecutor(OmpKind kind, int chunk, int threads, AffinityMode affinity,
                int mix_a = -1, int mix_b = -1);

    void parallel_for_range(const char* name, std::size_t begin, std::size_t end,
                             FunctionRef<void(std::size_t, std::size_t)> body) override;

    [[nodiscard]] std::string label() const override;

private:
    OmpKind kind_;
    int chunk_;
    int threads_;
    AffinityMode affinity_;
    int mix_a_{-1}; // for Mixed: number of P threads
    int mix_b_{-1}; // for Mixed: number of E threads
    std::vector<int> cpu_list_; // pinning list
    void build_cpu_list();
};

} // namespace bench
} // namespace hs
