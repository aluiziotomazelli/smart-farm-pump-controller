# Smart Farm Pump Controller - System Design & Architecture

## 1. Executive Summary & Overview

The **Smart Farm Pump Controller** is a mission-critical IoT actuator node based on the **Seeed Studio XIAO ESP32-C3**. It manages dual-source power switching (**Solar Photovoltaic Inverter** vs. **Utility Grid**) for high-power water pumping in an automated agricultural environment.

The system features robust hardware and software safety interlocking, bidirectional **ESP-NOW** communication with the Smart Farm Hub, autonomous safety watchdog timers, local manual overrides (3-position switch + action button), and real-time visual diagnostic indicators via a dedicated addressable WS2812B LED strip task.

```
                  +----------------------------------------------+
                  |               Smart Farm Hub                 |
                  +----------------------------------------------+
                                         |
                                         | ESP-NOW (2.4 GHz)
                                         v
+----------------------------------------------------------------------------------+
|                          Smart Farm Pump Controller                              |
|                                                                                  |
|   +---------------------+     +--------------------+     +-------------------+   |
|   |   Local HMI Inputs  |     |   PumpController   |     |  TankStripDisplay |   |
|   |  - 3-pos Switch     | --> |  (FreeRTOS Task,   | --> |  (FreeRTOS Task,  |   |
|   |    (SOLAR/AUTO/GRID)|     |   Priority 5)      |     |   Priority 2)     |   |
|   |  - Pushbutton       |     +---------+----------+     +---------+---------+   |
|   +---------------------+               |                          |             |
|                                         v                          v             |
|                              +----------------------+    +-------------------+   |
|                              |  PumpStateMachine    |    | WS2812B LED Strip |   |
|                              |  (Safety Watchdog)   |    | (Level + Status)  |   |
|                              +----------+-----------+    +-------------------+   |
|                                         |                                        |
|                                         v                                        |
|                              +----------------------+                            |
|                              | ContactorController  |                            |
|                              | (Break-before-make)  |                            |
|                              +----------+-----------+                            |
+-----------------------------------------|----------------------------------------+
                                          |
                     +--------------------+--------------------+
                     |                                         |
                     v                                         v
        +--------------------------+              +--------------------------+
        |   Grid AC Contactor      |              |   Solar AC Contactor     |
        | (Utility 220V AC Source) |              | (Inverter 220V AC Source)|
        +-------------+------------+              +------------+-------------+
                      |                                        |
                      +-------------------+--------------------+
                                          |
                                          v
                               +---------------------+
                               |   Water Pump Motor  |
                               +---------------------+
```

---

## 2. Hardware Architecture & Electrical Safety

### 2.1. Dual-Source Power Delivery
- **Paralleled 5V Power Rails:** Two 5V AC/DC converters (one powered by Grid 220V AC, the other by Solar Inverter 220V AC) have their DC outputs paralleled through Schottky diodes to power the XIAO ESP32-C3 and driver circuitry.
- **Battery Autonomy:** Optional 1S Li-Po / 18650 cell allows continuous ESP-NOW reception and telemetry reporting during complete AC outages.

### 2.2. Contactor Actuation & Interlocking
To prevent catastrophic short-circuits between unsynchronized AC sources (Grid and Solar Inverter):
1. **Mechanical / Electrical Interlock:** Auxiliary NC (normally closed) contacts physically prevent both contactor coils from energizing simultaneously.
2. **Software Break-Before-Make:** `ContactorController` enforces an asynchronous **demagnetization delay (150 ms default)** whenever switching between sources or turning off a contactor.
3. **Driver Compatibility:** Configurable `active_level` (Active-High for optically isolated MOC3043M TRIAC drivers, or Active-Low for standard relay modules) with automatic pull-up/pull-down configuration on boot.

---

## 3. Hardware Pinout (Seeed Studio XIAO ESP32-C3)

