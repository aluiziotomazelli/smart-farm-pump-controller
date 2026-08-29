// main/src/tank_strip_display.cpp
#include <cmath>
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "tank_strip_display.hpp"

static const char* TAG = "TankStripDisplay";

// Semantic Color Palette Definitions (HSV Hue 0..360)
static constexpr uint16_t HUE_GRID = 0;           ///< Red (Grid indicator & alert)
static constexpr uint16_t HUE_TIMEOUT = 30;       ///< Orange (Communication timeout warning)
static constexpr uint16_t HUE_BACKUP = 35;        ///< Amber / Gold (Backup float switch mode)
static constexpr uint16_t HUE_SOLAR = 120;        ///< Green (Solar indicator & success)
static constexpr uint16_t HUE_FILL = 180;         ///< Cyan (Water level fill)
static constexpr uint16_t HUE_OTA = 280;          ///< Purple (OTA update scanner)
static constexpr uint16_t HUE_BOOT_SUCCESS = 120; ///< Green (Boot success sweep)
static constexpr uint16_t HUE_BOOT_ERROR = 0;     ///< Red (Boot error SOS)
static constexpr uint16_t HUE_FAULT = 0;          ///< Red (Hardware fault / stuck contactor)

static constexpr uint8_t SAT_FULL = 255;
static constexpr uint8_t SAT_CYAN = 240;
static constexpr uint8_t SAT_BACKUP = 255;

static constexpr uint8_t VAL_FULL = 255;
static constexpr uint8_t VAL_CYAN = 200;
static constexpr uint8_t VAL_BACKUP = 220;

static constexpr uint16_t IDLE_BREATHE_DURATION_MS = 1800;

TankStripDisplay::TankStripDisplay(
    IHalLedStrip& hal_strip,
    idf_hals::IHalFreertos& hal_freertos,
    const TankStripConfig& config)
    : hal_strip_(hal_strip)
    , hal_freertos_(hal_freertos)
    , config_(config)
    , brightness_(config.default_brightness)
{
}

TankStripDisplay::~TankStripDisplay()
{
    stop();
    if (strip_handle_ != nullptr) {
        hal_strip_.del(strip_handle_);
        strip_handle_ = nullptr;
    }
}

esp_err_t TankStripDisplay::init()
{
    if (config_.num_leds == 0) {
        ESP_LOGE(TAG, "Invalid config: num_leds is 0");
        return ESP_ERR_INVALID_ARG;
    }

    led_strip_config_t strip_config{};
    strip_config.strip_gpio_num = config_.gpio_pin;
    strip_config.max_leds = config_.num_leds;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = 0;

    led_strip_rmt_config_t rmt_config{};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = config_.rmt_resolution_hz;
    rmt_config.mem_block_symbols = 0;
    rmt_config.flags.with_dma = 0;

    esp_err_t err = hal_strip_.new_rmt_device(&strip_config, &rmt_config, &strip_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RMT LED strip device (%s)", esp_err_to_name(err));
        return err;
    }

    if (display_queue_ == nullptr) {
        display_queue_ = hal_freertos_.queue_create(16, sizeof(DisplayCommand));
        if (display_queue_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create display command queue");
            hal_strip_.del(strip_handle_);
            strip_handle_ = nullptr;
            return ESP_ERR_NO_MEM;
        }
    }

    clear();
    ESP_LOGI(
        TAG,
        "TankStripDisplay initialized (%lu LEDs on GPIO %d)",
        static_cast<unsigned long>(config_.num_leds),
        config_.gpio_pin);
    return ESP_OK;
}

