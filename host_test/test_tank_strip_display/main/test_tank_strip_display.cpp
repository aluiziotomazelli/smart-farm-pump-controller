// host_test/test_tank_strip_display/main/test_tank_strip_display.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

#include "tank_strip_display.hpp"
#include "mock_hal_led_strip.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

class TankStripDisplayTest : public ::testing::Test
{
protected:
    NiceMock<MockHalLedStrip> hal_strip_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    TankStripConfig config_{
        .gpio_pin = GPIO_NUM_7,
        .num_leds = 10,
        .default_brightness = 255,
        .rmt_resolution_hz = 10000000,
    };

    led_strip_handle_t dummy_handle_ = reinterpret_cast<led_strip_handle_t>(0x1234);
    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x5678);
    TaskHandle_t dummy_task_ = reinterpret_cast<TaskHandle_t>(0x9ABC);
    std::unique_ptr<TankStripDisplay> display_;

    struct PixelCapture {
        uint32_t index;
        uint16_t hue;
        uint8_t saturation;
        uint8_t value;
    };
    std::vector<PixelCapture> captured_pixels_;

    void SetUp() override
    {
        ON_CALL(hal_strip_, new_rmt_device(_, _, _))
            .WillByDefault(DoAll(SetArgPointee<2>(dummy_handle_), Return(ESP_OK)));
        ON_CALL(hal_strip_, set_pixel_hsv(_, _, _, _, _))
            .WillByDefault([this](led_strip_handle_t, uint32_t idx, uint16_t h, uint8_t s, uint8_t v) {
                captured_pixels_.push_back({idx, h, s, v});
                return ESP_OK;
            });
        ON_CALL(hal_strip_, clear(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_strip_, refresh(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_strip_, del(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(hal_rtos_, queue_create(_, _)).WillByDefault(Return(dummy_queue_));
        ON_CALL(hal_rtos_, queue_send(_, _, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(hal_rtos_, queue_receive(_, _, _)).WillByDefault(Return(pdFALSE));
        ON_CALL(hal_rtos_, queue_delete(_)).WillByDefault(Return());
        ON_CALL(hal_rtos_, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(dummy_task_), Return(pdPASS)));
        ON_CALL(hal_rtos_, task_delete(_)).WillByDefault(Return());

        display_ = std::make_unique<TankStripDisplay>(hal_strip_, hal_rtos_, config_);
    }

    void InitAndClearCaptures()
    {
        ASSERT_EQ(display_->init(), ESP_OK);
        captured_pixels_.clear();
    }
};

TEST_F(TankStripDisplayTest, InitSuccessConfiguresRmtCreatesQueueAndClearsStrip)
{
    EXPECT_CALL(hal_strip_, new_rmt_device(_, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(dummy_handle_), Return(ESP_OK)));
    EXPECT_CALL(hal_rtos_, queue_create(16, sizeof(DisplayCommand)))
        .WillOnce(Return(dummy_queue_));
    EXPECT_CALL(hal_strip_, clear(dummy_handle_)).Times(::testing::AtLeast(1));

    EXPECT_EQ(display_->init(), ESP_OK);
}

TEST_F(TankStripDisplayTest, InitReturnsErrorWhenNumLedsIsZero)
{
    TankStripConfig zero_config = config_;
    zero_config.num_leds = 0;
    TankStripDisplay zero_display(hal_strip_, hal_rtos_, zero_config);

    EXPECT_EQ(zero_display.init(), ESP_ERR_INVALID_ARG);
}

TEST_F(TankStripDisplayTest, StartCreatesTaskWithPriority2)
{
    InitAndClearCaptures();

    EXPECT_CALL(hal_rtos_, task_create(_, ::testing::StrEq("strip_disp"), 3072, _, 2, _))
        .WillOnce(DoAll(SetArgPointee<5>(dummy_task_), Return(pdPASS)));

    EXPECT_EQ(display_->start(), ESP_OK);

    // Calling start again returns ESP_OK without creating duplicate task
    EXPECT_EQ(display_->start(), ESP_OK);
}

TEST_F(TankStripDisplayTest, StopDeletesTaskQueueAndClearsStrip)
{
    InitAndClearCaptures();
    ASSERT_EQ(display_->start(), ESP_OK);

    EXPECT_CALL(hal_rtos_, task_delete(dummy_task_)).Times(1);
    EXPECT_CALL(hal_rtos_, queue_delete(dummy_queue_)).Times(1);
    EXPECT_CALL(hal_strip_, clear(dummy_handle_)).Times(::testing::AtLeast(1));

    display_->stop();
}

TEST_F(TankStripDisplayTest, SetLevelSendsCommandToQueue)
{
    InitAndClearCaptures();

    EXPECT_CALL(hal_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce([](QueueHandle_t, const void* item, TickType_t) {
            const auto* cmd = static_cast<const DisplayCommand*>(item);
            EXPECT_EQ(cmd->type, DisplayCmdType::SET_LEVEL);
            EXPECT_EQ(cmd->level_data.level_permille, 750);
            EXPECT_FALSE(cmd->level_data.backup_mode);
            EXPECT_FALSE(cmd->level_data.is_full);
            return pdTRUE;
        });

    display_->set_level(750);
}

TEST_F(TankStripDisplayTest, UpdateStateSendsCommandToQueue)
{
    InitAndClearCaptures();

    EXPECT_CALL(hal_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce([](QueueHandle_t, const void* item, TickType_t) {
            const auto* cmd = static_cast<const DisplayCommand*>(item);
            EXPECT_EQ(cmd->type, DisplayCmdType::UPDATE_STATE);
            EXPECT_EQ(cmd->state_data.state, farm::LoadState::RUNNING);
            EXPECT_EQ(cmd->state_data.mode, farm::ControlMode::SOURCE_LOCKED);
            EXPECT_EQ(cmd->state_data.source, farm::PowerSource::SOLAR);
            return pdTRUE;
        });

    display_->update_state(farm::LoadState::RUNNING, farm::ControlMode::SOURCE_LOCKED, farm::PowerSource::SOLAR);
}

TEST_F(TankStripDisplayTest, SetOverridePatternSendsCommandToQueue)
{
    InitAndClearCaptures();

    EXPECT_CALL(hal_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce([](QueueHandle_t, const void* item, TickType_t) {
            const auto* cmd = static_cast<const DisplayCommand*>(item);
            EXPECT_EQ(cmd->type, DisplayCmdType::SET_OVERRIDE_PATTERN);
            EXPECT_EQ(cmd->pattern, TankStripPattern::OTA_UPDATING);
            return pdTRUE;
        });

    display_->set_override_pattern(TankStripPattern::OTA_UPDATING);
}

TEST_F(TankStripDisplayTest, CalculateActiveLedsCalculatesProportionsCorrectly)
{
    // 10 LEDs strip:
    EXPECT_EQ(display_->calculate_active_leds(0), 0);
    EXPECT_EQ(display_->calculate_active_leds(100), 1);
    EXPECT_EQ(display_->calculate_active_leds(250), 3);
    EXPECT_EQ(display_->calculate_active_leds(500), 5);
    EXPECT_EQ(display_->calculate_active_leds(750), 8);
    EXPECT_EQ(display_->calculate_active_leds(1000), 10);

    // 20 LEDs strip:
    TankStripConfig config20 = config_;
    config20.num_leds = 20;
    TankStripDisplay display20(hal_strip_, hal_rtos_, config20);
    EXPECT_EQ(display20.calculate_active_leds(0), 0);
    EXPECT_EQ(display20.calculate_active_leds(500), 10);
    EXPECT_EQ(display20.calculate_active_leds(1000), 20);
}

TEST_F(TankStripDisplayTest, IdleRendersStaticCyanProportionalToLevelInAuto)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}});
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::UNKNOWN}});

    // Advance past initial breathing timer (600ms)
    display_->process_frame(700);
    captured_pixels_.clear();

    // Now process in pure static state
    display_->process_frame(50);

    // Should have updated 10 pixels: 0..4 in Cyan (H=180), 5..9 in off (V=0)
    ASSERT_EQ(captured_pixels_.size(), 10);
    for (uint32_t i = 0; i < 5; i++) {
        EXPECT_EQ(captured_pixels_[i].index, i);
        EXPECT_EQ(captured_pixels_[i].hue, 180); // HUE_FILL
        EXPECT_GT(captured_pixels_[i].value, 0);
    }
    for (uint32_t i = 5; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].index, i);
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, IdleSourceLockedSolarRendersCyanBaseAndGreenTopLed)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}}); // 5 LEDs: 0..4
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::SOURCE_LOCKED, .source = farm::PowerSource::SOLAR}});

    display_->process_frame(700); // Settle breathing
    captured_pixels_.clear();

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base (0..3) in Cyan (H=180)
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 180);
    }
    // Top active LED (index 4) in Green (H=120, HUE_SOLAR)
    EXPECT_EQ(captured_pixels_[4].hue, 120);
    EXPECT_GT(captured_pixels_[4].value, 0);

    // Remaining (5..9) off
    for (uint32_t i = 5; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, IdleSourceLockedGridRendersCyanBaseAndRedTopLed)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}}); // 5 LEDs: 0..4
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::SOURCE_LOCKED, .source = farm::PowerSource::GRID}});

    display_->process_frame(700); // Settle breathing
    captured_pixels_.clear();

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base (0..3) in Cyan (H=180)
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 180);
    }
    // Top active LED (index 4) in Red (H=0, HUE_GRID)
    EXPECT_EQ(captured_pixels_[4].hue, 0);
    EXPECT_GT(captured_pixels_[4].value, 0);

    // Remaining (5..9) off
    for (uint32_t i = 5; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, SetLevelTriggersIdleSoftBreathingWave)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}});
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::UNKNOWN}});
    display_->process_frame(2000);

    // New update received -> triggers breathing wave
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 600, .backup_mode = false, .is_full = false}});
    captured_pixels_.clear();

    // Halfway through breathing wave (900ms) -> value should be lower (dip)
    display_->process_frame(900);
    ASSERT_GE(captured_pixels_.size(), 10);
    uint8_t dip_val = captured_pixels_[0].value;
    EXPECT_LT(dip_val, 200);

    // Settle past wave (1100ms more) -> value should be back to full static Cyan
    captured_pixels_.clear();
    display_->process_frame(1100);
    ASSERT_GE(captured_pixels_.size(), 10);
    uint8_t settled_val = captured_pixels_[0].value;
    EXPECT_GT(settled_val, dip_val);
}

