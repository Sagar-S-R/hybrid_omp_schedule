/// @file bfs.cpp
/// Level-synchronous BFS on a CSR graph (Erdos-Renyi generator), frontier expansion
/// with atomic visited compare-exchange. Irregular + memory-bound.

#include "kernels/kernel.hpp"
#include "common/executor.hpp"
#include "common/rng.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class BfsKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "bfs"; }

    void setup(const KernelParams& p) override {
        const std::size_t v_defaults[] = {256, 4096, 16384, 65536};
        n_verts_ = p.n > 0 ? p.n : v_defaults[static_cast<int>(p.size_class)];
        const double prob = 8.0 / static_cast<double>(n_verts_); // ~8 edges/vertex

        rng::Xoshiro256 rng(p.seed);
        build_graph(n_verts_, prob, rng);

        levels_.resize(n_verts_, -1);
        ref_levels_.clear();
        computed_ = false;
    }

    void run(Executor& ex) override {
        std::fill(levels_.begin(), levels_.end(), -1);
        std::vector<std::atomic<int>> atomic_levels(n_verts_);
        for (auto& al : atomic_levels) al.store(-1, std::memory_order_relaxed);

        const std::size_t source = 0;
        atomic_levels[source].store(0, std::memory_order_relaxed);

        std::vector<std::size_t> frontier{source};
        int level = 0;

        while (!frontier.empty()) {
            std::vector<std::size_t> next_frontier;
            std::mutex nf_mutex;
            const int next_level = level + 1;

            ex.parallel_for_range("bfs.frontier", 0, frontier.size(),
                                   [&](std::size_t lo, std::size_t hi) {
                                       std::vector<std::size_t> local_next;
                                       for (std::size_t fi = lo; fi < hi; ++fi) {
                                           const std::size_t u = frontier[fi];
                                           for (std::size_t ei = row_ptr_[u];
                                                ei < row_ptr_[u + 1]; ++ei) {
                                               const std::size_t v = col_idx_[ei];
                                               int expected = -1;
                                               if (atomic_levels[v].compare_exchange_strong(
                                                       expected, next_level,
                                                       std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
                                                   local_next.push_back(v);
                                               }
                                           }
                                       }
                                       if (!local_next.empty()) {
                                           std::lock_guard<std::mutex> lock(nf_mutex);
                                           next_frontier.insert(next_frontier.end(),
                                                                 local_next.begin(),
                                                                 local_next.end());
                                       }
                                   });

            ++level;
            frontier = std::move(next_frontier);
        }

        for (std::size_t i = 0; i < n_verts_; ++i)
            levels_[i] = atomic_levels[i].load(std::memory_order_relaxed);
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        if (ref_levels_.empty()) compute_reference();
        return levels_ == ref_levels_;
    }

    void reset() override {
        std::fill(levels_.begin(), levels_.end(), -1);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        return static_cast<double>(col_idx_.size()); // edges visited
    }

    [[nodiscard]] std::string work_unit_name() const override { return "edges"; }

private:
    void build_graph(std::size_t n, double prob, rng::Xoshiro256& rng) {
        // Erdos-Renyi: include each directed edge independently with probability prob
        row_ptr_.assign(n + 1, 0);
        std::vector<std::vector<std::size_t>> adj(n);

        for (std::size_t u = 0; u < n; ++u) {
            for (std::size_t v = 0; v < n; ++v) {
                if (u != v && rng.next_f64() < prob) {
                    adj[u].push_back(v);
                }
            }
        }

        std::size_t total_edges = 0;
        for (std::size_t u = 0; u < n; ++u) total_edges += adj[u].size();
        col_idx_.reserve(total_edges);

        for (std::size_t u = 0; u < n; ++u) {
            row_ptr_[u] = col_idx_.size();
            for (std::size_t v : adj[u]) col_idx_.push_back(v);
        }
        row_ptr_[n] = col_idx_.size();
    }

    void compute_reference() {
        // Serial BFS
        ref_levels_.assign(n_verts_, -1);
        const std::size_t source = 0;
        ref_levels_[source] = 0;
        std::vector<std::size_t> queue{source};
        std::size_t head = 0;
        while (head < queue.size()) {
            const std::size_t u = queue[head++];
            for (std::size_t ei = row_ptr_[u]; ei < row_ptr_[u + 1]; ++ei) {
                const std::size_t v = col_idx_[ei];
                if (ref_levels_[v] == -1) {
                    ref_levels_[v] = ref_levels_[u] + 1;
                    queue.push_back(v);
                }
            }
        }
    }

    std::size_t n_verts_{0};
    std::vector<std::size_t> row_ptr_;
    std::vector<std::size_t> col_idx_;
    std::vector<int> levels_;
    std::vector<int> ref_levels_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_bfs() {
    return std::make_unique<BfsKernel>();
}

} // namespace bench
} // namespace hs
