#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "test_hardware_loopback.hpp"
#include "output_monitor.hpp"
#include "hal_gpio.hpp"
#include "hal_freertos.hpp"
#include "contactor_controller.hpp"
#include "pump_state_machine.hpp"
#include "switch.hpp"
#include "button.hpp"
#include "hal_timer.hpp"
#include "hal_led_strip.hpp"
#include "tank_strip_display.hpp"

static const char* TAG = "PumpOnTargetTest";

TEST_CASE("HIL: OutputMonitor Hardware Loopback (D6 -> D10)", "[hil][output_monitor]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    OutputMonitorConfig mon_cfg{
        .gpio_pin = hil::DUT_OUTPUT_MONITOR,
        .active_level = 1,
        .pull_down_en = true,
        .pull_up_en = false,
    };
    OutputMonitor monitor(real_gpio, mon_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, monitor.init());

    // 1. Injetar Nível Baixo (Sem Tensão AC)
    hil::set_output_voltage_present(false);
    TEST_ASSERT_FALSE_MESSAGE(monitor.has_any_output_energy(), "D6=0 should report NO energy on D10");

    // 2. Injetar Nível Alto (Presença de Tensão AC)
    hil::set_output_voltage_present(true);
    TEST_ASSERT_TRUE_MESSAGE(monitor.has_any_output_energy(), "D6=1 should report ENERGY present on D10");

    // 3. Voltar para Nível Baixo
    hil::set_output_voltage_present(false);
    TEST_ASSERT_FALSE_MESSAGE(monitor.has_any_output_energy(), "D6=0 should return to NO energy on D10");
}

