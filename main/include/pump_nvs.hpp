// main/include/pump_nvs.hpp
#pragma once

#include "app_storage.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_pump_nvs.hpp"
#include "pump_stats.hpp"

/**
 * @class PumpNvs
 * @brief Persistent storage handler for the Pump Controller application.
 */
class PumpNvs : public IPumpNvs,
                public AppStorage<PumpStats, PUMP_STATS_MAGIC, PUMP_STATS_VERSION>
{
public:
    PumpNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
        : AppStorage<PumpStats, PUMP_STATS_MAGIC, PUMP_STATS_VERSION>(rtc_stats, nvs_stats, "PumpNvs")
    {
    }

    /** @copydoc IPumpNvs::init_app_data */
    esp_err_t init_app_data(PumpStats& stats, const PumpStats& default_stats) override
    {
        return init_app_data_impl(stats, default_stats);
    }

    /** @copydoc IPumpNvs::load_app_data */
    esp_err_t load_app_data(PumpStats& stats) override
    {
        return load_app_data_impl(stats);
    }

    /** @copydoc IPumpNvs::save_app_data */
    esp_err_t save_app_data(const PumpStats& stats, bool force_nvs_commit = false) override
    {
        return save_app_data_impl(stats, force_nvs_commit);
    }
};
