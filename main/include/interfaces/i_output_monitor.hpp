// main/include/interfaces/i_output_monitor.hpp
#pragma once

#include "esp_err.h"

/**
 * @interface IOutputMonitor
 * @brief Abstract interface for validating voltage and energy presence at contactor outputs.
 */
class IOutputMonitor
{
public:
    virtual ~IOutputMonitor() = default;

    /**
     * @brief Initializes input monitoring sensor(s).
     * @return ESP_OK on success, or ESP-IDF error code.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Checks if voltage is detected at Contactor 1 (Grid) output.
     * @return true if energized, false otherwise.
     */
    virtual bool has_contactor1_energy() const = 0;

    /**
     * @brief Checks if voltage is detected at Contactor 2 (Solar) output.
     * @return true if energized, false otherwise.
     */
    virtual bool has_contactor2_energy() const = 0;

    /**
     * @brief Checks if voltage is detected on any contactor output.
     * @return true if any output is energized.
     */
    virtual bool has_any_output_energy() const
    {
        return has_contactor1_energy() || has_contactor2_energy();
    }
};
