/// @file registry.cpp
/// Kernel factory registry: name -> factory function.

#include "kernels/kernel.hpp"

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace hs {
namespace bench {

// Forward declarations of factory functions
std::unique_ptr<Kernel> make_stream_triad();
std::unique_ptr<Kernel> make_mandelbrot();
std::unique_ptr<Kernel> make_matmul();
std::unique_ptr<Kernel> make_mlp_train();
std::unique_ptr<Kernel> make_conv_layer();
std::unique_ptr<Kernel> make_kmeans();
std::unique_ptr<Kernel> make_bfs();
std::unique_ptr<Kernel> make_uniform_fma();

using Factory = std::function<std::unique_ptr<Kernel>()>;

static const std::map<std::string, Factory>& get_registry() {
    static const std::map<std::string, Factory> reg = {
        {"stream_triad",  make_stream_triad},
        {"mandelbrot",    make_mandelbrot},
        {"matmul",        make_matmul},
        {"mlp_train",     make_mlp_train},
        {"conv_layer",    make_conv_layer},
        {"kmeans",        make_kmeans},
        {"bfs",           make_bfs},
        {"uniform_fma",   make_uniform_fma},
    };
    return reg;
}

std::unique_ptr<Kernel> make_kernel(const std::string& name) {
    const auto& reg = get_registry();
    const auto it = reg.find(name);
    if (it == reg.end())
        throw std::invalid_argument("Unknown kernel: " + name);
    return it->second();
}

std::vector<std::string> list_kernel_names() {
    std::vector<std::string> names;
    for (const auto& [k, _] : get_registry()) names.push_back(k);
    return names;
}

} // namespace bench
} // namespace hs
