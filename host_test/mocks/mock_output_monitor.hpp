#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_output_monitor.hpp"

class MockOutputMonitor : public IOutputMonitor
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(bool, has_contactor1_energy, (), (const, override));
    MOCK_METHOD(bool, has_contactor2_energy, (), (const, override));
    MOCK_METHOD(bool, has_any_output_energy, (), (const, override));
};
