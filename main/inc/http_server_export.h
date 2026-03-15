/**
 * \file
 * \brief HTTP server sessions export handler declaration.
 *
 * Declares the URI handler for \c GET /api/sessions/export, which is
 * implemented in http_server_export.c.
 *
 * \date 2026-03-15
 */

#ifndef HTTP_SERVER_EXPORT_H
#define HTTP_SERVER_EXPORT_H

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
 * \brief Handler for \c GET /api/sessions/export.
 *
 * Parses the \c format query parameter (\c csv or \c json; defaults to
 * \c csv), reads the session log, and dispatches to the appropriate
 * send helper.  Invalid \c format values return HTTP 400 with body
 * \c {"error":"invalid format"}.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_sessions_export_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_EXPORT_H

/*** end of file ***/
