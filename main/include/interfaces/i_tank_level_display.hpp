// main/include/interfaces/i_tank_level_display.hpp
#pragma once

#include <cstdint>
#include "esp_err.h"

/**
 * @interface ITankLevelDisplay
 * @brief Abstract interface for visual tank level indicator (e.g. LED strip).
 */
class ITankLevelDisplay
{
public:
    virtual ~ITankLevelDisplay() = default;

    /**
     * @brief Initializes display hardware and ensures default off state.
     * @return ESP_OK on success, or error code.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Updates the displayed tank level.
     * @param permille Water level in permille (0 to 1000).
     * @param backup_mode True if water tank is operating in backup mode (float switch only).
     * @param is_full True if mechanical float switch indicates full tank.
     */
    virtual void set_level(uint16_t permille, bool backup_mode = false, bool is_full = false) = 0;

    /**
     * @brief Gets the last received water level in permille.
     * @return Water level in permille.
     */
    virtual uint16_t get_level() const = 0;
};
