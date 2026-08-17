// main/include/interfaces/i_contactor_controller.hpp
#pragma once

#include "esp_err.h"
#include "farm_protocol_types.hpp"

/**
 * @interface IContactorController
 * @brief Abstract interface for controlling power source selection contactors.
 */
class IContactorController
{
public:
    virtual ~IContactorController() = default;

    /**
     * @brief Initializes GPIO outputs for contactor coils/gates.
     * @return ESP_OK on success, or ESP-IDF error code.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Energizes the contactor corresponding to the selected power source.
     *
     * Applies safety demagnetization delay if switching from another energized source.
     *
     * @param source Power source to activate (SOLAR or GRID).
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG if source is UNKNOWN.
     */
    virtual esp_err_t activate(farm::PowerSource source) = 0;

    /**
     * @brief De-energizes all contactors immediately.
     * @return ESP_OK on success.
     */
    virtual esp_err_t deactivate() = 0;

    /**
     * @brief Returns the currently commanded active power source.
     * @return Active PowerSource, or PowerSource::UNKNOWN if all contactors are idle.
     */
    virtual farm::PowerSource get_active_source() const = 0;

    /**
     * @brief Checks whether any contactor is currently energized.
     * @return true if active, false if all contactors are off.
     */
    virtual bool is_active() const = 0;
};
