// main/include/interfaces/i_pump_nvs.hpp
#pragma once

#include "esp_err.h"
#include "pump_stats.hpp"

/**
 * @class IPumpNvs
 * @brief Interface for persisting pump controller statistics and operational state.
 *
 * Abstracts both RTC RAM and NVS flash storage for host testability.
 */
class IPumpNvs
{
public:
    virtual ~IPumpNvs() = default;

    /**
     * @brief Initializes the pump application statistics and state.
     * @param[out] stats Populated with loaded or default data.
     * @param[in] default_stats Default data to persist if load fails.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t init_app_data(PumpStats& stats, const PumpStats& default_stats) = 0;

    /**
     * @brief Loads the pump application statistics and state.
     * @param[out] stats Populated with loaded data.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t load_app_data(PumpStats& stats) = 0;

    /**
     * @brief Persists the pump application statistics and state.
     * @param[in] stats Data struct containing the statistics to save.
     * @param[in] force_nvs_commit If true, forces an immediate commit to Flash NVS.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t save_app_data(const PumpStats& stats, bool force_nvs_commit = false) = 0;
};
