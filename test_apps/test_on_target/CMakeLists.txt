cmake_minimum_required(VERSION 3.16)

# Set default sdkconfig profiles
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.esp32c3")
endif()

# Get project root
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

# Add paths for components
list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/idf_hals"
    "${PROJECT_ROOT}/components/smart-farm-common"
    "${PROJECT_ROOT}/components/espnow_manager"
    "${PROJECT_ROOT}/components/ui_inputs"
    "${PROJECT_ROOT}/components/time_manager"
    "$ENV{IDF_PATH}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_pump_on_target)
