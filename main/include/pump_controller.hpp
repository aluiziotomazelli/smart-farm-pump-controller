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
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_pump_nvs.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "pump_command_handler.hpp"
#include "pump_stats.hpp"
#include "core_types.hpp"

/**
 * @class PumpController
 * @brief Coordinates input sampling, command handling, state machine execution, telemetry reporting, and persistence.
 */
class PumpController : public IPumpController
{
public:
    PumpController(
        INvsCore& core_storage,
        IPumpNvs& pump_storage,
        IPumpStateMachine& state_machine,
        PumpCommandHandler& command_handler,
        IPumpStatusReporter& status_reporter,
        IPumpLedController& led_controller,
        ITankLevelDisplay& tank_display,
        ui_inputs::ISwitch& switch_mode,
        ui_inputs::ISwitch& switch_source,
        ui_inputs::IButton& button_action,
        wifi_manager::IWiFiManager& wifi_manager,
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

    /**
     * @brief Persists dirty or pending storage states to NVS flash.
     * @param force_all If true, unconditionally commits both core and pump stats to NVS.
     */
    void save_persistent_state(bool force_all = false);

    const CoreData& get_core_data() const { return core_; }
    CoreData& get_core_data() { return core_; }
    const PumpStats& get_stats() const { return stats_; }
    PumpStats& get_stats() { return stats_; }

    bool is_pending_core_commit() const { return pending_core_commit_; }
    bool is_pending_controller_commit() const { return pending_controller_commit_; }

private:
    INvsCore& core_storage_;
    IPumpNvs& pump_storage_;
    IPumpStateMachine& state_machine_;
    PumpCommandHandler& command_handler_;
    IPumpStatusReporter& status_reporter_;
    IPumpLedController& led_controller_;
    ITankLevelDisplay& tank_display_;
    ui_inputs::ISwitch& switch_mode_;
    ui_inputs::ISwitch& switch_source_;
    ui_inputs::IButton& button_action_;
    wifi_manager::IWiFiManager& wifi_manager_;
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::ISystemHAL& hal_system_;

    CoreData core_{};
    PumpStats stats_{};
    bool pending_core_commit_{false};
    bool pending_controller_commit_{false};

    uint32_t runtime_accumulator_ms_{0};
    uint32_t nvs_commit_accumulator_ms_{0};

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};

    esp_err_t init_core_storage();
    esp_err_t init_pump_storage();

    static void task_entry(void* arg);
    void run_task();
};
