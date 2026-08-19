// main/include/interfaces/i_hal_led_strip.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

/**
 * @interface IHalLedStrip
 * @brief Hardware Abstraction Layer interface for Espressif LED Strip component.
 */
class IHalLedStrip
{
public:
    virtual ~IHalLedStrip() = default;

    /** @copydoc led_strip_new_rmt_device */
    virtual esp_err_t new_rmt_device(
        const led_strip_config_t* led_config,
        const led_strip_rmt_config_t* rmt_config,
        led_strip_handle_t* ret_strip) = 0;

    /** @copydoc led_strip_set_pixel */
    virtual esp_err_t set_pixel(
        led_strip_handle_t strip,
        uint32_t index,
        uint32_t red,
        uint32_t green,
        uint32_t blue) = 0;

    /** @copydoc led_strip_set_pixel_hsv */
    virtual esp_err_t set_pixel_hsv(
        led_strip_handle_t strip,
        uint32_t index,
        uint16_t hue,
        uint8_t saturation,
        uint8_t value) = 0;

    /** @copydoc led_strip_refresh */
    virtual esp_err_t refresh(led_strip_handle_t strip) = 0;

    /** @copydoc led_strip_clear */
    virtual esp_err_t clear(led_strip_handle_t strip) = 0;

    /** @copydoc led_strip_del */
    virtual esp_err_t del(led_strip_handle_t strip) = 0;
};
