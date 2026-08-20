// main/include/pump_types.hpp
#pragma once

#include <cstdint>
#include "driver/gpio.h"
#include "farm_protocol_types.hpp"

/**
 * @struct ContactorConfig
 * @brief Hardware pinout and polarity configuration for contactor coils/gates.
 */
struct ContactorConfig
{
    gpio_num_t grid_gpio{GPIO_NUM_5};       ///< D3 on Xiao C3 (Grid Contactor Gate)
    gpio_num_t solar_gpio{GPIO_NUM_6};      ///< D4 on Xiao C3 (Solar Contactor Gate)
    uint8_t active_level{1};                ///< 1 for active-high (MOC/TRIAC), 0 for active-low (Relay)
    uint32_t demagnetization_delay_ms{150}; ///< Safety delay between switching sources
};

/**
 * @struct PumpStateSnapshot
 * @brief Current operational snapshot of the pump state machine.
 */
struct PumpStateSnapshot
{
    farm::LoadState state{farm::LoadState::IDLE};
    farm::ControlMode mode{farm::ControlMode::AUTO};
    farm::PowerSource source{farm::PowerSource::UNKNOWN};
    uint16_t power_w{0};
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
    uint16_t nominal_power_w{320};          ///< Nominal power consumption in Watts when active
};

/**
 * @struct PumpStatusReporterConfig
 * @brief Configuration parameters for periodic and event-driven status reporting.
 */
struct PumpStatusReporterConfig
{
    uint8_t circuit_id{0};                            ///< Circuit ID to report (default 0)
    uint32_t running_report_interval_ms{5000};        ///< Periodic reporting interval while pump is RUNNING (default 5s)
    farm::NodeId dest_node_id{farm::NodeId::HUB};     ///< Destination node (Hub)
    bool require_ack{false};                          ///< True if telemetry requires transport ACK
};
