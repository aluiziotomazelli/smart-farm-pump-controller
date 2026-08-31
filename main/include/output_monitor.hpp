// main/include/output_monitor.hpp
#pragma once

#include "interfaces/i_hal_gpio.hpp"
#include "interfaces/i_output_monitor.hpp"
#include "pump_types.hpp"

/**
 * @class OutputMonitor
 * @brief Polling-based hardware output voltage presence sensor driver.
 *
 * Checks whether AC voltage is present at the motor output terminals
 * by querying an optoisolated digital input via IGpioHAL.
 */
class OutputMonitor : public IOutputMonitor
{
public:
    OutputMonitor(idf_hals::IGpioHAL& gpio, const OutputMonitorConfig& config = OutputMonitorConfig{});

    ~OutputMonitor() override = default;

    /** @copydoc IOutputMonitor::init */
    esp_err_t init() override;

    /** @copydoc IOutputMonitor::has_any_output_energy */
    bool has_any_output_energy() const override;

    /**
     * @brief Gets current configuration.
     */
    const OutputMonitorConfig& get_config() const { return config_; }

private:
    idf_hals::IGpioHAL& gpio_;
    OutputMonitorConfig config_;
    bool initialized_{false};
};
