load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary", "cc_test")

cc_library(
	name = "network",
	srcs = glob(["*.cpp"], exclude = ["handlers_test.cpp", "connection_test.cpp"]),
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

cc_test(
	name = "handlers_test",
	srcs = ["handlers_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":network",
		"@googletest//:gtest_main",
	],
)

cc_test(
	name = "connection_test",
	srcs = ["connection_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":network",
		"@googletest//:gtest_main",
	],
)

