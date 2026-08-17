// main/include/pump_command_handler.hpp
#pragma once

#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_tank_level_display.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "core_types.hpp"

/**
 * @struct PumpCommandProcessResult
 * @brief Result flags returned after draining and processing ESP-NOW queue messages.
 */
struct PumpCommandProcessResult
{
    bool core_modified{false};     ///< True if system clock or core state was updated
    bool ota_requested{false};    ///< True if START_OTA command was received
    bool reboot_requested{false}; ///< True if REBOOT command was received
};

/**
 * @class PumpCommandHandler
 * @brief Drains incoming ESP-NOW message queue and routes commands to appropriate subsystems.
 */
class PumpCommandHandler
{
public:
    PumpCommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        IPumpStateMachine& state_machine,
        time_manager::ITimeManager& time_manager,
        ITankLevelDisplay& tank_display,
        CoreStorage& core,
        idf_hals::IHalFreertos& hal_freertos);

    /**
     * @brief Drains all available messages in rx_queue and executes corresponding handlers.
     * @return PumpCommandProcessResult struct with execution flags.
     */
    PumpCommandProcessResult process();

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    IPumpStateMachine& state_machine_;
    time_manager::ITimeManager& time_manager_;
    ITankLevelDisplay& tank_display_;
    CoreStorage& core_;
    idf_hals::IHalFreertos& hal_freertos_;

    void process_command_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result);
    void process_data_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result);
};