TEST_CASE("HIL: 3-Position Selector Switch Loopback (D7->D2, D8->D1)", "[hil][switch]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    idf_hals::TimerHAL real_timer;

    ui_inputs::SwitchConfig sw_cfg{.debounce_ms = 20, .enable_internal_pull = true};
    ui_inputs::Switch sw_grid(real_gpio, real_timer, hil::DUT_SWITCH_GRID, true, sw_cfg);
    ui_inputs::Switch sw_solar(real_gpio, real_timer, hil::DUT_SWITCH_SOLAR, true, sw_cfg);

    TEST_ASSERT_EQUAL(ESP_OK, sw_grid.init());
    TEST_ASSERT_EQUAL(ESP_OK, sw_solar.init());

    // 1. Simular Posição AUTO (D7=1, D8=1)
    hil::set_switch_position_auto();
    for (int i = 0; i < 5; ++i) {
        sw_grid.update();
        sw_solar.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE_MESSAGE(sw_grid.get_state() == ui_inputs::SwitchState::OPEN, "Grid switch should be open in AUTO");
    TEST_ASSERT_TRUE_MESSAGE(
        sw_solar.get_state() == ui_inputs::SwitchState::OPEN, "Solar switch should be open in AUTO");

    // 2. Simular Posição GRID (D7=0, D8=1)
    hil::set_switch_position_grid();
    for (int i = 0; i < 5; ++i) {
        sw_grid.update();
        sw_solar.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE_MESSAGE(sw_grid.get_state() == ui_inputs::SwitchState::CLOSED, "Grid switch should be closed");
    TEST_ASSERT_TRUE_MESSAGE(sw_solar.get_state() == ui_inputs::SwitchState::OPEN, "Solar switch should be open");

    // 3. Simular Posição SOLAR (D7=1, D8=0)
    hil::set_switch_position_solar();
    for (int i = 0; i < 5; ++i) {
        sw_grid.update();
        sw_solar.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE_MESSAGE(sw_grid.get_state() == ui_inputs::SwitchState::OPEN, "Grid switch should be open");
    TEST_ASSERT_TRUE_MESSAGE(sw_solar.get_state() == ui_inputs::SwitchState::CLOSED, "Solar switch should be closed");
}

TEST_CASE("HIL: Real Contactor Interlock & Break-Before-Make (D3, D4)", "[hil][contactor]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    idf_hals::HalFreertos real_rtos;

    ContactorConfig cfg{
        .grid_gpio = hil::DUT_CONTACTOR_GRID,
        .solar_gpio = hil::DUT_CONTACTOR_SOLAR,
        .active_level = 1,
        .demagnetization_delay_ms = 100, // 100ms for fast test execution
    };
    ContactorController contactor(real_gpio, real_rtos, cfg);
    TEST_ASSERT_EQUAL(ESP_OK, contactor.init());

    // 1. Initial state: both inactive (0)
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // 2. Activate Grid: Grid=1, Solar=0
    TEST_ASSERT_EQUAL(ESP_OK, contactor.activate(farm::PowerSource::GRID));
    TEST_ASSERT_EQUAL(1, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // 3. Hot-switch to Solar: Solar=1, Grid=0
    TEST_ASSERT_EQUAL(ESP_OK, contactor.activate(farm::PowerSource::SOLAR));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(1, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // 4. Deactivate: both 0
    TEST_ASSERT_EQUAL(ESP_OK, contactor.deactivate());
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));
}

TEST_CASE("HIL: PumpStateMachine & OutputMonitor Safety Integration", "[hil][fsm_monitor]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    idf_hals::HalFreertos real_rtos;

    ContactorConfig contactor_cfg{
        .grid_gpio = hil::DUT_CONTACTOR_GRID,
        .solar_gpio = hil::DUT_CONTACTOR_SOLAR,
        .active_level = 1,
        .demagnetization_delay_ms = 50,
    };
    ContactorController contactor(real_gpio, real_rtos, contactor_cfg);
    contactor.init();

    OutputMonitorConfig mon_cfg{
        .gpio_pin = hil::DUT_OUTPUT_MONITOR,
        .active_level = 1,
        .pull_down_en = true,
        .pull_up_en = false,
    };
    OutputMonitor monitor(real_gpio, mon_cfg);
    monitor.init();

    PumpStateMachineConfig fsm_cfg{
        .default_watchdog_s = 60,
        .enable_output_validation = true,
        .nominal_power_w = 320,
    };
    PumpStateMachine fsm(contactor, monitor, fsm_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, fsm.init());

    // 1. Test Stuck Contactor in IDLE: Inject D6=1
    hil::set_output_voltage_present(true);
    fsm.tick(100);
    TEST_ASSERT_EQUAL(farm::LoadState::ERROR_CONTACTOR_STUCK, fsm.get_state());

    // Recover: remove D6=1, clear error by turning off
    hil::set_output_voltage_present(false);
    farm::LoadOffCommand off_cmd{.circuit_id = 0};
    fsm.handle_load_off(off_cmd);
    TEST_ASSERT_EQUAL(farm::LoadState::IDLE, fsm.get_state());

    // 2. Normal Activation with Voltage Present: Start LoadOn, inject D6=1
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 60,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fsm.handle_load_on(on_cmd));
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL(1, gpio_get_level(hil::DUT_CONTACTOR_GRID));

    hil::set_output_voltage_present(true);
    fsm.tick(600); // Past 500ms stabilization
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());

    // 3. Loss of Power in RUNNING: Drop D6 to 0 -> should trip ERROR_NO_SOURCE and drop Grid coil
    hil::set_output_voltage_present(false);
    fsm.tick(100);
    TEST_ASSERT_EQUAL(farm::LoadState::ERROR_NO_SOURCE, fsm.get_state());
    TEST_ASSERT_EQUAL_MESSAGE(
        0, gpio_get_level(hil::DUT_CONTACTOR_GRID), "Contactor must be deactivated on power loss!");
}

TEST_CASE("HIL: Manual Control, Button Push Action & Live Hot-Switching", "[hil][manual_control]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    idf_hals::HalFreertos real_rtos;
    idf_hals::TimerHAL real_timer;

    ContactorConfig contactor_cfg{
        .grid_gpio = hil::DUT_CONTACTOR_GRID,
        .solar_gpio = hil::DUT_CONTACTOR_SOLAR,
        .active_level = 1,
        .demagnetization_delay_ms = 100,
    };
    ContactorController contactor(real_gpio, real_rtos, contactor_cfg);
    contactor.init();

    OutputMonitorConfig mon_cfg{
        .gpio_pin = hil::DUT_OUTPUT_MONITOR,
        .active_level = 1,
        .pull_down_en = true,
        .pull_up_en = false,
    };
    OutputMonitor monitor(real_gpio, mon_cfg);
    monitor.init();

    PumpStateMachineConfig fsm_cfg{
        .default_watchdog_s = 60,
        .enable_output_validation = true,
        .nominal_power_w = 320,
    };
    PumpStateMachine fsm(contactor, monitor, fsm_cfg);
    fsm.init();

    ui_inputs::SwitchConfig sw_cfg{.debounce_ms = 20, .enable_internal_pull = true};
    ui_inputs::Switch sw_grid(real_gpio, real_timer, hil::DUT_SWITCH_GRID, true, sw_cfg);
    ui_inputs::Switch sw_solar(real_gpio, real_timer, hil::DUT_SWITCH_SOLAR, true, sw_cfg);
    sw_grid.init();
    sw_solar.init();

    ui_inputs::ButtonConfig btn_cfg{
        .debounce_press_ms = 20,
        .debounce_release_ms = 20,
        .double_click_ms = 100,
        .long_click_ms = 500,
        .very_long_click_ms = 1500,
        .enable_internal_pull = true,
    };
    ui_inputs::Button btn_action(real_gpio, real_timer, hil::DUT_BUTTON_ACTION, true, btn_cfg);
    btn_action.init();

    // Helper lambda to simulate PumpController tick loop
    auto update_inputs_and_fsm = [&](uint32_t delta_ms) {
        sw_grid.update();
        sw_solar.update();
        btn_action.update();

        bool solar_sel = (sw_solar.get_state() == ui_inputs::SwitchState::CLOSED);
        bool grid_sel = (sw_grid.get_state() == ui_inputs::SwitchState::CLOSED);
        farm::PowerSource locked = farm::PowerSource::UNKNOWN;
        if (solar_sel)
            locked = farm::PowerSource::SOLAR;
        else if (grid_sel)
            locked = farm::PowerSource::GRID;

        fsm.set_source_lock(locked);

        auto click = btn_action.get_last_click();
        if (click != ui_inputs::ButtonClickType::NONE_CLICK) {
            auto snap = fsm.get_snapshot();
            if (snap.state == farm::LoadState::RUNNING) {
                fsm.handle_operator_stop();
                hil::set_output_voltage_present(false); // Contactor opens immediately on stop
            }
            else if (locked != farm::PowerSource::UNKNOWN) {
                fsm.handle_operator_start(locked);
            }
        }
        fsm.tick(delta_ms);
    };

    // 1. Initial State in AUTO: IDLE, all contactors off
    hil::set_switch_position_auto();
    for (int i = 0; i < 5; ++i) {
        update_inputs_and_fsm(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_EQUAL(farm::LoadState::IDLE, fsm.get_state());
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // 2. Select GRID position on switch
    hil::set_switch_position_grid();
    for (int i = 0; i < 5; ++i) {
        update_inputs_and_fsm(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_EQUAL(farm::LoadState::IDLE, fsm.get_state()); // Still IDLE until button press!
    // 3. Operator presses Action Pushbutton -> Pump Starts on GRID
    gpio_set_level(hil::CTRL_BUTTON_ACTION, 0); // Press
    for (int i = 0; i < 5; ++i) {
        update_inputs_and_fsm(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gpio_set_level(hil::CTRL_BUTTON_ACTION, 1); // Release
    for (int i = 0; i < 20; ++i) {
        update_inputs_and_fsm(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL(farm::PowerSource::GRID, fsm.get_snapshot().active_source);
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_get_level(hil::DUT_CONTACTOR_GRID), "Grid contactor D3 must be ON");
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // Keep voltage present during run
    hil::set_output_voltage_present(true);
    for (int i = 0; i < 5; ++i) {
        update_inputs_and_fsm(50);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());

    // 4. Live Hot-Switching: Operator flips switch from GRID to SOLAR while RUNNING
    hil::set_switch_position_solar();
    for (int i = 0; i < 20; ++i) {
        update_inputs_and_fsm(20);
        // During hot-switch, contactor opens (500ms delay) then closes on Solar:
        if (gpio_get_level(hil::DUT_CONTACTOR_SOLAR) == 1) {
            hil::set_output_voltage_present(true);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL(farm::PowerSource::SOLAR, fsm.get_snapshot().active_source);
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_get_level(hil::DUT_CONTACTOR_GRID), "Grid contactor D3 must be OFF");
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_get_level(hil::DUT_CONTACTOR_SOLAR), "Solar contactor D4 must be ON");

    // 5. Operator presses Action Pushbutton to STOP
    gpio_set_level(hil::CTRL_BUTTON_ACTION, 0); // Press
    for (int i = 0; i < 5; ++i) {
        update_inputs_and_fsm(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gpio_set_level(hil::CTRL_BUTTON_ACTION, 1); // Release
    for (int i = 0; i < 20; ++i) {
        update_inputs_and_fsm(10);
        // Cut voltage as soon as contactor is opened:
        if (gpio_get_level(hil::DUT_CONTACTOR_SOLAR) == 0 && gpio_get_level(hil::DUT_CONTACTOR_GRID) == 0) {
            hil::set_output_voltage_present(false);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    hil::set_output_voltage_present(false);
    update_inputs_and_fsm(50);
    TEST_ASSERT_EQUAL(farm::LoadState::IDLE, fsm.get_state());
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_GRID));
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));
}

TEST_CASE("HIL: LED Strip Physical Visual Effects Demonstration", "[hil][led_strip_effects]")
{
    HalLedStrip hal_led_strip;
    idf_hals::HalFreertos hal_rtos;

    TankStripConfig strip_cfg{
        .gpio_pin = GPIO_NUM_7, // D5 on Xiao C3
        .num_leds = 20,
        .default_brightness = 50,
        .rmt_resolution_hz = 10 * 1000 * 1000,
    };
    TankStripDisplay display(hal_led_strip, hal_rtos, strip_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, display.init());
    TEST_ASSERT_EQUAL(ESP_OK, display.start());

    ESP_LOGI(TAG, "=== DEMO 1: Boot Success Pattern (Green Sweep) ===");
    display.set_override_pattern(TankStripPattern::BOOT_SUCCESS);
    vTaskDelay(pdMS_TO_TICKS(2000));
    display.set_override_pattern(TankStripPattern::AUTO);

    ESP_LOGI(TAG, "=== DEMO 2: Level Rising + Breathing Effect (0% -> 25% -> 50% -> 75% -> 100%) ===");
    uint16_t levels[] = {100, 250, 500, 750, 1000};
    for (uint16_t lvl : levels) {
        ESP_LOGI(TAG, "Displaying Level %u%% (Breathing)", lvl / 10);
        display.update_state(farm::LoadState::IDLE, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
        display.set_level(lvl, false, lvl >= 1000);
        vTaskDelay(pdMS_TO_TICKS(2000)); // Time to observe breathing cycle
    }

    ESP_LOGI(TAG, "=== DEMO 3: Filling in AUTO Mode (Cyan level + Blue/Amber upward chase) ===");
    display.set_level(400, false, false); // 40% full
    ESP_LOGI(TAG, "Filling AUTO on SOLAR source");
    display.update_state(farm::LoadState::RUNNING, farm::ControlMode::AUTO, farm::PowerSource::SOLAR);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Filling AUTO on GRID source");
    display.update_state(farm::LoadState::RUNNING, farm::ControlMode::AUTO, farm::PowerSource::GRID);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=== DEMO 4: Filling in MANUAL_RUN Mode (Solid highlight + Upward chase) ===");
    ESP_LOGI(TAG, "Filling MANUAL on SOLAR (Amber Solid + Chase)");
    display.update_state(farm::LoadState::RUNNING, farm::ControlMode::MANUAL_RUN, farm::PowerSource::SOLAR);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Filling MANUAL on GRID (Blue Solid + Chase)");
    display.update_state(farm::LoadState::RUNNING, farm::ControlMode::MANUAL_RUN, farm::PowerSource::GRID);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=== DEMO 5: Backup Mode (Float Switch / Amber Color) ===");
    ESP_LOGI(TAG, "Backup Mode: Tank Empty (1 LED breathing)");
    display.set_level(0, true, false); // Float switch empty
    display.update_state(farm::LoadState::IDLE, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Backup Mode: Filling (Amber base + Chase)");
    display.update_state(farm::LoadState::RUNNING, farm::ControlMode::AUTO, farm::PowerSource::SOLAR);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Backup Mode: Tank Full (All LEDs Amber)");
    display.set_level(1000, true, true); // Float switch full
    display.update_state(farm::LoadState::IDLE, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=== DEMO 6: Error States ===");
    ESP_LOGI(TAG, "ERROR: Timeout Alert (Blinking Top LED)");
    display.set_level(600, false, false);
    display.update_state(farm::LoadState::ERROR_TIMEOUT, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "=== DEMO 7: Error States ===");
    ESP_LOGI(TAG, "ERROR: Backup Timeout Alert (Blinking Top LED)");
    display.set_level(600, true, false);
    display.update_state(farm::LoadState::ERROR_TIMEOUT, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "ERROR: Contactor Stuck / Critical Fault (Fast Red Breathing Pulse)");
    display.update_state(farm::LoadState::ERROR_CONTACTOR_STUCK, farm::ControlMode::AUTO, farm::PowerSource::UNKNOWN);
    vTaskDelay(pdMS_TO_TICKS(4000));

    ESP_LOGI(TAG, "=== DEMO 7: OTA Updating (Purple Cylon Scanner) ===");
    display.set_override_pattern(TankStripPattern::OTA_UPDATING);
    vTaskDelay(pdMS_TO_TICKS(3000));

    display.stop();
    ESP_LOGI(TAG, "=== DEMO FINISHED ===");
}

TEST_CASE("HIL: Filling Watchdog Timeout Safety Protection", "[hil][watchdog]")
{
    hil::init_control_pins();

    idf_hals::GpioHAL real_gpio;
    idf_hals::HalFreertos real_rtos;

    ContactorConfig contactor_cfg{
        .grid_gpio = hil::DUT_CONTACTOR_GRID,
        .solar_gpio = hil::DUT_CONTACTOR_SOLAR,
        .active_level = 1,
        .demagnetization_delay_ms = 50,
    };
    ContactorController contactor(real_gpio, real_rtos, contactor_cfg);
    contactor.init();

    OutputMonitorConfig mon_cfg{
        .gpio_pin = hil::DUT_OUTPUT_MONITOR,
        .active_level = 1,
        .pull_down_en = true,
        .pull_up_en = false,
    };
    OutputMonitor monitor(real_gpio, mon_cfg);
    monitor.init();

    PumpStateMachineConfig fsm_cfg{
        .default_watchdog_s = 2, // 2 seconds default watchdog for fast test
        .enable_output_validation = true,
        .nominal_power_w = 320,
    };
    PumpStateMachine fsm(contactor, monitor, fsm_cfg);
    TEST_ASSERT_EQUAL(ESP_OK, fsm.init());

    // 1. Remote command LOAD_ON with 2 seconds watchdog
    farm::LoadOnCommand on_cmd{
        .circuit_id = 0,
        .power_source = farm::PowerSource::GRID,
        .watchdog_timeout_s = 2,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fsm.handle_load_on(on_cmd));
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_get_level(hil::DUT_CONTACTOR_GRID), "Grid contactor D3 must be ON");

    // Keep voltage present so output monitor doesn't trip
    hil::set_output_voltage_present(true);

    // 2. Refresh Watchdog at 1 second (Simulating periodic Hub fill command)
    fsm.tick(1000);
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL(1, gpio_get_level(hil::DUT_CONTACTOR_GRID));

    // Send watchdog refresh
    TEST_ASSERT_EQUAL(ESP_OK, fsm.handle_load_on(on_cmd));
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());

    // 3. Let Watchdog Expire (Advance 2.2 seconds without Hub refresh)
    fsm.tick(1000);
    TEST_ASSERT_EQUAL(farm::LoadState::RUNNING, fsm.get_state());
    TEST_ASSERT_EQUAL(1, gpio_get_level(hil::DUT_CONTACTOR_GRID));

    fsm.tick(1200); // Exceeds remaining 1000ms watchdog
    // The watchdog must trip:
    TEST_ASSERT_EQUAL(farm::LoadState::ERROR_TIMEOUT, fsm.get_state());
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_get_level(hil::DUT_CONTACTOR_GRID), "Contactor must be OFF immediately on watchdog timeout!");
    TEST_ASSERT_EQUAL(0, gpio_get_level(hil::DUT_CONTACTOR_SOLAR));

    // 4. Recover from ERROR_TIMEOUT via LOAD_OFF
    hil::set_output_voltage_present(false);
    farm::LoadOffCommand off_cmd{.circuit_id = 0};
    TEST_ASSERT_EQUAL(ESP_OK, fsm.handle_load_off(off_cmd));
    TEST_ASSERT_EQUAL(farm::LoadState::IDLE, fsm.get_state());
}
