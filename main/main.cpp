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
#include "hal_led_strip.hpp"

#include "farm_protocol_types.hpp"
#include "persistence_backend.hpp"
#include "nvs_core.hpp"
#include "pump_nvs.hpp"
#include "pump_stats.hpp"
#include "espnow_manager.hpp"
#include "wifi_manager.hpp"
#include "time_manager.hpp"
#include "contactor_controller.hpp"
#include "tank_strip_display.hpp"
#include "null_output_monitor.hpp"
#include "pump_state_machine.hpp"
#include "pump_command_handler.hpp"
#include "pump_status_reporter.hpp"
#include "pump_controller.hpp"
#include "button.hpp"
#include "switch.hpp"

#include "http_client.hpp"
#include "manifest_parser.hpp"
#include "ota_session.hpp"
#include "system.hpp"
#include "task_scheduler.hpp"
#include "rollback_manager.hpp"
#include "ota_manager.hpp"
#include "ota_controller.hpp"
#include "button_ota_trigger.hpp"

static const char* TAG = "main";

// Pinout mapping for Seeed Studio XIAO ESP32-C3
static constexpr gpio_num_t PIN_BUTTON_ACTION   = GPIO_NUM_2; // D0
static constexpr gpio_num_t PIN_SWITCH_MODE     = GPIO_NUM_3; // D1
static constexpr gpio_num_t PIN_SWITCH_SOURCE   = GPIO_NUM_4; // D2
static constexpr gpio_num_t PIN_CONTACTOR_GRID  = GPIO_NUM_5; // D3
static constexpr gpio_num_t PIN_CONTACTOR_SOLAR = GPIO_NUM_6; // D4
static constexpr gpio_num_t PIN_LED_STRIP_DATA  = GPIO_NUM_7; // D5 (Addressable WS2812 strip DIN)
static constexpr gpio_num_t PIN_BUTTON_BOOT_OTA = GPIO_NUM_9; // D9 (Onboard BOOT button)

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
static HalLedStrip hal_led_strip;

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

// Addressable LED Strip Unified Display (Level + Pump & OTA status)
static TankStripConfig strip_cfg{
    .gpio_pin = PIN_LED_STRIP_DATA,
    .num_leds = 10,
    .default_brightness = 200,
    .rmt_resolution_hz = 10 * 1000 * 1000};
static TankStripDisplay tank_display{hal_led_strip, strip_cfg};

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

// OTA Manager & Controller
static HttpClient http_client;
static ManifestParser manifest_parser;
static OtaSession ota_session;
static System ota_system;
static TaskScheduler task_scheduler;
static RollbackManager rollback_manager;
static OtaDependencies ota_deps = {
    .http_client = http_client,
    .manifest_parser = manifest_parser,
    .ota_session = ota_session,
    .system = ota_system,
    .task_scheduler = task_scheduler,
    .rollback_manager = rollback_manager,
};
static OtaManager ota_manager(ota_deps);
static OtaController ota_controller(ota_manager, hal_freertos);

// Hardware Boot Button OTA Trigger (3000 ms long press)
static ButtonOtaTrigger btn_trigger(hal_gpio, hal_freertos, PIN_BUTTON_BOOT_OTA, 3000);

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
        tank_display,
        switch_mode,
        switch_source,
        button_action,
        wifi,
        ota_controller,
        btn_trigger,
        espnow,
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
