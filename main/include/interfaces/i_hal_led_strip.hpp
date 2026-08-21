// main/include/interfaces/i_hal_led_strip.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"

#if !defined(CONFIG_IDF_TARGET_LINUX) && __has_include("led_strip.h")
#include "led_strip.h"
#include "led_strip_rmt.h"
#else
#include <cstddef>

typedef struct led_strip_t* led_strip_handle_t;

typedef enum {
    LED_MODEL_WS2812,
    LED_MODEL_SK6812,
    LED_MODEL_WS2816,
} led_model_t;

typedef enum {
    LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    LED_STRIP_COLOR_COMPONENT_FMT_BGR,
    LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    LED_STRIP_COLOR_COMPONENT_FMT_GRBW,
} led_color_component_format_t;

typedef struct {
    int strip_gpio_num;
    uint32_t max_leds;
    led_model_t led_model;
    led_color_component_format_t color_component_format;
    struct {
        uint32_t invert_out: 1;
    } flags;
} led_strip_config_t;

#if __has_include("driver/rmt_types.h")
#include "driver/rmt_types.h"
#else
typedef int rmt_clock_source_t;
#endif

#ifndef RMT_CLK_SRC_DEFAULT
#define RMT_CLK_SRC_DEFAULT static_cast<rmt_clock_source_t>(0)
#endif

typedef struct {
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t with_dma: 1;
    } flags;
} led_strip_rmt_config_t;

#endif

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
