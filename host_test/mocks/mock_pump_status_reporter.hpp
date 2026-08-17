#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_pump_status_reporter.hpp"

class MockPumpStatusReporter : public IPumpStatusReporter
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, send_status_report, (), (override));
    MOCK_METHOD(void, tick, (uint32_t delta_ms), (override));
    MOCK_METHOD(void, notify_state_change, (), (override));
};
