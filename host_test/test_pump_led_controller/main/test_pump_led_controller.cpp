// host_test/test_pump_led_controller/main/test_pump_led_controller.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "pump_led_controller.hpp"
#include "mock_led_controller.hpp"
#include "farm_protocol_types.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class PumpLedControllerTest : public ::testing::Test
{
protected:
    NiceMock<MockLedController> led_grid_;
    NiceMock<MockLedController> led_solar_;

    std::unique_ptr<PumpLedController> sut_;

    void SetUp() override
    {
        ON_CALL(led_grid_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(led_solar_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(led_grid_, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(led_solar_, start()).WillByDefault(Return(ESP_OK));

        sut_ = std::make_unique<PumpLedController>(led_grid_, led_solar_);
    }
};

TEST_F(PumpLedControllerTest, InitInitializesBothLeds)
{
    EXPECT_CALL(led_grid_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(led_solar_, init()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(PumpLedControllerTest, StartStartsBothLeds)
{
    EXPECT_CALL(led_grid_, start()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(led_solar_, start()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(PumpLedControllerTest, StopStopsBothLeds)
{
    EXPECT_CALL(led_grid_, stop()).Times(1);
    EXPECT_CALL(led_solar_, stop()).Times(1);

    sut_->stop();
}

TEST_F(PumpLedControllerTest, UpdateIdleSetsBothLedsOff)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::OFF)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::OFF)).Times(1);

    sut_->update(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN);
}

TEST_F(PumpLedControllerTest, UpdateRunningGridSetsGridActiveSolarOff)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::IDLE_BEACON)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::OFF)).Times(1);

    sut_->update(farm::LoadState::RUNNING, farm::PowerSource::GRID);
}

TEST_F(PumpLedControllerTest, UpdateRunningSolarSetsSolarActiveGridOff)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::OFF)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::IDLE_BEACON)).Times(1);

    sut_->update(farm::LoadState::RUNNING, farm::PowerSource::SOLAR);
}

TEST_F(PumpLedControllerTest, UpdateErrorNoSourceGridSetsGridBurstSolarOff)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::OFF)).Times(1);

    sut_->update(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::GRID);
}

TEST_F(PumpLedControllerTest, UpdateErrorNoSourceSolarSetsSolarBurstGridOff)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::OFF)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);

    sut_->update(farm::LoadState::ERROR_NO_SOURCE, farm::PowerSource::SOLAR);
}

TEST_F(PumpLedControllerTest, UpdateErrorContactorStuckSetsBothBurst)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);

    sut_->update(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::PowerSource::UNKNOWN);
}

TEST_F(PumpLedControllerTest, UpdateErrorTimeoutSetsBothBurst)
{
    EXPECT_CALL(led_grid_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);
    EXPECT_CALL(led_solar_, set_pattern(BlinkPattern::ERROR_BURST)).Times(1);

    sut_->update(farm::LoadState::ERROR_TIMEOUT, farm::PowerSource::UNKNOWN);
}
