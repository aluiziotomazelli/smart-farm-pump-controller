// host_test/test_pump_command_handler/main/test_pump_command_handler.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

#include "pump_command_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_pump_state_machine.hpp"
#include "mock_time_manager.hpp"
#include "mock_tank_level_display.hpp"
#include "mock_hal_freertos.hpp"
#include "farm_protocol_types.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class PumpCommandHandlerTest : public ::testing::Test
{
protected:
    NiceMock<espnow::MockEspNowManager> espnow_;
    NiceMock<MockPumpStateMachine> state_machine_;
    NiceMock<time_manager::MockTimeManager> time_manager_;
    NiceMock<MockTankLevelDisplay> tank_display_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);

    std::unique_ptr<PumpCommandHandler> sut_;

    void SetUp() override
    {
        ON_CALL(espnow_, confirm_reception(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(state_machine_, handle_load_on(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(state_machine_, handle_load_off(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(time_manager_, sync_from_time_packet(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(time_manager_, is_synchronized()).WillByDefault(Return(true));
        ON_CALL(time_manager_, get_timestamp_ms()).WillByDefault(Return(1700000000000ULL));

        sut_ = std::make_unique<PumpCommandHandler>(
            dummy_queue_,
            espnow_,
            state_machine_,
            time_manager_,
            tank_display_,
            hal_rtos_);
    }

    void SetupQueueWithMessages(const std::vector<espnow::AppMessage>& messages)
    {
        auto it = std::make_shared<size_t>(0);
        auto msgs = std::make_shared<std::vector<espnow::AppMessage>>(messages);

        ON_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
            .WillByDefault([it, msgs](QueueHandle_t, void* buf, TickType_t) -> BaseType_t {
                if (*it < msgs->size()) {
                    std::memcpy(buf, &(*msgs)[*it], sizeof(espnow::AppMessage));
                    (*it)++;
                    return pdTRUE;
                }
                return pdFALSE;
            });
    }
};

TEST_F(PumpCommandHandlerTest, NullQueueReturnsDefaultResult)
{
    PumpCommandHandler null_handler(
        nullptr,
        espnow_,
        state_machine_,
        time_manager_,
        tank_display_,
        hal_rtos_);

    auto res = null_handler.process();
    EXPECT_FALSE(res.time_synced);
    EXPECT_FALSE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
}

TEST_F(PumpCommandHandlerTest, EmptyQueueReturnsDefaultResult)
{
    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0)).WillOnce(Return(pdFALSE));

    auto res = sut_->process();
    EXPECT_FALSE(res.time_synced);
    EXPECT_FALSE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
}

TEST_F(PumpCommandHandlerTest, ProcessStartOtaCommandSetsFlagAndAcks)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    msg.sequence_number = 42;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 42, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process();
    EXPECT_TRUE(res.ota_requested);
    EXPECT_FALSE(res.reboot_requested);
}

TEST_F(PumpCommandHandlerTest, ProcessRebootCommandSetsFlagAndAcks)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg.sequence_number = 100;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 100, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process();
    EXPECT_TRUE(res.reboot_requested);
}

TEST_F(PumpCommandHandlerTest, ProcessLoadOnCommandDispatchesToStateMachineAndAcksOk)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 180};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::LOAD_ON);
    msg.payload_len = sizeof(cmd);
    std::memcpy(msg.payload, &cmd, sizeof(cmd));
    msg.sequence_number = 55;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(state_machine_, handle_load_on(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 55, espnow::AckStatus::OK)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, ProcessLoadOnCommandDispatchesToStateMachineAndAcksErrorProcessingOnFailure)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::LOAD_ON);
    msg.payload_len = sizeof(cmd);
    std::memcpy(msg.payload, &cmd, sizeof(cmd));
    msg.sequence_number = 77;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(state_machine_, handle_load_on(_)).WillOnce(Return(ESP_ERR_INVALID_STATE));
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 77, espnow::AckStatus::ERROR_PROCESSING)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, ProcessLoadOffCommandDispatchesToStateMachineAndAcks)
{
    farm::LoadOffCommand cmd{.circuit_id = 0};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::LOAD_OFF);
    msg.payload_len = sizeof(cmd);
    std::memcpy(msg.payload, &cmd, sizeof(cmd));
    msg.sequence_number = 88;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(state_machine_, handle_load_off(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 88, espnow::AckStatus::OK)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, ProcessSyncTimeCommandSynchronizesTimeAndReturnsTimeSynced)
{
    time_manager::TimeSyncPacket packet{
        .timestamp_ms = 1700000000000ULL,
        .tz_offset_min = -180,
        .sync_source = time_manager::TimeSyncSource::SNTP,
        .flags = 1};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.payload_len = sizeof(packet);
    std::memcpy(msg.payload, &packet, sizeof(packet));
    msg.sequence_number = 99;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(time_manager_, sync_from_time_packet(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 99, espnow::AckStatus::OK)).Times(1);

    auto res = sut_->process();
    EXPECT_TRUE(res.time_synced);
}

TEST_F(PumpCommandHandlerTest, ProcessTankLevelUpdateDispatchesToDisplayAndAcks)
{
    farm::TankLevelUpdate update{
        .tank_id = 1,
        .level_permille = 750,
        .backup_mode_active = false,
        .float_switch_is_full = false};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::DATA;
    msg.payload_type = static_cast<uint8_t>(farm::PayloadType::TANK_LEVEL_UPDATE);
    msg.payload_len = sizeof(update);
    std::memcpy(msg.payload, &update, sizeof(update));
    msg.sequence_number = 120;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(tank_display_, set_level(750, false, false)).Times(1);
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 120, espnow::AckStatus::OK)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, ProcessTankLevelUpdate_WithBackupMode_DispatchesToDisplayAndAcks)
{
    farm::TankLevelUpdate update{
        .tank_id = 0,
        .level_permille = 0,
        .backup_mode_active = true,
        .float_switch_is_full = true};

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::DATA;
    msg.payload_type = static_cast<uint8_t>(farm::PayloadType::TANK_LEVEL_UPDATE);
    msg.payload_len = sizeof(update);
    std::memcpy(msg.payload, &update, sizeof(update));
    msg.sequence_number = 121;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(tank_display_, set_level(0, true, true)).Times(1);
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 121, espnow::AckStatus::OK)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, InvalidPayloadLengthSendsErrorInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::LOAD_ON);
    msg.payload_len = 1; // Corrupted length
    msg.sequence_number = 12;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(state_machine_, handle_load_on(_)).Times(0);
    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 12, espnow::AckStatus::ERROR_INVALID_DATA)).Times(1);

    sut_->process();
}

TEST_F(PumpCommandHandlerTest, UnknownCommandSendsErrorInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<espnow::NodeId>(farm::NodeId::HUB);
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = 0xFE; // Unknown
    msg.sequence_number = 13;
    msg.requires_ack = true;

    SetupQueueWithMessages({msg});

    EXPECT_CALL(espnow_, confirm_reception(msg.sender_id, 13, espnow::AckStatus::ERROR_INVALID_DATA)).Times(1);

    sut_->process();
}
