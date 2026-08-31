#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "output_monitor.hpp"
#include "mock_hal_gpio.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class OutputMonitorTest : public ::testing::Test
{
protected:
    NiceMock<idf_hals::MockGpioHAL> hal_gpio_;
    OutputMonitorConfig config_{
        .gpio_pin = GPIO_NUM_10,
        .active_level = 1,
        .pull_down_en = true,
        .pull_up_en = false};

    std::unique_ptr<OutputMonitor> sut_;

    void SetUp() override
    {
        ON_CALL(hal_gpio_, config(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_gpio_, get_level(_)).WillByDefault(Return(0));

        sut_ = std::make_unique<OutputMonitor>(hal_gpio_, config_);
    }
};

TEST_F(OutputMonitorTest, InitConfiguresInputGpioWithPullDown)
{
    EXPECT_CALL(
        hal_gpio_,
        config(::testing::AllOf(
            ::testing::Field(&gpio_config_t::mode, GPIO_MODE_INPUT),
            ::testing::Field(&gpio_config_t::pull_down_en, GPIO_PULLDOWN_ENABLE),
            ::testing::Field(&gpio_config_t::pull_up_en, GPIO_PULLUP_DISABLE))))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(OutputMonitorTest, InitRejectsInvalidGpioNC)
{
    OutputMonitorConfig invalid_cfg{.gpio_pin = GPIO_NUM_NC};
    OutputMonitor invalid_sut(hal_gpio_, invalid_cfg);

    EXPECT_CALL(hal_gpio_, config(_)).Times(0);
    EXPECT_EQ(invalid_sut.init(), ESP_ERR_INVALID_ARG);
}

TEST_F(OutputMonitorTest, CallingHasEnergyBeforeInitReturnsFalse)
{
    // Calling before init should return false and not crash
    EXPECT_FALSE(sut_->has_any_output_energy());
}

TEST_F(OutputMonitorTest, ActiveHighDetectsPresenceWhenPinIsHigh)
{
    EXPECT_EQ(sut_->init(), ESP_OK);

    EXPECT_CALL(hal_gpio_, get_level(GPIO_NUM_10)).WillOnce(Return(1));
    EXPECT_TRUE(sut_->has_any_output_energy());

    EXPECT_CALL(hal_gpio_, get_level(GPIO_NUM_10)).WillOnce(Return(0));
    EXPECT_FALSE(sut_->has_any_output_energy());
}

TEST_F(OutputMonitorTest, ActiveLowDetectsPresenceWhenPinIsLow)
{
    OutputMonitorConfig active_low_cfg{
        .gpio_pin = GPIO_NUM_10,
        .active_level = 0,
        .pull_down_en = false,
        .pull_up_en = true};
    OutputMonitor active_low_sut(hal_gpio_, active_low_cfg);

    EXPECT_EQ(active_low_sut.init(), ESP_OK);

    EXPECT_CALL(hal_gpio_, get_level(GPIO_NUM_10)).WillOnce(Return(0));
    EXPECT_TRUE(active_low_sut.has_any_output_energy());

    EXPECT_CALL(hal_gpio_, get_level(GPIO_NUM_10)).WillOnce(Return(1));
    EXPECT_FALSE(active_low_sut.has_any_output_energy());
}
