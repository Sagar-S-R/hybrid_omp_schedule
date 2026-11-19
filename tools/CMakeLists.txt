add_executable(hs_power_profile power_profile.cpp)
target_include_directories(hs_power_profile PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hs_power_profile PRIVATE hybrid_sched::hybrid_sched OpenMP::OpenMP_CXX Threads::Threads)
target_compile_options(hs_power_profile PRIVATE ${HS_WARN_FLAGS})

add_executable(hs_analyze analyze.cpp)
target_include_directories(hs_analyze PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hs_analyze PRIVATE hybrid_sched::hybrid_sched)
target_compile_options(hs_analyze PRIVATE ${HS_WARN_FLAGS})

add_executable(hs_decision_guide decision_guide.cpp)
target_include_directories(hs_decision_guide PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(hs_decision_guide PRIVATE hybrid_sched::hybrid_sched)
target_compile_options(hs_decision_guide PRIVATE ${HS_WARN_FLAGS})
