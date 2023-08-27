# CODEGEN_NOTE
# it should NOT be modified by user

cc_library(
    name = "datatypes",
    hdrs = [
        "datatypes.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//MRA_COMPONENTS_ROOT/COMPONENT_REL_PATH/interface:interface",
    ],
)

cc_library(
    name = "implementation",
    hdrs = [
        "COMPONENT_CPP_NAME_CAMELCASE.hpp", # generated by MRA-codegen.py
    ],
    srcs = [
        "tick.cpp", # to be implemented by user, generated if not existing
    ],
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//base:abstract_interface",
        ":datatypes",
        BAZEL_IMPLEMENTATION_DEPENDENCIES
    ],
)

cc_test(
    name = "testsuite",
    srcs = [
        "test.cpp",
    ],
    data = glob(["testdata/*.json"]),
    deps = [
        "//base:testing_base",
        ":implementation",
        "@com_google_googletest//:gtest_main",
        "@nlohmann_json",
    ],
)