| Pin | GPIO | Identifier | Direction | Description | Active Level |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **D0** | `GPIO 2` | `PIN_BUTTON_ACTION` | Input | Manual Start/Stop Pushbutton (or Manual Fill Request in AUTO) | Low (Pull-Up, Press = Trigger) |
| **D1** | `GPIO 3` | `PIN_SWITCH_SOLAR` | Input | 3-pos Switch Solar Position | Low (Pull-Up, Closed = Solar Selected) |
| **D2** | `GPIO 4` | `PIN_SWITCH_GRID` | Input | 3-pos Switch Grid Position | Low (Pull-Up, Closed = Grid Selected) |
| **D3** | `GPIO 5` | `PIN_CONTACTOR_GRID` | Output | Grid Contactor Gate Trigger | High (MOC) / Low (Relay) |
| **D4** | `GPIO 6` | `PIN_CONTACTOR_SOLAR`| Output | Solar Contactor Gate Trigger | High (MOC) / Low (Relay) |
| **D5** | `GPIO 7` | `PIN_LED_STRIP_DATA` | Output | WS2812B Addressable LED Strip Data | High (RMT Periph) |
| **D9** | `GPIO 9` | `PIN_BUTTON_BOOT_OTA`| Input | Onboard BOOT Button / Emergency OTA Trigger | Low (Pull-Up, Press = OTA) |
| **D10**| `GPIO 10`| `PIN_OUTPUT_MONITOR` | Input | Output Voltage AC Monitor (Phase 2) | Low/High |

> **Note on 3-Position Switch**: Center position leaves both `PIN_SWITCH_SOLAR` and `PIN_SWITCH_GRID` open $\rightarrow$ `ControlMode::AUTO`. If both are closed (mechanical contact bounce/conflict), the firmware safely defaults to Grid priority.

---

## 4. Software Architecture & Design Principles

The firmware adheres to clean architecture principles tailored for high-reliability embedded systems:

- **Single Responsibility Principle (SRP):** Each class encapsulates a single functional domain (State Machine, Contactors, LED Strip Display, Protocol Parsing, Telemetry).
- **Hardware Abstraction Layer (HAL):** Business logic has zero direct dependencies on ESP-IDF or FreeRTOS headers. Hardware primitives are injected through abstract interfaces (`IGpioHAL`, `ITimerHAL`, `IHalFreertos`, `IHalLedStrip`, `ISystemHAL`).
- **Dependency Injection (DI):** All components receive their dependencies via constructors, enabling 100% host-based unit testing on Linux.
- **Dedicated Tasks & Non-Blocking Messaging**:
  - `PumpController`: Runs at 50 ms loop intervals (Priority 5, Stack 4096 bytes), handling safety logic, switch updates, FSM transitions, and telemetry.
  - `TankStripDisplay`: Runs an independent FreeRTOS task at 20 FPS / 50 ms periods (Priority 2, Stack 3072 bytes). Methods post to a thread-safe message queue (`display_queue_`), completely isolating LED rendering from network and flash operations.

---

## 5. Component Breakdown

```
main/
├── include/
│   ├── interfaces/
│   │   ├── i_contactor_controller.hpp   # Contactor hardware abstraction
│   │   ├── i_hal_led_strip.hpp          # LED strip hardware abstraction
│   │   ├── i_output_monitor.hpp         # Output AC voltage feedback
│   │   ├── i_pump_controller.hpp        # Top-level coordinator interface
│   │   ├── i_pump_state_machine.hpp     # Pump FSM and watchdog interface
│   │   ├── i_pump_status_reporter.hpp   # Telemetry sender interface
│   │   ├── i_tank_level_display.hpp     # Base display interface (set_level)
│   │   └── i_tank_strip_display.hpp     # Addressable LED strip display interface
│   ├── contactor_controller.hpp         # Break-before-make contactor driver
│   ├── hal_led_strip.hpp                # Concrete RMT LED strip wrapper
│   ├── pump_command_handler.hpp         # ESP-NOW inbound command router
│   ├── pump_controller.hpp              # Main orchestrator task
│   ├── pump_state_machine.hpp           # State machine with safety watchdog
│   ├── pump_status_reporter.hpp         # Telemetry broadcaster
│   ├── pump_types.hpp                   # Domain enums and data structures
│   └── tank_strip_display.hpp           # Dedicated task WS2812B display controller
└── src/
    ├── contactor_controller.cpp
    ├── hal_led_strip.cpp
    ├── pump_command_handler.cpp
    ├── pump_controller.cpp
    ├── pump_state_machine.cpp
    ├── pump_status_reporter.cpp
    └── tank_strip_display.cpp
```

---

