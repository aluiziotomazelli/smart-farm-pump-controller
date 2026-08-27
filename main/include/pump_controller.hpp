// main/include/pump_controller.hpp
#pragma once

#include <cstdint>

#include "interfaces/i_pump_controller.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_pump_status_reporter.hpp"
#include "interfaces/i_tank_strip_display.hpp"
#include "interfaces/i_switch.hpp"
#include "interfaces/i_button.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_pump_nvs.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "interfaces/i_ota_controller.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_command_handler.hpp"
#include "pump_stats.hpp"
#include "core_types.hpp"

/**
 * @class PumpController
 * @brief Coordinates input sampling, command handling, state machine execution, visual display, and persistence.
 */
class PumpController : public IPumpController, public IOtaTriggerListener
{
public:
    PumpController(
        QueueHandle_t rx_queue,
        INvsCore& core_storage,
        IPumpNvs& pump_storage,
        IPumpStateMachine& state_machine,
        IPumpCommandHandler& command_handler,
        IPumpStatusReporter& status_reporter,
        ITankStripDisplay& display,
        ui_inputs::ISwitch& switch_solar,
        ui_inputs::ISwitch& switch_grid,
        ui_inputs::IButton& button_action,
        time_manager::ITimeManager& time_manager,
        wifi_manager::IWiFiManager& wifi_manager,
        IOtaController& ota_controller,
        IOtaTrigger& btn_trigger,
        espnow::IEspNowManager& espnow,
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

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

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
    QueueHandle_t rx_queue_;
    INvsCore& core_storage_;
    IPumpNvs& pump_storage_;
    IPumpStateMachine& state_machine_;
    IPumpCommandHandler& command_handler_;
    IPumpStatusReporter& status_reporter_;
    ITankStripDisplay& display_;
    ui_inputs::ISwitch& switch_solar_;
    ui_inputs::ISwitch& switch_grid_;
    ui_inputs::IButton& button_action_;
    time_manager::ITimeManager& time_manager_;
    wifi_manager::IWiFiManager& wifi_manager_;
    IOtaController& ota_controller_;
    IOtaTrigger& btn_trigger_;
    espnow::IEspNowManager& espnow_;
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::ISystemHAL& hal_system_;

    CoreData core_{};
    PumpStats stats_{};
    bool pending_core_commit_{false};
    bool pending_controller_commit_{false};
    bool ota_triggered_{false};

    uint32_t runtime_accumulator_ms_{0};
    uint32_t nvs_commit_accumulator_ms_{0};
    uint32_t brightness_check_accumulator_ms_{0};
    uint8_t current_display_brightness_{0};

    TaskHandle_t task_handle_{nullptr};
    bool is_running_{false};

    esp_err_t init_core_storage();
    esp_err_t init_pump_storage();
    esp_err_t init_wifi();
    esp_err_t init_ota();
    esp_err_t init_espnow();
    esp_err_t init_time_manager();
    void update_running_version();
    bool check_firmware_health(bool session_healthy);
    void update_display_brightness(uint32_t delta_ms);

    void process_pending_ota();
    esp_err_t send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code);
    esp_err_t send_fill_request();

    static void task_entry(void* arg);
    void run_task();
};
