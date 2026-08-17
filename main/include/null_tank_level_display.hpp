// main/include/null_tank_level_display.hpp
#pragma once

#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_tank_level_display.hpp"

/**
 * @class NullTankLevelDisplay
 * @brief Phase 1 stub implementation for tank level visual indicator.
 */
class NullTankLevelDisplay : public ITankLevelDisplay
{
public:
    ~NullTankLevelDisplay() override = default;

    esp_err_t init() override
    {
        ESP_LOGI("NullTankLevelDisplay", "NullTankLevelDisplay initialized (software stub)");
        return ESP_OK;
    }

    void set_level(uint16_t permille) override
    {
        level_permille_ = permille;
        ESP_LOGI("NullTankLevelDisplay", "Tank level updated to %u permille (%u%%)",
                 permille, permille / 10);
    }

    uint16_t get_level() const override
    {
        return level_permille_;
    }

private:
    uint16_t level_permille_{0};
};
