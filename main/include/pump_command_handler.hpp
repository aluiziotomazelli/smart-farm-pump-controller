// main/include/pump_command_handler.hpp
#pragma once

#include "interfaces/i_pump_command_handler.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_pump_state_machine.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_tank_level_display.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class PumpCommandHandler
 * @brief Drains incoming ESP-NOW message queue and routes commands to appropriate subsystems.
 */
class PumpCommandHandler : public IPumpCommandHandler
{
public:
    PumpCommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        IPumpStateMachine& state_machine,
        time_manager::ITimeManager& time_manager,
        ITankLevelDisplay& tank_display,
        idf_hals::IHalFreertos& hal_freertos);

    /** @copydoc IPumpCommandHandler::process */
    PumpCommandProcessResult process() override;

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    IPumpStateMachine& state_machine_;
    time_manager::ITimeManager& time_manager_;
    ITankLevelDisplay& tank_display_;
    idf_hals::IHalFreertos& hal_freertos_;

    void process_command_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result);
    void process_data_message(const espnow::AppMessage& msg, PumpCommandProcessResult& result);
};
