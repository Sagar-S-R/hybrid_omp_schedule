/// @file loop_state.cpp

#include "hybrid_sched/loop_state.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace hs {

namespace {
std::mutex g_registry_mutex;
} // anonymous namespace

LoopState& LoopStateRegistry::get(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    for (auto& s : states_) {
        if (s.name == name) return s;
    }
    states_.push_back(LoopState{});
    states_.back().name = name;
    return states_.back();
}

void LoopStateRegistry::reset() {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    states_.clear();
}

} // namespace hs
