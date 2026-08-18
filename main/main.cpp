// main/main.cpp
#include <cstdint>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "hal_timer.hpp"
#include "hal_nvs.hpp"
#include "hal_sys_rom.hpp"
#include "hal_gpio.hpp"
#include "hal_freertos.hpp"
#include "hal_system.hpp"
#include "hal_system_time.hpp"
#include "hal_sntp.hpp"

#include "farm_protocol_types.hpp"
#include "persistence_backend.hpp"
#include "nvs_core.hpp"
#include "pump_nvs.hpp"
#include "pump_stats.hpp"
#include "espnow_manager.hpp"
#include "wifi_manager.hpp"
#include "time_manager.hpp"
#include "led_controller.hpp"
#include "contactor_controller.hpp"
#include "pump_led_controller.hpp"
#include "null_output_monitor.hpp"
#include "null_tank_level_display.hpp"
#include "pump_state_machine.hpp"
#include "pump_command_handler.hpp"
#include "pump_status_reporter.hpp"
#include "pump_controller.hpp"
#include "button.hpp"
#include "switch.hpp"

static const char* TAG = "main";

// Pinout mapping for Seeed Studio XIAO ESP32-C3
static constexpr gpio_num_t PIN_BUTTON_ACTION  = GPIO_NUM_2;  // D0
static constexpr gpio_num_t PIN_SWITCH_MODE    = GPIO_NUM_3;  // D1
static constexpr gpio_num_t PIN_SWITCH_SOURCE  = GPIO_NUM_4;  // D2
static constexpr gpio_num_t PIN_CONTACTOR_GRID = GPIO_NUM_5;  // D3
static constexpr gpio_num_t PIN_CONTACTOR_SOLAR= GPIO_NUM_6;  // D4
static constexpr gpio_num_t PIN_LED_GRID       = GPIO_NUM_21; // D6
static constexpr gpio_num_t PIN_LED_SOLAR      = GPIO_NUM_20; // D7
static constexpr gpio_num_t PIN_BUTTON_BOOT_OTA= GPIO_NUM_9;  // D9 (Onboard BOOT)

static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* PUMP_STATS_NVS_KEY = "pump_stats";

// HAL instances
static idf_hals::TimerHAL hal_timer;
static idf_hals::NvsHAL nvs_hal;
static idf_hals::SysRomHAL hal_sys_rom;
static idf_hals::GpioHAL hal_gpio;
static idf_hals::HalFreertos hal_freertos;
static idf_hals::SystemHAL hal_system;
static idf_hals::HalSystemTime hal_sys_time;
static idf_hals::HalSntp hal_sntp;

// Persistence: Core Storage
static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{nvs_hal, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

// Persistence: Pump Stats Storage
static RTC_DATA_ATTR PumpStorage g_rtc_pump_stats;
static RtcBackend rtc_pump_backend(&g_rtc_pump_stats, sizeof(PumpStorage));
static NvsBackend nvs_pump_backend{nvs_hal, PUMP_STATS_NVS_KEY};
static PumpNvs pump_nvs{rtc_pump_backend, nvs_pump_backend};

// Time Manager
static time_manager::TimeManager time_mgr{hal_sntp, hal_sys_time};

// Actuation & Displays
static ContactorConfig contactor_config{
    .grid_gpio = PIN_CONTACTOR_GRID,
    .solar_gpio = PIN_CONTACTOR_SOLAR,
    .active_level = 1, // 1 for MOC/TRIAC (Active-High), 0 for Relay
    .demagnetization_delay_ms = 150};
static ContactorController contactor_ctrl{hal_gpio, hal_freertos, contactor_config};

static NullOutputMonitor output_monitor;
static NullTankLevelDisplay tank_display;

// LED Controllers
static LedConfig led_grid_config{
    .gpio_num = PIN_LED_GRID,
    .task_stack_size = 2048,
    .task_priority = 1,
    .active_level = 1};
static LedController led_grid{hal_gpio, hal_freertos, led_grid_config};

static LedConfig led_solar_config{
    .gpio_num = PIN_LED_SOLAR,
    .task_stack_size = 2048,
    .task_priority = 1,
    .active_level = 1};
static LedController led_solar{hal_gpio, hal_freertos, led_solar_config};

static PumpLedController pump_leds{led_grid, led_solar};

// State Machine
static PumpStateMachineConfig fsm_config{
    .default_watchdog_s = 3600,
    .demagnetization_delay_ms = 150,
    .enable_output_validation = false};
static PumpStateMachine state_machine{contactor_ctrl, output_monitor, fsm_config};

// Telemetry Reporter
static PumpStatusReporterConfig reporter_config{
    .circuit_id = 0,
    .heartbeat_interval_ms = 5000,
    .dest_node_id = farm::NodeId::HUB,
    .require_ack = false};
static PumpStatusReporter status_reporter{
    espnow::EspNowManager::instance(), state_machine, hal_timer, reporter_config};

// UI Inputs (Switches and Buttons)
static ui_inputs::SwitchConfig switch_cfg{
    .debounce_ms = 50,
    .enable_internal_pull = true};
static ui_inputs::Switch switch_mode{hal_gpio, hal_timer, PIN_SWITCH_MODE, true, switch_cfg};
static ui_inputs::Switch switch_source{hal_gpio, hal_timer, PIN_SWITCH_SOURCE, true, switch_cfg};

static ui_inputs::ButtonConfig button_cfg{
    .debounce_press_ms = 50,
    .debounce_release_ms = 50,
    .double_click_ms = 300,
    .long_click_ms = 1000,
    .very_long_click_ms = 3000,
    .timeout_ms = 6000,
    .enable_internal_pull = true};
static ui_inputs::Button button_action{hal_gpio, hal_timer, PIN_BUTTON_ACTION, true, button_cfg};

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Starting Smart Farm Pump Controller...");

    // Create ESP-NOW receive queue
    QueueHandle_t rx_queue = hal_freertos.queue_create(30, sizeof(espnow::AppMessage));

    // Get singletons
    auto& espnow = espnow::EspNowManager::instance();
    auto& wifi = wifi_manager::WiFiManager::get_instance();

    // Instantiate Command Handler
    PumpCommandHandler command_handler{
        rx_queue,
        espnow,
        state_machine,
        time_mgr,
        tank_display,
        hal_freertos};

    // Instantiate Main Orchestrator
    PumpController pump_controller{
        nvs_core,
        pump_nvs,
        state_machine,
        command_handler,
        status_reporter,
        pump_leds,
        tank_display,
        switch_mode,
        switch_source,
        button_action,
        wifi,
        hal_freertos,
        hal_system};

    // Initialize all components and load persisted state
    esp_err_t err = pump_controller.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PumpController: %s", esp_err_to_name(err));
        return;
    }

    // Start background tasks
    err = pump_controller.start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PumpController: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Smart Farm Pump Controller started successfully");
}
