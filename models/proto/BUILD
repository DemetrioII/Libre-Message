load("@rules_proto//proto:defs.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

proto_library(
	name = "message_proto",
	srcs = ["message.proto"],
)

cc_proto_library(
	name = "message_cc_proto",
	deps = [":message_proto"],
	visibility = [
		"//visibility:public",
	],
)
