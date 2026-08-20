// main/src/tank_strip_display.cpp
#include <cmath>
#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "tank_strip_display.hpp"

static const char* TAG = "TankStripDisplay";

// Color Palette Definitions (HSV)
static constexpr uint16_t HUE_RED = 0;
static constexpr uint16_t HUE_ORANGE = 30;
static constexpr uint16_t HUE_GREEN = 120;
static constexpr uint16_t HUE_CYAN = 180;
static constexpr uint16_t HUE_PURPLE = 280;

static constexpr uint8_t SAT_FULL = 255;
static constexpr uint8_t SAT_CYAN = 240;

static constexpr uint8_t VAL_FULL = 255;
static constexpr uint8_t VAL_CYAN = 200;

TankStripDisplay::TankStripDisplay(IHalLedStrip& hal_strip, const TankStripConfig& config)
    : hal_strip_(hal_strip)
    , config_(config)
    , brightness_(config.default_brightness)
{
}

TankStripDisplay::~TankStripDisplay()
{
    clear();
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

    clear();
    ESP_LOGI(
        TAG,
        "TankStripDisplay initialized (%lu LEDs on GPIO %d)",
        static_cast<unsigned long>(config_.num_leds),
        config_.gpio_pin);
    return ESP_OK;
}

void TankStripDisplay::set_level(uint16_t permille)
{
    if (permille > 1000) {
        permille = 1000;
    }
    level_permille_ = permille;

    // Trigger soft breathing confirmation cycle on IDLE (600ms single wave)
    idle_breathe_timer_ms_ = 600;
}

void TankStripDisplay::update_state(farm::LoadState state, farm::ControlMode mode, farm::PowerSource source)
{
    state_ = state;
    mode_ = mode;
    source_ = source;
}

void TankStripDisplay::set_override_pattern(TankStripPattern pattern)
{
    override_pattern_ = pattern;
    if (pattern == TankStripPattern::BOOT_SUCCESS) {
        boot_sweep_idx_ = 0;
        boot_hold_ms_ = 0;
        boot_timer_ms_ = 0;
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

void TankStripDisplay::tick(uint32_t delta_ms)
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
    uint32_t active_leds = calculate_active_leds(level_permille_);

    switch (state_) {
    case farm::LoadState::RUNNING:
        if (mode_ == farm::ControlMode::MANUAL) {
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
        render_idle(active_leds);
        break;
    }
}

void TankStripDisplay::render_timeout(uint32_t active_leds)
{
    bool is_on = (error_timer_ms_ % 1000) < 500;
    uint32_t top_idx = (active_leds > 0) ? (active_leds - 1) : 0;

    for (uint32_t i = 0; i < active_leds; i++) {
        if (is_on && i == top_idx) {
            render_pixel_hsv(i, HUE_ORANGE, SAT_FULL, VAL_FULL);
        }
        else {
            render_pixel_hsv(i, HUE_CYAN, SAT_CYAN, VAL_CYAN);
        }
    }
    for (uint32_t i = active_leds; i < config_.num_leds; i++) {
        render_pixel_hsv(i, 0, 0, 0);
    }
}

void TankStripDisplay::render_idle(uint32_t active_leds)
{
    uint8_t val = VAL_CYAN;

    // Smooth single wave breathing on data update (Value: 100% -> 40% -> 100%)
    if (idle_breathe_timer_ms_ > 0) {
        float t = static_cast<float>(600 - idle_breathe_timer_ms_) / 600.0f;
        float wave = 0.4f + 0.6f * 0.5f * (1.0f + std::cos(2.0f * 3.14159265f * t));
        val = static_cast<uint8_t>(VAL_CYAN * wave);
    }

    for (uint32_t i = 0; i < config_.num_leds; i++) {
        if (i < active_leds) {
            render_pixel_hsv(i, HUE_CYAN, SAT_CYAN, val);
        }
        else {
            render_pixel_hsv(i, 0, 0, 0);
        }
    }
}

void TankStripDisplay::render_filling_auto(uint32_t active_leds, farm::PowerSource source)
{
    uint16_t source_hue = (source == farm::PowerSource::SOLAR) ? HUE_GREEN : HUE_RED;

    // Render active level base in Cyan
    for (uint32_t i = 0; i < active_leds; i++) {
        render_pixel_hsv(i, HUE_CYAN, SAT_CYAN, VAL_CYAN);
    }

    // Render upward chase in source color
    if (active_leds < config_.num_leds) {
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
        uint8_t top_val = (phase < 0.5f) ? VAL_FULL : 80;
        render_pixel_hsv(config_.num_leds - 1, source_hue, SAT_FULL, top_val);
    }
}

void TankStripDisplay::render_filling_manual(uint32_t active_leds, farm::PowerSource source)
{
    uint16_t source_hue = (source == farm::PowerSource::SOLAR) ? HUE_GREEN : HUE_RED;

    // Full active bar solid in source color (Manual warning highlight)
    for (uint32_t i = 0; i < active_leds; i++) {
        render_pixel_hsv(i, source_hue, SAT_FULL, VAL_FULL);
    }

    // Upward chase above level in source color
    if (active_leds < config_.num_leds) {
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
        uint8_t top_val = (phase < 0.5f) ? VAL_FULL : 80;
        render_pixel_hsv(config_.num_leds - 1, source_hue, SAT_FULL, top_val);
    }
}

void TankStripDisplay::render_fault(uint32_t active_leds)
{
    // Fast Red breathing at 2Hz across the entire strip
    float phase = static_cast<float>(error_timer_ms_ % 500) / 500.0f;
    float intensity = 0.2f + 0.8f * 0.5f * (1.0f + std::cos(2.0f * 3.14159265f * phase));
    uint8_t val = static_cast<uint8_t>(VAL_FULL * intensity);

    for (uint32_t i = 0; i < config_.num_leds; i++) {
        render_pixel_hsv(i, HUE_RED, SAT_FULL, val);
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
        render_pixel_hsv(static_cast<uint32_t>(pos), HUE_PURPLE, SAT_FULL, VAL_FULL);
    }
    if (pos - 1 >= 0 && pos - 1 < static_cast<int32_t>(config_.num_leds)) {
        render_pixel_hsv(static_cast<uint32_t>(pos - 1), HUE_PURPLE, SAT_FULL, 80);
    }
    if (pos + 1 >= 0 && pos + 1 < static_cast<int32_t>(config_.num_leds)) {
        render_pixel_hsv(static_cast<uint32_t>(pos + 1), HUE_PURPLE, SAT_FULL, 80);
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
            render_pixel_hsv(i, HUE_CYAN, SAT_FULL, VAL_FULL);
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
            render_pixel_hsv(i, HUE_RED, SAT_FULL, VAL_FULL);
        }
        else {
            render_pixel_hsv(i, 0, 0, 0);
        }
    }
}