esp_err_t TankStripDisplay::start()
{
    if (is_running_) {
        ESP_LOGW(TAG, "Display task already running");
        return ESP_OK;
    }

    if (strip_handle_ == nullptr || display_queue_ == nullptr) {
        ESP_LOGE(TAG, "Cannot start display task: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    is_running_ = true;
    BaseType_t ret = hal_freertos_.task_create(
        task_entry,
        "strip_disp",
        3072,
        this,
        2, // Low priority for secondary visual display
        &task_handle_);

    if (ret != pdPASS) {
        is_running_ = false;
        task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create strip_disp task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TankStripDisplay task started (Priority 2, 20 FPS)");
    return ESP_OK;
}

void TankStripDisplay::stop()
{
    is_running_ = false;

    if (task_handle_ != nullptr) {
        hal_freertos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }

    if (display_queue_ != nullptr) {
        hal_freertos_.queue_delete(display_queue_);
        display_queue_ = nullptr;
    }

    clear();
    ESP_LOGI(TAG, "TankStripDisplay stopped and resources deallocated");
}

void TankStripDisplay::set_level(uint16_t permille, bool backup_mode, bool is_full)
{
    if (permille > 1000) {
        permille = 1000;
    }

    if (display_queue_ != nullptr) {
        DisplayCommand cmd{};
        cmd.type = DisplayCmdType::SET_LEVEL;
        cmd.level_data.level_permille = permille;
        cmd.level_data.backup_mode = backup_mode;
        cmd.level_data.is_full = is_full;
        hal_freertos_.queue_send(display_queue_, &cmd, 0);
    }
    else {
        level_permille_ = permille;
        backup_mode_active_ = backup_mode;
        float_switch_is_full_ = is_full;
        idle_breathe_timer_ms_ = IDLE_BREATHE_DURATION_MS;
    }
}

void TankStripDisplay::update_state(farm::LoadState state, farm::ControlMode mode, farm::PowerSource source)
{
    if (display_queue_ != nullptr) {
        DisplayCommand cmd{};
        cmd.type = DisplayCmdType::UPDATE_STATE;
        cmd.state_data.state = state;
        cmd.state_data.mode = mode;
        cmd.state_data.source = source;
        hal_freertos_.queue_send(display_queue_, &cmd, 0);
    }
    else {
        state_ = state;
        mode_ = mode;
        source_ = source;
    }
}

void TankStripDisplay::set_override_pattern(TankStripPattern pattern)
{
    if (display_queue_ != nullptr) {
        DisplayCommand cmd{};
        cmd.type = DisplayCmdType::SET_OVERRIDE_PATTERN;
        cmd.pattern = pattern;
        hal_freertos_.queue_send(display_queue_, &cmd, 0);
    }
    else {
        override_pattern_ = pattern;
        if (pattern == TankStripPattern::BOOT_SUCCESS) {
            boot_sweep_idx_ = 0;
            boot_hold_ms_ = 0;
            boot_timer_ms_ = 0;
        }
    }
}

void TankStripDisplay::set_brightness(uint8_t brightness)
{
    if (display_queue_ != nullptr) {
        DisplayCommand cmd{};
        cmd.type = DisplayCmdType::SET_BRIGHTNESS;
        cmd.brightness = brightness;
        hal_freertos_.queue_send(display_queue_, &cmd, 0);
    }
    else {
        brightness_ = brightness;
    }
}

void TankStripDisplay::clear()
{
    if (strip_handle_ != nullptr) {
        hal_strip_.clear(strip_handle_);
    }
}

uint32_t TankStripDisplay::calculate_active_leds(uint16_t permille) const
{
    if (permille == 0 || config_.num_leds == 0) {
        return 0;
    }
    uint32_t active = (static_cast<uint32_t>(permille) * config_.num_leds + 500) / 1000;
    if (active > config_.num_leds) {
        active = config_.num_leds;
    }
    return active;
}

void TankStripDisplay::process_command(const DisplayCommand& cmd)
{
    switch (cmd.type) {
    case DisplayCmdType::SET_LEVEL:
        level_permille_ = cmd.level_data.level_permille;
        backup_mode_active_ = cmd.level_data.backup_mode;
        float_switch_is_full_ = cmd.level_data.is_full;
        idle_breathe_timer_ms_ = IDLE_BREATHE_DURATION_MS; // Trigger soft breathing confirmation cycle on IDLE
        break;

    case DisplayCmdType::UPDATE_STATE:
        state_ = cmd.state_data.state;
        mode_ = cmd.state_data.mode;
        source_ = cmd.state_data.source;
        break;

    case DisplayCmdType::SET_OVERRIDE_PATTERN:
        override_pattern_ = cmd.pattern;
        if (cmd.pattern == TankStripPattern::BOOT_SUCCESS) {
            boot_sweep_idx_ = 0;
            boot_hold_ms_ = 0;
            boot_timer_ms_ = 0;
        }
        break;

    case DisplayCmdType::SET_BRIGHTNESS:
        brightness_ = cmd.brightness;
        break;

    case DisplayCmdType::CLEAR:
        clear();
        break;
    }
}

void TankStripDisplay::process_frame(uint32_t delta_ms)
{
    if (strip_handle_ == nullptr || config_.num_leds == 0) {
        return;
    }

    // 1. Advance Timers
    if (idle_breathe_timer_ms_ > delta_ms) {
        idle_breathe_timer_ms_ -= delta_ms;
    }
    else {
        idle_breathe_timer_ms_ = 0;
    }

    chase_timer_ms_ += delta_ms;
    if (chase_timer_ms_ >= 200) {
        chase_timer_ms_ = 0;
        chase_offset_++;
    }

    ota_timer_ms_ += delta_ms;
    if (ota_timer_ms_ >= 80) {
        ota_timer_ms_ = 0;
        if (ota_scan_forward_) {
            ota_scan_pos_++;
            if (ota_scan_pos_ >= static_cast<int32_t>(config_.num_leds - 1)) {
                ota_scan_pos_ = static_cast<int32_t>(config_.num_leds - 1);
                ota_scan_forward_ = false;
            }
        }
        else {
            ota_scan_pos_--;
            if (ota_scan_pos_ <= 0) {
                ota_scan_pos_ = 0;
                ota_scan_forward_ = true;
            }
        }
    }

    error_timer_ms_ += delta_ms;

    // 2. Render Active Pattern
    switch (override_pattern_) {
    case TankStripPattern::OTA_UPDATING:
        render_ota();
        break;
    case TankStripPattern::BOOT_SUCCESS:
        boot_timer_ms_ += delta_ms;
        render_boot_success();
        break;
    case TankStripPattern::BOOT_ERROR:
        render_boot_error();
        break;
    case TankStripPattern::AUTO:
    default:
        render_auto_pattern();
        break;
    }

    // 3. Flush to Hardware
    hal_strip_.refresh(strip_handle_);
}

void TankStripDisplay::task_entry(void* param)
{
    auto* self = static_cast<TankStripDisplay*>(param);
    self->run_task();
}

void TankStripDisplay::run_task()
{
    while (is_running_) {
        // Drains all pending messages
        if (display_queue_ != nullptr) {
            DisplayCommand cmd{};
            while (hal_freertos_.queue_receive(display_queue_, &cmd, 0) == pdTRUE) {
                process_command(cmd);
            }
        }

        // Render frame at 20 FPS (50ms)
        process_frame(50);

        hal_freertos_.task_delay(pdMS_TO_TICKS(50));
    }
}

void TankStripDisplay::render_pixel_hsv(uint32_t index, uint16_t hue, uint8_t saturation, uint8_t value)
{
    if (index >= config_.num_leds || strip_handle_ == nullptr) {
        return;
    }
    uint8_t scaled_val = static_cast<uint8_t>((static_cast<uint32_t>(value) * brightness_) / 255);
    hal_strip_.set_pixel_hsv(strip_handle_, index, hue, saturation, scaled_val);
}

void TankStripDisplay::render_auto_pattern()
{
    uint32_t active_leds = 0;
    if (backup_mode_active_) {
        active_leds = float_switch_is_full_ ? config_.num_leds : (config_.num_leds > 0 ? 1 : 0);
    }
    else {
        active_leds = calculate_active_leds(level_permille_);
    }

    switch (state_) {
    case farm::LoadState::RUNNING:
        if (mode_ == farm::ControlMode::MANUAL_RUN || mode_ == farm::ControlMode::FULL_MANUAL) {
            render_filling_manual(active_leds, source_);
        }
        else {
            render_filling_auto(active_leds, source_);
        }
        break;
    case farm::LoadState::ERROR_TIMEOUT:
        render_timeout(active_leds);
        break;
    case farm::LoadState::ERROR_NO_SOURCE:
    case farm::LoadState::ERROR_CONTACTOR_STUCK:
        render_fault(active_leds);
        break;
    case farm::LoadState::IDLE:
    default:
        render_idle(active_leds, mode_, source_);
        break;
    }
}

void TankStripDisplay::render_idle(uint32_t active_leds, farm::ControlMode mode, farm::PowerSource source)
{
    // Smooth single wave breathing on data update (Value: 100% -> 40% -> 100%)
    float factor = 1.0f;
    if (idle_breathe_timer_ms_ > 0) {
        float t = static_cast<float>(IDLE_BREATHE_DURATION_MS - idle_breathe_timer_ms_) /
                  static_cast<float>(IDLE_BREATHE_DURATION_MS);
        factor = 0.2f + 0.8f * 0.5f * (1.0f + std::cos(2.0f * 3.14159265f * t));
    }

    uint16_t fill_hue = backup_mode_active_ ? HUE_BACKUP : HUE_FILL;
    uint8_t fill_sat = backup_mode_active_ ? SAT_BACKUP : SAT_CYAN;
    uint8_t base_val = backup_mode_active_ ? VAL_BACKUP : VAL_CYAN;

    uint8_t val_fill = static_cast<uint8_t>(base_val * factor);
    uint8_t val_full = static_cast<uint8_t>(VAL_FULL * factor);

    bool is_locked = (source == farm::PowerSource::SOLAR || source == farm::PowerSource::GRID);

    if (is_locked) {
        uint16_t locked_hue = (source == farm::PowerSource::GRID) ? HUE_GRID : HUE_SOLAR;

        if (active_leds == 0) {
            // Tank empty with locked source: LED 0 pulses gently in source color
            render_pixel_hsv(0, locked_hue, SAT_FULL, static_cast<uint8_t>(80 * factor));
            for (uint32_t i = 1; i < config_.num_leds; i++) {
                render_pixel_hsv(i, 0, 0, 0);
            }
        }
        else {
            // LEDs 0..(active_leds - 2) in fill color
            for (uint32_t i = 0; i + 1 < active_leds; i++) {
                render_pixel_hsv(i, fill_hue, fill_sat, val_fill);
            }
            // Top active LED in locked source color
            render_pixel_hsv(active_leds - 1, locked_hue, SAT_FULL, val_full);

            // Remaining LEDs off
            for (uint32_t i = active_leds; i < config_.num_leds; i++) {
                render_pixel_hsv(i, 0, 0, 0);
            }
        }
    }
    else {
        // Pure AUTO: all active LEDs in fill color (Cyan or Amber)
        for (uint32_t i = 0; i < config_.num_leds; i++) {
            if (i < active_leds) {
                render_pixel_hsv(i, fill_hue, fill_sat, val_fill);
            }
            else {
                render_pixel_hsv(i, 0, 0, 0);
            }
        }
    }
}

void TankStripDisplay::render_filling_auto(uint32_t active_leds, farm::PowerSource source)
{
    uint16_t source_hue = (source == farm::PowerSource::GRID) ? HUE_GRID : HUE_SOLAR;
    uint16_t fill_hue = backup_mode_active_ ? HUE_BACKUP : HUE_FILL;
    uint8_t fill_sat = backup_mode_active_ ? SAT_BACKUP : SAT_CYAN;
    uint8_t base_val = backup_mode_active_ ? VAL_BACKUP : VAL_CYAN;

    // Render active level base in fill color (Cyan / Amber)
    for (uint32_t i = 0; i < active_leds; i++) {
        render_pixel_hsv(i, fill_hue, fill_sat, base_val);
    }

    // Render upward chase in source color
    if (active_leds < config_.num_leds - 1) {
        uint32_t remaining = config_.num_leds - active_leds;
        uint32_t chase_idx = active_leds + (chase_offset_ % remaining);

        for (uint32_t i = active_leds; i < config_.num_leds; i++) {
            if (i == chase_idx) {
                render_pixel_hsv(i, source_hue, SAT_FULL, VAL_FULL);
            }
            else {
                render_pixel_hsv(i, 0, 0, 0);
            }
        }
    }
    else {
        // Tank full: Top LED pulses in source color
        float phase = static_cast<float>(chase_timer_ms_) / 200.0f;
        uint8_t top_val = (phase < 0.2f) ? VAL_FULL : VAL_FULL / 4;
        render_pixel_hsv(config_.num_leds - 1, source_hue, SAT_FULL, top_val);
    }
}

void TankStripDisplay::render_filling_manual(uint32_t active_leds, farm::PowerSource source)
{
    uint16_t source_hue = (source == farm::PowerSource::GRID) ? HUE_GRID : HUE_SOLAR;

    // Full active bar solid in source color (Manual warning highlight)
    for (uint32_t i = 0; i < active_leds; i++) {
        render_pixel_hsv(i, source_hue, SAT_FULL, VAL_FULL);
    }

    // Upward chase above level in source color
    if (active_leds < config_.num_leds - 1) {
        uint32_t remaining = config_.num_leds - active_leds;
        uint32_t chase_idx = active_leds + (chase_offset_ % remaining);

        for (uint32_t i = active_leds; i < config_.num_leds; i++) {
            if (i == chase_idx) {
                render_pixel_hsv(i, source_hue, SAT_FULL, VAL_FULL);
            }
            else {
                render_pixel_hsv(i, 0, 0, 0);
            }
        }
    }
    else {
        // Full tank manual: Top LED flashes
        float phase = static_cast<float>(chase_timer_ms_) / 200.0f;
        uint8_t top_val = (phase < 0.2f) ? VAL_FULL : VAL_FULL / 4;
        render_pixel_hsv(config_.num_leds - 1, source_hue, SAT_FULL, top_val);
    }
}

void TankStripDisplay::render_timeout(uint32_t active_leds)
{
    bool is_on = (error_timer_ms_ % 1000) < 500;
    uint32_t top_idx = (active_leds > 0) ? (active_leds - 1) : 0;
    uint16_t fill_hue = backup_mode_active_ ? HUE_BACKUP : HUE_FILL;
    uint8_t fill_sat = backup_mode_active_ ? SAT_BACKUP : SAT_CYAN;
    uint8_t base_val = backup_mode_active_ ? VAL_BACKUP : VAL_CYAN;

    for (uint32_t i = 0; i < active_leds; i++) {
        if (is_on && i == top_idx) {
            render_pixel_hsv(i, HUE_TIMEOUT, SAT_FULL, VAL_FULL);
        }
        else {
            render_pixel_hsv(i, fill_hue, fill_sat, base_val);
        }
    }
    for (uint32_t i = active_leds; i < config_.num_leds; i++) {
        render_pixel_hsv(i, 0, 0, 0);
    }
}

void TankStripDisplay::render_fault(uint32_t active_leds)
{
    // Fast Red breathing at 2Hz across the entire strip
    float phase = static_cast<float>(error_timer_ms_ % 500) / 500.0f;
    float intensity = 0.2f + 0.8f * 0.5f * (1.0f + std::cos(2.0f * 3.14159265f * phase));
    uint8_t val = static_cast<uint8_t>(VAL_FULL * intensity);

    for (uint32_t i = 0; i < config_.num_leds; i++) {
        render_pixel_hsv(i, HUE_FAULT, SAT_FULL, val);
    }
}

void TankStripDisplay::render_ota()
{
    // Clear all pixels
    for (uint32_t i = 0; i < config_.num_leds; i++) {
        render_pixel_hsv(i, 0, 0, 0);
    }

    // Purple scanner with tail
    int32_t pos = ota_scan_pos_;
    if (pos >= 0 && pos < static_cast<int32_t>(config_.num_leds)) {
        render_pixel_hsv(static_cast<uint32_t>(pos), HUE_OTA, SAT_FULL, VAL_FULL);
    }
    if (pos - 1 >= 0 && pos - 1 < static_cast<int32_t>(config_.num_leds)) {
        render_pixel_hsv(static_cast<uint32_t>(pos - 1), HUE_OTA, SAT_FULL, VAL_FULL / 6);
    }
    if (pos + 1 >= 0 && pos + 1 < static_cast<int32_t>(config_.num_leds)) {
        render_pixel_hsv(static_cast<uint32_t>(pos + 1), HUE_OTA, SAT_FULL, VAL_FULL / 6);
    }
}

void TankStripDisplay::render_boot_success()
{
    // Progressive green sweep (50ms per LED)
    while (boot_timer_ms_ >= 50) {
        boot_timer_ms_ -= 50;
        if (boot_sweep_idx_ < config_.num_leds) {
            boot_sweep_idx_++;
        }
        else {
            boot_hold_ms_ += 50;
            if (boot_hold_ms_ >= 500) {
                // Return to normal automatic rendering after sweep holds 500ms
                override_pattern_ = TankStripPattern::AUTO;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < config_.num_leds; i++) {
        if (i < boot_sweep_idx_) {
            render_pixel_hsv(i, HUE_BOOT_SUCCESS, SAT_FULL, VAL_FULL);
        }
        else {
            render_pixel_hsv(i, 0, 0, 0);
        }
    }
}

void TankStripDisplay::render_boot_error()
{
    // SOS-like 3 red flashes (100ms ON / 100ms OFF x 3) + 600ms pause = 1200ms period
    uint32_t cycle_ms = error_timer_ms_ % 1200;
    bool is_on = (cycle_ms < 100) || (cycle_ms >= 200 && cycle_ms < 300) || (cycle_ms >= 400 && cycle_ms < 500);

    for (uint32_t i = 0; i < config_.num_leds; i++) {
        if (is_on) {
            render_pixel_hsv(i, HUE_BOOT_ERROR, SAT_FULL, VAL_FULL);
        }
        else {
            render_pixel_hsv(i, 0, 0, 0);
        }
    }
}
