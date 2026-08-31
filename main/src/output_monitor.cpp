// main/src/output_monitor.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "output_monitor.hpp"

static const char* TAG = "OutputMonitor";

OutputMonitor::OutputMonitor(idf_hals::IGpioHAL& gpio, const OutputMonitorConfig& config)
    : gpio_(gpio)
    , config_(config)
{
}

esp_err_t OutputMonitor::init()
{
    if (config_.gpio_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Cannot init OutputMonitor: Invalid GPIO pin (NC)");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << config_.gpio_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = config_.pull_up_en ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = config_.pull_down_en ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_.config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", config_.gpio_pin, esp_err_to_name(err));
        return err;
    }

    initialized_ = true;
    ESP_LOGI(
        TAG,
        "OutputMonitor initialized on GPIO %d (active_level=%u, pull_up=%d, pull_down=%d)",
        config_.gpio_pin,
        config_.active_level,
        config_.pull_up_en,
        config_.pull_down_en);

    return ESP_OK;
}

bool OutputMonitor::has_any_output_energy() const
{
    if (!initialized_) {
        ESP_LOGW(TAG, "has_any_output_energy called before init!");
        return false;
    }

    int level = gpio_.get_level(config_.gpio_pin);
    return (level == config_.active_level);
}
