// main/include/pump_status_reporter.hpp
#pragma once

#include <cstdint>

#include "interfaces/i_pump_status_reporter.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "pump_types.hpp"

/**
 * @class PumpStatusReporter
 * @brief Manages periodic heartbeat telemetry and event-driven status updates over ESP-NOW.
 */
class PumpStatusReporter : public IPumpStatusReporter
{
public:
    PumpStatusReporter(
        espnow::IEspNowManager& espnow,
        IPumpStateMachine& state_machine,
        idf_hals::ITimerHAL& hal_timer,
        const PumpStatusReporterConfig& config = PumpStatusReporterConfig{});

    ~PumpStatusReporter() override = default;

    /** @copydoc IPumpStatusReporter::init */
    esp_err_t init() override;

    /** @copydoc IPumpStatusReporter::send_status_report */
    esp_err_t send_status_report() override;

    /** @copydoc IPumpStatusReporter::tick */
    void tick(uint32_t delta_ms) override;

    /** @copydoc IPumpStatusReporter::notify_state_change */
    void notify_state_change() override;

private:
    espnow::IEspNowManager& espnow_;
    IPumpStateMachine& state_machine_;
    idf_hals::ITimerHAL& hal_timer_;
    PumpStatusReporterConfig config_;

    uint32_t elapsed_since_last_send_ms_{0};

    farm::LoadControlStatus build_status_payload() const;
};
