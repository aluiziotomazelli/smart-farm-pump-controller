// main/include/pump_stats.hpp
#pragma once

#include <cstdint>

#include "app_storage.hpp"

// =============================
//  Pump Storage Constants
// =============================
static constexpr uint32_t PUMP_STATS_MAGIC = 0x50554D50; ///< "PUMP" in ASCII
static constexpr uint8_t PUMP_STATS_VERSION = 1;

/**
 * @struct PumpStats
 * @brief Pure domain struct representing operational statistics and mechanical wear for the Pump Controller.
 *
 * Contains no storage metadata (magic, version, crc) which are managed by the AppStorage envelope.
 */
struct PumpStats
{
    // Hourmeter (Lifetime Accumulated Operation)
    uint32_t total_runtime_s{0};        ///< Total seconds pump has operated across all sources
    uint32_t solar_runtime_s{0};        ///< Total seconds operated on Solar power
    uint32_t grid_runtime_s{0};         ///< Total seconds operated on Grid power

    // Lifecycle & Wear Metrics
    uint32_t start_cycles_total{0};     ///< Total number of pump start commutations
    uint32_t manual_starts_total{0};    ///< Number of starts initiated via manual physical button
    uint32_t auto_starts_total{0};      ///< Number of starts initiated remotely by Hub

    // Fault & Anomaly Counters
    uint32_t watchdog_timeouts_total{0};///< Total occurrences of safety watchdog timeouts
    uint32_t contactor_faults_total{0}; ///< Contactor stuck / pre-energized anomalies detected

    void reset()
    {
        *this = {};
    }

    bool operator==(const PumpStats& other) const
    {
        return total_runtime_s == other.total_runtime_s &&
               solar_runtime_s == other.solar_runtime_s &&
               grid_runtime_s == other.grid_runtime_s &&
               start_cycles_total == other.start_cycles_total &&
               manual_starts_total == other.manual_starts_total &&
               auto_starts_total == other.auto_starts_total &&
               watchdog_timeouts_total == other.watchdog_timeouts_total &&
               contactor_faults_total == other.contactor_faults_total;
    }

    bool operator!=(const PumpStats& other) const { return !(*this == other); }
};

/**
 * @brief Storage envelope alias used for allocating physical RTC/NVS storage buffers.
 */
using PumpStorage = StorageEnvelope<PumpStats, PUMP_STATS_MAGIC, PUMP_STATS_VERSION>;
