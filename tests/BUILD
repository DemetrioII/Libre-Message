load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "e2e_test",
    srcs = ["e2e_test.cpp"],
    copts = [
        "-std=c++23",
    ],
    data = [
        "//:application",
    ],
    linkopts = [
        "-pthread",
    ],
    tags = ["e2e"],
    timeout = "short",
    deps = [
        "@googletest//:gtest_main",
    ],
)
