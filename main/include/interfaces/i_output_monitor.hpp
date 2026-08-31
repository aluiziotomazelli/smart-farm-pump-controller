// main/include/interfaces/i_output_monitor.hpp
#pragma once

#include "esp_err.h"

/**
 * @interface IOutputMonitor
 * @brief Abstract interface for validating voltage presence at pump motor terminals.
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
     * @brief Checks if voltage is detected across the pump motor terminals.
     * @return true if energized, false otherwise.
     */
    virtual bool has_any_output_energy() const = 0;
};
