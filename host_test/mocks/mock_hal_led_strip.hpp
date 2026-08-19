// host_test/mocks/mock_hal_led_strip.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_hal_led_strip.hpp"

class MockHalLedStrip : public IHalLedStrip
{
public:
    MOCK_METHOD(esp_err_t, new_rmt_device, (const led_strip_config_t* led_config, const led_strip_rmt_config_t* rmt_config, led_strip_handle_t* ret_strip), (override));
    MOCK_METHOD(esp_err_t, set_pixel, (led_strip_handle_t strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue), (override));
    MOCK_METHOD(esp_err_t, set_pixel_hsv, (led_strip_handle_t strip, uint32_t index, uint16_t hue, uint8_t saturation, uint8_t value), (override));
    MOCK_METHOD(esp_err_t, refresh, (led_strip_handle_t strip), (override));
    MOCK_METHOD(esp_err_t, clear, (led_strip_handle_t strip), (override));
    MOCK_METHOD(esp_err_t, del, (led_strip_handle_t strip), (override));
};
