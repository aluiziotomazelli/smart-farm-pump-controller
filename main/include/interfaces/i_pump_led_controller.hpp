// main/include/interfaces/i_pump_led_controller.hpp
#pragma once

#include "esp_err.h"
#include "farm_protocol_types.hpp"

/**
 * @interface IPumpLedController
 * @brief Abstract interface for coordinating dual-LED visual feedback on the pump controller.
 */
class IPumpLedController
{
public:
    virtual ~IPumpLedController() = default;

    /**
     * @brief Initializes both LED controllers.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Starts background blink animation tasks for both LEDs.
     * @return ESP_OK on success.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Stops background blink tasks and turns off all LEDs.
     */
    virtual void stop() = 0;

    /**
     * @brief Updates LED patterns according to current load state and active power source.
     * @param state Current finite state machine load state.
     * @param source Current active power source.
     */
    virtual void update(farm::LoadState state, farm::PowerSource source) = 0;

    /**
     * @brief Sets both LEDs to OTA_UPDATING pattern during firmware update.
     */
    virtual void set_ota_updating() = 0;
};
