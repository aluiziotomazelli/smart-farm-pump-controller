# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.4] - 2026-08-30

### Added
- **Production `OutputMonitor` Driver (`IOutputMonitor`)**:
  - Implemented concrete driver [`OutputMonitor`](file:///home/german/dev/workspaces/smart-farm/smart-farm-pump-controller/main/include/output_monitor.hpp) utilizing `IGpioHAL` to perform non-blocking sampling of motor AC output terminal voltage.
  - Added [`OutputMonitorConfig`](file:///home/german/dev/workspaces/smart-farm/smart-farm-pump-controller/main/include/pump_types.hpp) with configurable pin, active level, and pull-up/down modes (default `GPIO 10 / D10`, active-high with pull-down).
  - Added dedicated host unit tests in `host_test/test_output_monitor`.
- **On-Target Hardware-in-the-Loop (HIL) Test Suite (`test_apps/test_on_target`)**:
  - Created standalone ESP-IDF + Unity test harness for Seeed Studio XIAO ESP32-C3 with loopback jumpers (`D6->D10`, `D7->D2`, `D8->D1`, `D9->D0`).
  - Implemented 7 on-target test cases validating real silicon behavior:
    - Output voltage sensor reading on physical GPIO.
    - 3-position selector switch with hardware debounce.
    - Contactor break-before-make interlock and demagnetization delay on actual output pins (`D3`, `D4`).
    - Full `PumpStateMachine` output validation (contactor stuck and power loss detection).
    - Manual start/stop via pushbutton and live hot-switching.
    - Remote fill watchdog timeout safety protection.
    - Visual LED strip pattern demonstration on `D5` (WS2812B).

### Changed
- **Contactor GPIO Pin Mode**:
  - Configured contactor GPIOs as `GPIO_MODE_INPUT_OUTPUT` in `ContactorController::init()` allowing direct register readback of output level.
- **Diffuser-Optimized LED Strip Display (`TankStripDisplay`)**:
  - In backup mode (`!float_switch_is_full_`), lights up a base of 3 LEDs (LEDs 0, 1, 2) in Amber to provide optimal visibility through diffuser panels.
  - In timeout alert (`render_timeout`), blinks the top two active LEDs in Amber while keeping lower level LEDs stable.

## [0.3.3] - 2026-08-30

### Added
- **Continuous Output Validation with Stabilization Delay (`PumpStateMachine`)**:
  - Implemented a 500 ms mechanical stabilization grace period upon contactor actuation before evaluating output voltage presence to prevent false-negative trips during relay/contactor pull-in.
  - Added continuous output monitoring in `PumpStateMachine::tick()`:
    - **`IDLE` state:** Immediately detects unexpected voltage across motor terminals (`has_any_output_energy() == true`) and transitions to `ERROR_CONTACTOR_STUCK` (protecting against welded/stuck contactor contacts).
    - **`RUNNING` state (post-stabilization):** Detects loss of output voltage (`!has_any_output_energy()`), automatically deactivates coil drivers, and transitions safely to `ERROR_NO_SOURCE` (loss of grid/solar power or tripped breaker).
- **Comprehensive Unit Tests**:
  - Added test cases in `test_pump_state_machine` validating:
    - Source lock changes in `AUTO` mode safely de-energizing the load.
    - Immediate hot-switching in `MANUAL_RUN` mode.
    - Spontaneous contactor stuck detection in `IDLE`.
    - Voltage loss detection after the 500 ms stabilization window.

### Changed
- **AUTO Mode Source Lock Transition Safety**:
  - Changing the physical source selector switch to a specific source (`SOLAR` or `GRID`) while running in `AUTO` mode now deactivates the active contactor safely and enters `IDLE` to notify the Hub for coordinated re-arbitration, preventing blind contactor chattering/energization into unpowered sources.
- **Safety Interlock & Demagnetization Timing**:
  - Increased `ContactorConfig::demagnetization_delay_ms` default to **500 ms** for robust arc-extinction, back-EMF discharge, and break-before-make source transfer.
  - Eliminated duplicate `demagnetization_delay_ms` from `PumpStateMachineConfig` to establish `ContactorConfig` as the single source of truth.
- **Simplified `IOutputMonitor` Interface**:
  - Streamlined `IOutputMonitor` to `has_any_output_energy()` for direct, robust terminal voltage validation across all grounding topologies.

## [0.3.1] - 2026-08-27

### Added
- **`IPumpCommandHandler` Interface & Mock**:
  - Extracted abstract interface [`IPumpCommandHandler`](file:///home/german/dev/workspaces/smart-farm/smart-farm-pump-controller/main/include/interfaces/i_pump_command_handler.hpp) with pure virtual `process()`.
  - Created [`MockPumpCommandHandler`](file:///home/german/dev/workspaces/smart-farm/smart-farm-pump-controller/host_test/mocks/mock_pump_command_handler.hpp) enabling direct command response mocking in `PumpControllerTest`.
- **100% Dependency Inversion in `PumpController`**:
  - `PumpController` now receives `IPumpCommandHandler&` via Constructor Dependency Injection.

### Changed
- **Decoupled `PumpCommandHandler` from `CoreData`**:
  - Removed `CoreData* core_` dependency and `set_core_data()` from `PumpCommandHandler`.
  - Updated `PumpCommandProcessResult` to return `bool time_synced` upon successful `SYNC_TIME` processing.
  - Centralized core storage mutation and persistence within `PumpController::tick()`.
- **Visual Polish (`TankStripDisplay`)**:
  - Adjusted idle packet confirmation breathing wave duration (`IDLE_BREATHE_DURATION_MS`) to 1800 ms.
  - Adjusted chase offsets and full-tank pulsing values for enhanced visual contrast.
- **Test Suite Updates**:
  - Updated `test_pump_command_handler` and `test_pump_controller` unit tests for `time_synced` dispatch.
  - Synchronized `test_tank_strip_display` frame durations with the updated 1800 ms breathing period.
- Bumped firmware version to `0.3.1`.

## [0.3.0] - 2026-08-22

### Added
- **Water Tank Backup Mode Support (`TankStripDisplay`)**:
  - Integrated visual feedback for degraded water tank telemetry (`backup_mode_active` and `float_switch_is_full` flags in `TankLevelUpdate`).
  - Added `HUE_BACKUP` (35 / Amber) to the semantic color palette to clearly indicate float switch contingency operation.
  - Implemented binary water level rendering for backup mode: full strip in Amber when `float_switch_is_full == true`, or base LED 0 in Amber when not full.
  - Preserved all dynamic operational animations (soft packet-arrival breathing wave, Solar/Grid chasing LEDs, source-locked top LED accents, and communication timeout indicators) with automatic color substitution.
- **Protocol & Interface Updates**:
  - Extended `ITankLevelDisplay::set_level(uint16_t permille, bool backup_mode, bool is_full)`.
  - Updated `PumpCommandHandler` to parse extended `farm::TankLevelUpdate` protocol payload and forward backup mode states.
- **Extended Test Suite**:
  - Added dedicated unit tests for backup mode in `test_tank_strip_display` and `test_pump_command_handler`, bringing total test coverage to **98 passing tests**.

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
