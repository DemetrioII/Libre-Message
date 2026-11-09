load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary", "cc_test")

cc_library(
	name = "models",
	copts = [
		"-std=c++23",
	],
	hdrs = glob([
		"*.hpp",
	]),
	srcs = glob([
		"*.cpp",
	], exclude = [
		"message_codec_test.cpp",
	]),
	deps = [
		"//models/proto:message_cc_proto",
	],
	visibility = [
		"//visibility:public",
	],
)

cc_test(
	name = "message_codec_test",
	srcs = ["message_codec_test.cpp"],
	copts = [
		"-std=c++23",
	],
	deps = [
		":models",
		"@googletest//:gtest_main",
	],
)
