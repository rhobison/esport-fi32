/**
 * \file
 * \brief HTTP server facade - starts the server and registers all URI handlers.
 *
 * \date 2026-03-15
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
#include "http_server_ota.h"

#include <string.h>

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
 * handlers for \c GET /, \c GET /config, \c POST /config,
 * \c POST /config/reset, \c GET /api/status, \c GET /api/sessions,
 * \c GET /api/sessions/export, and \c GET /api/sessions/daily.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_init(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80U;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 13U;

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
    static const httpd_uri_t sc_uri_config_reset = {
        .uri     = "/config/reset",
        .method  = HTTP_POST,
        .handler = http_srv_config_reset_handler,
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
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_config_reset);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_status);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_export);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_daily);

    static const httpd_uri_t sc_uri_ota_get = {
        .uri     = "/ota",
        .method  = HTTP_GET,
        .handler = http_srv_ota_get_handler,
    };
    static const httpd_uri_t sc_uri_ota_post = {
        .uri     = "/ota",
        .method  = HTTP_POST,
        .handler = http_srv_ota_post_handler,
    };
    static const httpd_uri_t sc_uri_ota_pwd_get = {
        .uri     = "/ota/pwd",
        .method  = HTTP_GET,
        .handler = http_srv_ota_pwd_get_handler,
    };
    static const httpd_uri_t sc_uri_ota_pwd_post = {
        .uri     = "/ota/pwd",
        .method  = HTTP_POST,
        .handler = http_srv_ota_pwd_post_handler,
    };
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_post);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_pwd_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_pwd_post);

    ESP_LOGI(gp_tag, "started on port %u", (unsigned)cfg.server_port);
    return ESP_OK;
}

/*** end of file ***/