TEST_F(TankStripDisplayTest, FillingAutoSolarRendersCyanBaseAndGreenChase)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}}); // 5 LEDs base
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::RUNNING, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::SOLAR}});

    // Frame 50ms (initial chase index = 5)
    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base (0..4) = Cyan (H=180)
    for (uint32_t i = 0; i < 5; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 180);
    }

    // Chase pixel at index 5 = Green (H=120)
    EXPECT_EQ(captured_pixels_[5].index, 5);
    EXPECT_EQ(captured_pixels_[5].hue, 120);
    EXPECT_EQ(captured_pixels_[5].value, 255);

    // Remaining above chase (6..9) = off
    for (uint32_t i = 6; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }

    // Advance 200ms -> chase moves to index 6
    captured_pixels_.clear();
    display_->process_frame(200);
    ASSERT_EQ(captured_pixels_.size(), 10);
    EXPECT_EQ(captured_pixels_[5].value, 0);
    EXPECT_EQ(captured_pixels_[6].hue, 120);
    EXPECT_EQ(captured_pixels_[6].value, 255);
}

TEST_F(TankStripDisplayTest, FillingAutoGridRendersCyanBaseAndRedChase)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 400, .backup_mode = false, .is_full = false}}); // 4 LEDs base
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::RUNNING, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::GRID}});

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base (0..3) = Cyan
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 180);
    }

    // Chase pixel at index 4 = Red (H=0)
    EXPECT_EQ(captured_pixels_[4].index, 4);
    EXPECT_EQ(captured_pixels_[4].hue, 0);
    EXPECT_EQ(captured_pixels_[4].value, 255);
}

