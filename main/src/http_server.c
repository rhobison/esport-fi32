]633;
E;
git status;f0493384-99a3-46f2-8e38-142ed1c650d5]633;
C/**
   * \file
   * \brief HTTP server — Phase 8 full implementation.
   *
   * Starts an \c esp_http_server instance on port 80 and registers all
   * application URI handlers.  Provides a configuration web portal
   * (GET+POST \c /config) and a JSON/CSV data API (\c /api/status,
   * \c /api/sessions, \c /api/sessions/export, \c /api/sessions/daily).
   *
   * \date 2026-03-14
   */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server.h"
#include "http_server_utils.h"
#include "http_server_config.h"
#include "http_server_api.h"
#include "http_server_export.h"
#include "http_server_dashboard.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config_manager.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"

    //==================================================================================================
    // Internal Constants/Macros/Datatypes
    //==================================================================================================

    //==================================================================================================
    // Variables/Data
    //==================================================================================================

    /** Module log tag. */
    static const char * gp_tag = "http_server";

/** Handle to the running \c esp_http_server instance, or \c NULL. */
static httpd_handle_t gp_server_handle = NULL;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Start the HTTP server and register all URI handlers.
 *
 * Creates an \c esp_http_server instance on port 80 and registers
 * handlers for \c GET /config, \c POST /config, \c GET /api/status,
 * \c GET /api/sessions, \c GET /api/sessions/export, and
 * \c GET /api/sessions/daily.  (\c GET / is reserved for Phase 9.)
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_init(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80U;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8U;

    esp_err_t ret = httpd_start(&gp_server_handle, &cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    static const httpd_uri_t sc_uri_root_get = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = http_srv_root_get_handler,
    };
    static const httpd_uri_t sc_uri_config_get = {
        .uri     = "/config",
        .method  = HTTP_GET,
        .handler = http_srv_config_get_handler,
    };
    static const httpd_uri_t sc_uri_config_post = {
        .uri     = "/config",
        .method  = HTTP_POST,
        .handler = http_srv_config_post_handler,
    };
    static const httpd_uri_t sc_uri_api_status = {
        .uri     = "/api/status",
        .method  = HTTP_GET,
        .handler = http_srv_api_status_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions = {
        .uri     = "/api/sessions",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions_export = {
        .uri     = "/api/sessions/export",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_export_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions_daily = {
        .uri     = "/api/sessions/daily",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_daily_handler,
    };

    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_root_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_config_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_config_post);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_status);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_export);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_daily);

    ESP_LOGI(gp_tag, "started on port %" PRIu16, cfg.server_port);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/*** end of file ***/
