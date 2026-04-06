/**
 * \file
 * \brief Exercise session detection and tracking public API.
 *
 * Listens for #ESPORT_EVENT_PULSE events and implements a two-phase
 * session detection algorithm:
 *
 *   -# \b Qualification: On the first pulse after idle, a qualification
 *      timer is started.  If pulses continue without a gap greater than
 *      \c idle_session_interval_s for the duration of
 *      \c start_session_interval_s, the session is confirmed.
 *   -# \b Active: An idle timer is reset on each pulse.  When the idle
 *      timer fires (no pulse for \c idle_session_interval_s seconds), the
 *      session is closed and #ESPORT_EVENT_SESSION_CLOSED is posted with
 *      a #session_trk_record_t payload.
 *
 * Configuration values are re-read from \c config_manager at the start of
 * each session so that changes apply without a reboot.
 *
 * \date 2026-03-14
 */

#ifndef SESSION_TRACKER_H
#define SESSION_TRACKER_H

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

/**
 * \brief Record describing a completed exercise session.
 *
 * Posted as the event data payload for #ESPORT_EVENT_SESSION_CLOSED
 * (copied by value, not by pointer).
 *
 * The NVS binary representation packs \c duration_s and \c pulse_count
 * into 16-bit fields; values are capped at \c UINT16_MAX when serialised
 * by \c session_log.
 */
typedef struct
{
    /**
     * \brief Session start, seconds since Unix epoch.
     *
     * Set to 0 (boot-epoch fallback) when the clock was not synchronised.
     */
    int64_t start_time_utc;

    /** \c true if the system clock was SNTP-synced at session start. */
    bool b_time_synced;

    /** Session duration in seconds (last_pulse_time - start_time). */
    uint32_t duration_s;

    /**
     * \brief Total accepted pulses during the session.
     *
     * Includes pulses counted during the qualification window.
     */
    uint32_t pulse_count;

    /**
     * \brief Average speed in km/h multiplied by 10.
     *
     * For example, 123 represents 12.3 km/h.  Zero if \c duration_s is zero.
     */
    uint16_t avg_speed_kmh_x10;

    /**
     * \brief Internet time earned during the session, in seconds.
     *
     * Computed at session close as \c pulse_count * \c seconds_per_pulse
     * (the value of \c seconds_per_pulse cached at session start).  This is
     * a theoretical maximum -- speed-gated pulses still contribute because
     * counting them reflects the full physical effort.  Display in h:mm:ss.
     */
    uint32_t internet_earned_s;
} session_trk_record_t;

/**
 * \brief Live session state snapshot for use by the HTTP status API.
 *
 * Populated by #session_trk_live_status_get(). Fields are consistent with
 * the current state machine state at the moment of the call.  This function
 * is intended for read-only display; no locking is performed.
 */
typedef struct session_trk_live_status_tag
{
    /** Human-readable state name: \c "idle", \c "qualifying", or \c "active". */
    const char * p_state_name;

    /** UTC timestamp of the confirmed session start.  Zero unless state is active. */
    int64_t start_utc;

    /** Elapsed seconds since #start_utc.  Zero unless state is active. */
    uint32_t duration_s;

    /** Accepted pulse count for the current session or qualifying window.  Zero when idle. */
    uint32_t pulse_count;

    /**
     * \brief Current session average speed in km/h x 10.
     *
     * Computed from (#pulse_count x centimeters_per_pulse) and #duration_s.
     * Zero when state is not active or #duration_s is zero.
     */
    uint16_t live_speed_kmh_x10;
} session_trk_live_status_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the session tracker and register the pulse event handler.
 *
 * Creates the FreeRTOS qualification and idle-detection timers (not started
 * until the first qualifying pulse) and registers a handler for
 * #ESPORT_EVENT_PULSE on the default event loop.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t session_trk_init(void);

/**
 * \brief Fill a #session_trk_live_status_t with the current session state.
 *
 * Reads the session tracker's internal state variables to provide a
 * point-in-time snapshot.  Intended for display purposes only; the snapshot
 * may be slightly stale under concurrent timer callbacks.
 *
 * \param[out] p_out  Destination status structure to populate.
 */
void session_trk_live_status_get(session_trk_live_status_t * p_out);

#ifdef __cplusplus
}
#endif

#endif // SESSION_TRACKER_H

/*** end of file ***/
