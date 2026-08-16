# PUMP CONTROL NODE - Architecture Overview

## System Overview

The Pump Control Node is a remote actuator (XIAO ESP32-C3) that manages a dual-source pump system via two electrically interlocked contactors. It operates in two modes: **AUTO** (Hub-controlled) and **MANUAL** (Local operator-controlled). The node maintains continuous bidirectional communication with the Smart Farm Hub via ESP-NOW and provides real-time status feedback.

---

## Electrical Architecture

**Power Sources:**
- Grid (220V AC) → Disjuntor → Source 5V #1
- Solar (220V AC, inverter) → Disjuntor → Source 5V #2
- Both 5V outputs paralleled and feed XIAO C3 + MOC/TRIAC circuits or relays

**Actuation:**
- Contactor 1 (Grid source) controlled via MOC3043M + TIC226D TRIAC pair (exactly parts need confirmation) or Relays
- Contactor 2 (Solar source) controlled via MOC3043M + TIC226D TRIAC pair (exactly parts need confirmation) or Relays
- Relay module with 4 relays and on in logic low level, two relays for contactor 1 and two for contactor 2.
- Both contactors electrically interlocked (auxiliary contacts prevent simultaneous closure)
- Outputs combined to single motor feed

**Power Delivery (Node):**
- XIAO C3 powered by 5V charger (Grid or Solar, auto-switchover)
- Battery fallback (internal LiPo) provides 24h autonomy for ESP-NOW receive
- Relay/TRIAC circuits draw from same paralleled 5V sources

**Monitoring (Future):**
- Voltage sensor on combined contactor output (EL817 + passive filter), only one in both phases (or phase + neutral) or two, onde for each phase + ground.
- Detects energization to prevent simultaneous source activation
- GPIO input to XIAO for validation logic

---

## Communication Protocol

### ESP-NOW CommandTypes (Hub → Node)

**LOAD_ON**
- Carries: circuit_id, power_source (GRID or SOLAR), watchdog_timeout_s
- Semantics: "Activate this circuit via specified source; stop responding after timeout if no refresh"
- Node behavior: Ligate target contactor, start watchdog counter

**LOAD_OFF**
- Carries: circuit_id
- Semantics: "Deactivate this circuit immediately"
- Node behavior: De-energize contactor, report idle state

### Payload Structures

**Status Report (Node → Hub)**
- circuit_id
- load_state (IDLE, RUNNING, ERROR_NO_SOURCE, ERROR_CONTACTOR_STUCK, ERROR_TIMEOUT)
- active_power_source (UNKNOWN, SOLAR, GRID) — reflects actual measured/confirmed state
- current_ma (reserved for future; currently 0 or fixed value)
- runtime_s (seconds in current cycle)
- uptime_s (node lifetime)

**Transmission Frequency:**
- On state change (immediate)
- Heartbeat every 5 seconds if no state change
- Persistent reporting during error conditions

---

## State Machine

### Core States

| State                 | Entry Condition                                      | Exit Condition                                         |
| --------------------- | ---------------------------------------------------- | ------------------------------------------------------ |
| IDLE                  | Power-on; LOAD_OFF received                          | LOAD_ON received; Manual push activated                |
| RUNNING               | Contactor successfully energized                     | LOAD_OFF received; Timeout expiry; Manual deactivation |
| ERROR_NO_SOURCE       | Selected source has no energy                        | Source restored; Operator reselects                    |
| ERROR_CONTACTOR_STUCK | Output still energized after demagnetization timeout | Manual intervention; Retry after timeout               |
| ERROR_TIMEOUT         | Command watchdog expired without refresh             | New LOAD_ON with fresh timeout                         |

### Operating Modes

**AUTO Mode:**
- Hub fully controls switching via LOAD_ON/LOAD_OFF
- Hub specifies power source based on availability sensors
- Watchdog mechanism ensures safe failsafe (default timeout resets if no refresh)
- Local switches override to MANUAL; ignored while in AUTO

**MANUAL Mode:**
- Operator controls via local switches (GRID/SOLAR selection + ON/OFF push)
- Node validates source has energy; reports error if not
- Hub receives status passively; cannot interrupt operator commands
- Useful for emergencies or system setup

---

## Local Control Interface

