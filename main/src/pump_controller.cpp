// main/src/pump_controller.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

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
#include "pump_controller.hpp"
#include "farm_protocol_types.hpp"

static const char* TAG = "PumpController";

PumpController::PumpController(
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
    idf_hals::ISystemHAL& hal_system)
    : state_machine_(state_machine)
    , command_handler_(command_handler)
    , status_reporter_(status_reporter)
    , led_controller_(led_controller)
    , tank_display_(tank_display)
    , switch_mode_(switch_mode)
    , switch_source_(switch_source)
    , button_start_(button_start)
    , button_stop_(button_stop)
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
    esp_err_t err = state_machine_.init();
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

    err = button_start_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init start button: %s", esp_err_to_name(err));
        return err;
    }

    err = button_stop_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init stop button: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PumpController initialized successfully");
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
    BaseType_t res = hal_rtos_.task_create(
        task_entry,
        "pump_ctrl_task",
        4096,
        this,
        5,
        &task_handle_);

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

void PumpController::tick(uint32_t delta_ms)
{
    // 1. Sample Switch Inputs
    switch_mode_.update();
    farm::ControlMode mode = (switch_mode_.get_state() == ui_inputs::SwitchState::CLOSED)
                                 ? farm::ControlMode::AUTO
                                 : farm::ControlMode::MANUAL;
    state_machine_.set_control_mode(mode);

    switch_source_.update();
    farm::PowerSource source = (switch_source_.get_state() == ui_inputs::SwitchState::CLOSED)
                                   ? farm::PowerSource::SOLAR
                                   : farm::PowerSource::GRID;

    // 2. Sample Manual Action Buttons
    button_start_.update();
    button_stop_.update();

    if (mode == farm::ControlMode::MANUAL) {
        if (button_start_.get_last_click() != ui_inputs::ButtonClickType::NONE_CLICK) {
            ESP_LOGI(TAG, "Manual START clicked for source %d", static_cast<int>(source));
            state_machine_.handle_manual_start(source);
        }
        if (button_stop_.get_last_click() != ui_inputs::ButtonClickType::NONE_CLICK) {
            ESP_LOGI(TAG, "Manual STOP clicked");
            state_machine_.handle_manual_stop();
        }
    }

    // 3. Process Inbound Remote Commands
    PumpCommandProcessResult cmd_res = command_handler_.process();
    if (cmd_res.reboot_requested) {
        ESP_LOGW(TAG, "Reboot requested via command; stopping actuator and restarting...");
        state_machine_.handle_manual_stop();
        hal_rtos_.task_delay(pdMS_TO_TICKS(100));
        hal_system_.restart();
        return;
    }

    // 4. Tick Subsystems
    state_machine_.tick(delta_ms);
    status_reporter_.tick(delta_ms);

    // 5. Update Visual Feedback
    auto snapshot = state_machine_.get_snapshot();
    led_controller_.update(snapshot.state, snapshot.source);
}
