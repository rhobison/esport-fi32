/**
 * \file
 * \brief Time counter and reward AP state machine public API.
 *
 * Maintains the exercise credit state machine.  Listens for
 * #ESPORT_EVENT_SESSION_OPENED and starts a one-shot timer for
 * \c internet_gate_threshold_s seconds.  When the timer fires the state
 * transitions to EARNING and accumulated session credits are flushed to the
 * current rider's #device_registry counter.
 *
 * While a session is open (#ESPORT_EVENT_SESSION_OPENED received), each
 * #ESPORT_EVENT_PULSE credits \c seconds_per_pulse seconds to a local
 * accumulator (#g_session_credits).  Credits are flushed to the current
 * rider's #device_registry counter when the threshold fires.  If the session
 * closes (#ESPORT_EVENT_SESSION_CLOSED) before the threshold timer fires, the
 * threshold timer is cancelled but #g_session_credits are **retained** so that
 * exercise effort is preserved.  The credits remain pending and will be
 * flushed when a subsequent session reaches the gate threshold, or when a new
 * session starts while the rider's device counter is already positive (gate
 * bypass).  Pulses in EARNING state add directly to the rider's
 * #device_registry counter.
 *
 * The 1-second tick timer runs permanently from #time_ctr_init() and calls
 * #device_reg_tick() unconditionally once per second.  Per-device counter
 * decrement and the sliding-window traffic gate are handled entirely inside
 * #device_reg_tick().
 *
 * State machine:
 *   - \b IDLE: no session open; threshold timer not running.
 *   - \b SESSION: session confirmed open; threshold timer running; pulses
 *     accumulate in #g_session_credits.  Transitions to EARNING when the
 *     timer fires, or back to IDLE when the session closes (accumulator reset).
 *   - \b EARNING: session has been active for \c internet_gate_threshold_s;
 *     pulses add directly to the current rider's counter; device counters
 *     decrement via #device_reg_tick() in tick callback.
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

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the time counter and register event handlers.
 *
 * Registers handlers for #ESPORT_EVENT_PULSE, #ESPORT_EVENT_SESSION_OPENED,
 * #ESPORT_EVENT_SESSION_CLOSED, and #ESPORT_EVENT_CONFIG_CHANGED.  Creates
 * the 1-second tick timer and starts it immediately so that #device_reg_tick()
 * is called every second for the lifetime of the firmware.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t time_ctr_init(void);

/**
 * \brief Return the current rider's internet time counter in seconds.
 *
 * Delegates to #device_reg_entry_counter_get() for the index returned by
 * #device_reg_current_rider_get().  Returns \c 0 when no rider is selected.
 *
 * Thread-safe: acquires and releases the internal spinlock.
 *
 * \return Current rider counter value in seconds, or \c 0 when no rider is selected.
 */
uint32_t time_ctr_get(void);

/**
 * \brief Return the most recently computed instantaneous speed in km/h x 10.
 *
 * Updated on every accepted pulse.  Returns \c 0 when the state machine is in
 * #TIME_CTR_STATE_IDLE or when fewer than two pulses have been accepted.
 * Thread-safe: acquires and releases the internal spinlock.
 *
 * \return Current speed in km/h x 10 (e.g. 123 = 12.3 km/h), or 0 when idle.
 */
uint32_t time_ctr_current_speed_x10_get(void);

/**
 * \brief Set the current rider's counter to \p val and persist to NVS immediately.
 *
 * Delegates to #device_reg_entry_counter_set() for the index returned by
 * #device_reg_current_rider_get().  Returns #ESP_ERR_INVALID_STATE and logs a
 * warning when no rider is selected.
 *
 * Calling this function is the correct way for the web UI to grant or edit
 * internet time at runtime.
 *
 * Thread-safe: acquires and releases the internal spinlock.
 *
 * \param[in] val  New counter value in seconds (0 -- UINT32_MAX).
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_STATE when no rider is currently selected.
 */
esp_err_t time_ctr_counter_set(uint32_t val);

#ifdef __cplusplus
}
#endif

#endif // TIME_COUNTER_H

/*** end of file ***/
