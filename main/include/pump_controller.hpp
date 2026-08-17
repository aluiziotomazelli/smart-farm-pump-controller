// main/include/pump_controller.hpp
#pragma once

#include <cstdint>

#include "interfaces/i_pump_controller.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_pump_status_reporter.hpp"
#include "interfaces/i_pump_led_controller.hpp"
#include "interfaces/i_tank_level_display.hpp"
#include "interfaces/i_switch.hpp"
#include "interfaces/i_button.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_system.hpp"
#include "pump_command_handler.hpp"
#include "core_types.hpp"

/**
 * @class PumpController
 * @brief Coordinates input sampling, command handling, state machine execution, and telemetry reporting.
 */
class PumpController : public IPumpController
{
public:
    PumpController(
        IPumpStateMachine& state_machine,
        PumpCommandHandler& command_handler,
        IPumpStatusReporter& status_reporter,
        IPumpLedController& led_controller,
        ITankLevelDisplay& tank_display,
        ui_inputs::ISwitch& switch_mode,
        ui_inputs::ISwitch& switch_source,
        ui_inputs::IButton& button_start,
        ui_inputs::IButton& button_stop,
        idf_hals::IHalFreertos& hal_rtos,
        idf_hals::ISystemHAL& hal_system);

    ~PumpController() override;

    /** @copydoc IPumpController::init */
    esp_err_t init() override;

    /** @copydoc IPumpController::start */
    esp_err_t start() override;

    /** @copydoc IPumpController::stop */
    void stop() override;

    /** @copydoc IPumpController::tick */
    void tick(uint32_t delta_ms) override;

private:
    IPumpStateMachine& state_machine_;
    PumpCommandHandler& command_handler_;
    IPumpStatusReporter& status_reporter_;
    IPumpLedController& led_controller_;
    ITankLevelDisplay& tank_display_;
    ui_inputs::ISwitch& switch_mode_;
    ui_inputs::ISwitch& switch_source_;
    ui_inputs::IButton& button_start_;
    ui_inputs::IButton& button_stop_;
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::ISystemHAL& hal_system_;

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};

    static void task_entry(void* arg);
    void run_task();
};
