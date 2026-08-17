// host_test/test_pump_status_reporter/main/test_pump_status_reporter.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <cstring>

#include "pump_status_reporter.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_pump_state_machine.hpp"
#include "mock_hal_timer.hpp"
#include "farm_protocol_types.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

class PumpStatusReporterTest : public ::testing::Test
{
protected:
    NiceMock<espnow::MockEspNowManager> espnow_;
    NiceMock<MockPumpStateMachine> state_machine_;
    NiceMock<idf_hals::MockTimerHAL> hal_timer_;
    PumpStatusReporterConfig config_{
        .circuit_id = 0,
        .heartbeat_interval_ms = 5000,
        .dest_node_id = farm::NodeId::HUB,
        .require_ack = false};

    std::unique_ptr<PumpStatusReporter> sut_;

    void SetUp() override
    {
        ON_CALL(espnow_, send_data(_, _, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(3600000000LL)); // 3600s = 1 hr

        PumpStateSnapshot default_snapshot{
            .state = farm::LoadState::RUNNING,
            .mode = farm::ControlMode::AUTO,
            .source = farm::PowerSource::SOLAR,
            .runtime_s = 120,
            .remaining_watchdog_s = 180,
            .state_changed = false};
        ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(default_snapshot));
        ON_CALL(state_machine_, consume_state_changed()).WillByDefault(Return(false));

        sut_ = std::make_unique<PumpStatusReporter>(espnow_, state_machine_, hal_timer_, config_);
    }
};

TEST_F(PumpStatusReporterTest, InitReturnsOk)
{
    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpStatusReporterTest, SendStatusReportFormatsAndTransmitsPayload)
{
    farm::LoadControlStatus captured_status{};

    EXPECT_CALL(
        espnow_,
        send_data(
            static_cast<espnow::NodeId>(farm::NodeId::HUB),
            static_cast<espnow::PayloadType>(farm::PayloadType::LOAD_CONTROL_STATUS),
            _,
            sizeof(farm::LoadControlStatus),
            false))
        .WillOnce([&captured_status](espnow::NodeId, espnow::PayloadType, const void* payload, size_t len, bool) {
            std::memcpy(&captured_status, payload, len);
            return ESP_OK;
        });

    EXPECT_EQ(sut_->send_status_report(), ESP_OK);
    EXPECT_EQ(captured_status.circuit_id, 0);
    EXPECT_EQ(captured_status.power_profile, farm::PowerProfile::ALWAYS_ON);
    EXPECT_EQ(captured_status.control_mode, farm::ControlMode::AUTO);
    EXPECT_EQ(captured_status.active_power_source, farm::PowerSource::SOLAR);
    EXPECT_EQ(captured_status.load_state, farm::LoadState::RUNNING);
    EXPECT_EQ(captured_status.runtime_s, 120);
    EXPECT_EQ(captured_status.uptime_s, 3600);
}

TEST_F(PumpStatusReporterTest, HeartbeatTickTriggersPeriodicSend)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(4000); // 4s < 5s interval

    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(1);
    sut_->tick(1000); // 5s reached
}

TEST_F(PumpStatusReporterTest, StateChangeDetectedOnTickSendsImmediatelyAndResetsHeartbeat)
{
    EXPECT_CALL(state_machine_, consume_state_changed()).WillOnce(Return(true));
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(1);

    sut_->tick(100); // Triggered immediately due to state change

    // Heartbeat timer was reset, so 4000ms later it shouldn't send yet
    EXPECT_CALL(state_machine_, consume_state_changed()).WillRepeatedly(Return(false));
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(4000);

    // After remaining 1000ms, heartbeat triggers
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(1);
    sut_->tick(1000);
}

TEST_F(PumpStatusReporterTest, ExplicitNotifyStateChangeTriggersImmediateSend)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(1);
    sut_->notify_state_change();
}

TEST_F(PumpStatusReporterTest, SendStatusReportPropagatesEspNowError)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(sut_->send_status_report(), ESP_FAIL);
}
