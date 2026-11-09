load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary", "cc_test")

cc_library(
	name = "transport",
	srcs = glob(["*.cpp"], exclude=["transport_test.cpp"]),
	hdrs = glob(["*.hpp"]),
	copts = [
		"-std=c++23",
	],
	visibility = [
		"//visibility:public",
	],
	linkopts = [
		"-pthread",
		"-lssl",
		"-lcrypto",
	],
	deps = [
		"//models:models",
		"@spdlog//:spdlog",
	],
)

cc_test(
	name = "transport_test",
	srcs = ["transport_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":transport",
		"//models:models",
		"@googletest//:gtest_main",
	],
)
