/**
 * \file
 * \brief esport-fi32 application event base and event ID definitions.
 *
 * All inter-module communication uses the default ESP event loop.
 * Every event is posted under #ESPORT_EVENT_BASE, which is declared
 * here and defined in \c main.c.
 *
 * \date 2026-03-14
 */

#ifndef EVENT_IDS_H
#define EVENT_IDS_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_event.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/**
 * \brief Application-wide event base for all esport-fi32 events.
 *
 * Declared here; defined once via \c ESP_EVENT_DEFINE_BASE in \c main.c.
 */
ESP_EVENT_DECLARE_BASE(ESPORT_EVENT_BASE);

/**
 * \brief esport-fi32 application event identifiers.
 *
 * All events are posted on the default ESP event loop under
 * #ESPORT_EVENT_BASE.  Payload types are noted per event.
 */
typedef enum
{
    /**
     * \brief A valid debounced pulse received from the bike sensor.
     *
     * No payload (NULL). The ISR inline payload is limited to 4 bytes; an
     * int64_t µs timestamp does not fit. Handlers derive timing by calling
     * esp_timer_get_time() directly — the sub-ms handler latency is negligible
     * for all second-resolution consumers.
     */
    ESPORT_EVENT_PULSE = 0,

    /**
     * \brief The time counter value changed (pulse credit or 1-second decrement).
     *
     * Payload: \c uint32_t current counter value in seconds.
     */
    ESPORT_EVENT_COUNTER_CHANGED = 1,

    /**
     * \brief The reward Soft AP was enabled (session duration reached the threshold).
     *
     * No payload.
     */
    ESPORT_EVENT_REWARD_AP_ON = 2,

    /**
     * \brief The reward Soft AP was disabled (counter reached zero).
     *
     * No payload.
     */
    ESPORT_EVENT_REWARD_AP_OFF = 3,

    /**
     * \brief An exercise session is confirmed open (QUALIFYING->ACTIVE transition).
     *
     * No payload.  Posted by \c session_tracker once the qualification window
     * (#start_session_interval_s) elapses without a disqualifying idle gap.
     */
    ESPORT_EVENT_SESSION_OPENED = 4,

    /**
     * \brief An exercise session closed after the idle timeout.
     *
     * Payload: #session_trk_record_t copied by value into event data.
     */
    ESPORT_EVENT_SESSION_CLOSED = 5,

    /**
     * \brief STA interface obtained an IP address (connected to home network).
     *
     * No payload.
     */
    ESPORT_EVENT_STA_CONNECTED = 6,

    /**
     * \brief One or more configuration values were saved via the web portal.
     *
     * No payload.  Modules that cache runtime configuration values must
     * re-read them from #config_manager in response to this event.
     */
    ESPORT_EVENT_CONFIG_CHANGED = 8,

    /**
     * \brief STA interface lost its connection or IP address.
     *
     * No payload.
     */
    ESPORT_EVENT_STA_DISCONNECTED = 7,

    /**
     * \brief The device registry was modified (entry added, removed, or counter changed).
     *
     * No payload.  Posted by #device_registry whenever any entry is added,
     * removed, or a per-device counter changes (including reaching zero).
     */
    ESPORT_EVENT_DEVICE_REGISTRY_CHANGED = 9,
} esport_event_id_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

#ifdef __cplusplus
}
#endif

#endif // EVENT_IDS_H

/*** end of file ***/
