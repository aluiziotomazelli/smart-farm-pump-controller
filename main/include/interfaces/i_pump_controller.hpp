// main/include/interfaces/i_pump_controller.hpp
#pragma once

#include "esp_err.h"

/**
 * @interface IPumpController
 * @brief Top-level coordinator interface for the pump control application.
 */
class IPumpController
{
public:
    virtual ~IPumpController() = default;

    /**
     * @brief Initializes all subsystems (FSM, inputs, LEDs, telemetry, display).
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Starts the background worker task and LED controller.
     * @return ESP_OK on success.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Stops the background task and shuts down outputs.
     */
    virtual void stop() = 0;

    /**
     * @brief Executes a single orchestration step.
     * @param delta_ms Elapsed time in milliseconds since previous tick.
     */
    virtual void tick(uint32_t delta_ms) = 0;
};
