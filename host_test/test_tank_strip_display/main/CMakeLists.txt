idf_component_register(
    SRCS 
        "main.cpp"
        "test_tank_strip_display.cpp"
        "../../../main/src/tank_strip_display.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/smart-farm-common/include"
        "../../../components/smart-farm-common/include/interfaces"
        "../../../components/idf_hals/include"
        "../../../components/idf_hals/include/interfaces"
        "../../../components/idf_hals/mocks"
        "../../../managed_components/espressif__led_strip/include"
        "../../../managed_components/espressif__led_strip/interface"
        "$ENV{IDF_PATH}/components/esp_driver_rmt/include"
        "$ENV{IDF_PATH}/components/esp_driver_spi/include"
        "$ENV{IDF_PATH}/components/hal/include"
    REQUIRES 
        gtest
        smart-farm-common
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