## 6. Finite State Machine (`PumpStateMachine`)

### 6.1. States & Transitions
```mermaid
stateDiagram-v2
    [*] --> IDLE
    
    IDLE --> RUNNING: LOAD_ON (AUTO mode) / Start Button (SOURCE_LOCKED mode)
    RUNNING --> IDLE: LOAD_OFF / Stop Button / Demagnetization Delay
    
    RUNNING --> ERROR_TIMEOUT: Watchdog expired without refresh (AUTO mode)
    ERROR_TIMEOUT --> RUNNING: Fresh LOAD_ON with valid watchdog
    ERROR_TIMEOUT --> IDLE: LOAD_OFF / Switch mode
    
    IDLE --> ERROR_CONTACTOR_STUCK: Output energized before contactor activation
    RUNNING --> ERROR_CONTACTOR_STUCK: Output de-energized unexpectedly
    ERROR_CONTACTOR_STUCK --> IDLE: Hardware reset / manual clear
```

### 6.2. Autonomous Safety Watchdog
- In **AUTO** mode, `LOAD_ON` specifies a `watchdog_timeout_s` (e.g., 300 seconds).
- `PumpStateMachine::tick(50ms)` decrements the timer.
- If the Hub goes offline or radio packets are lost, the node automatically cuts power to the pump when the watchdog reaches 0, preventing tank overflow or dry-run damage.

---

## 7. Communication Protocol & Telemetry

### 7.1. Outbound Telemetry (`LOAD_CONTROL_STATUS = 0x04`)
Broadcasted via ESP-NOW to `farm::NodeId::HUB`:
- **Event-Driven Instant Trigger:** Dispatched on the exact 50ms tick of any state transition (`IDLE` $\leftrightarrow$ `RUNNING`, mode switch, manual buttons, errors).
- **Periodic Telemetry:** Every 5 seconds when in `RUNNING` mode.
     - In `IDLE` mode, heartbeats rely on `espnow_manager` internal heartbeat system, which are supressed in `RUNNING` mode.

```cpp
struct LoadControlStatus
{
    uint8_t circuit_id;                  // Circuit ID (0 for primary pump)
    PowerProfile power_profile;          // ALWAYS_ON (0x00)
    ControlMode control_mode;            // AUTO (1), STOP_OVERRIDE (2), SOURCE_LOCKED (3)
    PowerSource active_power_source;     // UNKNOWN (0), SOLAR (1), GRID (2)
    LoadState load_state;                // IDLE (0), RUNNING (1), ERROR_*
    uint32_t runtime_s;                  // Continuous runtime in current activation
    uint32_t uptime_s;                   // Total node uptime in seconds
    uint32_t power_w;                    // Real-time instantaneous load power (W)
};
```

### 7.2. Outbound Requests (`FILL_REQUEST = 0x07`)
When the physical 3-position switch is in the center position (`ControlMode::AUTO`) and the user clicks the physical Pushbutton, the controller dispatches a `FillRequest` packet to the Hub. This allows the Hub to coordinate water tank refill operations using the best available power source.

### 7.3. Inbound Commands & ACK Routing
`PumpCommandHandler` drains inbound messages from `rx_queue` and issues immediate ACKs:
- `LOAD_ON` (`0x41`): Activates specified source with watchdog; returns `AckStatus::OK` or `ERROR_PROCESSING` (if in STOP_OVERRIDE mode).
- `LOAD_OFF` (`0x42`): Deactivates contactors; returns `AckStatus::OK`.
- `SYNC_TIME` (`0x43`): Synchronizes system clock via `TimeManager`.
- `TANK_LEVEL_UPDATE` (`0x05`): Updates local water level display via `display_.set_level(level_permille)`.
- `START_OTA` (`0x01`): Initiates firmware upgrade session.
- `REBOOT` (`0x02`): Gracefully de-energizes contactors, persists state, and restarts ESP32.

---

## 8. Visual Feedback (`TankStripDisplay`)

The addressable WS2812B LED strip (20 LEDs default) is driven by a dedicated FreeRTOS task running at 20 FPS (50 ms).

