// main/include/hal_led_strip.hpp
#pragma once

#include "interfaces/i_hal_led_strip.hpp"

/**
 * @class HalLedStrip
 * @brief Concrete Hardware Abstraction Layer for Espressif LED Strip component.
 */
class HalLedStrip : public IHalLedStrip
{
public:
    HalLedStrip() = default;
    ~HalLedStrip() override = default;

    /** @copydoc IHalLedStrip::new_rmt_device */
    esp_err_t new_rmt_device(
        const led_strip_config_t* led_config,
        const led_strip_rmt_config_t* rmt_config,
        led_strip_handle_t* ret_strip) override;

    /** @copydoc IHalLedStrip::set_pixel */
    esp_err_t set_pixel(
        led_strip_handle_t strip,
        uint32_t index,
        uint32_t red,
        uint32_t green,
        uint32_t blue) override;

    /** @copydoc IHalLedStrip::set_pixel_hsv */
    esp_err_t set_pixel_hsv(
        led_strip_handle_t strip,
        uint32_t index,
        uint16_t hue,
        uint8_t saturation,
        uint8_t value) override;

    /** @copydoc IHalLedStrip::refresh */
    esp_err_t refresh(led_strip_handle_t strip) override;

    /** @copydoc IHalLedStrip::clear */
    esp_err_t clear(led_strip_handle_t strip) override;

    /** @copydoc IHalLedStrip::del */
    esp_err_t del(led_strip_handle_t strip) override;
};