**Hardware:**
- Switch AUTO/MANUAL: toggles operating mode
- Switch GRID/SOLAR: selects energy source (active only in MANUAL)
- Push ON/OFF: triggers immediate on/off if in MANUAL, future behavior for AUTO mode: fill the tank if not full, needs to inquire HUB.
- LED Grid (Green): lit = Grid active, blinking = error state
- LED Solar (Yellow): lit = Solar active, blinking = error state

**LED Behavior:**
- Only one LED lit at a time (contatores are electrically interlocked)
- Blinking indicates error condition (source unavailable or contactor fault)
- Extinguished in IDLE or during source transitions

**Debouncing:**
- Software debounce for push switches (typically 20-50ms)
- Smooth transitions between MANUAL/AUTO mode
- Consider using of ui_inputs component (has used in HUB)

---

## Safety & Validation Logic

**Pre-Activation Checks:**
1. If voltage sensor available: confirm output is de-energized before activating new contactor
2. Apply 150ms demagnetization delay (allows TRIAC to block + contactor coil to desaturate)
3. Energize target contactor
4. If voltage sensor available: validate energization within 5s (ERROR_CONTACTOR_STUCK if fails)

**Watchdog Mechanism (AUTO mode):**
- Node starts countdown upon LOAD_ON reception
- If fresh LOAD_ON arrives before expiry: reset counter with new timeout value
- If timeout expires: automatically de-energize and report ERROR_TIMEOUT
- Hub re-commands if beeded.

**Mode Conflict Resolution:**
- Manual push always has priority (operator is on-site)
- Override does not change AUTO/MANUAL mode.
- Hub always receives updated status (never blind to local actions)

---

## Voltage Sensor Integration (Phase 2)

**Current State (MVP):**
- active_power_source reports selected/commanded source (not validated)
- Assumes source availability matches Hub's sensor checks

**Future State:**
- EL817 opto-coupler monitors combined output continuously
- Node reports active_power_source only if physically energized
- Missing energy triggers ERROR_NO_SOURCE state
- Prevents false positives where operator selected a dead source

---

## GPIO Allocation (XIAO ESP32-C3)

| Pin | Function                               | Type                       |
| --- | -------------------------------------- | -------------------------- |
| D0  | Mode Switch (AUTO/MANUAL)              | Digital Input              |
| D1  | Source Switch (GRID/SOLAR)             | Digital Input              |
| D2  | Push Button (ON/OFF)                   | Digital Input              |
| D3  | Contactor 1 Gate (TRIAC/MOC) or relays | Digital Output             |
| D4  | Contactor 2 Gate (TRIAC/MOC) or relays | Digital Output             |
| D5  | Voltage Sensor Input                   | Analog Input (future)      |
| D6  | LED Grid                               | Digital Output (PWM-ready) |
| D7  | LED Solar                              | Digital Output (PWM-ready) |

---

## Component Responsibilities (Proposed Architecture)

**PumpController (Main Application)**
- Receives ESP-NOW commands; dispatches to actuators
- Manages state machine transitions
- Coordinates local switch input with remote commands

**PumpContactorController**
- Abstracts contactor switching logic
- Implements pre-activation safety checks
- Manages demagnetization delays

**PumpOutputMonitor** (Future)
- Wraps voltage sensor input
- Provides `has_energy_at_output()` validation
- Abstract interface for testability

**PumpStatusReporter**
- Collects node state snapshots
- Formats and dispatches status payloads
- Manages heartbeat timing

---

## Key Design Decisions

1. **Dual CommandTypes (LOAD_ON vs LOAD_OFF):** Explicit, allows independent payload structures; ready for future command variants. Needs changes in `farm_protocol_types.hpp`, `enum class CommandType` in smart-farm-common submodule.

2. **Watchdog via Command Refresh:** Hub continuously re-sends LOAD_ON with recalculated timeout; simpler than node maintaining independent timeout logic.

3. **PowerSource as Confirmed State:** Supports MVP without sensor (reports commanded source) and scales to full validation when sensor added.

4. **Mode Priority:** Manual always wins locally; Hub remains aware; no command blindness.

5. **Sensor as Opt-In Validation:** Core logic works without it; phase 2 adds safety layer without refactoring.

---

