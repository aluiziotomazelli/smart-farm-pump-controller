cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_contactor_controller)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_contactor_controller 
    "*/main/src/contactor_controller.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_contactor_controller 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_contactor_controller test_contactor_controller.elf)
