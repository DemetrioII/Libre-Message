
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary", "cc_test")

cc_library(
	name = "event_loop",
	srcs = glob(["*.cpp"], exclude = ["timer_queue_test.cpp"]),
	hdrs = glob(["*.hpp"]),
	deps = [
		"@spdlog//:spdlog",
	],
	copts = [
		"-std=c++23",
	],
	visibility = [
		"//visibility:public",
	],
)

cc_test(
	name = "timer_queue_test",
	srcs = ["timer_queue_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":event_loop",
		"@googletest//:gtest_main",
	],
)
