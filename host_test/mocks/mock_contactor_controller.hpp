#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_contactor_controller.hpp"

class MockContactorController : public IContactorController
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, activate, (farm::PowerSource source), (override));
    MOCK_METHOD(esp_err_t, deactivate, (), (override));
    MOCK_METHOD(farm::PowerSource, get_active_source, (), (const, override));
    MOCK_METHOD(bool, is_active, (), (const, override));
};
