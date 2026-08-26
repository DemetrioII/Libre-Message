load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

cc_library(
	name = "transport",
	srcs = glob(["*.cpp"]),
	hdrs = glob(["*.hpp"]),
	copts = [
		"-std=c++23",
	],
	visibility = [
		"//visibility:public",
	],
	linkopts = [
		"-pthread",
	],
)
