/**
 * \file
 * \brief HTTP server public API.
 *
 * Starts an \c esp_http_server instance on port 80 and registers all
 * application URI handlers:
 *   - \c GET  /               Status dashboard (HTML, auto-refresh).
 *   - \c GET  /config         Configuration form (HTML).
 *   - \c POST /config         Save and apply configuration.
 *   - \c GET  /api/status     Live state (JSON).
 *   - \c GET  /api/sessions   Session history (JSON).
 *   - \c GET  /api/sessions/export  Downloadable CSV or JSON report.
 *   - \c GET  /api/sessions/daily   Daily aggregates for graph rendering.
 *
 * The server is reachable on every active IP (STA, reward AP, config AP)
 * and starts before the STA connection attempt so that the config portal
 * is immediately available via the config AP.
 *
 * \date 2026-03-14
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Start the HTTP server and register all URI handlers.
 *
 * Must be called after #wifi_manager_init() and #config_manager_init()
 * but before any HTTP request can be served.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_server_init(void);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_H

/*** end of file ***/
