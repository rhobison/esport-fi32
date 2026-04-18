/**
 * \file
 * \brief HTTP server activities page handler declarations.
 *
 * Declares the handlers for \c GET /activities/manage,
 * \c POST /activities/manage, and \c GET /activities.
 *
 * \date 2026-04-17
 */

#ifndef HTTP_SERVER_ACTIVITIES_H
#define HTTP_SERVER_ACTIVITIES_H

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
 * \brief Handler for \c GET /activities/manage.
 *
 * Requires HTTP Basic Auth.  Serves the admin activity-pool and assignment
 * management page.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_activities_manage_get_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c POST /activities/manage.
 *
 * Requires HTTP Basic Auth.  Processes add/update/delete activity and
 * assign/unassign actions.  Redirects to \c /activities/manage?saved=1 on
 * success, or returns HTTP 400 on validation error.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_activities_manage_post_handler(httpd_req_t * p_req);

/**
 * \brief Handler for \c GET /activities.
 *
 * Requires HTTP Basic Auth.  Serves the activity-award page; dynamic
 * content is populated by JavaScript fetch calls on the client side.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_activities_get_handler(httpd_req_t * p_req);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_ACTIVITIES_H

/*** end of file ***/
