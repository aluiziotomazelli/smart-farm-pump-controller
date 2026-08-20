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
        .running_report_interval_ms = 5000,
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
            .power_w = 320,
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
    EXPECT_EQ(captured_status.power_w, 320);
    EXPECT_EQ(captured_status.runtime_s, 120);
    EXPECT_EQ(captured_status.uptime_s, 3600);
}

TEST_F(PumpStatusReporterTest, RunningTickTriggersPeriodicSendWithoutAckWhenRunning)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, false)).Times(0);
    sut_->tick(4000); // 4s < 5s interval

    // After 5s, sends without ACK (require_ack = false)
    EXPECT_CALL(espnow_, send_data(_, _, _, _, false)).Times(1);
    sut_->tick(1000);
}

TEST_F(PumpStatusReporterTest, IdleTickDoesNotSendPeriodicReport)
{
    PumpStateSnapshot idle_snapshot{
        .state = farm::LoadState::IDLE,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::SOLAR,
        .power_w = 0,
        .runtime_s = 0,
        .remaining_watchdog_s = 0,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillRepeatedly(Return(idle_snapshot));

    // Even after 10000ms, no periodic report should be sent in IDLE
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(10000);
}

TEST_F(PumpStatusReporterTest, StateChangeDetectedOnTickSendsImmediatelyWithAckAndResetsRunningTimer)
{
    EXPECT_CALL(state_machine_, consume_state_changed()).WillOnce(Return(true));
    // State change MUST require ACK (require_ack = true)
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).Times(1).WillOnce(Return(ESP_OK));

    sut_->tick(100); // Triggered immediately due to state change

    // Timer was reset, so 4000ms later it shouldn't send yet
    EXPECT_CALL(state_machine_, consume_state_changed()).WillRepeatedly(Return(false));
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(4000);

    // After remaining 1000ms, running periodic report triggers without ACK
    EXPECT_CALL(espnow_, send_data(_, _, _, _, false)).Times(1);
    sut_->tick(1000);
}

TEST_F(PumpStatusReporterTest, FailedStateChangeReportSchedulesRetryUntilAcked)
{
    EXPECT_CALL(state_machine_, consume_state_changed()).WillOnce(Return(true));
    // First attempt fails (timeout/nack)
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).WillOnce(Return(ESP_ERR_TIMEOUT));

    sut_->tick(100); // Triggers state change, fails, schedules retry

    EXPECT_CALL(state_machine_, consume_state_changed()).WillRepeatedly(Return(false));

    // Before retry timer (500ms < 1000ms), no send
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(500);

    // At 1000ms, retries WITH ACK and succeeds
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).WillOnce(Return(ESP_OK));
    sut_->tick(500);

    // After success, retry is cleared; next tick within interval sends nothing
    EXPECT_CALL(espnow_, send_data(_, _, _, _, _)).Times(0);
    sut_->tick(1000);
}

TEST_F(PumpStatusReporterTest, ExplicitNotifyStateChangeTriggersImmediateSendWithAck)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).Times(1).WillOnce(Return(ESP_OK));
    sut_->notify_state_change();
}

TEST_F(PumpStatusReporterTest, SendStatusReportPropagatesEspNowError)
{
    EXPECT_CALL(espnow_, send_data(_, _, _, _, false)).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(sut_->send_status_report(false), ESP_FAIL);
}

TEST_F(PumpStatusReporterTest, StateTransitionToRunningDisablesHeartbeat)
{
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::SOLAR,
        .power_w = 320,
        .runtime_s = 0,
        .remaining_watchdog_s = 180,
        .state_changed = true};
    ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(running_snapshot));
    EXPECT_CALL(state_machine_, consume_state_changed()).WillOnce(Return(true));
    EXPECT_CALL(espnow_, set_enable_heartbeat(false)).Times(1);
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).WillOnce(Return(ESP_OK));

    sut_->tick(10);
}

TEST_F(PumpStatusReporterTest, StateTransitionToIdleEnablesHeartbeat)
{
    PumpStateSnapshot idle_snapshot{
        .state = farm::LoadState::IDLE,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::SOLAR,
        .power_w = 0,
        .runtime_s = 0,
        .remaining_watchdog_s = 0,
        .state_changed = true};
    ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(idle_snapshot));
    EXPECT_CALL(state_machine_, consume_state_changed()).WillOnce(Return(true));
    EXPECT_CALL(espnow_, set_enable_heartbeat(true)).Times(1);
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).WillOnce(Return(ESP_OK));

    sut_->tick(10);
}

TEST_F(PumpStatusReporterTest, ExplicitNotifyStateChangeUpdatesHeartbeatState)
{
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::STOP_OVERRIDE,
        .source = farm::PowerSource::GRID,
        .power_w = 320,
        .runtime_s = 10,
        .remaining_watchdog_s = 100,
        .state_changed = false};
    ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(running_snapshot));
    EXPECT_CALL(espnow_, set_enable_heartbeat(false)).Times(1);
    EXPECT_CALL(espnow_, send_data(_, _, _, _, true)).WillOnce(Return(ESP_OK));

    sut_->notify_state_change();
}
