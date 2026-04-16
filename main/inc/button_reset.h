/**
 * \file
 * \brief BOOT button long-press password reset module public API.
 *
 * Monitors the ESP32-C6 BOOT button (GPIO 9, configurable via Kconfig) using
 * a 100 ms periodic \c esp_timer.  When the button is held for
 * \c CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S seconds, both the config page and
 * OTA passwords are reset to their factory defaults and a 2.5 s buzzer beep
 * confirms the action.
 *
 * \date 2026-04-16
 */

#ifndef BUTTON_RESET_H
#define BUTTON_RESET_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Polling interval of the BOOT button timer in milliseconds. */
#define BTN_RST_POLL_INTERVAL_MS (100U)

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the button reset module.
 *
 * Configures \c CONFIG_ESPORT_BOOT_BUTTON_GPIO as a digital input with an
 * internal pull-up resistor and starts a 100 ms periodic \c esp_timer that
 * polls the GPIO level.  When the button is held for
 * \c CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S seconds, both config and OTA
 * passwords are reset to factory defaults and
 * \c BUZZER_PATTERN_PASSWORD_RESET is played.
 *
 * Must be called after \c buzzer_init(), \c config_mngr_init(), and
 * \c ota_mngr_init().
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t btn_rst_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_RESET_H */

/*** end of file ***/
