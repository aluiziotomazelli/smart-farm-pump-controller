# Fallback to secrets.example.hpp if secrets.hpp does not exist (e.g. on CI or fresh clone)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/secrets.hpp")
    message(STATUS "secrets.hpp not found, creating from secrets.example.hpp")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/include/secrets.example.hpp"
                   "${CMAKE_CURRENT_LIST_DIR}/include/secrets.hpp" COPYONLY)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "src/pump_state_machine.cpp"
        "src/pump_command_handler.cpp"
        "src/pump_status_reporter.cpp"
        "src/contactor_controller.cpp"
        "src/pump_led_controller.cpp"
        "src/pump_controller.cpp"
        "src/ota_controller.cpp"
        "src/hal_led_strip.cpp"
        "src/tank_strip_display.cpp"
    INCLUDE_DIRS
        "include"
        "include/interfaces"
    REQUIRES
        idf_hals
        smart-farm-common
        espnow_manager
        wifi_manager
        ota_manager
        ui_inputs
        time_manager
        espressif__led_strip
        driver
        esp_timer
        nvs_flash
        freertos
)
