/**
 * \file
 * \brief Time counter and reward AP state machine public API.
 *
 * Maintains the exercise time counter (unit: seconds, minimum 0).
 * Listens for #ESPORT_EVENT_PULSE events and credits
 * \c seconds_per_pulse seconds for each accepted pulse.
 *
 * A 1-second periodic timer decrements the counter by one when the reward
 * AP is active.  The counter never falls below zero.
 *
 * State machine:
 *   - \b IDLE: counter < threshold, reward AP off; pulses add credits only.
 *   - \b ACTIVE: counter >= threshold, reward AP on; pulses add credits,
 *     timer decrements.  Transitions back to IDLE when counter reaches 0.
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
esp_err_t time_counter_init(void);

/**
 * \brief Return the current time counter value in seconds.
 *
 * Thread-safe: acquires and releases the internal spinlock.
 *
 * \return Current counter value in seconds (>= 0).
 */
uint32_t time_counter_get(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_COUNTER_H

/*** end of file ***/
