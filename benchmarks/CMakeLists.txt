set(BENCH_SOURCES
    main.cpp
    common/cli.cpp
    common/csv_writer.cpp
    common/harness.cpp
    common/system_probe.cpp
    executors/omp_executors.cpp
    executors/hybrid_executor.cpp
    executors/fixed_ratio_executor.cpp
    kernels/stream_triad.cpp
    kernels/mandelbrot.cpp
    kernels/matmul.cpp
    kernels/mlp_train.cpp
    kernels/conv_layer.cpp
    kernels/kmeans.cpp
    kernels/bfs.cpp
    kernels/uniform_fma.cpp
    kernels/registry.cpp
    suites/sweep.cpp
    suites/compare.cpp
    suites/overhead.cpp
    suites/oracle.cpp
)

add_executable(hs_bench ${BENCH_SOURCES})

target_include_directories(hs_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/benchmarks
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(hs_bench PRIVATE
    hybrid_sched::hybrid_sched
    OpenMP::OpenMP_CXX
    Threads::Threads
)

target_compile_options(hs_bench PRIVATE ${HS_WARN_FLAGS})
