#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_tank_level_display.hpp"

class MockTankLevelDisplay : public ITankLevelDisplay
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(void, set_level, (uint16_t permille), (override));
    MOCK_METHOD(uint16_t, get_level, (), (const, override));
};
