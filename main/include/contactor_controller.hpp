// main/include/contactor_controller.hpp
#pragma once

#include <cstdint>

#include "interfaces/i_contactor_controller.hpp"
#include "interfaces/i_hal_gpio.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "pump_types.hpp"

/**
 * @class ContactorController
 * @brief Concrete implementation of IContactorController with mutual exclusion and demagnetization delays.
 */
class ContactorController : public IContactorController
{
public:
    ContactorController(
        idf_hals::IGpioHAL& hal_gpio,
        idf_hals::IHalFreertos& hal_rtos,
        const ContactorConfig& config = ContactorConfig{});

    ~ContactorController() override;

    /** @copydoc IContactorController::init */
    esp_err_t init() override;

    /** @copydoc IContactorController::activate */
    esp_err_t activate(farm::PowerSource source) override;

    /** @copydoc IContactorController::deactivate */
    esp_err_t deactivate() override;

    /** @copydoc IContactorController::get_active_source */
    farm::PowerSource get_active_source() const override { return active_source_; }

    /** @copydoc IContactorController::is_active */
    bool is_active() const override { return active_source_ != farm::PowerSource::UNKNOWN; }

private:
    idf_hals::IGpioHAL& hal_gpio_;
    idf_hals::IHalFreertos& hal_rtos_;
    ContactorConfig config_;

    farm::PowerSource active_source_{farm::PowerSource::UNKNOWN};

    void apply_gpio_state(farm::PowerSource source, bool active);
};
