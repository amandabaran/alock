load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "cluster_proto",
    srcs = ["cluster.proto"],
)

cc_proto_library(
    name = "cluster_cc_proto",
    deps = ["cluster_proto"],
)

py_proto_library(
    name = "cluster_py_proto",
    srcs = ["cluster.proto"],
)

cc_library(
    name = "common",
    hdrs = ["common.h"],
    deps = [
        "@rome//rome/logging",
    ],
)

cc_library(
    name = "lock_table",
    hdrs = ["lock_table.h"],
    deps = [
        ":cluster_cc_proto",
        ":common",
        "@rome//rome/rdma/memory_pool",
    ],
)

cc_library(
    name = "node_impl",
    hdrs = ["node_impl.h"],
    deps = [
        ":common",
        ":cluster_cc_proto",
        "@rome//rome/logging",
        "@rome//rome/rdma/memory_pool",
    ],
)

cc_library(
    name = "node",
    hdrs = ["node.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":node_impl",
        ":common",
        ":cluster_cc_proto",
        ":lock_table",
        "@absl//absl/strings",
    ],
)