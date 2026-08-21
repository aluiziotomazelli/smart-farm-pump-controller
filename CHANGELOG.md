# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-08-20

### Added
- **Dedicated Display Task (`TankStripDisplay`)**: Autonomous FreeRTOS background task (Priority 2, 20 FPS / 50 ms) for WS2812B addressable LED strip rendering with clean resource deallocation on stop and destructor.
- **Thread-Safe Display Queue**: Non-blocking message queue (`display_queue_`) for all public display API calls (`set_level`, `update_state`, `set_override_pattern`, `set_brightness`, `clear`), ensuring zero contention with the main control task and continuous animations during blocking operations (WiFi, OTA download).
- **HMI Redesign (3-Position Selector & Action Button)**:
  - 3-position switch input mapping (`PIN_SWITCH_SOLAR`, `PIN_SWITCH_GRID`, center = `AUTO`) with mechanical conflict resolution favoring Grid.
  - Action pushbutton triggering operator start/stop in `SOURCE_LOCKED` modes, or dispatching `FILL_REQUEST` (0x07) to the Central Hub in `AUTO` mode.
- **Semantic Color Palette**: Centralized color definitions (`HUE_SOLAR`, `HUE_GRID`, `HUE_FILL`, `HUE_OTA`, `HUE_BOOT_SUCCESS`, `HUE_BOOT_ERROR`, `HUE_TIMEOUT`, `HUE_FAULT`).
- **Dynamic Animation Patterns**:
  - `IDLE (AUTO)`: Water level in Cyan with soft breathing pulsation on incoming telemetry.
  - `IDLE (SOURCE_LOCKED)`: Water level in Cyan with top LED highlighted in Green (`HUE_SOLAR`) or Red (`HUE_GRID`).
  - `RUNNING (AUTO)`: Cyan base with Green/Red chasing LED upwards.
  - `RUNNING (Manual)`: High-visibility solid color bar with chasing LED.
  - `OTA_UPDATING`: Purple Knight Rider scanner running uninterrupted throughout the download.
  - `BOOT_SUCCESS` & `BOOT_ERROR`: Green progressive self-test sweep and Red SOS triple-flash.
- **Enhanced Boot Health Verification**: Integrated health checks in `PumpController::init()` covering both OTA pending confirmation and normal boot subsystem validity.
- **CI / Linux Host Decoupling**: Standalone fallback types in `i_hal_led_strip.hpp` allowing headless CI and Linux testing without downloading real hardware drivers.
- **Extended Test Suite**: Expanded host testing to **94 unit tests** across 7 test projects with 100% pass rate.

### Changed
- Removed deprecated `display_.tick(delta_ms)` polling from `PumpController::tick()` and `process_pending_ota()`.
- Initialized and started `TankStripDisplay` task during `PumpController::init()` for immediate boot feedback.

## [0.1.0] - 2026-08-18

### Added
- **Main Orchestrator (`PumpController`)**: Top-level coordinator FreeRTOS task running at 50 ms loop intervals, orchestrating switch and button sampling, FSM ticks, command execution, telemetry reporting, and persistence lifecycles.
- **Finite State Machine (`PumpStateMachine`)**: High-reliability actuator state machine implementing mutual-exclusion interlocks (break-before-make demagnetization delay), safety watchdog countdowns (default 3600s), manual and automatic control modes, and pre-activation contactor feedback validation.
- **Contactor Driver (`ContactorController`)**: Hardware abstraction for AC contactor gate triggers with configurable active level (Active-High for optically isolated MOC+TRIAC drivers or Active-Low for relay modules) and hardware demagnetization safety timing.
- **Visual Feedback (`PumpLedController`)**: Coordinated dual-LED status controller for Grid (Green) and Solar (Yellow) channels supporting steady active, off, and error burst flashing patterns.
- **Command Dispatcher (`PumpCommandHandler`)**: Non-blocking incoming ESP-NOW message queue processor handling remote `LOAD_ON`, `LOAD_OFF`, `SYNC_TIME`, `START_OTA`, and `REBOOT` commands.
- **Telemetry Reporting (`PumpStatusReporter`)**: Telemetry transmitter broadcasting pump status snapshots (`LoadState`, `PowerSource`, `ControlMode`, runtime, remaining watchdog) periodically via heartbeat and immediately on state changes to the Central Hub.
- **Domain Persistence (`PumpStats` & `PumpNvs`)**: Dedicated dual-tier non-volatile storage tracking lifetime operating hours across power sources (Solar vs Grid), contactor commutation cycles, manual/remote start counters, and fault occurrences, utilizing `AppStorage` and `StorageEnvelope`.
- **System Core Lifecycle (`NvsCore` & `CoreData`)**: Node identity, boot counters, power profiles, and time synchronization state persistence with automatic recovery.
- **Manual Control Interface**: Single pushbutton for manual Start/Stop toggle actuation, alongside dedicated hardware switches for Mode selection (`AUTO` / `MANUAL`) and Source selection (`SOLAR` / `GRID`).
- **Optimized Hardware Pinout**: Strapping pin audit and mapping on Seeed Studio XIAO ESP32-C3, ensuring safe reset states and dedicating the onboard BOOT button (GPIO 9) for OTA triggering.
- **Host Unit Testing Suite**: Comprehensive GoogleTest / GoogleMock test suites (100% pass) covering all subsystems (`test_pump_controller`, `test_pump_command_handler`, `test_pump_state_machine`, `test_contactor_controller`, `test_pump_led_controller`, and `test_pump_status_reporter`).
