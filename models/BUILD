load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

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
	]),
	deps = [
		"//models/proto:message_cc_proto",
	],
	visibility = [
		"//visibility:public",
	],
)
