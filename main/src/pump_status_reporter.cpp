// main/src/pump_status_reporter.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_pump_status_reporter.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "pump_status_reporter.hpp"
#include "farm_protocol_types.hpp"

static const char* TAG = "PumpStatusReporter";

PumpStatusReporter::PumpStatusReporter(
    espnow::IEspNowManager& espnow,
    IPumpStateMachine& state_machine,
    idf_hals::ITimerHAL& hal_timer,
    const PumpStatusReporterConfig& config)
    : espnow_(espnow)
    , state_machine_(state_machine)
    , hal_timer_(hal_timer)
    , config_(config)
{
}

esp_err_t PumpStatusReporter::init()
{
    elapsed_since_last_send_ms_ = 0;
    ESP_LOGI(TAG, "PumpStatusReporter initialized (heartbeat: %lu ms, dest: 0x%02X)",
             config_.heartbeat_interval_ms, static_cast<uint8_t>(config_.dest_node_id));
    return ESP_OK;
}

farm::LoadControlStatus PumpStatusReporter::build_status_payload() const
{
    auto snapshot = state_machine_.get_snapshot();
    int64_t time_us = hal_timer_.get_time_us();
    uint32_t uptime_s = (time_us > 0) ? static_cast<uint32_t>(time_us / 1000000) : 0;

    farm::LoadControlStatus status{};
    status.circuit_id = config_.circuit_id;
    status.power_profile = farm::PowerProfile::ALWAYS_ON;
    status.control_mode = snapshot.mode;
    status.active_power_source = snapshot.source;
    status.load_state = snapshot.state;
    status.runtime_s = snapshot.runtime_s;
    status.uptime_s = uptime_s;

    return status;
}

esp_err_t PumpStatusReporter::send_status_report()
{
    farm::LoadControlStatus status = build_status_payload();

    ESP_LOGI(TAG, "Sending LOAD_CONTROL_STATUS: state=%d, mode=%d, source=%d, runtime=%lu s, uptime=%lu s",
             static_cast<int>(status.load_state),
             static_cast<int>(status.control_mode),
             static_cast<int>(status.active_power_source),
             status.runtime_s,
             status.uptime_s);

    esp_err_t err = espnow_.send_data(
        static_cast<espnow::NodeId>(config_.dest_node_id),
        static_cast<espnow::PayloadType>(farm::PayloadType::LOAD_CONTROL_STATUS),
        &status,
        sizeof(status),
        config_.require_ack);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send LOAD_CONTROL_STATUS: %s", esp_err_to_name(err));
        return err;
    }

    elapsed_since_last_send_ms_ = 0;
    return ESP_OK;
}

void PumpStatusReporter::tick(uint32_t delta_ms)
{
    // Check if state machine reported a transition
    if (state_machine_.consume_state_changed()) {
        ESP_LOGI(TAG, "State transition detected on tick; sending immediate status report");
        send_status_report();
        return;
    }

    elapsed_since_last_send_ms_ += delta_ms;
    if (elapsed_since_last_send_ms_ >= config_.heartbeat_interval_ms) {
        ESP_LOGD(TAG, "Heartbeat timer expired; sending periodic status report");
        send_status_report();
    }
}

void PumpStatusReporter::notify_state_change()
{
    ESP_LOGI(TAG, "Explicit notify_state_change triggered");
    send_status_report();
}
