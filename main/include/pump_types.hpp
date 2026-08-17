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

/**
 * @struct PumpStatusReporterConfig
 * @brief Configuration parameters for periodic and event-driven status reporting.
 */
struct PumpStatusReporterConfig
{
    uint8_t circuit_id{0};                            ///< Circuit ID to report (default 0)
    uint32_t heartbeat_interval_ms{5000};             ///< Periodic reporting interval (default 5s)
    farm::NodeId dest_node_id{farm::NodeId::HUB};     ///< Destination node (Hub)
    bool require_ack{false};                          ///< True if telemetry requires transport ACK
};
