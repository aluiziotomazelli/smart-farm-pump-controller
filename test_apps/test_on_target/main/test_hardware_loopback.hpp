#pragma once

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hil {

// Target Inputs (DUT Pins)
static constexpr gpio_num_t DUT_BUTTON_ACTION  = GPIO_NUM_2;   // D0
static constexpr gpio_num_t DUT_SWITCH_SOLAR   = GPIO_NUM_3;   // D1
static constexpr gpio_num_t DUT_SWITCH_GRID    = GPIO_NUM_4;   // D2
static constexpr gpio_num_t DUT_CONTACTOR_GRID = GPIO_NUM_5;   // D3
static constexpr gpio_num_t DUT_CONTACTOR_SOLAR= GPIO_NUM_6;   // D4
static constexpr gpio_num_t DUT_OUTPUT_MONITOR = GPIO_NUM_10;  // D10

// Test Stimulus Generator Outputs (Connected via Jumper)
static constexpr gpio_num_t CTRL_OUTPUT_MONITOR = GPIO_NUM_21; // D6 -> Jumper to D10
static constexpr gpio_num_t CTRL_SWITCH_GRID    = GPIO_NUM_20; // D7 -> Jumper to D2
static constexpr gpio_num_t CTRL_SWITCH_SOLAR   = GPIO_NUM_8;  // D8 -> Jumper to D1
static constexpr gpio_num_t CTRL_BUTTON_ACTION  = GPIO_NUM_9;  // D9 -> Jumper to D0

inline void init_control_pins()
{
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << CTRL_OUTPUT_MONITOR) |
                           (1ULL << CTRL_SWITCH_GRID) |
                           (1ULL << CTRL_SWITCH_SOLAR) |
                           (1ULL << CTRL_BUTTON_ACTION);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Initial safe resting state:
    // D6 (Voltage monitor) = 0 (No voltage)
    // D7 (Grid switch) = 1 (Open / not selected due to active-low)
    // D8 (Solar switch) = 1 (Open / not selected due to active-low)
    // D9 (Button) = 1 (Released due to active-low pullup)
    gpio_set_level(CTRL_OUTPUT_MONITOR, 0);
    gpio_set_level(CTRL_SWITCH_GRID, 1);
    gpio_set_level(CTRL_SWITCH_SOLAR, 1);
    gpio_set_level(CTRL_BUTTON_ACTION, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

inline void set_switch_position_auto()
{
    gpio_set_level(CTRL_SWITCH_GRID, 1);
    gpio_set_level(CTRL_SWITCH_SOLAR, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

inline void set_switch_position_grid()
{
    gpio_set_level(CTRL_SWITCH_GRID, 0);
    gpio_set_level(CTRL_SWITCH_SOLAR, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

inline void set_switch_position_solar()
{
    gpio_set_level(CTRL_SWITCH_GRID, 1);
    gpio_set_level(CTRL_SWITCH_SOLAR, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
}

inline void set_output_voltage_present(bool present)
{
    gpio_set_level(CTRL_OUTPUT_MONITOR, present ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(10));
}

inline void pulse_button_action(uint32_t press_duration_ms = 100)
{
    gpio_set_level(CTRL_BUTTON_ACTION, 0); // Press (active low)
    vTaskDelay(pdMS_TO_TICKS(press_duration_ms));
    gpio_set_level(CTRL_BUTTON_ACTION, 1); // Release
    vTaskDelay(pdMS_TO_TICKS(50));
}

} // namespace hil
