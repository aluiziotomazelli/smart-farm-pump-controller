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
    control_mode_ = farm::ControlMode::AUTO;
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
    if (control_mode_ == farm::ControlMode::MANUAL_RUN || control_mode_ == farm::ControlMode::FULL_MANUAL) {
        ESP_LOGW(TAG, "Rejecting LOAD_ON: In manual or bypass mode (mode: %d)", static_cast<int>(control_mode_));
        return ESP_ERR_INVALID_STATE;
    }

    farm::PowerSource target_source = (locked_source_ != farm::PowerSource::UNKNOWN && locked_source_ != farm::PowerSource::AUTO)
                                          ? locked_source_
                                          : cmd.power_source;

    if (target_source != farm::PowerSource::SOLAR && target_source != farm::PowerSource::GRID) {
        ESP_LOGE(TAG, "Rejecting LOAD_ON: Invalid power source");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timeout_s = (cmd.watchdog_timeout_s > 0) ? cmd.watchdog_timeout_s : config_.default_watchdog_s;

    if (state_ == farm::LoadState::RUNNING && active_source_ == target_source) {
        // Watchdog refresh
        remaining_watchdog_ms_ = timeout_s * 1000;
        ESP_LOGI(TAG, "Watchdog refreshed to %lu s", static_cast<unsigned long>(timeout_s));
        return ESP_OK;
    }

    if (config_.enable_output_validation && monitor_.has_any_output_energy()) {
        ESP_LOGE(TAG, "Output already energized before activation - potential stuck contactor");
        transition_to(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::PowerSource::UNKNOWN);
        contactor_.deactivate();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = contactor_.activate(target_source);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate contactor: %s", esp_err_to_name(err));
        transition_to(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::UNKNOWN);
        return err;
    }

    runtime_s_ = 0;
    runtime_ms_accum_ = 0;
    remaining_watchdog_ms_ = timeout_s * 1000;
    control_mode_ = farm::ControlMode::AUTO;
    transition_to(farm::LoadState::RUNNING, target_source);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_load_off(const farm::LoadOffCommand& cmd)
{
    (void)cmd;
    if (control_mode_ == farm::ControlMode::FULL_MANUAL) {
        ESP_LOGW(TAG, "Rejecting LOAD_OFF: In FULL_MANUAL mode");
        return ESP_ERR_INVALID_STATE;
    }

    contactor_.deactivate();
    remaining_watchdog_ms_ = 0;
    control_mode_ = farm::ControlMode::AUTO;
    transition_to(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_operator_start(farm::PowerSource source)
{
    if (source != farm::PowerSource::SOLAR && source != farm::PowerSource::GRID) {
        ESP_LOGE(TAG, "Rejecting operator start: Invalid power source");
        return ESP_ERR_INVALID_ARG;
    }

    if (config_.enable_output_validation && monitor_.has_any_output_energy()) {
        ESP_LOGE(TAG, "Output already energized before operator activation");
        transition_to(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::PowerSource::UNKNOWN);
        contactor_.deactivate();
        return ESP_ERR_INVALID_STATE;
    }

    if (state_ == farm::LoadState::RUNNING && active_source_ == source) {
        control_mode_ = farm::ControlMode::MANUAL_RUN;
        state_changed_ = true;
        return ESP_OK;
    }

    esp_err_t err = contactor_.activate(source);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to activate contactor on operator start: %s", esp_err_to_name(err));
        transition_to(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::UNKNOWN);
        return err;
    }

    runtime_s_ = 0;
    runtime_ms_accum_ = 0;
    remaining_watchdog_ms_ = 0; // No watchdog on local operator activation
    control_mode_ = farm::ControlMode::MANUAL_RUN;
    transition_to(farm::LoadState::RUNNING, source);
    return ESP_OK;
}

esp_err_t PumpStateMachine::handle_operator_stop()
{
    contactor_.deactivate();
    remaining_watchdog_ms_ = 0;
    control_mode_ = farm::ControlMode::AUTO;
    transition_to(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN);
    return ESP_OK;
}

void PumpStateMachine::set_source_lock(farm::PowerSource source)
{
    if (locked_source_ == source) {
        return;
    }

    locked_source_ = source;
    ESP_LOGI(TAG, "Locked source updated to: %d", static_cast<int>(source));

    if (locked_source_ == farm::PowerSource::UNKNOWN || locked_source_ == farm::PowerSource::AUTO) {
        if (control_mode_ == farm::ControlMode::MANUAL_RUN) {
            ESP_LOGI(TAG, "Source lock released (switched to AUTO) -> returning control_mode to AUTO");
            control_mode_ = farm::ControlMode::AUTO;
        }
    }
    else if (state_ == farm::LoadState::RUNNING) {
        if (active_source_ != locked_source_) {
            ESP_LOGI(TAG, "Hot-switching source from %d to %d while RUNNING", static_cast<int>(active_source_), static_cast<int>(locked_source_));
            esp_err_t err = contactor_.activate(locked_source_);
            if (err == ESP_OK) {
                active_source_ = locked_source_;
                state_changed_ = true;
            } else {
                ESP_LOGE(TAG, "Failed to hot-switch source: %s", esp_err_to_name(err));
                transition_to(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::UNKNOWN);
            }
        }
    }
    state_changed_ = true;
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

        // Watchdog countdown (active whenever remaining_watchdog_ms_ > 0)
        if (remaining_watchdog_ms_ > 0) {
            if (delta_ms >= remaining_watchdog_ms_) {
                remaining_watchdog_ms_ = 0;
                ESP_LOGW(TAG, "Watchdog timeout expired! Deactivating pump.");
                contactor_.deactivate();
                control_mode_ = farm::ControlMode::AUTO;
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
    snapshot.selected_source = (locked_source_ == farm::PowerSource::UNKNOWN) ? farm::PowerSource::AUTO : locked_source_;
    snapshot.active_source = active_source_;
    snapshot.power_w = (state_ == farm::LoadState::RUNNING) ? config_.nominal_power_w : 0;
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
