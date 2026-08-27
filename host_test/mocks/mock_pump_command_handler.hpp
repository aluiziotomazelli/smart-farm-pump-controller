// host_test/mocks/mock_pump_command_handler.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_pump_command_handler.hpp"

class MockPumpCommandHandler : public IPumpCommandHandler
{
public:
    MOCK_METHOD(PumpCommandProcessResult, process, (), (override));
};
