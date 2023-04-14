load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

package(default_visibility = ["//alock:__subpackages__"])

string_flag(
    name = "remote_only",
    build_setting_default = "false",
    values = [
        "true",
        "false",
    ],
    visibility = ["//visibility:public"],
)

config_setting(
    name = "remote_only_true",
    flag_values = {":remote_only": "true"},
)

config_setting(
    name = "remote_only_false",
    flag_values = {":remote_only": "false"},
)


cc_library(
    name = "node_impl",
    hdrs = ["node_impl.h"],
    deps = [
        ":handler_pool",
        "//alock/cluster:cluster_cc_proto",
        "//alock/cluster:common",
        "@rome//rome/logging",
    ],
)

cc_library(
    name = "node",
    hdrs = ["node.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":handler_pool",
        ":server_impl",
        "//alock/cluster:cluster_cc_proto",
        "//alock/cluster:common",
        "@absl//absl/strings",
    ],
)