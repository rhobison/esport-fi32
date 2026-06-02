/**
 * \file
 * \brief HTTP server telemetry endpoint and client logging page declarations.
 *
 * Declares the URI handlers for \c GET /api/telemetry (live diagnostic JSON)
 * and \c GET /telemetry (a self-contained client-side logging page) that are
 * implemented in http_server_telemetry.c.
 *
 * \date 2026-05-30
 */

#ifndef HTTP_SERVER_TELEMETRY_H
#define HTTP_SERVER_TELEMETRY_H

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Handler for \c GET /api/telemetry.
 *
 * Returns a JSON object with live diagnostic metrics (uptime, last reset
 * reason, heap statistics, CPU load, Wi-Fi link quality, reconnect/disconnect
 * counters, and reward-AP client state) for the \c /telemetry logging page.
 * Open access (no auth).
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_telemetry_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /api/coredump.
 *
 * Returns a JSON object describing the most recent core dump stored in the
 * dedicated flash partition.  If no valid core dump is present the response
 * is \c {"found":false}.  On success the object contains the faulting task
 * name, program counter, RISC-V register snapshot, and the raw stack dump
 * encoded as a hex string.  Open access (diagnostic data only).
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_coredump_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /telemetry.
 *
 * Serves a static HTML page that polls \c /api/telemetry and logs the most
 * recent 1000 responses browser-side with local timestamps.  Open access.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success.
 */
esp_err_t http_srv_telemetry_page_get_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_TELEMETRY_H

/*** end of file ***/
