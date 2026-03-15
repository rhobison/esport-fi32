/**
 * \file
 * \brief GPIO pulse input module public API.
 *
 * Configures #CONFIG_ESPORT_PULSE_GPIO as an input with internal pull-up
 * and installs a falling-edge interrupt.  Software debounce rejects edges
 * that arrive sooner than \c pulse_debounce_time_ms milliseconds after the
 * previously accepted edge.  Each accepted pulse posts an
 * #ESPORT_EVENT_PULSE event on the default event loop.
 *
 * The ISR is kept minimal (\c IRAM_ATTR); all business logic runs in event
 * callbacks outside interrupt context.
 *
 * \date 2026-03-14
 */

#ifndef PULSE_INPUT_H
#define PULSE_INPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdint.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Configure the pulse GPIO and install the falling-edge ISR.
 *
 * Reads #config_mngr_pulse_debounce_time_ms_get() to initialise the debounce
 * window.  Installs the GPIO ISR service if not already installed.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t pulse_in_init(void);

/**
 * \brief Return the cumulative count of accepted (debounced) pulses.
 *
 * Intended for diagnostic and status display use only.
 *
 * \return Total accepted pulse count since boot.
 */
uint32_t pulse_in_total_count_get(void);

/**
 * \brief Return the count of pulses accepted by the ISR but dropped because
 * the event loop queue was full.
 *
 * A non-zero value means the system cannot keep up; increase
 * CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE if this grows continuously.
 *
 * \return Dropped pulse count since boot.
 */
uint32_t pulse_in_dropped_count_get(void);

/**
 * \brief Return the last error code from a failed esp_event_isr_post call.
 *
 * \return ESP_OK if no post has ever failed, otherwise the last failure code.
 */
esp_err_t pulse_in_last_post_err_get(void);

#ifdef __cplusplus
}
#endif

#endif // PULSE_INPUT_H

/*** end of file ***/
