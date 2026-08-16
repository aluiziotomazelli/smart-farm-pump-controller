#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "hal_freertos.hpp"

static const char* TAG = "main";

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Starting Smart Farm Pump Controller...");
}
