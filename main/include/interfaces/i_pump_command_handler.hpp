// main/include/interfaces/i_pump_command_handler.hpp
#pragma once

#include <cstdint>

/**
 * @struct PumpCommandProcessResult
 * @brief Result flags returned after draining and processing ESP-NOW queue messages.
 */
struct PumpCommandProcessResult
{
    bool time_synced{false};      ///< True if system clock was updated from incoming SYNC_TIME packet
    bool ota_requested{false};    ///< True if START_OTA command was received
    bool reboot_requested{false}; ///< True if REBOOT command was received
};

/**
 * @class IPumpCommandHandler
 * @brief Interface for draining incoming ESP-NOW message queue and routing commands to pump subsystems.
 */
class IPumpCommandHandler
{
public:
    virtual ~IPumpCommandHandler() = default;

    /**
     * @brief Drains all available messages in rx_queue and executes corresponding handlers.
     * @return PumpCommandProcessResult struct with execution flags.
     */
    virtual PumpCommandProcessResult process() = 0;
};
