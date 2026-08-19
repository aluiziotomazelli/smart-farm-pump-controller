// host_test/mocks/mock_tank_strip_display.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_tank_strip_display.hpp"

class MockTankStripDisplay : public ITankStripDisplay
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(void, set_level, (uint16_t permille), (override));
    MOCK_METHOD(uint16_t, get_level, (), (const, override));
    MOCK_METHOD(void, update_state, (farm::LoadState state, farm::ControlMode mode, farm::PowerSource source), (override));
    MOCK_METHOD(void, set_override_pattern, (TankStripPattern pattern), (override));
    MOCK_METHOD(TankStripPattern, get_override_pattern, (), (const, override));
    MOCK_METHOD(void, set_brightness, (uint8_t brightness), (override));
    MOCK_METHOD(uint8_t, get_brightness, (), (const, override));
    MOCK_METHOD(void, tick, (uint32_t delta_ms), (override));
    MOCK_METHOD(void, clear, (), (override));
};