TEST_F(TankStripDisplayTest, FillingManualSolarRendersSolidGreenBarAndGreenChase)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 600, .backup_mode = false, .is_full = false}}); // 6 LEDs base
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::RUNNING, .mode = farm::ControlMode::STOP_OVERRIDE, .source = farm::PowerSource::SOLAR}});

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Active base (0..5) = Solid Green (H=120) for manual emphasis
    for (uint32_t i = 0; i < 6; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 120);
        EXPECT_EQ(captured_pixels_[i].value, 255);
    }

    // Chase pixel at index 6 = Green (H=120)
    EXPECT_EQ(captured_pixels_[6].hue, 120);
    EXPECT_EQ(captured_pixels_[6].value, 255);
}

TEST_F(TankStripDisplayTest, FillingManualGridRendersSolidRedBarAndRedChase)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 600, .backup_mode = false, .is_full = false}}); // 6 LEDs base
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::RUNNING, .mode = farm::ControlMode::STOP_OVERRIDE, .source = farm::PowerSource::GRID}});

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Active base (0..5) = Solid Red (H=0) for manual grid alert
    for (uint32_t i = 0; i < 6; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 0);
        EXPECT_EQ(captured_pixels_[i].value, 255);
    }

    // Chase pixel at index 6 = Red (H=0)
    EXPECT_EQ(captured_pixels_[6].hue, 0);
    EXPECT_EQ(captured_pixels_[6].value, 255);
}

