/**
 * \file
 * \brief HTTP server dynamic activities page handler declarations.
 *
 * Declares the handlers for the user-facing \c GET /dyn launch page and the
 * \c GET /dyn_activities/\* wildcard file server that serves embedded HTML
 * mini-game files.
 *
 * \date 2026-04-18
 */

#ifndef HTTP_SERVER_DYN_H
#define HTTP_SERVER_DYN_H

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Handler for \c GET /dyn.
 *
 * User-facing dynamic activities launch page.  No admin auth is required.
 * Identifies the requesting device by resolving the TCP connection's source
 * IP address against the lwIP ARP table and matching the resulting MAC address
 * against the device registry.  Renders that device's assigned dynamic
 * activities as launch buttons populated by a JavaScript fetch to
 * \c GET /api/dyn.  Shows an error page when the device is unregistered.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_dyn_page_get_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /dyn_activities/\*.
 *
 * Serves an embedded dynamic activity HTML file by logical name.  The
 * filename portion of the URI (after \c /dyn_activities/) is matched against
 * \c g_dyn_act_registry[]; a trailing \c .html extension is stripped before
 * the lookup.  Returns HTTP 404 when the name is not found.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_dyn_file_get_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_DYN_H */

/*** end of file ***/
