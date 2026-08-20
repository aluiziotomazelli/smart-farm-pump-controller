// main/include/interfaces/i_pump_status_reporter.hpp
#pragma once

#include "esp_err.h"

/**
 * @interface IPumpStatusReporter
 * @brief Abstract interface for telemetry status reporting to the central hub.
 */
class IPumpStatusReporter
{
public:
    virtual ~IPumpStatusReporter() = default;

    /**
     * @brief Initializes the status reporter.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Formats and immediately transmits a LoadControlStatus report via ESP-NOW.
     * @param require_ack True if transport layer logical ACK is required.
     * @return ESP_OK on success, or ESP-IDF error code.
     */
    virtual esp_err_t send_status_report(bool require_ack = false) = 0;

    /**
     * @brief Periodic tick handler for heartbeat timing.
     * @param delta_ms Elapsed time in milliseconds since last tick.
     */
    virtual void tick(uint32_t delta_ms) = 0;

    /**
     * @brief Notifies the reporter that a state transition occurred, triggering an immediate report.
     */
    virtual void notify_state_change() = 0;
};
