// main/src/pump_state_machine.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_contactor_controller.hpp"
#include "interfaces/i_output_monitor.hpp"
#include "pump_state_machine.hpp"
#include "pump_types.hpp"

static const char* TAG = "PumpStateMachine";

PumpStateMachine::PumpStateMachine(
    IContactorController& contactor,
    IOutputMonitor& monitor,
    const PumpStateMachineConfig& config)
    : contactor_(contactor)
    , monitor_(monitor)
    , config_(config)
{
}

esp_err_t PumpStateMachine::init()
{
    esp_err_t err = contactor_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize contactor controller: %s", esp_err_to_name(err));
        return err;
    }

    err = monitor_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize output monitor: %s", esp_err_to_name(err));
        return err;
    }

    contactor_.deactivate();
    state_ = farm::LoadState::IDLE;
    active_source_ = farm::PowerSource::UNKNOWN;
    runtime_s_ = 0;
    runtime_ms_accum_ = 0;
    remaining_watchdog_ms_ = 0;
    state_changed_ = true;

    ESP_LOGI(TAG, "PumpStateMachine initialized in IDLE state");
    return ESP_OK;
}

void PumpStateMachine::transition_to(farm::LoadState new_state, farm::PowerSource source)
{
    if (state_ != new_state || active_source_ != source) {
        state_ = new_state;
        active_source_ = source;
        state_changed_ = true;
        ESP_LOGI(TAG, "State transitioned to %d, source: %d", static_cast<int>(new_state), static_cast<int>(source));
    }
}

esp_err_t PumpStateMachine::handle_load_on(const farm::LoadOnCommand& cmd)
{
    if (control_mode_ != farm::ControlMode::AUTO) {
        ESP_LOGW(TAG, "Rejecting LOAD_ON: Not in AUTO mode");
        return ESP_ERR_INVALID_STATE;
    }

    if (cmd.power_source != farm::PowerSource::SOLAR && cmd.power_source != farm::PowerSource::GRID) {
        ESP_LOGE(TAG, "Rejecting LOAD_ON: Invalid power source");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timeout_s = (cmd.watchdog_timeout_s > 0) ? cmd.watchdog_timeout_s : config_.default_watchdog_s;

    if (state_ == farm::LoadState::RUNNING && active_source_ == cmd.power_source) {
        // Watchdog refresh
        remaining_watchdog_ms_ = timeout_s * 1000;
        ESP_LOGI(TAG, "Watchdog refreshed to %lu s", timeout_s);
        return ESP_OK;
    }

    if (config_.enable_output_validation && monitor_.has_any_output_energy()) {
        ESP_LOGE(TAG, "Output already energized before activation - potential stuck contactor");
        transition_to(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::PowerSource::UNKNOWN);
        contactor_.deactivate();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = contactor_.activate(cmd.power_source);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate contactor: %s", esp_err_to_name(err));
        transition_to(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::UNKNOWN);
        return err;
    }

    runtime_s_ = 0;
    runtime_ms_accum_ = 0;
    remaining_watchdog_ms_ = timeout_s * 1000;
    transition_to(farm::LoadState::RUNNING, cmd.power_source);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_load_off(const farm::LoadOffCommand& cmd)
{
    (void)cmd;
    if (control_mode_ != farm::ControlMode::AUTO) {
        ESP_LOGW(TAG, "Rejecting LOAD_OFF: Not in AUTO mode");
        return ESP_ERR_INVALID_STATE;
    }

    contactor_.deactivate();
    remaining_watchdog_ms_ = 0;
    transition_to(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_manual_start(farm::PowerSource source)
{
    if (control_mode_ != farm::ControlMode::MANUAL) {
        ESP_LOGW(TAG, "Rejecting manual start: Not in MANUAL mode");
        return ESP_ERR_INVALID_STATE;
    }

    if (source != farm::PowerSource::SOLAR && source != farm::PowerSource::GRID) {
        ESP_LOGE(TAG, "Rejecting manual start: Invalid power source");
        return ESP_ERR_INVALID_ARG;
    }

    if (config_.enable_output_validation && monitor_.has_any_output_energy()) {
        ESP_LOGE(TAG, "Output already energized before manual activation");
        transition_to(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::PowerSource::UNKNOWN);
        contactor_.deactivate();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = contactor_.activate(source);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to manually activate contactor: %s", esp_err_to_name(err));
        transition_to(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::UNKNOWN);
        return err;
    }

    runtime_s_ = 0;
    runtime_ms_accum_ = 0;
    remaining_watchdog_ms_ = 0; // No watchdog in MANUAL mode
    transition_to(farm::LoadState::RUNNING, source);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_manual_stop()
{
    contactor_.deactivate();
    remaining_watchdog_ms_ = 0;
    transition_to(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN);
    return ESP_OK;
}

void PumpStateMachine::set_control_mode(farm::ControlMode mode)
{
    if (control_mode_ != mode) {
        control_mode_ = mode;
        state_changed_ = true;
        ESP_LOGI(TAG, "Control mode changed to %d", static_cast<int>(mode));
    }
}

void PumpStateMachine::tick(uint32_t delta_ms)
{
    if (state_ == farm::LoadState::RUNNING) {
        // Accumulate runtime
        runtime_ms_accum_ += delta_ms;
        while (runtime_ms_accum_ >= 1000) {
            runtime_s_++;
            runtime_ms_accum_ -= 1000;
        }

        // AUTO mode watchdog countdown
        if (control_mode_ == farm::ControlMode::AUTO && remaining_watchdog_ms_ > 0) {
            if (delta_ms >= remaining_watchdog_ms_) {
                remaining_watchdog_ms_ = 0;
                ESP_LOGW(TAG, "Watchdog timeout expired! Deactivating pump.");
                contactor_.deactivate();
                transition_to(farm::LoadState::ERROR_TIMEOUT, farm::PowerSource::UNKNOWN);
            } else {
                remaining_watchdog_ms_ -= delta_ms;
            }
        }
    }
}

PumpStateSnapshot PumpStateMachine::get_snapshot() const
{
    PumpStateSnapshot snapshot;
    snapshot.state = state_;
    snapshot.mode = control_mode_;
    snapshot.source = active_source_;
    snapshot.runtime_s = runtime_s_;
    snapshot.remaining_watchdog_s = (remaining_watchdog_ms_ + 999) / 1000;
    snapshot.state_changed = state_changed_;
    return snapshot;
}

bool PumpStateMachine::consume_state_changed()
{
    bool changed = state_changed_;
    state_changed_ = false;
    return changed;
}
