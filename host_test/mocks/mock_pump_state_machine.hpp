#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_pump_state_machine.hpp"

class MockPumpStateMachine : public IPumpStateMachine
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, handle_load_on, (const farm::LoadOnCommand& cmd), (override));
    MOCK_METHOD(esp_err_t, handle_load_off, (const farm::LoadOffCommand& cmd), (override));
    MOCK_METHOD(esp_err_t, handle_manual_start, (farm::PowerSource source), (override));
    MOCK_METHOD(esp_err_t, handle_manual_stop, (), (override));
    MOCK_METHOD(void, set_control_mode, (farm::ControlMode mode), (override));
    MOCK_METHOD(void, tick, (uint32_t delta_ms), (override));
    MOCK_METHOD(farm::LoadState, get_state, (), (const, override));
    MOCK_METHOD(farm::ControlMode, get_control_mode, (), (const, override));
    MOCK_METHOD(farm::PowerSource, get_active_source, (), (const, override));
    MOCK_METHOD(uint32_t, get_runtime_s, (), (const, override));
    MOCK_METHOD(PumpStateSnapshot, get_snapshot, (), (const, override));
    MOCK_METHOD(bool, consume_state_changed, (), (override));
};
