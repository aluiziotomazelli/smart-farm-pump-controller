// main/src/pump_led_controller.cpp
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_pump_led_controller.hpp"
#include "interfaces/i_led_controller.hpp"
#include "pump_led_controller.hpp"
#include "farm_protocol_types.hpp"

static const char* TAG = "PumpLedController";

PumpLedController::PumpLedController(
    ILedController& led_grid,
    ILedController& led_solar)
    : led_grid_(led_grid)
    , led_solar_(led_solar)
{
}

esp_err_t PumpLedController::init()
{
    esp_err_t err = led_grid_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Grid LED: %s", esp_err_to_name(err));
        return err;
    }

    err = led_solar_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Solar LED: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PumpLedController initialized successfully");
    return ESP_OK;
}

esp_err_t PumpLedController::start()
{
    esp_err_t err = led_grid_.start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Grid LED task: %s", esp_err_to_name(err));
        return err;
    }

    err = led_solar_.start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Solar LED task: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PumpLedController started successfully");
    return ESP_OK;
}

void PumpLedController::stop()
{
    led_grid_.stop();
    led_solar_.stop();
    ESP_LOGI(TAG, "PumpLedController stopped");
}

void PumpLedController::update(farm::LoadState state, farm::PowerSource source)
{
    switch (state) {
    case farm::LoadState::IDLE:
        led_grid_.set_pattern(BlinkPattern::OFF);
        led_solar_.set_pattern(BlinkPattern::OFF);
        break;

    case farm::LoadState::RUNNING:
        if (source == farm::PowerSource::GRID) {
            led_grid_.set_pattern(BlinkPattern::IDLE_BEACON);
            led_solar_.set_pattern(BlinkPattern::OFF);
        } else if (source == farm::PowerSource::SOLAR) {
            led_grid_.set_pattern(BlinkPattern::OFF);
            led_solar_.set_pattern(BlinkPattern::IDLE_BEACON);
        } else {
            led_grid_.set_pattern(BlinkPattern::OFF);
            led_solar_.set_pattern(BlinkPattern::OFF);
        }
        break;

    case farm::LoadState::ERROR_NO_SOURCE:
        if (source == farm::PowerSource::GRID) {
            led_grid_.set_pattern(BlinkPattern::ERROR_BURST);
            led_solar_.set_pattern(BlinkPattern::OFF);
        } else if (source == farm::PowerSource::SOLAR) {
            led_grid_.set_pattern(BlinkPattern::OFF);
            led_solar_.set_pattern(BlinkPattern::ERROR_BURST);
        } else {
            led_grid_.set_pattern(BlinkPattern::ERROR_BURST);
            led_solar_.set_pattern(BlinkPattern::ERROR_BURST);
        }
        break;

    case farm::LoadState::ERROR_CONTACTOR_STUCK:
    case farm::LoadState::ERROR_TIMEOUT:
        led_grid_.set_pattern(BlinkPattern::ERROR_BURST);
        led_solar_.set_pattern(BlinkPattern::ERROR_BURST);
        break;

    default:
        led_grid_.set_pattern(BlinkPattern::OFF);
        led_solar_.set_pattern(BlinkPattern::OFF);
        break;
    }
}
