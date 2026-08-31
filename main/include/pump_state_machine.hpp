#pragma once

#include <cstdint>

#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_contactor_controller.hpp"
#include "interfaces/i_output_monitor.hpp"
#include "pump_types.hpp"

/**
 * @class PumpStateMachine
 * @brief Core finite state machine managing dual-source contactors, safety watchdog and operational modes.
 */
class PumpStateMachine : public IPumpStateMachine
{
public:
    PumpStateMachine(
        IContactorController& contactor,
        IOutputMonitor& monitor,
        const PumpStateMachineConfig& config = PumpStateMachineConfig{});

    ~PumpStateMachine() override = default;

    /** @copydoc IPumpStateMachine::init */
    esp_err_t init() override;

    /** @copydoc IPumpStateMachine::handle_load_on */
    esp_err_t handle_load_on(const farm::LoadOnCommand& cmd) override;

    /** @copydoc IPumpStateMachine::handle_load_off */
    esp_err_t handle_load_off(const farm::LoadOffCommand& cmd) override;

    /** @copydoc IPumpStateMachine::handle_operator_start */
    esp_err_t handle_operator_start(farm::PowerSource source) override;

    /** @copydoc IPumpStateMachine::handle_operator_stop */
    esp_err_t handle_operator_stop() override;

    /** @copydoc IPumpStateMachine::set_source_lock */
    void set_source_lock(farm::PowerSource source) override;

    /** @copydoc IPumpStateMachine::tick */
    void tick(uint32_t delta_ms) override;

    /** @copydoc IPumpStateMachine::get_state */
    farm::LoadState get_state() const override { return state_; }

    /** @copydoc IPumpStateMachine::get_control_mode */
    farm::ControlMode get_control_mode() const override { return control_mode_; }

    /** @copydoc IPumpStateMachine::get_active_source */
    farm::PowerSource get_active_source() const override { return active_source_; }

    /** @copydoc IPumpStateMachine::get_locked_source */
    farm::PowerSource get_locked_source() const override { return locked_source_; }

    /** @copydoc IPumpStateMachine::get_runtime_s */
    uint32_t get_runtime_s() const override { return runtime_s_; }

    /** @copydoc IPumpStateMachine::get_snapshot */
    PumpStateSnapshot get_snapshot() const override;

    /** @copydoc IPumpStateMachine::consume_state_changed */
    bool consume_state_changed() override;

private:
    IContactorController& contactor_;
    IOutputMonitor& monitor_;
    PumpStateMachineConfig config_;

    farm::LoadState state_{farm::LoadState::IDLE};
    farm::ControlMode control_mode_{farm::ControlMode::AUTO};
    farm::PowerSource active_source_{farm::PowerSource::UNKNOWN};
    farm::PowerSource locked_source_{farm::PowerSource::UNKNOWN};

    uint32_t runtime_s_{0};
    uint32_t runtime_ms_accum_{0};
    uint32_t remaining_watchdog_ms_{0};
    uint32_t stabilization_delay_ms_{0};
    bool state_changed_{false};

    void transition_to(farm::LoadState new_state, farm::PowerSource source = farm::PowerSource::UNKNOWN);
};
