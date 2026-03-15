/**
 * \file
 * \brief Time counter and reward AP state machine public API.
 *
 * Maintains the exercise time counter (unit: seconds, minimum 0).
 * Listens for #ESPORT_EVENT_SESSION_OPENED and starts a one-shot timer for
 * \c soft_ap_start_threshold_s seconds.  When the timer fires the reward AP
 * is enabled and a 1-second periodic timer starts decrementing the counter.
 *
 * While a session is open (#ESPORT_EVENT_SESSION_OPENED received), each
 * #ESPORT_EVENT_PULSE credits \c seconds_per_pulse seconds to the counter.
 * If the session closes (#ESPORT_EVENT_SESSION_CLOSED) before the threshold
 * timer fires, the threshold timer is cancelled and the counter resets to
 * zero.  If the AP is already active the session-close event is ignored and
 * the counter continues to drain.
 *
 * State machine:
 *   - \b IDLE: no session, counter 0, reward AP off.
 *   - \b SESSION: session confirmed open; threshold timer running; pulses add
 *     credits.  Transitions to AP_ACTIVE when the timer fires, or back to
 *     IDLE when the session closes (counter reset).
 *   - \b AP_ACTIVE: reward AP on; pulses still add credits; 1-second tick
 *     decrements.  Transitions back to IDLE when counter reaches 0.
 *
 * The counter variable is protected by a spinlock against concurrent access
 * from the FreeRTOS timer callback and the ESP event loop callbacks.
 *
 * \date 2026-03-14
 */

#ifndef TIME_COUNTER_H
#define TIME_COUNTER_H

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
 * \brief Initialise the time counter and register the pulse event handler.
 *
 * Registers a handler for #ESPORT_EVENT_PULSE on the default event loop
 * and creates the 1-second decrement timer (not started until the counter
 * first reaches the threshold).
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t time_ctr_init(void);

/**
 * \brief Return the current time counter value in seconds.
 *
 * Thread-safe: acquires and releases the internal spinlock.
 *
 * \return Current counter value in seconds (>= 0).
 */
uint32_t time_ctr_get(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_COUNTER_H

/*** end of file ***/