### 8.1. Semantic Color Palette
- `HUE_GRID` (0 / Red): Utility grid power source.
- `HUE_TIMEOUT` (30 / Orange): Watchdog timeout error.
- `HUE_BACKUP` (35 / Amber): Water tank operating in degraded backup mode (mechanical float switch).
- `HUE_SOLAR` (120 / Green): Solar photovoltaic inverter power source.
- `HUE_FILL` (180 / Cyan): Water volume in tank.
- `HUE_OTA` (280 / Purple): OTA firmware upgrade in progress.
- `HUE_BOOT_SUCCESS` (120 / Green): Successful boot self-test sweep.
- `HUE_BOOT_ERROR` (0 / Red): Boot failure SOS warning.
- `HUE_FAULT` (0 / Red): Electrical contactor fault.

### 8.2. Dynamic Animation Patterns
| Mode / State | Animation Pattern | Description |
| :--- | :--- | :--- |
| **IDLE (AUTO)** | Static Cyan Bar + Breathing Wave | Shows current tank level in Cyan (`HUE_FILL`). Pulses in brightness on every received telemetry packet. |
| **IDLE (AUTO - Backup Mode)** | Amber Bar (Full or 1/4 of `num_leds`) + Breathing Wave | When Water Tank operates in backup mode (`backup_mode_active`), base water shifts from Cyan to Amber (`HUE_BACKUP`). Full strip lights up if `float_switch_is_full == true`; 1/4 of `num_leds` lights up if not full. |
| **IDLE (SOURCE_LOCKED)** | Cyan/Amber Bar + Top LED Color Accent | Shows tank level in Cyan (or Amber in backup), with the top LED highlighted in Green (`HUE_SOLAR`) or Red (`HUE_GRID`) indicating the locked source. |
| **RUNNING (AUTO - Solar)** | Cyan/Amber Bar + Green Chasing LED | Base water level in Cyan (or Amber in backup) with a Green LED chasing upwards (200 ms/LED). |
| **RUNNING (AUTO - Grid)** | Cyan/Amber Bar + Red Chasing LED | Base water level in Cyan (or Amber in backup) with a Red LED chasing upwards (200 ms/LED). |
| **RUNNING (Manual)** | Solid Color Bar + Chasing LED | Active base in solid Green (Solar) or Red (Grid) for high-visibility operator alert. |
| **ERROR_TIMEOUT** | Cyan/Amber Bar + Blinking Orange Top LED | Top LED blinks in Orange (1 Hz) while maintaining tank level visibility. |
| **ERROR_CONTACTOR_STUCK**| Full-Strip Red Pulsing | All LEDs pulse in Red at 2 Hz for critical hardware alert. |
| **OTA_UPDATING** | Purple Knight Rider Scanner | Bidirectional Purple scanner running continuously in the background during download. |
| **BOOT_SUCCESS** | Green Sweep Animation | Progressive 0% to 100% green sweep on boot confirmation, automatically reverting to normal operation. |
| **BOOT_ERROR** | SOS Red Triple-Flash | 3 fast red bursts on failed boot verification before rollback. |

---

## 9. Testing & CI/CD Strategy

### 9.1. Host-Based Unit Testing (Linux)
Testing is treated as a first-class citizen. 100% of business logic is compiled and verified natively on Linux using GoogleTest and GoogleMock:

| Test Project | Test Count | Description |
| :--- | :--- | :--- |
| `test_pump_state_machine` | 14 tests | State transitions, watchdog expiry, auto/manual rules, source locking |
| `test_pump_command_handler` | 12 tests | ESP-NOW command decoding, validation, ACK replies, tank level routing |
| `test_pump_status_reporter` | 11 tests | Heartbeat timing, state change triggers, payloads, power telemetry |
| `test_contactor_controller` | 8 tests | Demagnetization delay, active-high/low, interlocking |
| `test_pump_controller` | 15 tests | 3-pos switch sampling, pushbutton triggers, OTA flow, boot health checks |
| `test_ota_controller` | 13 tests | Partition verification, rollback, download execution |
| `test_tank_strip_display` | 25 tests | Dedicated task, command queue, semantic palette, animation frames, backup mode |
| **Total** | **98 tests** | **100% Passing** |

### 9.2. Automated GitHub Actions CI
- **`build.yml`:** Compiles production target firmware for ESP32-C3.
- **`host_test.yml`:** Executes full test suite natively on Linux runners with zero hardware dependencies.
