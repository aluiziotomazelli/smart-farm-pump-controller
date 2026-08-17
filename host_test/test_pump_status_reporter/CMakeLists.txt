cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_pump_status_reporter)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_pump_status_reporter 
    "*/main/src/pump_status_reporter.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_pump_status_reporter 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_pump_status_reporter test_pump_status_reporter.elf)
