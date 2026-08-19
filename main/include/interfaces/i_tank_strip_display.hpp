// main/include/interfaces/i_tank_strip_display.hpp
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "farm_protocol_types.hpp"
#include "interfaces/i_tank_level_display.hpp"

/**
 * @enum TankStripPattern
 * @brief Explicit override patterns for full-strip status indicators.
 */
enum class TankStripPattern : uint8_t
{
    AUTO,         ///< Render pattern automatically according to state, mode, source, and level
    OTA_UPDATING, ///< Purple scanner (Knight Rider) during firmware download
    BOOT_SUCCESS, ///< One-shot green progressive sweep on successful boot
    BOOT_ERROR,   ///< SOS 3-flash red burst on failed boot / rollback
};

/**
 * @struct TankStripConfig
 * @brief Configuration parameters for addressable LED strip.
 */
struct TankStripConfig
{
    gpio_num_t gpio_pin{GPIO_NUM_7};               ///< Data GPIO pin connected to LED strip DIN
    uint32_t num_leds{10};                         ///< Total number of addressable LEDs on the strip
    uint8_t default_brightness{200};               ///< Master brightness level (0 - 255)
    uint32_t rmt_resolution_hz{10 * 1000 * 1000}; ///< RMT tick resolution (10MHz)
};

/**
 * @interface ITankStripDisplay
 * @brief Unified interface for addressable LED strip handling water level and pump operational state.
 */
class ITankStripDisplay : public ITankLevelDisplay
{
public:
    ~ITankStripDisplay() override = default;

    /**
     * @brief Updates finite state machine operating parameters for visual rendering.
     * @param state Current load state (IDLE, RUNNING, FAULT).
     * @param mode Current control mode (AUTO, MANUAL).
     * @param source Current active power source (SOLAR, GRID, UNKNOWN).
     */
    virtual void update_state(farm::LoadState state, farm::ControlMode mode, farm::PowerSource source) = 0;

    /**
     * @brief Sets an explicit pattern override.
     * @param pattern Override pattern (e.g. OTA_UPDATING, BOOT_SUCCESS, BOOT_ERROR, or AUTO to return).
     */
    virtual void set_override_pattern(TankStripPattern pattern) = 0;

    /**
     * @brief Gets current active pattern override.
     * @return Active TankStripPattern.
     */
    virtual TankStripPattern get_override_pattern() const = 0;

    /**
     * @brief Sets master display brightness.
     * @param brightness Master brightness level (0 - 255).
     */
    virtual void set_brightness(uint8_t brightness) = 0;

    /**
     * @brief Gets current master display brightness.
     * @return Current brightness level (0 - 255).
     */
    virtual uint8_t get_brightness() const = 0;

    /**
     * @brief Advances animation timers and pushes pixel buffer to hardware strip.
     * @param delta_ms Elapsed time in milliseconds since last tick.
     */
    virtual void tick(uint32_t delta_ms) = 0;

    /**
     * @brief Clears and turns off all strip LEDs.
     */
    virtual void clear() = 0;
};
