// main/include/tank_strip_display.hpp
#pragma once

#include <cstdint>

#include "interfaces/i_hal_led_strip.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_tank_strip_display.hpp"

/**
 * @enum DisplayCmdType
 * @brief Internal command types processed by the display FreeRTOS task.
 */
enum class DisplayCmdType : uint8_t
{
    SET_LEVEL,
    UPDATE_STATE,
    SET_OVERRIDE_PATTERN,
    SET_BRIGHTNESS,
    CLEAR
};

/**
 * @struct DisplayCommand
 * @brief Thread-safe message payload passed through display_queue_.
 */
struct DisplayCommand
{
    DisplayCmdType type;
    union {
        struct {
            uint16_t level_permille;
            bool backup_mode;
            bool is_full;
        } level_data;
        struct {
            farm::LoadState state;
            farm::ControlMode mode;
            farm::PowerSource source;
        } state_data;
        TankStripPattern pattern;
        uint8_t brightness;
    };
};

/**
 * @class TankStripDisplay
 * @brief Manages addressable LED strip animations for water level display, operational states, and diagnostics.
 */
class TankStripDisplay : public ITankStripDisplay
{
public:
    TankStripDisplay(IHalLedStrip& hal_strip, idf_hals::IHalFreertos& hal_freertos, const TankStripConfig& config);
    ~TankStripDisplay() override;

    /** @copydoc ITankLevelDisplay::init */
    esp_err_t init() override;

    /** @copydoc ITankStripDisplay::start */
    esp_err_t start() override;

    /** @copydoc ITankStripDisplay::stop */
    void stop() override;

    /** @copydoc ITankLevelDisplay::set_level */
    void set_level(uint16_t permille, bool backup_mode = false, bool is_full = false) override;

    /** @copydoc ITankLevelDisplay::get_level */
    uint16_t get_level() const override { return level_permille_; }

    /** @brief Checks if display is currently operating in backup mode */
    bool is_backup_mode() const { return backup_mode_active_; }

    /** @brief Checks if float switch is currently indicating full tank */
    bool is_float_full() const { return float_switch_is_full_; }

    /** @copydoc ITankStripDisplay::update_state */
    void update_state(farm::LoadState state, farm::ControlMode mode, farm::PowerSource source) override;

    /** @copydoc ITankStripDisplay::set_override_pattern */
    void set_override_pattern(TankStripPattern pattern) override;

    /** @copydoc ITankStripDisplay::get_override_pattern */
    TankStripPattern get_override_pattern() const override { return override_pattern_; }

    /** @copydoc ITankStripDisplay::set_brightness */
    void set_brightness(uint8_t brightness) override;

    /** @copydoc ITankStripDisplay::get_brightness */
    uint8_t get_brightness() const override { return brightness_; }

    /** @copydoc ITankStripDisplay::clear */
    void clear() override;

    /**
     * @brief Computes how many LEDs should be lit for a given permille level.
     * @param permille Water level in permille (0 to 1000).
     * @return Number of active base LEDs (0 to num_leds).
     */
    uint32_t calculate_active_leds(uint16_t permille) const;

    /**
     * @brief Processes a single display command (exposed for deterministic host testing).
     * @param cmd Command to process.
     */
    void process_command(const DisplayCommand& cmd);

    /**
     * @brief Processes animation step and renders a frame (exposed for deterministic host testing).
     * @param delta_ms Elapsed time in milliseconds.
     */
    void process_frame(uint32_t delta_ms);

private:
    IHalLedStrip& hal_strip_;
    idf_hals::IHalFreertos& hal_freertos_;
    TankStripConfig config_;
    led_strip_handle_t strip_handle_{nullptr};
    QueueHandle_t display_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    volatile bool is_running_{false};

    uint16_t level_permille_{0};
    bool backup_mode_active_{false};
    bool float_switch_is_full_{false};
    farm::LoadState state_{farm::LoadState::IDLE};
    farm::ControlMode mode_{farm::ControlMode::AUTO};
    farm::PowerSource source_{farm::PowerSource::UNKNOWN};
    TankStripPattern override_pattern_{TankStripPattern::AUTO};
    uint8_t brightness_{200};

    // Animation Timers & States
    uint32_t chase_timer_ms_{0};
    uint32_t chase_offset_{0};

    uint32_t idle_breathe_timer_ms_{0};

    uint32_t ota_timer_ms_{0};
    int32_t ota_scan_pos_{0};
    bool ota_scan_forward_{true};

    uint32_t boot_timer_ms_{0};
    uint32_t boot_sweep_idx_{0};
    uint32_t boot_hold_ms_{0};

    uint32_t error_timer_ms_{0};

    static void task_entry(void* param);
    void run_task();

    void render_pixel_hsv(uint32_t index, uint16_t hue, uint8_t saturation, uint8_t value);
    void render_auto_pattern();
    void render_idle(uint32_t active_leds, farm::ControlMode mode, farm::PowerSource source);
    void render_filling_auto(uint32_t active_leds, farm::PowerSource source);
    void render_filling_manual(uint32_t active_leds, farm::PowerSource source);
    void render_timeout(uint32_t active_leds);
    void render_fault(uint32_t active_leds);
    void render_ota();
    void render_boot_success();
    void render_boot_error();
};
