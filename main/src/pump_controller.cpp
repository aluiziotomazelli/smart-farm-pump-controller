// main/src/pump_controller.cpp
#include <cstdint>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "pump_controller.hpp"
#include "farm_protocol_types.hpp"
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
#include "secrets.hpp"

static const char* TAG = "PumpController";

PumpController::PumpController(
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
    idf_hals::ISystemHAL& hal_system)
    : core_storage_(core_storage)
    , pump_storage_(pump_storage)
    , state_machine_(state_machine)
    , command_handler_(command_handler)
    , status_reporter_(status_reporter)
    , led_controller_(led_controller)
    , tank_display_(tank_display)
    , switch_mode_(switch_mode)
    , switch_source_(switch_source)
    , button_action_(button_action)
    , wifi_manager_(wifi_manager)
    , hal_rtos_(hal_rtos)
    , hal_system_(hal_system)
{
}

PumpController::~PumpController()
{
    stop();
}

esp_err_t PumpController::init()
{
    esp_err_t err = init_core_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init core storage: %s", esp_err_to_name(err));
        return err;
    }

    err = init_pump_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init pump storage: %s", esp_err_to_name(err));
        return err;
    }

    err = init_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %s", esp_err_to_name(err));
        return err;
    }

    command_handler_.set_core_data(core_);

    err = state_machine_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init state machine: %s", esp_err_to_name(err));
        return err;
    }

    err = status_reporter_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init status reporter: %s", esp_err_to_name(err));
        return err;
    }

    err = led_controller_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LED controller: %s", esp_err_to_name(err));
        return err;
    }

    err = tank_display_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init tank display: %s", esp_err_to_name(err));
        return err;
    }

    err = switch_mode_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init mode switch: %s", esp_err_to_name(err));
        return err;
    }

    err = switch_source_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init source switch: %s", esp_err_to_name(err));
        return err;
    }

    err = button_action_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init action button: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(
        TAG,
        "PumpController initialized successfully (Boot #%lu, Runtime: %lu s)",
        static_cast<unsigned long>(core_.boot_count),
        static_cast<unsigned long>(stats_.total_runtime_s));
    return ESP_OK;
}

esp_err_t PumpController::start()
{
    esp_err_t err = led_controller_.start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LED controller: %s", esp_err_to_name(err));
        return err;
    }

    is_running_ = true;
    BaseType_t res = hal_rtos_.task_create(task_entry, "pump_ctrl_task", 4096, this, 5, &task_handle_);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PumpController task");
        is_running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PumpController task started successfully");
    return ESP_OK;
}

void PumpController::stop()
{
    is_running_ = false;
    if (task_handle_ != nullptr) {
        hal_rtos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }
    led_controller_.stop();
    ESP_LOGI(TAG, "PumpController stopped");
}

