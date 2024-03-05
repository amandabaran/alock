add_executable(main main.cc)

target_link_libraries(main PUBLIC rome::rdma rome::workload rome::util)

target_compile_definitions(main PUBLIC ROME_LOG_LEVEL=${LOG_LEVEL})