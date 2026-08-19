// main/src/hal_led_strip.cpp
#include <cstdint>

#include "led_strip.h"
#include "led_strip_rmt.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "hal_led_strip.hpp"

esp_err_t HalLedStrip::new_rmt_device(
    const led_strip_config_t* led_config,
    const led_strip_rmt_config_t* rmt_config,
    led_strip_handle_t* ret_strip)
{
    return led_strip_new_rmt_device(led_config, rmt_config, ret_strip);
}

esp_err_t HalLedStrip::set_pixel(
    led_strip_handle_t strip,
    uint32_t index,
    uint32_t red,
    uint32_t green,
    uint32_t blue)
{
    return led_strip_set_pixel(strip, index, red, green, blue);
}

esp_err_t HalLedStrip::set_pixel_hsv(
    led_strip_handle_t strip,
    uint32_t index,
    uint16_t hue,
    uint8_t saturation,
    uint8_t value)
{
    return led_strip_set_pixel_hsv(strip, index, hue, saturation, value);
}

esp_err_t HalLedStrip::refresh(led_strip_handle_t strip)
{
    return led_strip_refresh(strip);
}

esp_err_t HalLedStrip::clear(led_strip_handle_t strip)
{
    return led_strip_clear(strip);
}

esp_err_t HalLedStrip::del(led_strip_handle_t strip)
{
    return led_strip_del(strip);
}
