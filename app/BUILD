load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")
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
	],
	deps = [
		"//models/proto:message_cc_proto",
		"//models:models",
		"//net/network:network",
	],
)
