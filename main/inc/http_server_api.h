/**
 * \file
 * \brief HTTP server JSON API handler declarations.
 *
 * Declares the three JSON API URI handler functions and the shared
 * daily-bin builder helper implemented in http_server_api.c.
 *
 * \date 2026-03-15
 */

#ifndef HTTP_SERVER_API_H
#define HTTP_SERVER_API_H

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>
#include "session_log.h"
#include "http_server_utils.h"

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Datatypes
//==================================================================================================

/**
 * \brief Aggregated session data for one calendar day (daily-aggregates window).
 */
typedef struct http_srv_daily_bin_tag
{
    int      year;             /**< tm_year value (year - 1900). */
    int      mon;              /**< tm_mon value (0–11). */
    int      mday;             /**< Day of month (1–31). */
    uint32_t sessions;         /**< Session count for this day. */
    uint64_t speed_sum_x10;    /**< Sum of avg_speed_kmh_x10 for averaging. */
    uint32_t total_duration_s; /**< Sum of duration_s for this day. */
} http_srv_daily_bin_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Fill a caller-supplied array of daily aggregation bins for the 31-day window.
 *
 * Determines today's local date from \c time_mngr_utc_get(), fills
 * \p p_bins[0..#HTTP_SRV_DAILY_WINDOW_DAYS-1] with calendar dates (oldest
 * first, ending today), then accumulates every entry in
 * \p p_sessions[0..\p count-1] into the matching bin by local date.
 * The caller must zero \p p_bins before calling this function.
 *
 * \param[out] p_bins     Caller-supplied array of #HTTP_SRV_DAILY_WINDOW_DAYS bins.
 * \param[in]  p_sessions Array of session records to aggregate.
 * \param[in]  count      Number of valid entries in \p p_sessions.
 */
void http_srv_daily_bins_build(http_srv_daily_bin_t * p_bins,
    const session_trk_record_t * p_sessions, uint16_t count);

/**
 * \brief Handler for \c GET /api/status.
 *
 * Allocates a heap buffer, builds a JSON object with live system state from
 * all subsystems, and sends it as an \c application/json response.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_status_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /api/sessions.
 *
 * Returns up to #SESSION_LOG_MAX_ENTRIES sessions as a JSON array (newest
 * first).  Each entry includes \c start_utc, \c start_local (ISO-8601 in
 * local time), \c time_synced, \c duration_s, \c pulse_count, and
 * \c avg_speed_kmh_x10.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_sessions_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /api/sessions/daily.
 *
 * Aggregates session log data into a 31-day window ending today (local date).
 * Days with no sessions are included with zero-filled counts.  Returns a JSON
 * object with a \c "days" array matching spec §6.6.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_sessions_daily_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /api/activities.
 *
 * Returns all pool activities (or only those assigned to a device if the
 * optional \c device_idx query parameter is supplied) as a JSON object with
 * an \c "activities" array.  When \c device_idx is present only activities
 * with \c credit_s > 0 are included and per-user daily status fields are added.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_activities_get_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c POST /api/activities/credit.
 *
 * Requires HTTP Basic Auth.  Accepts a JSON body with \c device_idx,
 * \c act_id, \c credits_s, and optional \c completion_time_s, then calls
 * #act_mngr_activity_credit.  Returns HTTP 200 with the new counter on
 * success, or HTTP 400/429 on validation failure.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_activities_credit_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /api/activities/log.
 *
 * Requires \c device_idx query parameter (0–3).  Returns a JSON object with
 * a \c "log" array of credit-log entries for that user, newest first.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_activities_log_get_handler(httpd_req_t * p_req);
/**
 * \brief Handler for \c GET /api/dyn.
 *
 * Returns the device PIN and the list of assigned dynamic activities with
 * daily status for the device specified by the required \c device_idx query
 * parameter.  No admin auth required.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_dyn_get_handler(httpd_req_t * p_req);
#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_API_H

/*** end of file ***/
