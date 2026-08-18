// host_test/mocks/mock_pump_nvs.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_pump_nvs.hpp"

class MockPumpNvs : public IPumpNvs
{
public:
    MOCK_METHOD(esp_err_t, init_app_data, (PumpStats& stats, const PumpStats& default_stats), (override));
    MOCK_METHOD(esp_err_t, load_app_data, (PumpStats& stats), (override));
    MOCK_METHOD(esp_err_t, save_app_data, (const PumpStats& stats, bool force_nvs_commit), (override));
};
