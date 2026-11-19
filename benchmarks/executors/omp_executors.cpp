/// @file omp_executors.cpp

#include "omp_executors.hpp"
#include "hybrid_sched/timer.hpp"
#include "hybrid_sched/topology.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <omp.h>
#include <sstream>
#include <string>
#include <vector>

namespace hs {
namespace bench {

OmpExecutor::OmpExecutor(OmpKind kind, int chunk, int threads,
                          AffinityMode affinity, int mix_a, int mix_b)
    : kind_(kind), chunk_(chunk), threads_(threads),
      affinity_(affinity), mix_a_(mix_a), mix_b_(mix_b) {
    build_cpu_list();
}

void OmpExecutor::build_cpu_list() {
    const Topology topo = detect_topology();
    cpu_list_.clear();

    switch (affinity_) {
    case AffinityMode::OS:
        break;
    case AffinityMode::Ordered: {
        for (int c : topo.p_cpus) cpu_list_.push_back(c);
        for (int c : topo.e_cpus) cpu_list_.push_back(c);
        if (threads_ > 0 && threads_ < static_cast<int>(cpu_list_.size()))
            cpu_list_.resize(static_cast<std::size_t>(threads_));
        break;
    }
    case AffinityMode::P_Only:
        for (int c : topo.p_cpus) cpu_list_.push_back(c);
        if (threads_ > 0 && threads_ < static_cast<int>(cpu_list_.size()))
            cpu_list_.resize(static_cast<std::size_t>(threads_));
        break;
    case AffinityMode::E_Only:
        for (int c : topo.e_cpus) cpu_list_.push_back(c);
        if (threads_ > 0 && threads_ < static_cast<int>(cpu_list_.size()))
            cpu_list_.resize(static_cast<std::size_t>(threads_));
        break;
    case AffinityMode::Mixed: {
        const int a = (mix_a_ >= 0) ? mix_a_ : static_cast<int>(topo.p_cpus.size());
        const int b = (mix_b_ >= 0) ? mix_b_ : static_cast<int>(topo.e_cpus.size());
        for (int i = 0; i < a && i < static_cast<int>(topo.p_cpus.size()); ++i)
            cpu_list_.push_back(topo.p_cpus[static_cast<std::size_t>(i)]);
        for (int j = 0; j < b && j < static_cast<int>(topo.e_cpus.size()); ++j)
            cpu_list_.push_back(topo.e_cpus[static_cast<std::size_t>(j)]);
        break;
    }
    }
}

void OmpExecutor::parallel_for_range(const char* /*name*/,
                                      std::size_t begin, std::size_t end,
                                      FunctionRef<void(std::size_t, std::size_t)> body) {
    if (begin >= end) return;

    const int effective_threads = cpu_list_.empty()
        ? (threads_ > 0 ? threads_ : omp_get_max_threads())
        : static_cast<int>(cpu_list_.size());

    const bool do_pin = !cpu_list_.empty() &&
                        !util::getenv_bool("HS_DISABLE_PINNING");
    const auto& cpulist = cpu_list_;

    omp_set_dynamic(0);

    std::atomic<std::size_t> counter{begin};

    const std::size_t N = end - begin;
    std::size_t chunk_sz = 1;
    if (kind_ == OmpKind::Static) {
        chunk_sz = 0;
    } else if (chunk_ > 0) {
        chunk_sz = static_cast<std::size_t>(chunk_);
    } else {
        chunk_sz = std::max<std::size_t>(
            1, N / static_cast<std::size_t>(std::max(1, effective_threads) * 16));
    }

    // Per-thread finish times for imbalance measurement
    std::vector<double> finish_times(static_cast<std::size_t>(effective_threads), 0.0);
    const double t_loop_start = now_s();

#pragma omp parallel num_threads(effective_threads)
    {
        const int tid = omp_get_thread_num();
        if (do_pin && tid < static_cast<int>(cpulist.size()))
            pin_current_thread(cpulist[static_cast<std::size_t>(tid)]);

        if (kind_ == OmpKind::Static) {
            const std::size_t T  = static_cast<std::size_t>(effective_threads);
            const std::size_t t  = static_cast<std::size_t>(tid);
            const std::size_t base_chunk = N / T;
            const std::size_t rem        = N % T;
            const std::size_t lo = begin + t * base_chunk + std::min(t, rem);
            const std::size_t hi = lo + base_chunk + (t < rem ? 1 : 0);
            if (lo < hi) body(lo, hi);
        } else {
            while (true) {
                const std::size_t lo =
                    counter.fetch_add(chunk_sz, std::memory_order_relaxed);
                if (lo >= end) break;
                const std::size_t hi = std::min(lo + chunk_sz, end);
                body(lo, hi);
            }
        }
        finish_times[static_cast<std::size_t>(tid)] = now_s() - t_loop_start;
    }

    // Compute imbalance: (Tmax - Tmean) / Tmax * 100
    double t_max = 0.0, t_sum = 0.0;
    for (int t = 0; t < effective_threads; ++t) {
        t_max = std::max(t_max, finish_times[static_cast<std::size_t>(t)]);
        t_sum += finish_times[static_cast<std::size_t>(t)];
    }
    const double t_mean = t_sum / static_cast<double>(effective_threads);
    last_imbalance_pct_ = (t_max > 1e-12) ? (t_max - t_mean) / t_max * 100.0 : 0.0;
}

std::string OmpExecutor::label() const {
    std::ostringstream ss;
    ss << "omp_";
    switch (kind_) {
    case OmpKind::Static:  ss << "static";  break;
    case OmpKind::Dynamic: ss << "dynamic"; break;
    case OmpKind::Guided:  ss << "guided";  break;
    }
    ss << ",c=" << chunk_ << ",t=" << threads_;
    ss << ",aff=";
    switch (affinity_) {
    case AffinityMode::OS:      ss << "OS";      break;
    case AffinityMode::Ordered: ss << "ORDERED"; break;
    case AffinityMode::P_Only:  ss << "P_ONLY";  break;
    case AffinityMode::E_Only:  ss << "E_ONLY";  break;
    case AffinityMode::Mixed:
        ss << "MIXED(" << mix_a_ << "," << mix_b_ << ")";
        break;
    }
    return ss.str();
}

} // namespace bench
} // namespace hs
