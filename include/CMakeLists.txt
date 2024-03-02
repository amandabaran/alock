add_subdirectory(locks)
add_subdirectory(cluster)

target_link_libraries(locks PUBLIC romoe::rdma rome::workload rome::util protos)