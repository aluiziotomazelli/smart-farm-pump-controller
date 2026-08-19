#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_pump_led_controller.hpp"

class MockPumpLedController : public IPumpLedController
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, update, (farm::LoadState state, farm::PowerSource source), (override));
    MOCK_METHOD(void, set_ota_updating, (), (override));
};
