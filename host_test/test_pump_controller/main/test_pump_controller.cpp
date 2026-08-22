#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "pump_controller.hpp"
#include "mock_pump_state_machine.hpp"
#include "mock_pump_status_reporter.hpp"
#include "mock_tank_strip_display.hpp"
#include "mock_switch.hpp"
#include "mock_button.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_system.hpp"
#include "mock_nvs_core.hpp"
#include "mock_pump_nvs.hpp"
#include "mock_i_wifi_manager.hpp"
#include "mock_i_ota_controller.hpp"
#include "mock_i_ota_trigger.hpp"

#include "secrets.hpp"

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
    NiceMock<MockTankStripDisplay> display_;
    NiceMock<MockPumpStatusReporter> status_reporter_;
    NiceMock<ui_inputs::MockSwitch> switch_solar_;
    NiceMock<ui_inputs::MockSwitch> switch_grid_;
    NiceMock<ui_inputs::MockButton> button_action_;
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi_;
    NiceMock<MockOtaController> mock_ota_;
    NiceMock<MockOtaTrigger> btn_trigger_;
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

        ON_CALL(mock_wifi_, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi_, add_credentials(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi_, start(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi_, connect(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi_, disconnect(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi_, stop(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_ota_, init(_)).WillByDefault(Return(true));
        ON_CALL(mock_ota_, check_pending_verify()).WillByDefault(Return(false));
        ON_CALL(mock_ota_, get_running_version()).WillByDefault(Return(OtaVersion{1, 0, 0}));

        ON_CALL(btn_trigger_, arm(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(time_manager_, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(time_manager_, is_synchronized()).WillByDefault(Return(false));
        ON_CALL(time_manager_, get_timestamp_sec()).WillByDefault(Return(0));

        ON_CALL(state_machine_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(status_reporter_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(display_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(display_, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(display_, stop()).WillByDefault(Return());
        ON_CALL(display_, set_brightness(_)).WillByDefault(Return());
        ON_CALL(switch_solar_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(switch_grid_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(button_action_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(espnow_, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_rtos_, queue_receive(_, _, _)).WillByDefault(Return(pdFALSE));
        ON_CALL(hal_rtos_, task_create(_, _, _, _, _, _)).WillByDefault(Return(pdPASS));

        PumpStateSnapshot default_snapshot{
            .state = farm::LoadState::IDLE,
            .mode = farm::ControlMode::AUTO,
            .source = farm::PowerSource::UNKNOWN,
            .power_w = 0,
            .runtime_s = 0,
            .remaining_watchdog_s = 0,
            .state_changed = false};
        ON_CALL(state_machine_, get_snapshot()).WillByDefault(Return(default_snapshot));

        command_handler_ = std::make_unique<PumpCommandHandler>(
            dummy_queue_,
            espnow_,
            state_machine_,
            time_manager_,
            display_,
            hal_rtos_);

        sut_ = std::make_unique<PumpController>(
            dummy_queue_,
            nvs_core_,
            pump_nvs_,
            state_machine_,
            *command_handler_,
            status_reporter_,
            display_,
            switch_solar_,
            switch_grid_,
            button_action_,
            time_manager_,
            mock_wifi_,
            mock_ota_,
            btn_trigger_,
            espnow_,
            hal_rtos_,
            hal_system_);
    }
};

TEST_F(PumpControllerTest, InitInitializesAllSubsystemsAndStorage)
{
    EXPECT_CALL(nvs_core_, init(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(pump_nvs_, init_app_data(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi_, init(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi_, add_credentials(::testing::StrEq(WIFI_SSID), ::testing::StrEq(WIFI_PASS)))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi_, start(10000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_ota_, init(_)).WillOnce(Return(true));
    EXPECT_CALL(btn_trigger_, arm(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, init(::testing::Field(&espnow::EspNowConfig::node_id, static_cast<espnow::NodeId>(farm::NodeId::PUMP_CONTROL))))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(time_manager_, init(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(state_machine_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(status_reporter_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, start()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(switch_solar_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(switch_grid_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(button_action_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::BOOT_SUCCESS)).Times(1);

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpControllerTest, InitPendingVerifyConfirmsFirmwareSuccessfully)
{
    EXPECT_CALL(mock_ota_, check_pending_verify()).WillOnce(Return(true));
    OtaActionResult confirm_res{.success = true, .exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS, .error_code = farm::OtaErrorCode::NONE};
    EXPECT_CALL(mock_ota_, confirm_firmware(true)).WillOnce(Return(confirm_res));
    EXPECT_CALL(espnow_, send_data(espnow::ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT), _, _, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(nvs_core_, save_core(_, true)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::BOOT_SUCCESS)).Times(1);

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpControllerTest, InitPendingVerifyRollbackTriggersReboot)
{
    EXPECT_CALL(mock_ota_, check_pending_verify()).WillOnce(Return(true));
    OtaActionResult fail_res{.success = false, .exec_result = farm::OtaExecResult::ROLLBACK_TRIGGERED, .error_code = farm::OtaErrorCode::HEALTH_CHECK_FAILED};
    EXPECT_CALL(mock_ota_, confirm_firmware(true)).WillOnce(Return(fail_res));
    EXPECT_CALL(espnow_, send_data(espnow::ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT), _, _, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::BOOT_ERROR)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(_)).Times(::testing::AtLeast(1));
    EXPECT_CALL(mock_ota_, rollback_and_reboot()).Times(1);

    EXPECT_EQ(sut_->init(), ESP_FAIL);
}

TEST_F(PumpControllerTest, InitNormalBootUnhealthySetsBootErrorAndFails)
{
    EXPECT_CALL(mock_wifi_, init(_)).WillOnce(Return(ESP_FAIL)); // Marks session_healthy = false
    EXPECT_CALL(mock_ota_, check_pending_verify()).WillOnce(Return(false));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::BOOT_ERROR)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(_)).Times(::testing::AtLeast(1));

    EXPECT_EQ(sut_->init(), ESP_FAIL);
}

TEST_F(PumpControllerTest, InitTimeManagerFailureMarksSessionUnhealthy)
{
    EXPECT_CALL(time_manager_, init(_)).WillOnce(Return(ESP_FAIL)); // Marks session_healthy = false
    EXPECT_CALL(mock_ota_, check_pending_verify()).WillOnce(Return(false));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::BOOT_ERROR)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(_)).Times(::testing::AtLeast(1));

    EXPECT_EQ(sut_->init(), ESP_FAIL);
}

TEST_F(PumpControllerTest, StartLaunchesTask)
{
    EXPECT_CALL(hal_rtos_, task_create(_, ::testing::StrEq("pump_ctrl_task"), 4096, _, 5, _))
        .WillOnce(Return(pdPASS));

    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(PumpControllerTest, TickSamplesCenterSwitchAutoModeAndSendsFillRequestOnButtonClick)
{
    // Center switch position: both OPEN -> UNKNOWN source lock
    EXPECT_CALL(switch_solar_, update()).Times(1);
    EXPECT_CALL(switch_grid_, update()).Times(1);
    EXPECT_CALL(switch_solar_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));
    EXPECT_CALL(switch_grid_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));
    EXPECT_CALL(state_machine_, set_source_lock(farm::PowerSource::UNKNOWN)).Times(1);

    // Button clicked when pump is IDLE in AUTO
    EXPECT_CALL(button_action_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));
    PumpStateSnapshot idle_snapshot{
        .state = farm::LoadState::IDLE,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::UNKNOWN,
        .power_w = 0,
        .runtime_s = 0,
        .remaining_watchdog_s = 0,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillRepeatedly(Return(idle_snapshot));

    EXPECT_CALL(espnow_, send_data(espnow::ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::FILL_REQUEST), _, sizeof(farm::FillRequest), true))
        .WillOnce(Return(ESP_OK));

    EXPECT_CALL(state_machine_, tick(50)).Times(1);
    EXPECT_CALL(status_reporter_, tick(50)).Times(1);
    EXPECT_CALL(display_, update_state(farm::LoadState::IDLE, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN)).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickSamplesSolarSwitchAndHandlesOperatorStartWhenOff)
{
    // Solar switch CLOSED, Grid OPEN -> SOLAR locked
    EXPECT_CALL(switch_solar_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));
    EXPECT_CALL(switch_grid_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));
    EXPECT_CALL(state_machine_, set_source_lock(farm::PowerSource::SOLAR)).Times(1);

    // Button clicked when pump is IDLE
    EXPECT_CALL(button_action_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));
    PumpStateSnapshot idle_snapshot{
        .state = farm::LoadState::IDLE,
        .mode = farm::ControlMode::SOURCE_LOCKED,
        .source = farm::PowerSource::SOLAR,
        .power_w = 0,
        .runtime_s = 0,
        .remaining_watchdog_s = 0,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillRepeatedly(Return(idle_snapshot));

    EXPECT_CALL(state_machine_, handle_operator_start(farm::PowerSource::SOLAR)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(pump_nvs_, save_app_data(_, false)).Times(1);

    sut_->tick(50);
    EXPECT_EQ(sut_->get_stats().manual_starts_total, 1);
    EXPECT_EQ(sut_->get_stats().start_cycles_total, 1);
}

TEST_F(PumpControllerTest, TickSamplesDualSwitchConflictDefaultsToGrid)
{
    // Dual closed conflict (mechanical error) -> defaults to GRID
    EXPECT_CALL(switch_solar_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));
    EXPECT_CALL(switch_grid_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));
    EXPECT_CALL(state_machine_, set_source_lock(farm::PowerSource::GRID)).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickHandlesOperatorStopWhenPumpIsRunning)
{
    // Running state
    EXPECT_CALL(switch_solar_, get_state()).WillOnce(Return(ui_inputs::SwitchState::CLOSED));
    EXPECT_CALL(switch_grid_, get_state()).WillOnce(Return(ui_inputs::SwitchState::OPEN));

    EXPECT_CALL(button_action_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::STOP_OVERRIDE,
        .source = farm::PowerSource::SOLAR,
        .power_w = 320,
        .runtime_s = 15,
        .remaining_watchdog_s = 0,
        .state_changed = false};
    EXPECT_CALL(state_machine_, get_snapshot()).WillRepeatedly(Return(running_snapshot));

    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);

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
    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);
    EXPECT_CALL(hal_system_, restart()).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickHandlesOtaCommandStopsPumpConnectsWiFiAndRestartsOnSuccess)
{
    espnow::AppMessage ota_msg{};
    ota_msg.msg_type = espnow::MessageType::COMMAND;
    ota_msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    ota_msg.requires_ack = false;

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
        .WillOnce([ota_msg](QueueHandle_t, void* buf, TickType_t) {
            std::memcpy(buf, &ota_msg, sizeof(ota_msg));
            return pdTRUE;
        })
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(btn_trigger_, disarm()).Times(::testing::AtLeast(1));
    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::OTA_UPDATING)).Times(1);
    EXPECT_CALL(mock_wifi_, connect(15000, 3, 1500)).WillOnce(Return(ESP_OK));

    OtaActionResult download_ok{.success = true, .exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS, .error_code = farm::OtaErrorCode::NONE};
    EXPECT_CALL(mock_ota_, execute_download(60000)).WillOnce(Return(download_ok));

    EXPECT_CALL(nvs_core_, save_core(_, true)).Times(1);
    EXPECT_CALL(pump_nvs_, save_app_data(_, true)).Times(1);
    EXPECT_CALL(mock_wifi_, disconnect(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi_, stop(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_system_, restart()).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, ButtonOtaTriggerInvokesOtaFlowOnTick)
{
    // Trigger OTA via button callback
    sut_->on_ota_triggered(OtaTriggerSource::BUTTON);

    EXPECT_CALL(btn_trigger_, disarm()).Times(::testing::AtLeast(1));
    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::OTA_UPDATING)).Times(1);
    EXPECT_CALL(mock_wifi_, connect(15000, 3, 1500)).WillOnce(Return(ESP_OK));

    OtaActionResult download_ok{.success = true, .exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS, .error_code = farm::OtaErrorCode::NONE};
    EXPECT_CALL(mock_ota_, execute_download(60000)).WillOnce(Return(download_ok));

    EXPECT_CALL(nvs_core_, save_core(_, true)).Times(1);
    EXPECT_CALL(pump_nvs_, save_app_data(_, true)).Times(1);
    EXPECT_CALL(mock_wifi_, disconnect(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_wifi_, stop(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_system_, restart()).Times(1);

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickHandlesOtaCommandWiFiFailureSendsReportAndRearmsTrigger)
{
    espnow::AppMessage ota_msg{};
    ota_msg.msg_type = espnow::MessageType::COMMAND;
    ota_msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    ota_msg.requires_ack = false;

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
        .WillOnce([ota_msg](QueueHandle_t, void* buf, TickType_t) {
            std::memcpy(buf, &ota_msg, sizeof(ota_msg));
            return pdTRUE;
        })
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(btn_trigger_, disarm()).Times(::testing::AtLeast(1));
    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::OTA_UPDATING)).Times(1);
    EXPECT_CALL(espnow_, set_channel_policy(espnow::ChannelPolicy::FIXED)).Times(1);
    EXPECT_CALL(mock_wifi_, connect(15000, 3, 1500)).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(mock_wifi_, disconnect(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, set_channel_policy(espnow::ChannelPolicy::SCAN)).Times(1);

    EXPECT_CALL(espnow_, send_data(espnow::ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT), _, _, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::AUTO)).Times(1);
    EXPECT_CALL(btn_trigger_, arm(_)).WillOnce(Return(ESP_OK));

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickHandlesOtaCommandDownloadFailureSendsReportAndRestoresScan)
{
    espnow::AppMessage ota_msg{};
    ota_msg.msg_type = espnow::MessageType::COMMAND;
    ota_msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    ota_msg.requires_ack = false;

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, 0))
        .WillOnce([ota_msg](QueueHandle_t, void* buf, TickType_t) {
            std::memcpy(buf, &ota_msg, sizeof(ota_msg));
            return pdTRUE;
        })
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(btn_trigger_, disarm()).Times(::testing::AtLeast(1));
    EXPECT_CALL(state_machine_, handle_operator_stop()).Times(1);
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::OTA_UPDATING)).Times(1);
    EXPECT_CALL(espnow_, set_channel_policy(espnow::ChannelPolicy::FIXED)).Times(1);
    EXPECT_CALL(mock_wifi_, connect(15000, 3, 1500)).WillOnce(Return(ESP_OK));

    OtaActionResult download_fail{.success = false, .exec_result = farm::OtaExecResult::DOWNLOAD_FAILED, .error_code = farm::OtaErrorCode::HTTP_DOWNLOAD_FAILED};
    EXPECT_CALL(mock_ota_, execute_download(60000)).WillOnce(Return(download_fail));

    EXPECT_CALL(mock_wifi_, disconnect(2000)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(espnow_, set_channel_policy(espnow::ChannelPolicy::SCAN)).Times(1);
    EXPECT_CALL(espnow_, send_data(espnow::ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT), _, _, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(display_, set_override_pattern(TankStripPattern::AUTO)).Times(1);
    EXPECT_CALL(btn_trigger_, arm(_)).WillOnce(Return(ESP_OK));

    sut_->tick(50);
}

TEST_F(PumpControllerTest, TickRuntimeAccountingIncrementsHourmeter)
{
    PumpStateSnapshot running_snapshot{
        .state = farm::LoadState::RUNNING,
        .mode = farm::ControlMode::AUTO,
        .source = farm::PowerSource::SOLAR,
        .power_w = 320,
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

static time_t make_test_time(int hour)
{
    struct tm t = {};
    t.tm_year = 126; // 2026
    t.tm_mon = 7;    // August
    t.tm_mday = 20;
    t.tm_hour = hour;
    t.tm_min = 30;
    t.tm_sec = 0;
    return mktime(&t);
}

TEST_F(PumpControllerTest, TickUpdatesDisplayBrightnessToDayWhenSynchronized)
{
    EXPECT_CALL(time_manager_, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(time_manager_, get_timestamp_sec()).WillRepeatedly(Return(make_test_time(12))); // 12:30 (Day)

    EXPECT_CALL(display_, set_brightness(180)).Times(1);
    sut_->tick(10000); // 10s check interval
}

TEST_F(PumpControllerTest, TickUpdatesDisplayBrightnessToTwilightWhenSynchronized)
{
    EXPECT_CALL(time_manager_, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(time_manager_, get_timestamp_sec()).WillRepeatedly(Return(make_test_time(19))); // 19:30 (Twilight)

    EXPECT_CALL(display_, set_brightness(30)).Times(1);
    sut_->tick(10000);
}

TEST_F(PumpControllerTest, TickUpdatesDisplayBrightnessToNightWhenSynchronized)
{
    EXPECT_CALL(time_manager_, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(time_manager_, get_timestamp_sec()).WillRepeatedly(Return(make_test_time(23))); // 23:30 (Night)

    EXPECT_CALL(display_, set_brightness(20)).Times(1);
    sut_->tick(10000);
}

TEST_F(PumpControllerTest, TickMaintainsDefaultBrightnessWhenUnsynchronized)
{
    EXPECT_CALL(time_manager_, is_synchronized()).WillRepeatedly(Return(false));

    // When unsynced, display stays on its hardware default; set_brightness is not invoked
    EXPECT_CALL(display_, set_brightness(_)).Times(0);
    sut_->tick(10000);
}
