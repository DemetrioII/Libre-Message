load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

cc_library(
	name = "network",
	srcs = glob(["*.cpp"]),
	hdrs = glob(["*.hpp"]),
	copts = [
		"-std=c++23",
	],
	deps = [
		"//net/event_loop:event_loop",
		"//net/transport:transport",
		"@spdlog//:spdlog",
	],
	linkopts = [
		"-pthread",
	],
	visibility = [
		"//visibility:public"
	],
)

