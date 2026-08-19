cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_tank_strip_display)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_tank_strip_display 
    "*/main/src/tank_strip_display.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_tank_strip_display 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_tank_strip_display test_tank_strip_display.elf)
