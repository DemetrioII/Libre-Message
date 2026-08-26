load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

cc_library(
	name = "network",
	srcs = glob(["*.cpp"], exclude=["connector.cpp", "listener.cpp"]),
	hdrs = glob(["*.hpp"]),
	copts = [
		"-std=c++23",
	],
	deps = [
		"//net/event_loop:event_loop",
		"//net/transport:transport",
	],
	linkopts = [
		"-pthread",
	],
)

cc_binary(
	name = "connector",
	srcs = glob(["*.cpp", "*.hpp"], exclude=["listening.cpp"]),
	copts = [
		"-std=c++23",
	],
	deps = [
		"//net/network:network",
	],
)

cc_binary(
	name = "listener",
	srcs = glob(["*.cpp", "*.hpp"], exclude=["connector.cpp"]),
	copts = [
		"-std=c++23",
	],
	deps = [
		"//net/network:network",
	],
)
