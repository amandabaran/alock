load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

proto_library(
    name = "experiment_proto",
    srcs = ["experiment.proto"],
    deps = [
        "//alock/src/cluster:cluster_proto",
        "@rome//protos:colosseum_proto",
        "@rome//protos:metrics_proto",
    ],
)

cc_proto_library(
    name = "experiment_cc_proto",
    deps = [":experiment_proto"],
)

py_proto_library(
    name = "experiment_py_proto",
    srcs = [
        "experiment.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//alock/src/cluster:cluster_py_proto",
        "@rome//protos:colosseum_py_proto",
        "@rome//protos:metrics_py_proto",
    ],
)

cc_library(
    name = "setup",
    hdrs = ["setup.h"],
    deps = [
        "//alock/src/cluster:cluster_cc_proto",
        "@absl//absl/status",
        "@rome//rome/logging",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/util:status_util",
    ],
)

cc_library(
    name = "client",
    hdrs = ["client.h"],
    deps = [
        ":experiment_cc_proto",
        ":setup",
        "//alock/src/cluster:node",
        "//alock/src/cluster:lock_table",
        "//alock/src/locks/rdma_mcs_lock:rdma_mcs_lock",
        "//alock/src/locks/rdma_spin_lock:rdma_spin_lock",
        "//alock/src/locks/a_lock:a_lock",
        "//alock/src/locks/spin_lock:spin_lock",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/rdma/connection_manager",
    ],
)

string_flag(
    name = "lock_type",
    build_setting_default = "mcs",
    values = [
        "mcs",
        "spin",
        "alock",
        "lspin",
    ],
)

config_setting(
    name = "alock",
    flag_values = {
        ":lock_type": "alock",
    },
)

config_setting(
    name = "mcs",
    flag_values = {
        ":lock_type": "mcs",
    },
)

config_setting(
    name = "spin",
    flag_values = {
        ":lock_type": "spin",
    },
)

config_setting(
    name = "lspin",
    flag_values = {
        ":lock_type": "lspin",
    },
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    copts = ["-Wno-deprecated-enum-enum-conversion"],
    data = ["//alock/benchmark:nodefiles"],
    defines = select(
        {
            # ":mcs": ["LOCK_TYPE=::X::RdmaMcsLock", "LOCK_HANDLE=::X::RdmaMcsLockHandle"],
            # ":spin": ["LOCK_TYPE=::X::RdmaSpinLock", "LOCK_HANDLE=::X::RdmaSpinLockHandle"],
            ":alock": ["LOCK_TYPE=::X::ALock", "LOCK_HANDLE=::X::ALockHandle"],
            # ":lspin": ["LOCK_TYPE=::X::SpinLock", "LOCK_HANDLE=::X::SpinLockHandle"],
        },

    ),
    deps = [
        ":client",
        ":experiment_cc_proto",
        "//alock/src/cluster:cluster_cc_proto",
        "@absl//absl/flags:flag",
        "@absl//absl/flags:parse",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/colosseum:client_adaptor",
        "@rome//rome/colosseum:qps_controller",
        "@rome//rome/colosseum:workload_driver",
        "@rome//rome/colosseum/streams",
        "@rome//rome/logging",
        "@rome//rome/metrics:summary",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/util:proto_util",
    ],
)

# We assume that data is saved to a directory within this subdirectory called `results`.
filegroup(
    name = "results",
    srcs = glob(["results/**"]),
)

py_binary(
    name = "launch",
    srcs = ["launch.py"],
    data = [
        "//alock/benchmark:nodefiles",
    ],
    deps = [
        ":experiment_py_proto",
        ":plot",
        "@rome//rome/util:debugpy_util",
    ],
)

py_binary(
    name = "plot",
    srcs = ["plot.py"],

    deps = [
        ":experiment_py_proto",
        "@rome//protos:colosseum_py_proto",
        "@rome//protos:metrics_py_proto",
    ],
)
