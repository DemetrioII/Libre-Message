load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "qrcodegen",
    srcs = ["cpp/qrcodegen.cpp"],
    hdrs = ["cpp/qrcodegen.hpp"],
    includes = ["cpp"],
    copts = ["-std=c++23"],
    visibility = ["//visibility:public"],
)
