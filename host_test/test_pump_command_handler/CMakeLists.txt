cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_pump_command_handler)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_pump_command_handler 
    "*/main/src/pump_command_handler.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_pump_command_handler 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_pump_command_handler test_pump_command_handler.elf)