TEST_F(TankStripDisplayTest, ErrorTimeoutRendersCyanBaseAndOrangeBlinkingTopLed)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 500, .backup_mode = false, .is_full = false}}); // 5 LEDs base
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::ERROR_TIMEOUT, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::SOLAR}});

    // In ON phase (error_timer < 500ms)
    display_->process_frame(100);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Top base LED (index 4) should be Orange (H=30, HUE_TIMEOUT)
    EXPECT_EQ(captured_pixels_[4].hue, 30);
    EXPECT_EQ(captured_pixels_[4].value, 255);

    // In OFF phase (error_timer at 600ms)
    captured_pixels_.clear();
    display_->process_frame(500);
    ASSERT_EQ(captured_pixels_.size(), 10);
    EXPECT_EQ(captured_pixels_[4].hue, 180); // Reverts to Cyan
}

TEST_F(TankStripDisplayTest, ErrorContactorStuckRendersFullStripRedBreathing)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::ERROR_CONTACTOR_STUCK, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::SOLAR}});

    display_->process_frame(100);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // All LEDs must be Red (H=0) with pulsing brightness
    for (uint32_t i = 0; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 0);
        EXPECT_GT(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, OtaUpdatingRendersPurpleKnightRiderScanner)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{.type = DisplayCmdType::SET_OVERRIDE_PATTERN, .pattern = TankStripPattern::OTA_UPDATING});
    EXPECT_EQ(display_->get_override_pattern(), TankStripPattern::OTA_UPDATING);

    display_->process_frame(50);
    ASSERT_GE(captured_pixels_.size(), 10);

    // Center of scanner is Purple (H=280, HUE_OTA)
    bool has_purple = false;
    for (const auto& p : captured_pixels_) {
        if (p.hue == 280 && p.value == 255) {
            has_purple = true;
            break;
        }
    }
    EXPECT_TRUE(has_purple);
}

TEST_F(TankStripDisplayTest, BootSuccessRendersProgressiveGreenSweepAndRevertsToAuto)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{.type = DisplayCmdType::SET_OVERRIDE_PATTERN, .pattern = TankStripPattern::BOOT_SUCCESS});

    // Advance step by step (50ms per LED = 500ms total for 10 LEDs)
    for (uint32_t step = 0; step < 10; step++) {
        display_->process_frame(50);
    }

    // After 10 steps, all 10 LEDs are lit Green (HUE_BOOT_SUCCESS)
    captured_pixels_.clear();
    display_->process_frame(50);
    ASSERT_GE(captured_pixels_.size(), 10);
    for (uint32_t i = 0; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].hue, 120);
        EXPECT_EQ(captured_pixels_[i].value, 255);
    }

    // Advance past the 500ms hold -> pattern automatically reverts to AUTO
    display_->process_frame(550);
    EXPECT_EQ(display_->get_override_pattern(), TankStripPattern::AUTO);
}

