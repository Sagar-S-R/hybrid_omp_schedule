/// @file kmeans.cpp
/// K-means clustering: iterative assign + update, tests recalibration over time.

#include "kernels/kernel.hpp"
#include "common/executor.hpp"
#include "common/rng.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class KmeansKernel : public Kernel {
public:
    [[nodiscard]] std::string name() const override { return "kmeans"; }

    void setup(const KernelParams& p) override {
        const std::size_t n_defaults[] = {256, 4096, 32768, 131072};
        const std::size_t k_defaults[] = {4,   16,   32,    64};

        n_pts_   = p.n > 0 ? p.n : n_defaults[static_cast<int>(p.size_class)];
        n_clust_ = p.m > 0 ? p.m : k_defaults[static_cast<int>(p.size_class)];
        dims_    = p.k > 0 ? p.k : 32;
        iters_   = p.iters > 0 ? p.iters : 20;

        rng::Xoshiro256 rng(p.seed);
        data_.resize(n_pts_ * dims_);
        for (auto& v : data_) v = static_cast<float>(rng.next_f64());

        centroids_.resize(n_clust_ * dims_);
        // Initialise centroids to first k points
        for (std::size_t c = 0; c < n_clust_; ++c)
            for (std::size_t d = 0; d < dims_; ++d)
                centroids_[c * dims_ + d] = data_[c * dims_ + d];

        assignments_.resize(n_pts_, 0);
        ref_assignments_.clear();
        computed_ = false;

        // Save for reset
        centroids_init_ = centroids_;
    }

    void run(Executor& ex) override {
        for (std::size_t it = 0; it < iters_; ++it) {
            assign_step(ex);
            update_step(ex);
        }
        computed_ = true;
    }

    [[nodiscard]] bool verify() override {
        if (!computed_) return false;
        if (ref_assignments_.empty()) compute_reference();
        for (std::size_t i = 0; i < n_pts_; ++i)
            if (assignments_[i] != ref_assignments_[i]) return false;
        return true;
    }

    void reset() override {
        centroids_ = centroids_init_;
        std::fill(assignments_.begin(), assignments_.end(), 0);
        computed_ = false;
    }

    [[nodiscard]] double work_units() const override {
        return static_cast<double>(iters_) * static_cast<double>(n_pts_) *
               static_cast<double>(n_clust_) * static_cast<double>(dims_) * 2.0;
    }

    [[nodiscard]] std::string work_unit_name() const override { return "FLOP"; }

private:
    void assign_step(Executor& ex) {
        ex.parallel_for_range("kmeans.assign", 0, n_pts_,
                               [this](std::size_t lo, std::size_t hi) {
                                   for (std::size_t i = lo; i < hi; ++i) {
                                       float best_dist = std::numeric_limits<float>::max();
                                       int best_c = 0;
                                       for (std::size_t c = 0; c < n_clust_; ++c) {
                                           float dist = 0.0f;
                                           for (std::size_t d = 0; d < dims_; ++d) {
                                               const float diff =
                                                   data_[i * dims_ + d] -
                                                   centroids_[c * dims_ + d];
                                               dist += diff * diff;
                                           }
                                           if (dist < best_dist) {
                                               best_dist = dist;
                                               best_c = static_cast<int>(c);
                                           }
                                       }
                                       assignments_[i] = best_c;
                                   }
                               });
    }

    void update_step(Executor& ex) {
        // Parallel update: each thread accumulates its own cluster sums
        // then reduce (serial merge is fine for small n_clust_)
        const int K = static_cast<int>(n_clust_);
        const std::size_t D = dims_;
        const std::size_t N = n_pts_;

        // Use per-cluster parallelism to avoid races
        ex.parallel_for_range("kmeans.update", 0, static_cast<std::size_t>(K),
                               [this, N, D](std::size_t lo, std::size_t hi) {
                                   for (std::size_t c = lo; c < hi; ++c) {
                                       std::vector<float> sum(D, 0.0f);
                                       std::size_t count = 0;
                                       for (std::size_t i = 0; i < N; ++i) {
                                           if (static_cast<std::size_t>(assignments_[i]) == c) {
                                               for (std::size_t d = 0; d < D; ++d)
                                                   sum[d] += data_[i * D + d];
                                               ++count;
                                           }
                                       }
                                       if (count > 0)
                                           for (std::size_t d = 0; d < D; ++d)
                                               centroids_[c * D + d] =
                                                   sum[d] / static_cast<float>(count);
                                   }
                               });
    }

    void compute_reference() {
        // Serial run from same initial state
        std::vector<int> asgn(n_pts_, 0);
        std::vector<float> c = centroids_init_;

        for (std::size_t it = 0; it < iters_; ++it) {
            // Assign
            for (std::size_t i = 0; i < n_pts_; ++i) {
                float best = std::numeric_limits<float>::max();
                int bc = 0;
                for (std::size_t cc = 0; cc < n_clust_; ++cc) {
                    float dist = 0.0f;
                    for (std::size_t d = 0; d < dims_; ++d) {
                        const float diff = data_[i * dims_ + d] - c[cc * dims_ + d];
                        dist += diff * diff;
                    }
                    if (dist < best) { best = dist; bc = static_cast<int>(cc); }
                }
                asgn[i] = bc;
            }
            // Update
            for (std::size_t cc = 0; cc < n_clust_; ++cc) {
                std::vector<float> sum(dims_, 0.0f);
                std::size_t cnt = 0;
                for (std::size_t i = 0; i < n_pts_; ++i)
                    if (static_cast<std::size_t>(asgn[i]) == cc) {
                        for (std::size_t d = 0; d < dims_; ++d)
                            sum[d] += data_[i * dims_ + d];
                        ++cnt;
                    }
                if (cnt > 0)
                    for (std::size_t d = 0; d < dims_; ++d)
                        c[cc * dims_ + d] = sum[d] / static_cast<float>(cnt);
            }
        }
        ref_assignments_ = asgn;
    }

    std::size_t n_pts_{0}, n_clust_{0}, dims_{0}, iters_{0};
    std::vector<float> data_;
    std::vector<float> centroids_, centroids_init_;
    std::vector<int> assignments_;
    std::vector<int> ref_assignments_;
    bool computed_{false};
};

std::unique_ptr<Kernel> make_kmeans() {
    return std::make_unique<KmeansKernel>();
}

} // namespace bench
} // namespace hs
