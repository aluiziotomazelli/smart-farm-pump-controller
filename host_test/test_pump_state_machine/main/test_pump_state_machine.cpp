// host_test/test_pump_state_machine/main/test_pump_state_machine.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "pump_state_machine.hpp"
#include "mock_contactor_controller.hpp"
#include "mock_output_monitor.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class PumpStateMachineTest : public ::testing::Test
{
protected:
    NiceMock<MockContactorController> contactor_;
    NiceMock<MockOutputMonitor> monitor_;
    PumpStateMachineConfig config_{.default_watchdog_s = 60, .enable_output_validation = false};
    std::unique_ptr<PumpStateMachine> sut_;

    void SetUp() override
    {
        ON_CALL(contactor_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(contactor_, activate(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(contactor_, deactivate()).WillByDefault(Return(ESP_OK));
        ON_CALL(monitor_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(monitor_, has_any_output_energy()).WillByDefault(Return(false));

        sut_ = std::make_unique<PumpStateMachine>(contactor_, monitor_, config_);
    }
};

TEST_F(PumpStateMachineTest, InitialStateIsIdleInAutoMode)
{
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
    EXPECT_EQ(sut_->get_runtime_s(), 0);
}

TEST_F(PumpStateMachineTest, InitInitializesHardwareAndDeactivatesContactor)
{
    EXPECT_CALL(contactor_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(monitor_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpStateMachineTest, AutoModeLoadOnActivatesContactorAndTransitionsToRunning)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 300};

    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->handle_load_on(cmd), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
    EXPECT_TRUE(sut_->consume_state_changed());
    EXPECT_FALSE(sut_->consume_state_changed()); // Cleared
}

TEST_F(PumpStateMachineTest, AutoModeLoadOnWithInvalidSourceFails)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::UNKNOWN,
        .watchdog_timeout_s = 60};

    EXPECT_CALL(contactor_, activate(_)).Times(0);

    EXPECT_EQ(sut_->handle_load_on(cmd), ESP_ERR_INVALID_ARG);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
}

TEST_F(PumpStateMachineTest, AutoModeLoadOnUsesDefaultWatchdogIfZeroProvided)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 0};

    EXPECT_EQ(sut_->handle_load_on(cmd), ESP_OK);
    auto snapshot = sut_->get_snapshot();
    EXPECT_EQ(snapshot.remaining_watchdog_s, 60); // config default
}

TEST_F(PumpStateMachineTest, AutoModeLoadOnRefreshesWatchdogWhileRunning)
{
    farm::LoadOnCommand cmd1{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 100};
    EXPECT_EQ(sut_->handle_load_on(cmd1), ESP_OK);

    sut_->tick(10000); // 10s elapsed
    EXPECT_EQ(sut_->get_snapshot().remaining_watchdog_s, 90);

    farm::LoadOnCommand cmd2{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 200};
    EXPECT_EQ(sut_->handle_load_on(cmd2), ESP_OK);
    EXPECT_EQ(sut_->get_snapshot().remaining_watchdog_s, 200);
}

TEST_F(PumpStateMachineTest, AutoModeLoadOffDeactivatesContactorAndReturnsToIdle)
{
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 100};
    sut_->handle_load_on(on_cmd);
    sut_->consume_state_changed();

    farm::LoadOffCommand off_cmd{.circuit_id = 0};
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->handle_load_off(off_cmd), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
    EXPECT_TRUE(sut_->consume_state_changed());
}

TEST_F(PumpStateMachineTest, WatchdogExpiryTransitionsToErrorTimeout)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 5};
    sut_->handle_load_on(cmd);
    sut_->consume_state_changed();

    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));

    // Tick 6 seconds (exceeding 5s)
    sut_->tick(6000);

    EXPECT_EQ(sut_->get_state(), farm::LoadState::ERROR_TIMEOUT);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
    EXPECT_TRUE(sut_->consume_state_changed());
}

TEST_F(PumpStateMachineTest, ErrorTimeoutRecoversWithFreshLoadOn)
{
    farm::LoadOnCommand cmd1{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 2};
    sut_->handle_load_on(cmd1);
    sut_->tick(3000);
    ASSERT_EQ(sut_->get_state(), farm::LoadState::ERROR_TIMEOUT);

    farm::LoadOnCommand cmd2{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60};
    EXPECT_CALL(contactor_, activate(farm::PowerSource::GRID)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->handle_load_on(cmd2), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::GRID);
}

TEST_F(PumpStateMachineTest, ManualModeRejectsRemoteLoadCommands)
{
    sut_->set_control_mode(farm::ControlMode::MANUAL);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::MANUAL);

    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60};
    EXPECT_EQ(sut_->handle_load_on(on_cmd), ESP_ERR_INVALID_STATE);

    farm::LoadOffCommand off_cmd{.circuit_id = 0};
    EXPECT_EQ(sut_->handle_load_off(off_cmd), ESP_ERR_INVALID_STATE);
}

TEST_F(PumpStateMachineTest, ManualStartAndStopInManualMode)
{
    sut_->set_control_mode(farm::ControlMode::MANUAL);

    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_manual_start(farm::PowerSource::SOLAR), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);

    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_manual_stop(), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
}

TEST_F(PumpStateMachineTest, ManualStartFailsInAutoMode)
{
    EXPECT_EQ(sut_->handle_manual_start(farm::PowerSource::SOLAR), ESP_ERR_INVALID_STATE);
}

TEST_F(PumpStateMachineTest, RuntimeAccumulatesOnTickWhenRunning)
{
    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 100};
    sut_->handle_load_on(cmd);

    sut_->tick(500);
    EXPECT_EQ(sut_->get_runtime_s(), 0);

    sut_->tick(500); // 1000ms total
    EXPECT_EQ(sut_->get_runtime_s(), 1);

    sut_->tick(2000); // 3000ms total
    EXPECT_EQ(sut_->get_runtime_s(), 3);
}

TEST_F(PumpStateMachineTest, PreActivationOutputValidationDetectsStuckContactor)
{
    PumpStateMachineConfig valid_config{.enable_output_validation = true};
    PumpStateMachine validate_sut(contactor_, monitor_, valid_config);

    // Simulate unexpected energy on output before activation
    EXPECT_CALL(monitor_, has_any_output_energy()).WillOnce(Return(true));
    EXPECT_CALL(contactor_, activate(_)).Times(0);

    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60};

    EXPECT_EQ(validate_sut.handle_load_on(cmd), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(validate_sut.get_state(), farm::LoadState::ERROR_CONTACTOR_STUCK);
}
