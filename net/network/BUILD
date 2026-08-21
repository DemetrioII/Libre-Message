load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

cc_library(
	name = "network",
	srcs = glob(["*.cpp"], exclude=["main.cpp"]),
	hdrs = glob(["*.hpp"]),
	copts = [
		"-std=c++23",
	],
)

cc_binary(
	name = "network_bin",
	srcs = glob(["*.cpp", "*.hpp"]),
	copts = [
		"-std=c++23",
	],
)
