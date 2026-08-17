#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_switch.hpp"

namespace ui_inputs {

class MockSwitch : public ISwitch
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(void, update, (), (override));
    MOCK_METHOD(SwitchState, get_state, (), (const, override));
    MOCK_METHOD(SwitchEvent, get_last_event, (), (override));
};

} // namespace ui_inputs
