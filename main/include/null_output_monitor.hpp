// main/include/null_output_monitor.hpp
#pragma once

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_output_monitor.hpp"

/**
 * @class NullOutputMonitor
 * @brief Phase 1 stub implementation for output voltage monitoring.
 */
class NullOutputMonitor : public IOutputMonitor
{
public:
    ~NullOutputMonitor() override = default;

    esp_err_t init() override
    {
        ESP_LOGI("NullOutputMonitor", "NullOutputMonitor initialized (software stub)");
        return ESP_OK;
    }

    bool has_contactor1_energy() const override
    {
        return false;
    }

    bool has_contactor2_energy() const override
    {
        return false;
    }

    bool has_any_output_energy() const override
    {
        return false;
    }
};