TEST_F(TankStripDisplayTest, BootErrorRendersSosRedFlashes)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{.type = DisplayCmdType::SET_OVERRIDE_PATTERN, .pattern = TankStripPattern::BOOT_ERROR});

    // Flash 1 ON (time < 100ms)
    display_->process_frame(50);
    ASSERT_GE(captured_pixels_.size(), 10);
    EXPECT_EQ(captured_pixels_[0].hue, 0); // HUE_BOOT_ERROR
    EXPECT_EQ(captured_pixels_[0].value, 255);

    // Flash 1 OFF (time 150ms)
    captured_pixels_.clear();
    display_->process_frame(100);
    ASSERT_GE(captured_pixels_.size(), 10);
    EXPECT_EQ(captured_pixels_[0].value, 0);
}

TEST_F(TankStripDisplayTest, BrightnessScalesAllValueChannels)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 1000, .backup_mode = false, .is_full = false}}); // All 10 LEDs
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::UNKNOWN}});
    display_->process_frame(2000); // Past breathing

    // Set brightness to 50% (128/255)
    display_->process_command(DisplayCommand{.type = DisplayCmdType::SET_BRIGHTNESS, .brightness = 128});
    EXPECT_EQ(display_->get_brightness(), 128);

    captured_pixels_.clear();
    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);
    // Base Cyan (Val 200) scaled by 128/255 = ~100
    EXPECT_NEAR(captured_pixels_[0].value, 100, 2);

    // Set brightness to 0 -> all output should be 0
    display_->process_command(DisplayCommand{.type = DisplayCmdType::SET_BRIGHTNESS, .brightness = 0});
    captured_pixels_.clear();
    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);
    EXPECT_EQ(captured_pixels_[0].value, 0);
}

TEST_F(TankStripDisplayTest, IdleBackupMode_FloatFull_RendersAllAmberLeds)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 0, .backup_mode = true, .is_full = true}});
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::UNKNOWN}});

    EXPECT_TRUE(display_->is_backup_mode());
    EXPECT_TRUE(display_->is_float_full());

    // Advance past initial breathing timer
    display_->process_frame(700);
    captured_pixels_.clear();

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // All 10 LEDs must be Amber (H=35, HUE_BACKUP)
    for (uint32_t i = 0; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].index, i);
        EXPECT_EQ(captured_pixels_[i].hue, 35);
        EXPECT_GT(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, IdleBackupMode_FloatNotFull_RendersOnlyBaseAmberLed)
{
    InitAndClearCaptures();

    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 0, .backup_mode = true, .is_full = false}});
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::IDLE, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::UNKNOWN}});

    EXPECT_TRUE(display_->is_backup_mode());
    EXPECT_FALSE(display_->is_float_full());

    display_->process_frame(700);
    captured_pixels_.clear();

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base LED (0) is Amber (H=35)
    EXPECT_EQ(captured_pixels_[0].index, 0);
    EXPECT_EQ(captured_pixels_[0].hue, 35);
    EXPECT_GT(captured_pixels_[0].value, 0);

    // LEDs 1..9 are off
    for (uint32_t i = 1; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].index, i);
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }
}

TEST_F(TankStripDisplayTest, FillingAutoSolar_BackupMode_RendersAmberBaseAndGreenChase)
{
    InitAndClearCaptures();

    // Backup mode not full -> 1 base LED (Amber), chase on remaining 9 LEDs
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::SET_LEVEL,
        .level_data = {.level_permille = 0, .backup_mode = true, .is_full = false}});
    display_->process_command(DisplayCommand{
        .type = DisplayCmdType::UPDATE_STATE,
        .state_data = {.state = farm::LoadState::RUNNING, .mode = farm::ControlMode::AUTO, .source = farm::PowerSource::SOLAR}});

    display_->process_frame(50);
    ASSERT_EQ(captured_pixels_.size(), 10);

    // Base LED (0) = Amber (H=35)
    EXPECT_EQ(captured_pixels_[0].hue, 35);

    // Chase pixel at index 1 = Green (H=120)
    EXPECT_EQ(captured_pixels_[1].index, 1);
    EXPECT_EQ(captured_pixels_[1].hue, 120);
    EXPECT_EQ(captured_pixels_[1].value, 255);

    // Remaining above chase (2..9) = off
    for (uint32_t i = 2; i < 10; i++) {
        EXPECT_EQ(captured_pixels_[i].value, 0);
    }
}
