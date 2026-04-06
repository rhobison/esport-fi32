/**
 * \file
 * \brief HTTP OTA handler public declarations.
 *
 * Provides four URI handler functions that together implement a
 * Basic-Auth-protected firmware-upload page, a firmware upload endpoint,
 * and an OTA password management sub-page.
 *
 * \date 2026-04-06
 */

#ifndef HTTP_SERVER_OTA_H
#define HTTP_SERVER_OTA_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_err.h"
#include "esp_http_server.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Handle GET /ota -- serve the firmware upload page.
 *
 * Requires HTTP Basic Auth.  Returns HTTP 401 if credentials are absent or
 * incorrect.  On success, sends an HTML page with the running firmware version
 * and a file-input form for uploading a new \c .bin image.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always (error responses are sent inside the handler).
 */
esp_err_t http_srv_ota_get_handler(httpd_req_t * p_req);

/**
 * \brief Handle POST /ota -- receive and flash a firmware binary.
 *
 * Requires HTTP Basic Auth.  Reads the request body as
 * \c application/octet-stream, writes it to the inactive OTA partition, and
 * reboots into the new firmware.  Returns HTTP 500 on any write or
 * verification error.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK on success (device reboots before returning).
 * \retval \c ESP_FAIL on any OTA error.
 */
esp_err_t http_srv_ota_post_handler(httpd_req_t * p_req);

/**
 * \brief Handle GET /ota/pwd -- serve the OTA password change form.
 *
 * Requires HTTP Basic Auth.  Returns HTTP 401 if credentials are absent or
 * incorrect.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_ota_pwd_get_handler(httpd_req_t * p_req);

/**
 * \brief Handle POST /ota/pwd -- save a new OTA password.
 *
 * Requires HTTP Basic Auth.  Validates the current password, checks that the
 * new and confirmation passwords match, and persists the new password to NVS.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always (error responses are sent inside the handler).
 */
esp_err_t http_srv_ota_pwd_post_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_OTA_H

/*** end of file ***/
