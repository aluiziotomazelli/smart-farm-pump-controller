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
#include "interfaces/i_tank_strip_display.hpp"
#include "interfaces/i_switch.hpp"
#include "interfaces/i_button.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_pump_nvs.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "interfaces/i_ota_controller.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "pump_command_handler.hpp"
#include "secrets.hpp"

static const char* TAG = "PumpController";

PumpController::PumpController(
    INvsCore& core_storage,
    IPumpNvs& pump_storage,
    IPumpStateMachine& state_machine,
    PumpCommandHandler& command_handler,
    IPumpStatusReporter& status_reporter,
    ITankStripDisplay& display,
    ui_inputs::ISwitch& switch_mode,
    ui_inputs::ISwitch& switch_source,
    ui_inputs::IButton& button_action,
    wifi_manager::IWiFiManager& wifi_manager,
    IOtaController& ota_controller,
    IOtaTrigger& btn_trigger,
    espnow::IEspNowManager& espnow,
    idf_hals::IHalFreertos& hal_rtos,
    idf_hals::ISystemHAL& hal_system)
    : core_storage_(core_storage)
    , pump_storage_(pump_storage)
    , state_machine_(state_machine)
    , command_handler_(command_handler)
    , status_reporter_(status_reporter)
    , display_(display)
    , switch_mode_(switch_mode)
    , switch_source_(switch_source)
    , button_action_(button_action)
    , wifi_manager_(wifi_manager)
    , ota_controller_(ota_controller)
    , btn_trigger_(btn_trigger)
    , espnow_(espnow)
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
    bool session_healthy = true;

    esp_err_t err = init_core_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init core storage: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = init_pump_storage();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init pump storage: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = init_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %s", esp_err_to_name(err));
        session_healthy = false;
    }

    err = init_ota();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init OTA: %s", esp_err_to_name(err));
        session_healthy = false;
    }

    update_running_version();

    command_handler_.set_core_data(core_);

    err = state_machine_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init state machine: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = status_reporter_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init status reporter: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = display_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init tank display: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = switch_mode_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init mode switch: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = switch_source_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init source switch: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = button_action_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init action button: %s", esp_err_to_name(err));
        session_healthy = false;
        return err;
    }

    err = btn_trigger_.arm(*this);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to arm Boot Button OTA trigger: %s", esp_err_to_name(err));
        session_healthy = false;
    }

    // Perform post-boot firmware verification after all subsystems initialized
    if (!check_firmware_health(session_healthy)) {
        return ESP_FAIL;
    }

    // Trigger visual self-test sweep on every successful boot
    display_.set_override_pattern(TankStripPattern::BOOT_SUCCESS);

    ESP_LOGI(
        TAG,
        "PumpController initialized successfully (Boot #%lu, Runtime: %lu s)",
        static_cast<unsigned long>(core_.boot_count),
        static_cast<unsigned long>(stats_.total_runtime_s));
    return ESP_OK;
}

esp_err_t PumpController::start()
{
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
    btn_trigger_.disarm();
    display_.clear();
    ESP_LOGI(TAG, "PumpController stopped");
}

void PumpController::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
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

    if (cmd_res.ota_requested || ota_triggered_) {
        process_pending_ota();
        return;
    }

    // 4. Tick Subsystems
    state_machine_.tick(delta_ms);
    status_reporter_.tick(delta_ms);

    // 5. Update Visual Feedback on Addressable Strip & Runtime Accounting
    auto snapshot = state_machine_.get_snapshot();
    display_.update_state(snapshot.state, mode, snapshot.source);
    display_.tick(delta_ms);

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

