// main/src/contactor_controller.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_contactor_controller.hpp"
#include "interfaces/i_hal_gpio.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "contactor_controller.hpp"

static const char* TAG = "ContactorController";

ContactorController::ContactorController(
    idf_hals::IGpioHAL& hal_gpio,
    idf_hals::IHalFreertos& hal_rtos,
    const ContactorConfig& config)
    : hal_gpio_(hal_gpio)
    , hal_rtos_(hal_rtos)
    , config_(config)
{
}

ContactorController::~ContactorController()
{
    deactivate();
}

esp_err_t ContactorController::init()
{
    if (config_.grid_gpio == GPIO_NUM_NC || config_.solar_gpio == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid GPIO pins configured for contactors");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t pin_mask = (1ULL << config_.grid_gpio) | (1ULL << config_.solar_gpio);
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = (config_.active_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (config_.active_level == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = hal_gpio_.config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure contactor GPIOs: %s", esp_err_to_name(err));
        return err;
    }

    deactivate();
    ESP_LOGI(TAG, "ContactorController initialized (Grid GPIO %d, Solar GPIO %d, active_%s, pull_%s)",
             config_.grid_gpio, config_.solar_gpio,
             config_.active_level ? "high" : "low",
             config_.active_level ? "down" : "up");
    return ESP_OK;
}

void ContactorController::apply_gpio_state(farm::PowerSource source, bool active)
{
    uint32_t level = active ? config_.active_level : (config_.active_level ? 0 : 1);
    if (source == farm::PowerSource::GRID) {
        hal_gpio_.set_level(config_.grid_gpio, level);
    } else if (source == farm::PowerSource::SOLAR) {
        hal_gpio_.set_level(config_.solar_gpio, level);
    }
}

esp_err_t ContactorController::activate(farm::PowerSource source)
{
    if (source != farm::PowerSource::GRID && source != farm::PowerSource::SOLAR) {
        ESP_LOGE(TAG, "Cannot activate invalid power source %d", static_cast<int>(source));
        return ESP_ERR_INVALID_ARG;
    }

    if (active_source_ == source) {
        return ESP_OK; // Already active on requested source
    }

    if (active_source_ != farm::PowerSource::UNKNOWN) {
        // Switching from an already active source -> apply demagnetization delay
        ESP_LOGI(TAG, "Switching source from %d to %d (applying %lu ms delay)",
                 static_cast<int>(active_source_), static_cast<int>(source), config_.demagnetization_delay_ms);
        apply_gpio_state(active_source_, false);
        if (config_.demagnetization_delay_ms > 0) {
            hal_rtos_.task_delay(pdMS_TO_TICKS(config_.demagnetization_delay_ms));
        }
    }

    apply_gpio_state(source, true);
    active_source_ = source;
    ESP_LOGI(TAG, "Contactor activated for source %d", static_cast<int>(source));
    return ESP_OK;
}

esp_err_t ContactorController::deactivate()
{
    apply_gpio_state(farm::PowerSource::GRID, false);
    apply_gpio_state(farm::PowerSource::SOLAR, false);
    active_source_ = farm::PowerSource::UNKNOWN;
    ESP_LOGI(TAG, "All contactors deactivated");
    return ESP_OK;
}