void PumpController::tick(uint32_t delta_ms)
{
    // 1. Sample Switch Inputs
    switch_mode_.update();
    farm::ControlMode mode = (switch_mode_.get_state() == ui_inputs::SwitchState::CLOSED) ? farm::ControlMode::AUTO
                                                                                          : farm::ControlMode::MANUAL;
    state_machine_.set_control_mode(mode);

    switch_source_.update();
    farm::PowerSource source = (switch_source_.get_state() == ui_inputs::SwitchState::CLOSED) ? farm::PowerSource::SOLAR
                                                                                              : farm::PowerSource::GRID;

    // 2. Sample Manual Action Button (Toggle Start/Stop)
    button_action_.update();

    if (mode == farm::ControlMode::MANUAL) {
        if (button_action_.get_last_click() != ui_inputs::ButtonClickType::NONE_CLICK) {
            auto current_snapshot = state_machine_.get_snapshot();
            if (current_snapshot.state == farm::LoadState::RUNNING) {
                ESP_LOGI(TAG, "Manual Action: Pump is RUNNING -> STOP triggered");
                state_machine_.handle_manual_stop();
            } else {
                ESP_LOGI(TAG, "Manual Action: Pump is OFF -> START triggered for source %d", static_cast<int>(source));
                esp_err_t start_err = state_machine_.handle_manual_start(source);
                if (start_err == ESP_OK) {
                    stats_.manual_starts_total++;
                    stats_.start_cycles_total++;
                    pump_storage_.save_app_data(stats_, false);
                }
            }
        }
    }

    // 3. Process Inbound Remote Commands
    PumpCommandProcessResult cmd_res = command_handler_.process();
    if (cmd_res.core_modified) {
        pending_core_commit_ = true;
        core_storage_.save_core(core_, false);
    }

    if (cmd_res.reboot_requested) {
        ESP_LOGW(TAG, "Reboot requested via command; persisting state and restarting...");
        save_persistent_state(true);
        state_machine_.handle_manual_stop();
        hal_rtos_.task_delay(pdMS_TO_TICKS(100));
        hal_system_.restart();
        return;
    }

    // 4. Tick Subsystems
    state_machine_.tick(delta_ms);
    status_reporter_.tick(delta_ms);

    // 5. Update Visual Feedback & Runtime Accounting
    auto snapshot = state_machine_.get_snapshot();
    led_controller_.update(snapshot.state, snapshot.source);

    if (snapshot.state == farm::LoadState::RUNNING) {
        runtime_accumulator_ms_ += delta_ms;
        while (runtime_accumulator_ms_ >= 1000) {
            runtime_accumulator_ms_ -= 1000;
            stats_.total_runtime_s++;
            if (snapshot.source == farm::PowerSource::SOLAR) {
                stats_.solar_runtime_s++;
            }
            else {
                stats_.grid_runtime_s++;
            }
        }
        pump_storage_.save_app_data(stats_, false);
    }

    // 6. Periodic NVS Commit (every 15 minutes = 900,000 ms)
    static constexpr uint32_t STORAGE_COMMIT_PERIOD_MS = 900000;
    nvs_commit_accumulator_ms_ += delta_ms;
    if (nvs_commit_accumulator_ms_ >= STORAGE_COMMIT_PERIOD_MS) {
        nvs_commit_accumulator_ms_ = 0;
        save_persistent_state();
    }
}

void PumpController::save_persistent_state(bool force_all)
{
    bool force_core = pending_core_commit_ || force_all;
    bool force_pump = pending_controller_commit_ || force_all;

    if (force_core) {
        if (core_storage_.save_core(core_, true) == ESP_OK) {
            pending_core_commit_ = false;
        }
        else {
            ESP_LOGE(TAG, "Failed to save core storage to NVS");
        }
    }

    if (force_pump) {
        if (pump_storage_.save_app_data(stats_, true) == ESP_OK) {
            pending_controller_commit_ = false;
        }
        else {
            ESP_LOGE(TAG, "Failed to save pump stats to NVS");
        }
    }
}

// =============================================================================
// Private Methods
// =============================================================================

esp_err_t PumpController::init_core_storage()
{
    CoreData default_core{};
    default_core.node_id = farm::NodeId::UNKNOWN;
    default_core.node_type = farm::NodeType::ACTUATOR;
    default_core.power_profile = farm::PowerProfile::ALWAYS_ON;

    esp_err_t ret = core_storage_.init(core_, default_core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(ret));
        return ret;
    }

    core_storage_.process_boot_reasons(
        core_, hal_system_.reset_reason(), ESP_SLEEP_WAKEUP_UNDEFINED, pending_core_commit_);

    return ESP_OK;
}

esp_err_t PumpController::init_pump_storage()
{
    PumpStats default_stats{};
    default_stats.reset();

    esp_err_t ret = pump_storage_.init_app_data(stats_, default_stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize pump storage: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t PumpController::init_wifi()
{
    esp_err_t err = wifi_manager_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Manager: %s", esp_err_to_name(err));
        return err;
    }

    wifi_manager_.add_credentials(WIFI_SSID, WIFI_PASS);

    err = wifi_manager_.start(10000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi driver: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi initialized and started (ESP-NOW ready, connection on-demand for OTA)");
    return ESP_OK;
}

void PumpController::task_entry(void* arg)
{
    auto* self = static_cast<PumpController*>(arg);
    self->run_task();
}

void PumpController::run_task()
{
    const uint32_t loop_period_ms = 50;
    while (is_running_) {
        tick(loop_period_ms);
        hal_rtos_.task_delay(pdMS_TO_TICKS(loop_period_ms));
    }
    hal_rtos_.task_delete(nullptr);
}
