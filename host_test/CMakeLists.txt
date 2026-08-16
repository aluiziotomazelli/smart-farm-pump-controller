cmake_minimum_required(VERSION 3.16)
project(pump_controller_host_tests NONE)

# Enable CTest for this project
include(CTest)

# Unified Coverage Configuration
find_program(LCOV_PATH lcov REQUIRED)
find_program(GENHTML_PATH genhtml REQUIRED)

# The unified coverage report will be placed in host_test/coverage
set(UNIFIED_COVERAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coverage")

# This target aggregates coverage data (.gcda files) from all test build directories
add_custom_target(unified_coverage
    COMMAND ${LCOV_PATH} --capture --directory .. --output-file unified_coverage.info --rc branch_coverage=1 --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch,inconsistent,source
    COMMAND ${LCOV_PATH} --extract unified_coverage.info "*/main/src/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${LCOV_PATH} --remove unified_coverage_filtered.info "*/main/include/interfaces/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${GENHTML_PATH} unified_coverage_filtered.info --output-directory ${UNIFIED_COVERAGE_DIR} --title "Pump Controller Unified Coverage" --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,source
    COMMAND ${LCOV_PATH} --list unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating unified coverage report from all host tests"
)

# Cleanup target to manually clear previous results if needed
add_custom_target(coverage_clean
    COMMAND find .. -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UNIFIED_COVERAGE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning up coverage data"
)
