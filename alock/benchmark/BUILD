load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

filegroup(
    name = "nodefiles",
    srcs = glob(["nodefiles/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "configfiles",
    srcs = glob(["configfiles/**"]),
)

filegroup(
    name = "experiments",
    srcs = glob(["experiments/**"]),
)
