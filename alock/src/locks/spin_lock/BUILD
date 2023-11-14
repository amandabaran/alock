cc_library(
    name = "spin_lock",
    hdrs = ["spin_lock.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//alock:util",
        "@rome//rome/logging",
        "@rome//rome/util:thread_util",
        "@rome//rome/rdma:rdma_memory",
        
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
    ],
)

cc_test(
    name = "spin_lock_test",
    srcs = ["spin_lock_test.cc"],
    deps = [
        ":spin_lock",
        "@gtest//:gtest_main",
    ],
)
