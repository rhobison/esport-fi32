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
     * Payload: \c int64_t timestamp in microseconds
     * (\c esp_timer_get_time() value at acceptance).
     */
    ESPORT_EVENT_PULSE = 0,

    /**
     * \brief The time counter value changed (pulse credit or 1-second decrement).
     *
     * Payload: \c uint32_t current counter value in seconds.
     */
    ESPORT_EVENT_COUNTER_CHANGED = 1,

    /**
     * \brief The reward Soft AP was enabled (counter crossed the threshold).
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
     * \brief An exercise session closed after the idle timeout.
     *
     * Payload: #session_record_t copied by value into event data.
     */
    ESPORT_EVENT_SESSION_CLOSED = 4,

    /**
     * \brief STA interface obtained an IP address (connected to home network).
     *
     * No payload.
     */
    ESPORT_EVENT_STA_CONNECTED = 5,

    /**
     * \brief STA interface lost its connection or IP address.
     *
     * No payload.
     */
    ESPORT_EVENT_STA_DISCONNECTED = 6,
} esport_event_id_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

#ifdef __cplusplus
}
#endif

#endif // EVENT_IDS_H

/*** end of file ***/
