/**
 * \file
 * \brief HTTP server status dashboard handler declaration.
 *
 * Declares the URI handler for \c GET /, which renders the self-contained
 * HTML status dashboard implemented in http_server_dashboard.c.
 *
 * \date 2026-03-15
 */

#ifndef HTTP_SERVER_DASHBOARD_H
#define HTTP_SERVER_DASHBOARD_H

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
 * \brief Handler for \c GET /.
 *
 * Allocates a #HTTP_SRV_HTML_BUF_LEN heap buffer and builds a self-contained
 * HTML status dashboard via multiple \c httpd_resp_sendstr_chunk() calls.
 * Dynamic sections are formatted with \c snprintf into the shared buffer
 * before each chunk is sent.  The page auto-refreshes every 5 seconds.
 *
 * Sections included: System (time, NTP, uptime), Wi-Fi, Exercise Counter,
 * Current Session, Session History (last 20), Session Graphs (two SVG bar
 * charts over a 31-day window), Export Controls, and Navigation.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_root_get_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_DASHBOARD_H

/*** end of file ***/
