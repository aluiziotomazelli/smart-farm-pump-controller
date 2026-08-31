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
    PumpStateMachineConfig config_{.default_watchdog_s = 60, .enable_output_validation = false, .nominal_power_w = 320};
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
    EXPECT_EQ(sut_->get_locked_source(), farm::PowerSource::UNKNOWN);
    EXPECT_EQ(sut_->get_runtime_s(), 0);
    EXPECT_EQ(sut_->get_snapshot().power_w, 0);
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
    EXPECT_EQ(sut_->get_snapshot().power_w, 320);
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
    EXPECT_EQ(sut_->get_snapshot().power_w, 0);
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
    EXPECT_EQ(sut_->get_snapshot().power_w, 0);
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

TEST_F(PumpStateMachineTest, SourceLockEnforcesSourceLockedModeAndLocksLoadOnSource)
{
    sut_->set_source_lock(farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
    EXPECT_EQ(sut_->get_locked_source(), farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_snapshot().selected_source, farm::PowerSource::SOLAR);

    // Hub sends LOAD_ON requesting GRID, but pump locks and activates SOLAR
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60};
    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->handle_load_on(on_cmd), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
}

TEST_F(PumpStateMachineTest, OperatorStartTransitionsToManualRunMode)
{
    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_operator_start(farm::PowerSource::SOLAR), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::MANUAL_RUN);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_snapshot().power_w, 320);

    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_operator_stop(), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
}

TEST_F(PumpStateMachineTest, ManualRunRejectsLoadOnButAcceptsLoadOffFromHub)
{
    EXPECT_CALL(contactor_, activate(farm::PowerSource::GRID)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_operator_start(farm::PowerSource::GRID), ESP_OK);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::MANUAL_RUN);

    // Hub tries to send LOAD_ON -> rejected
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 60};
    EXPECT_EQ(sut_->handle_load_on(on_cmd), ESP_ERR_INVALID_STATE);

    // Hub sends LOAD_OFF -> accepted!
    farm::LoadOffCommand off_cmd{.circuit_id = 0};
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_load_off(off_cmd), ESP_OK);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
}

TEST_F(PumpStateMachineTest, HotSwitchFromSolarToGridWhileRunningOnHubCycle)
{
    // Hub starts on SOLAR
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 100};
    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_load_on(on_cmd), ESP_OK);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);

    // Operator turns switch to GRID while running in AUTO mode -> deactivates and enters IDLE to notify Hub
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));
    sut_->set_source_lock(farm::PowerSource::GRID);

    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
    EXPECT_EQ(sut_->get_locked_source(), farm::PowerSource::GRID);
    EXPECT_EQ(sut_->get_snapshot().selected_source, farm::PowerSource::GRID);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::IDLE);
}

TEST_F(PumpStateMachineTest, HotSwitchInManualRunModePerformsImmediateSwitch)
{
    // Operator starts on GRID manually
    EXPECT_CALL(contactor_, activate(farm::PowerSource::GRID)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_operator_start(farm::PowerSource::GRID), ESP_OK);
    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::MANUAL_RUN);

    // Operator turns switch to SOLAR while in MANUAL_RUN -> immediate hot-switch
    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    sut_->set_source_lock(farm::PowerSource::SOLAR);

    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::MANUAL_RUN);
    EXPECT_EQ(sut_->get_locked_source(), farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
}

TEST_F(PumpStateMachineTest, SwitchReturnsToCenterWhileRunningMaintainsOperation)
{
    // Operator locked to SOLAR and running
    sut_->set_source_lock(farm::PowerSource::SOLAR);
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 100};
    EXPECT_CALL(contactor_, activate(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->handle_load_on(on_cmd), ESP_OK);

    // Operator returns switch to center (UNKNOWN)
    EXPECT_CALL(contactor_, activate(_)).Times(0); // Should NOT re-actuate
    sut_->set_source_lock(farm::PowerSource::UNKNOWN);

    EXPECT_EQ(sut_->get_control_mode(), farm::ControlMode::AUTO);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
    EXPECT_EQ(sut_->get_state(), farm::LoadState::RUNNING);
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

TEST_F(PumpStateMachineTest, OutputValidationDetectsSpontaneousStuckContactorInIdle)
{
    PumpStateMachineConfig valid_config{.enable_output_validation = true};
    PumpStateMachine validate_sut(contactor_, monitor_, valid_config);

    EXPECT_CALL(monitor_, has_any_output_energy()).WillOnce(Return(true));
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));

    validate_sut.tick(100);
    EXPECT_EQ(validate_sut.get_state(), farm::LoadState::ERROR_CONTACTOR_STUCK);
}

TEST_F(PumpStateMachineTest, OutputValidationIgnoresDuringStabilizationThenDetectsLossOfPower)
{
    PumpStateMachineConfig valid_config{.enable_output_validation = true};
    PumpStateMachine validate_sut(contactor_, monitor_, valid_config);

    EXPECT_CALL(monitor_, has_any_output_energy()).WillRepeatedly(Return(false));
    EXPECT_CALL(contactor_, activate(_)).WillOnce(Return(ESP_OK));

    farm::LoadOnCommand cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::SOLAR,
        .watchdog_timeout_s = 60};

    EXPECT_EQ(validate_sut.handle_load_on(cmd), ESP_OK);
    EXPECT_EQ(validate_sut.get_state(), farm::LoadState::RUNNING);

    // During first 500ms stabilization delay, absence of voltage is ignored
    validate_sut.tick(300);
    EXPECT_EQ(validate_sut.get_state(), farm::LoadState::RUNNING);

    // After stabilization (>500ms total), loss of output voltage triggers ERROR_NO_SOURCE
    EXPECT_CALL(contactor_, deactivate()).WillOnce(Return(ESP_OK));
    validate_sut.tick(300); // 600ms total
    EXPECT_EQ(validate_sut.get_state(), farm::LoadState::ERROR_NO_SOURCE);
}
