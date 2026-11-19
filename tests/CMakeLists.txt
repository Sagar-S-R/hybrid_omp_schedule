add_executable(hs_tests
    test_main.cpp
    test_util.cpp
    test_stats.cpp
    test_topology.cpp
    test_energy.cpp
    test_power_model.cpp
    test_planner.cpp
    test_partition.cpp
    test_hybrid_for.cpp
    test_kernels.cpp
    # Include kernel source files directly so test_kernels can use them
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/stream_triad.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/mandelbrot.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/matmul.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/mlp_train.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/conv_layer.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/kmeans.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/bfs.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/uniform_fma.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/kernels/registry.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/common/cli.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/common/csv_writer.cpp
    ${CMAKE_SOURCE_DIR}/benchmarks/common/system_probe.cpp
)

target_include_directories(hs_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/benchmarks
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(hs_tests PRIVATE
    hybrid_sched::hybrid_sched
    OpenMP::OpenMP_CXX
    Threads::Threads
)

target_compile_options(hs_tests PRIVATE ${HS_WARN_FLAGS})

add_test(
    NAME hs_tests
    COMMAND hs_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_tests_properties(hs_tests PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HS_FORCE_HOMOGENEOUS=1;OMP_PROC_BIND=false;OMP_DYNAMIC=false;OMP_NUM_THREADS=2"
)
