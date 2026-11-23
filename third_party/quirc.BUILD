load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "quirc",
    srcs = ["lib/quirc.c", "lib/decode.c", "lib/identify.c", "lib/version_db.c"],
    hdrs = ["lib/quirc.h", "lib/quirc_internal.h"],
    includes = ["lib"],
    linkopts = ["-lm"],
    visibility = ["//visibility:public"],
)
