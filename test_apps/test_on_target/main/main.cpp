#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_test_runner.h"

extern "C" void app_main(void)
{
    // Disable Task Watchdog to avoid triggers in Unity interactive menu loop
    esp_task_wdt_deinit();

    // Give some time for UART serial monitor to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    unity_run_menu();
}