esp_err_t PumpController::init_ota()
{
    OtaConfig ota_cfg{};
    ota_cfg.device_type = "pump_controller";
    ota_cfg.manifest_url = SERVER_URL;
    ota_cfg.task_stack_size = 8192;
    ota_cfg.task_priority = 5;
    ota_cfg.transport.manifest_timeout_ms = 10000;
    ota_cfg.transport.firmware_timeout_ms = 30000;
    ota_cfg.security.allow_http_during_development = true;
    ota_cfg.allow_same_version = true;
    ota_cfg.restart_on_success = false;

    if (!ota_controller_.init(ota_cfg)) {
        ESP_LOGE(TAG, "Failed to initialize OtaController");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void PumpController::update_running_version()
{
    auto current_version = ota_controller_.get_running_version();
    if (current_version.has_value()) {
        if (core_.fw_major != current_version->major || core_.fw_minor != current_version->minor ||
            core_.fw_patch != current_version->patch) {
            core_.fw_major = current_version->major;
            core_.fw_minor = current_version->minor;
            core_.fw_patch = current_version->patch;
            pending_core_commit_ = true;
        }
    }
    ESP_LOGI(TAG, "Running firmware version: %u.%u.%u", core_.fw_major, core_.fw_minor, core_.fw_patch);
}

bool PumpController::check_firmware_health(bool session_healthy)
{
    if (!ota_controller_.check_pending_verify()) {
        return true;
    }

    ESP_LOGI(TAG, "Pending OTA verification detected on boot; confirming firmware...");
    OtaActionResult result = ota_controller_.confirm_firmware(session_healthy);
    send_ota_report(result.exec_result, result.error_code);

    if (result.success) {
        pending_core_commit_ = true;
        save_persistent_state(true);
        return true;
    }

    display_.set_override_pattern(TankStripPattern::BOOT_ERROR);
    ESP_LOGE(TAG, "Post-boot OTA verification failed! Triggering rollback and reboot...");
    for (int i = 0; i < 15; i++) {
        display_.tick(100);
        hal_rtos_.task_delay(pdMS_TO_TICKS(100));
    }
    ota_controller_.rollback_and_reboot();
    return false;
}

void PumpController::process_pending_ota()
{
    ota_triggered_ = false;
    ESP_LOGI(TAG, "Processing pending OTA update...");

    // Disarm trigger while executing OTA
    btn_trigger_.disarm();

    // 1. Safety Interlock: Stop pump immediately before firmware flash
    state_machine_.handle_manual_stop();

    // 2. Visual feedback on addressable strip
    display_.set_override_pattern(TankStripPattern::OTA_UPDATING);
    display_.tick(0);

    // 3. Connect to WiFi with sync retries
    ESP_LOGI(TAG, "Connecting to WiFi for OTA download (timeout: 15000 ms, max_retries: 3)...");
    esp_err_t wifi_err = wifi_manager_.connect(15000, 3, 1500);
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi for OTA (%s)", esp_err_to_name(wifi_err));
        send_ota_report(farm::OtaExecResult::DOWNLOAD_FAILED, farm::OtaErrorCode::WIFI_CONNECT_FAILED);
        display_.set_override_pattern(TankStripPattern::AUTO);
        btn_trigger_.arm(*this);
        return;
    }

    // 4. Execute OTA download
    OtaActionResult result = ota_controller_.execute_download();
    if (result.success) {
        ESP_LOGI(TAG, "OTA download succeeded! Persisting state and restarting...");
        pending_core_commit_ = true;
        pending_controller_commit_ = true;
        save_persistent_state(true);
        wifi_manager_.disconnect(2000);
        wifi_manager_.stop(2000);
        hal_system_.restart();
        return;
    }
    else {
        ESP_LOGE(TAG, "OTA download failed (error_code: %d)", static_cast<int>(result.error_code));
        send_ota_report(result.exec_result, result.error_code);
        display_.set_override_pattern(TankStripPattern::AUTO);
        wifi_manager_.disconnect(2000);
        btn_trigger_.arm(*this);
    }
}

esp_err_t PumpController::send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code)
{
    farm::OtaStatusReport report{};
    report.power_profile = core_.power_profile;
    report.result = result;
    report.error_code = error_code;
    report.fw_major = core_.fw_major;
    report.fw_minor = core_.fw_minor;
    report.fw_patch = core_.fw_patch;

    ESP_LOGI(
        TAG,
        "Sending OTA status report to Hub: result=%u, error_code=%u (FW v%u.%u.%u)",
        static_cast<uint8_t>(result),
        static_cast<uint8_t>(error_code),
        core_.fw_major,
        core_.fw_minor,
        core_.fw_patch);

    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT),
        &report,
        sizeof(report),
        true);
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
