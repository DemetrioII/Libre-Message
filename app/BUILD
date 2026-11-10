load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

cc_library(
	name = "business",
	copts = [
		"-std=c++23",
	],
	hdrs = [
		"manager.hpp",
		"session.hpp",
		"connection_manager.hpp"
	],
	srcs = [
		"manager.cpp",
		"session.cpp",
		"connection_manager.cpp",
	],
	deps = [
		"//models/proto:message_cc_proto",
		"//models:models",
		"//net/network:network",
	],
	visibility = [
		"//visibility:public"
	],
)

cc_test(
	name = "connection_manager_test",
	srcs = ["connection_manager_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":business",
		"@googletest//:gtest_main",
	],
)
