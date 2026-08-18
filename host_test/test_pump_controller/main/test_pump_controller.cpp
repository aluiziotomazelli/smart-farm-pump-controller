// host_test/test_pump_controller/main/test_pump_controller.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "pump_controller.hpp"
#include "mock_pump_state_machine.hpp"
#include "mock_pump_status_reporter.hpp"
#include "mock_pump_led_controller.hpp"
#include "mock_tank_level_display.hpp"
#include "mock_switch.hpp"
#include "mock_button.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_system.hpp"
#include "mock_nvs_core.hpp"
#include "mock_pump_nvs.hpp"
#include "mock_i_wifi_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

class PumpControllerTest : public ::testing::Test
{
protected:
    NiceMock<MockNvsCore> nvs_core_;
    NiceMock<MockPumpNvs> pump_nvs_;
    NiceMock<MockPumpStateMachine> state_machine_;
    NiceMock<espnow::MockEspNowManager> espnow_;
    NiceMock<time_manager::MockTimeManager> time_manager_;
    NiceMock<MockTankLevelDisplay> tank_display_;
    NiceMock<MockPumpStatusReporter> status_reporter_;
    NiceMock<MockPumpLedController> led_controller_;
    NiceMock<ui_inputs::MockSwitch> switch_mode_;
    NiceMock<ui_inputs::MockSwitch> switch_source_;
    NiceMock<ui_inputs::MockButton> button_action_;
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    NiceMock<idf_hals::MockSystemHAL> hal_system_;

    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x5678);

    std::unique_ptr<PumpCommandHandler> command_handler_;
    std::unique_ptr<PumpController> sut_;

    void SetUp() override
    {
        ON_CALL(nvs_core_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(nvs_core_, save_core(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(pump_nvs_, init_app_data(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(pump_nvs_, save_app_data(_, _)).WillByDefault(Return(ESP_OK));

        ON_CALL(state_machine_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(status_reporter_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(led_controller_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(led_controller_, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(tank_display_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(switch_mode_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(switch_source_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(button_action_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_rtos_, queue_receive(_, _, _)).WillByDefault(Return(pdFALSE));
        ON_CALL(hal_rtos_, task_create(_, _, _, _, _, _)).WillByDefault(Return(pdPASS));

        PumpStateSnapshot default_snapshot{
            .state = farm::LoadState::IDLE,
            .mode = farm::ControlMode::AUTO,
            .source = farm::PowerSource::UNKNOWN,
            .runtime_s = 0,
            .remaining_watchdog_s = 0,
            .state_changed = false};
        ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(default_snapshot));

        command_handler_ = std::make_unique<PumpCommandHandler>(
            dummy_queue_,
            espnow_,
            state_machine_,
            time_manager_,
            tank_display_,
            hal_rtos_);

        sut_ = std::make_unique<PumpController>(
            nvs_core_,
            pump_nvs_,
            state_machine_,
            *command_handler_,
            status_reporter_,
            led_controller_,
            tank_display_,
            switch_mode_,
            switch_source_,
            button_action_,
            mock_wifi_,
            hal_rtos_,
            hal_system_);
    }
};

TEST_F(PumpControllerTest, InitInitializesAllSubsystemsAndStorage)
{
    EXPECT_CALL(nvs_core_, init(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(pump_nvs_, init_app_data(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(state_machine_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(status_reporter_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(led_controller_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(tank_display_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(switch_mode_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(switch_source_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(button_action_, init()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpControllerTest, StartLaunchesTaskAndLedController)
{
    EXPECT_CALL(led_controller_, start()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_rtos_, task_create(_, ::testing::StrEq("pump_ctrl_task"), 4096, _, 5, _))
        .WillOnce(Return(pdPASS));

    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(PumpControllerTest, TickSamplesSwitchModeAutoAndUpdatesStateMachine)
{
    // Mode switch CLOSED -> AUTO mode
    EXPECT_CALL(switch_mode_, update()).Times(1);
    EXPECT_CALL(switch_mode_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));
    EXPECT_CALL(state_machine_, set_control_mode(farm::ControlMode::AUTO)).Times(1);

    EXPECT_CALL(state_machine_, tick(50)).Times(1);
    EXPECT_CALL(status_reporter_, tick(50)).Times(1);
    EXPECT_CALL(led_controller_, update(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN)).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickSamplesSwitchModeManualAndHandlesActionButtonStartsPumpWhenOff)
{
    // Mode switch OPEN -> MANUAL mode
    EXPECT_CALL(switch_mode_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));
    EXPECT_CALL(state_machine_, set_control_mode(farm::ControlMode::MANUAL)).Times(1);

    // Source switch CLOSED -> SOLAR
    EXPECT_CALL(switch_source_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));

    // Action button clicked when pump is IDLE
    EXPECT_CALL(button_action_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));
    PumpStateSnapshot idle_snapshot{
        .state = farm::LoadState::IDLE,
        .mode = farm::ControlMode::MANUAL,
        .source = farm::PowerSource::UNKNOWN,
        .runtime_s = 0,
        .remaining_watchdog_s = 0,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillOnce(Return(idle_snapshot)).WillRepeatedly(Return(idle_snapshot));

    EXPECT_CALL(state_machine_, handle_manual_start(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(pump_nvs_, save_app_data(_, false)).Times(1);

    sut_->tick(50);
    EXPECT_EQ(sut_->get_stats().manual_starts_total, 1);
    EXPECT_EQ(sut_->get_stats().start_cycles_total, 1);
}

TEST_F(PumpControllerTest, TickSamplesSwitchModeManualAndHandlesActionButtonStopsPumpWhenRunning)
{
    // Mode switch OPEN -> MANUAL mode
    EXPECT_CALL(switch_mode_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));

    // Action button clicked when pump is RUNNING
    EXPECT_CALL(button_action_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::MANUAL,
        .source = farm::PowerSource::SOLAR,
        .runtime_s = 15,
        .remaining_watchdog_s = 3585,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillOnce(Return(running_snapshot)).WillRepeatedly(Return(running_snapshot));

    EXPECT_CALL(state_machine_, handle_manual_stop()).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickHandlesRebootCommandGracefullyAndPersistsState)
{
    // Mock queue returning a REBOOT command
    espnow::AppMessage reboot_msg{};
    reboot_msg.msg_type = espnow::MessageType::COMMAND;
    reboot_msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    reboot_msg.requires_ack = false;

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
        .WillOnce([reboot_msg](QueueHandle_t, void* buf, TickType_t) {
            std::memcpy(buf, &reboot_msg, sizeof(reboot_msg));
            return pdTRUE;
        })
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(nvs_core_, save_core(_, true)).Times(1);
    EXPECT_CALL(pump_nvs_, save_app_data(_, true)).Times(1);
    EXPECT_CALL(state_machine_, handle_manual_stop()).Times(1);
    EXPECT_CALL(hal_system_, restart()).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickRuntimeAccountingIncrementsHourmeter)
{
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::SOLAR,
        .runtime_s = 10,
        .remaining_watchdog_s = 3590,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillRepeatedly(Return(running_snapshot));

    // Tick 1000 ms -> should increment runtime by 1s
    EXPECT_CALL(pump_nvs_, save_app_data(_, false)).Times(1);
    sut_->tick(1000);

    EXPECT_EQ(sut_->get_stats().total_runtime_s, 1);
    EXPECT_EQ(sut_->get_stats().solar_runtime_s, 1);
    EXPECT_EQ(sut_->get_stats().grid_runtime_s, 0);
}
