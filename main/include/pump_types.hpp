// main/include/pump_types.hpp
#pragma once

#include <cstdint>
#include "farm_protocol_types.hpp"

/**
 * @struct PumpStateSnapshot
 * @brief Current operational snapshot of the pump state machine.
 */
struct PumpStateSnapshot
{
    farm::LoadState state{farm::LoadState::IDLE};
    farm::ControlMode mode{farm::ControlMode::AUTO};
    farm::PowerSource source{farm::PowerSource::UNKNOWN};
    uint32_t runtime_s{0};
    uint32_t remaining_watchdog_s{0};
    bool state_changed{false};
};

/**
 * @struct PumpStateMachineConfig
 * @brief Tuning parameters for pump safety and state transitions.
 */
struct PumpStateMachineConfig
{
    uint32_t default_watchdog_s{3600};     ///< Default watchdog timeout if 0 is passed
    uint32_t demagnetization_delay_ms{150}; ///< Delay between de-energizing one and energizing another
    bool enable_output_validation{false};   ///< If true, queries IOutputMonitor before/after actuation
};
