/// @file test_kernels.cpp
/// Each kernel: setup with SizeClass::Tiny, run under a serial executor, verify().

#include "minitest.hpp"

#include "benchmarks/kernels/kernel.hpp"
#include "benchmarks/common/executor.hpp"
#include "hybrid_sched/function_ref.hpp"

#include <memory>
#include <string>

namespace hs { namespace bench {
    // Forward declarations from registry.cpp
    std::unique_ptr<Kernel> make_kernel(const std::string& name);
}}

namespace {

/// A simple serial executor — runs the range body directly in the caller thread.
class SerialExecutor : public hs::bench::Executor {
public:
    void parallel_for_range(const char* /*name*/,
                             std::size_t begin, std::size_t end,
                             hs::FunctionRef<void(std::size_t, std::size_t)> body) override {
        body(begin, end);
    }
    [[nodiscard]] std::string label() const override { return "serial"; }
};

void run_kernel_test(const std::string& name) {
    auto k = hs::bench::make_kernel(name);
    hs::bench::KernelParams p;
    p.size_class = hs::bench::SizeClass::Tiny;
    p.seed = 42;
    k->setup(p);

    SerialExecutor ex;
    k->run(ex);

    if (!k->verify()) {
        throw minitest::FailException{"Kernel " + name + ": verify() failed"};
    }
}

} // anonymous namespace

TEST("kernel: stream_triad tiny verify") { run_kernel_test("stream_triad"); }
TEST("kernel: mandelbrot tiny verify")   { run_kernel_test("mandelbrot"); }
TEST("kernel: matmul tiny verify")       { run_kernel_test("matmul"); }
TEST("kernel: mlp_train tiny verify")    { run_kernel_test("mlp_train"); }
TEST("kernel: conv_layer tiny verify")   { run_kernel_test("conv_layer"); }
TEST("kernel: kmeans tiny verify")       { run_kernel_test("kmeans"); }
TEST("kernel: bfs tiny verify")          { run_kernel_test("bfs"); }
TEST("kernel: uniform_fma tiny verify")  { run_kernel_test("uniform_fma"); }

TEST("kernel: reset restores state") {
    auto k = hs::bench::make_kernel("stream_triad");
    hs::bench::KernelParams p;
    p.size_class = hs::bench::SizeClass::Tiny;
    p.seed = 42;
    k->setup(p);
    SerialExecutor ex;
    k->run(ex);
    CHECK(k->verify());
    k->reset();
    k->run(ex);
    CHECK(k->verify());
}

TEST("kernel: work_units positive") {
    for (const std::string& name : {"stream_triad", "mandelbrot", "matmul",
                                     "uniform_fma", "kmeans"}) {
        auto k = hs::bench::make_kernel(name);
        hs::bench::KernelParams p;
        p.size_class = hs::bench::SizeClass::Tiny;
        k->setup(p);
        CHECK_GT(k->work_units(), 0.0);
    }
}
