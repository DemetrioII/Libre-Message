load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

cc_binary(
	name = "application",
	copts = [
		"-std=c++23",
	],
	srcs = [
		"main.cpp",
	],
	deps = [
		"//app:business",
		"//net/network:network",
		"//net/event_loop:event_loop",
		"//net/transport:transport",
	],
)
