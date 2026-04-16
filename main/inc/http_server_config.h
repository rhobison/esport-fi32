/**
 * \file
 * \brief HTTP server configuration page handler declarations.
 *
 * Declares the GET and POST /config URI handler functions implemented in
 * http_server_config.c.  Include this header from http_server.c and any
 * other translation unit that registers these handlers.
 *
 * \date 2026-03-15
 */

#ifndef HTTP_SERVER_CONFIG_H
#define HTTP_SERVER_CONFIG_H

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

/** Maximum length including NUL for the Authorization header value. */
#define HTTP_SRV_CFG_AUTH_HDR_MAX (256U)

/** Minimum length of "Basic " prefix in the Authorization header. */
#define HTTP_SRV_CFG_BASIC_PREFIX_LEN (6U)

/** Maximum length of the decoded "user:password" string. */
#define HTTP_SRV_CFG_DECODED_MAX (192U)

/**
 * \brief Handler for \c GET /config.
 *
 * Reads all 11 configuration parameters and sends an HTML form pre-populated
 * with their current values.  If the query string contains \c saved=1 a
 * success banner is prepended to the form body.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_config_get_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c POST /config.
 *
 * Reads the URL-encoded request body (up to #HTTP_SRV_POST_BODY_MAX_LEN
 * bytes), validates each of the 11 configuration fields present in the body,
 * and applies them via the config_manager setters.  Rejects string values
 * exceeding the spec-maximum length with HTTP 400.  On success, issues an
 * HTTP 302 redirect to \c /config?saved=1.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_config_post_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c POST /config/reset.
 *
 * Resets every configuration parameter to its factory default, applies the
 * default timezone, and posts \c ESPORT_EVENT_CONFIG_CHANGED.  On success,
 * issues an HTTP 302 redirect to \c /config?reset=1.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_config_reset_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /config/pwd.
 *
 * Serves the config page password change form.  Requires HTTP Basic Auth.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_config_pwd_get_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c POST /config/pwd.
 *
 * Validates and saves a new config page password.  Requires HTTP Basic Auth.
 * Redirects to \c /config/pwd?saved=1 on success, or returns HTTP 400 on
 * validation failure.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_config_pwd_post_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_CONFIG_H

/*** end of file ***/
