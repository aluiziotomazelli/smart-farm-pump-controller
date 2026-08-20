#pragma once

#include "esp_err.h"
#include "farm_protocol_types.hpp"
#include "pump_types.hpp"

/**
 * @interface IPumpStateMachine
 * @brief Abstract interface defining pump control state machine operations.
 */
class IPumpStateMachine
{
public:
    virtual ~IPumpStateMachine() = default;

    /**
     * @brief Initializes state machine and puts actuators into safe IDLE state.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Processes a remote LOAD_ON command (valid in AUTO and SOURCE_LOCKED modes).
     * @param cmd Load activation command parameters.
     * @return ESP_OK if accepted, ESP_ERR_INVALID_STATE if in STOP_OVERRIDE/FULL_MANUAL or invalid source.
     */
    virtual esp_err_t handle_load_on(const farm::LoadOnCommand& cmd) = 0;

    /**
     * @brief Processes a remote LOAD_OFF command (valid in AUTO, SOURCE_LOCKED, and STOP_OVERRIDE modes).
     * @param cmd Load deactivation command parameters.
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE in FULL_MANUAL.
     */
    virtual esp_err_t handle_load_off(const farm::LoadOffCommand& cmd) = 0;

    /**
     * @brief Processes local operator start push button activation.
     * @param source Selected power source (GRID or SOLAR).
     * @return ESP_OK on success.
     */
    virtual esp_err_t handle_operator_start(farm::PowerSource source) = 0;

    /**
     * @brief Processes local operator stop push button deactivation.
     * @return ESP_OK on success.
     */
    virtual esp_err_t handle_operator_stop() = 0;

    /**
     * @brief Sets or clears the operator source lock based on hardware switch position.
     * @param source Selected power source (UNKNOWN for AUTO center position, SOLAR or GRID).
     */
    virtual void set_source_lock(farm::PowerSource source) = 0;

    /**
     * @brief Periodic tick handler for watchdog countdown and runtime accumulation.
     * @param delta_ms Elapsed time in milliseconds since last tick.
     */
    virtual void tick(uint32_t delta_ms) = 0;

    /**
     * @brief Returns current state machine state.
     */
    virtual farm::LoadState get_state() const = 0;

    /**
     * @brief Returns current control mode.
     */
    virtual farm::ControlMode get_control_mode() const = 0;

    /**
     * @brief Returns currently active power source.
     */
    virtual farm::PowerSource get_active_source() const = 0;

    /**
     * @brief Returns currently locked power source.
     */
    virtual farm::PowerSource get_locked_source() const = 0;

    /**
     * @brief Returns continuous runtime of current active cycle in seconds.
     */
    virtual uint32_t get_runtime_s() const = 0;

    /**
     * @brief Returns full state snapshot.
     */
    virtual PumpStateSnapshot get_snapshot() const = 0;

    /**
     * @brief Checks and clears the state_changed flag.
     * @return true if a state transition occurred since the last call.
     */
    virtual bool consume_state_changed() = 0;
};
