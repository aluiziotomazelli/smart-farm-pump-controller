#include <cstdint>
#include <cstring>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "pump_command_handler.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_tank_level_display.hpp"
#include "interfaces/i_hal_freertos.hpp"

static const char* TAG = "PumpCommandHandler";

PumpCommandHandler::PumpCommandHandler(
    QueueHandle_t rx_queue,
    espnow::IEspNowManager& espnow,
    IPumpStateMachine& state_machine,
    time_manager::ITimeManager& time_manager,
    ITankLevelDisplay& tank_display,
    idf_hals::IHalFreertos& hal_freertos,
    CoreData* core)
    : rx_queue_(rx_queue)
    , espnow_(espnow)
    , state_machine_(state_machine)
    , time_manager_(time_manager)
    , tank_display_(tank_display)
    , hal_freertos_(hal_freertos)
    , core_(core)
{
}

PumpCommandProcessResult PumpCommandHandler::process()
{
    PumpCommandProcessResult result{};

    if (rx_queue_ == nullptr) {
        return result;
    }

    espnow::AppMessage msg{};
    while (hal_freertos_.queue_receive(rx_queue_, &msg, 0) == pdTRUE) {
        if (msg.msg_type == espnow::MessageType::COMMAND) {
            process_command_message(msg, result);
        } else if (msg.msg_type == espnow::MessageType::DATA) {
            process_data_message(msg, result);
        } else {
            ESP_LOGW(TAG, "Ignoring unsupported message type 0x%02X from 0x%02X",
                     static_cast<uint8_t>(msg.msg_type), msg.sender_id);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    }

    return result;
}

void PumpCommandHandler::process_command_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result)
{
    uint8_t cmd_type = msg.payload_type;
    ESP_LOGI(TAG, "Processing command 0x%02X from sender 0x%02X", cmd_type, msg.sender_id);

    if (cmd_type == static_cast<uint8_t>(espnow::CommandType::START_OTA)) {
        result.ota_requested = true;
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
        }
    } else if (cmd_type == static_cast<uint8_t>(espnow::CommandType::REBOOT)) {
        result.reboot_requested = true;
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
        }
        ESP_LOGW(TAG, "REBOOT command received");
    } else if (cmd_type == static_cast<uint8_t>(farm::CommandType::LOAD_ON)) {
        if (msg.payload_len >= sizeof(farm::LoadOnCommand)) {
            const auto* cmd = reinterpret_cast<const farm::LoadOnCommand*>(msg.payload);
            esp_err_t err = state_machine_.handle_load_on(*cmd);
            if (msg.requires_ack) {
                espnow::AckStatus status = (err == ESP_OK) ? espnow::AckStatus::OK : espnow::AckStatus::ERROR_PROCESSING;
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, status);
            }
        } else {
            ESP_LOGE(TAG, "Invalid payload length for LOAD_ON: %zu", msg.payload_len);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    } else if (cmd_type == static_cast<uint8_t>(farm::CommandType::LOAD_OFF)) {
        if (msg.payload_len >= sizeof(farm::LoadOffCommand)) {
            const auto* cmd = reinterpret_cast<const farm::LoadOffCommand*>(msg.payload);
            esp_err_t err = state_machine_.handle_load_off(*cmd);
            if (msg.requires_ack) {
                espnow::AckStatus status = (err == ESP_OK) ? espnow::AckStatus::OK : espnow::AckStatus::ERROR_PROCESSING;
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, status);
            }
        } else {
            ESP_LOGE(TAG, "Invalid payload length for LOAD_OFF: %zu", msg.payload_len);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    } else if (cmd_type == static_cast<uint8_t>(farm::CommandType::SYNC_TIME)) {
        if (msg.payload_len >= sizeof(time_manager::TimeSyncPacket)) {
            const auto* packet = reinterpret_cast<const time_manager::TimeSyncPacket*>(msg.payload);
            esp_err_t err = time_manager_.sync_from_time_packet(*packet);
            if (err == ESP_OK) {
                if (core_ != nullptr) {
                    core_->has_valid_time = time_manager_.is_synchronized();
                    core_->last_sync_unix_time_ms = time_manager_.get_timestamp_ms();
                }
                result.core_modified = true;
                if (msg.requires_ack) {
                    espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
                }
            } else {
                ESP_LOGE(TAG, "Time synchronization failed: %s", esp_err_to_name(err));
                if (msg.requires_ack) {
                    espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_PROCESSING);
                }
            }
        } else {
            ESP_LOGE(TAG, "Invalid payload length for SYNC_TIME: %zu", msg.payload_len);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    } else {
        ESP_LOGW(TAG, "Unknown or unhandled command 0x%02X", cmd_type);
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
        }
    }
}

void PumpCommandHandler::process_data_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result)
{
    (void)result;
    uint8_t payload_type = msg.payload_type;

    if (payload_type == static_cast<uint8_t>(farm::PayloadType::TANK_LEVEL_UPDATE)) {
        if (msg.payload_len >= sizeof(farm::TankLevelUpdate)) {
            const auto* update = reinterpret_cast<const farm::TankLevelUpdate*>(msg.payload);
            ESP_LOGI(TAG, "Received TANK_LEVEL_UPDATE: tank %u, level %u permille", update->tank_id, update->level_permille);
            tank_display_.set_level(update->level_permille);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
            }
        } else {
            ESP_LOGE(TAG, "Invalid payload length for TANK_LEVEL_UPDATE: %zu", msg.payload_len);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    } else {
        ESP_LOGW(TAG, "Ignoring unexpected DATA payload type 0x%02X", payload_type);
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
        }
    }
}
