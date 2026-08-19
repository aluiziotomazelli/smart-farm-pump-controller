// main/include/pump_led_controller.hpp
#pragma once

#include "interfaces/i_pump_led_controller.hpp"
#include "interfaces/i_led_controller.hpp"
#include "farm_protocol_types.hpp"

/**
 * @class PumpLedController
 * @brief Manages mutual exclusion and pattern updates for Grid and Solar status LEDs.
 */
class PumpLedController : public IPumpLedController
{
public:
    PumpLedController(
        ILedController& led_grid,
        ILedController& led_solar);

    ~PumpLedController() override = default;

    /** @copydoc IPumpLedController::init */
    esp_err_t init() override;

    /** @copydoc IPumpLedController::start */
    esp_err_t start() override;

    /** @copydoc IPumpLedController::stop */
    void stop() override;

    /** @copydoc IPumpLedController::update */
    void update(farm::LoadState state, farm::PowerSource source) override;

    /** @copydoc IPumpLedController::set_ota_updating */
    void set_ota_updating() override;

private:
    ILedController& led_grid_;
    ILedController& led_solar_;
};
