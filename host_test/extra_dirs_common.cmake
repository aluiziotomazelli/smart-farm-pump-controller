# host_test/extra_dirs_common.cmake
# Common setup for host-based test projects in smart-farm-pump-controller.

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Fallback to secrets.example.hpp if secrets.hpp does not exist (e.g. on CI or fresh clone)
if(NOT EXISTS "${PROJECT_ROOT}/main/include/secrets.hpp")
    if(EXISTS "${PROJECT_ROOT}/main/include/secrets.example.hpp")
        message(STATUS "secrets.hpp not found in host_test, creating from secrets.example.hpp")
        configure_file("${PROJECT_ROOT}/main/include/secrets.example.hpp"
                       "${PROJECT_ROOT}/main/include/secrets.hpp" COPYONLY)
    endif()
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/smart-farm-common"
    "${PROJECT_ROOT}/components/smart-farm-common/host_test/common"
    "${PROJECT_ROOT}/components/espnow_manager"
    "${PROJECT_ROOT}/components/wifi_manager"
    "${PROJECT_ROOT}/components/ui_inputs"
    "${PROJECT_ROOT}/components/idf_hals"
    "${PROJECT_ROOT}/components/time_manager"
    "${PROJECT_ROOT}/components/time_manager/host_test/mocks"
    "${PROJECT_ROOT}/components/ota_manager"
    "${PROJECT_ROOT}/components/ota_manager/host_test/mocks"
    "${PROJECT_ROOT}/host_test/mocks"
    "${PROJECT_ROOT}/host_test/gtest"

    "$ENV{IDF_PATH}/tools/mocks/driver"
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
)

set(COMPONENTS 
    "main"
    "smart-farm-common"
    "espnow_manager"
    "wifi_manager"
    "ota_manager"
    "ui_inputs"
    "time_manager"
    "idf_hals"
    "esp_timer"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
