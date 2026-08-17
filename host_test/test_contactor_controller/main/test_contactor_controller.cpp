// host_test/test_contactor_controller/main/test_contactor_controller.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "contactor_controller.hpp"
#include "mock_hal_gpio.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

class ContactorControllerTest : public ::testing::Test
{
protected:
    NiceMock<idf_hals::MockGpioHAL> hal_gpio_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    ContactorConfig config_{
        .grid_gpio = GPIO_NUM_5,
        .solar_gpio = GPIO_NUM_6,
        .active_level = 1, // Active-High (MOC+TRIAC)
        .demagnetization_delay_ms = 150};

    std::unique_ptr<ContactorController> sut_;

    void SetUp() override
    {
        ON_CALL(hal_gpio_, config(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_gpio_, set_level(_, _)).WillByDefault(Return(ESP_OK));
        EXPECT_CALL(hal_gpio_, set_level(_, _)).Times(::testing::AnyNumber());

        sut_ = std::make_unique<ContactorController>(hal_gpio_, hal_rtos_, config_);
    }
};

TEST_F(ContactorControllerTest, InitConfiguresGpiosAndSetsInactiveLevels)
{
    EXPECT_CALL(hal_gpio_, config(_)).WillOnce(Return(ESP_OK));
    // Inactive Grid (active-high) = 0; Inactive Solar (active-high) = 0
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_5, 0)).Times(::testing::AtLeast(1));
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_6, 0)).Times(::testing::AtLeast(1));

    EXPECT_EQ(sut_->init(), ESP_OK);
    EXPECT_FALSE(sut_->is_active());
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
}

TEST_F(ContactorControllerTest, InitConfiguresPullUpForActiveLowRelay)
{
    ContactorConfig relay_cfg{
        .grid_gpio = GPIO_NUM_5,
        .solar_gpio = GPIO_NUM_6,
        .active_level = 0, // Active-Low (Relay)
        .demagnetization_delay_ms = 150};
    ContactorController relay_sut(hal_gpio_, hal_rtos_, relay_cfg);

    EXPECT_CALL(hal_gpio_, config(::testing::Field(&gpio_config_t::pull_up_en, GPIO_PULLUP_ENABLE)))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(relay_sut.init(), ESP_OK);
}

TEST_F(ContactorControllerTest, InitFailsWithInvalidGpio)
{
    ContactorConfig invalid_cfg{.grid_gpio = GPIO_NUM_NC, .solar_gpio = GPIO_NUM_6};
    ContactorController invalid_sut(hal_gpio_, hal_rtos_, invalid_cfg);

    EXPECT_EQ(invalid_sut.init(), ESP_ERR_INVALID_ARG);
}

TEST_F(ContactorControllerTest, ActivateGridSetsGridActive)
{
    // Active Grid (active-high) = 1
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_5, 1)).Times(1);

    EXPECT_EQ(sut_->activate(farm::PowerSource::GRID), ESP_OK);
    EXPECT_TRUE(sut_->is_active());
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::GRID);

    // Idempotent second activation doesn't toggle
    EXPECT_EQ(sut_->activate(farm::PowerSource::GRID), ESP_OK);
}

TEST_F(ContactorControllerTest, ActivateSolarSetsSolarActive)
{
    // Active Solar (active-high) = 1
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_6, 1)).Times(1);

    EXPECT_EQ(sut_->activate(farm::PowerSource::SOLAR), ESP_OK);
    EXPECT_TRUE(sut_->is_active());
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::SOLAR);
}

TEST_F(ContactorControllerTest, SwitchingSourcesAppliesDemagnetizationDelay)
{
    // First activate SOLAR
    ASSERT_EQ(sut_->activate(farm::PowerSource::SOLAR), ESP_OK);

    InSequence seq;
    // 1. Deactivate SOLAR (level = 0)
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_6, 0)).Times(1);
    // 2. Demagnetization delay
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(150))).Times(1);
    // 3. Activate GRID (level = 1)
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_5, 1)).Times(1);

    EXPECT_EQ(sut_->activate(farm::PowerSource::GRID), ESP_OK);
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::GRID);
}

TEST_F(ContactorControllerTest, DeactivateTurnsOffBothContactors)
{
    ASSERT_EQ(sut_->activate(farm::PowerSource::SOLAR), ESP_OK);

    // Grid inactive = 0, Solar inactive = 0
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_5, 0)).Times(::testing::AtLeast(1));
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_6, 0)).Times(::testing::AtLeast(1));

    EXPECT_EQ(sut_->deactivate(), ESP_OK);
    EXPECT_FALSE(sut_->is_active());
    EXPECT_EQ(sut_->get_active_source(), farm::PowerSource::UNKNOWN);
}

TEST_F(ContactorControllerTest, ActivateRejectsInvalidSource)
{
    EXPECT_EQ(sut_->activate(farm::PowerSource::UNKNOWN), ESP_ERR_INVALID_ARG);
}
