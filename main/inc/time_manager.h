/**
 * \file
 * \brief SNTP time synchronisation and timezone management public API.
 *
 * Starts SNTP synchronisation once the STA interface has an IP address.
 * Applies the POSIX timezone string from \c config_manager via
 * \c setenv("TZ", ...) and \c tzset().  Maintains a \c time_synced flag
 * that is set to \c true on the first successful synchronisation.
 *
 * When synchronisation has never completed, timestamps are generated from
 * \c esp_timer_get_time() offset from epoch \c 946684800
 * (2000-01-01T00:00:00Z) so the device remains functional with monotonic
 * timestamps even without network access.
 *
 * \date 2026-03-14
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the time manager and register event-driven SNTP start.
 *
 * Registers a handler on the default event loop for
 * #ESPORT_EVENT_STA_CONNECTED.  When that event fires, SNTP polling is
 * started using \c pool.ntp.org and \c time.cloudflare.com.  Also calls
 * #time_mngr_timezone_apply() immediately to set the initial timezone.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t time_mngr_init(void);

/**
 * \brief Query whether SNTP has completed at least one successful
 * synchronisation.
 *
 * \return \c true after the first successful sync; \c false otherwise.
 */
bool time_mngr_is_synced(void);

/**
 * \brief Return the current time as seconds since the Unix epoch (UTC).
 *
 * If SNTP has synced, this is equivalent to \c time(NULL).  If not yet
 * synced, returns a monotonically increasing value derived from
 * \c esp_timer_get_time() offset from epoch \c 946684800.
 *
 * \return Current UTC time as a \c time_t value.
 */
time_t time_mngr_utc_get(void);

/**
 * \brief Apply the stored POSIX timezone string via \c setenv and \c tzset.
 *
 * Reads the \c timezone parameter from \c config_manager and applies it.
 * Call this after any configuration change that modifies the timezone.
 */
void time_mngr_timezone_apply(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_MANAGER_H

/*** end of file ***/
