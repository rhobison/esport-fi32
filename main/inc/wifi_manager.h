/**
 * \file
 * \brief Wi-Fi manager public API (AP+STA mode with NAT).
 *
 * Manages three logical Wi-Fi interfaces:
 *   - \b STA: connects to the home network.
 *   - \b Reward SoftAP: enabled/disabled by the time counter module.
 *   - \b Config SoftAP: automatically enabled when STA is not connected,
 *     providing access to the configuration web portal.
 *
 * IP_NAPT is enabled on the AP netif so devices on either softAP can route
 * traffic through the STA interface.
 *
 * \date 2026-03-14
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the TCP/IP stack and Wi-Fi subsystem in AP+STA mode.
 *
 * Creates the default netif instances, registers event handlers, and
 * attempts STA connection using credentials from \c config_manager.
 * If the STA SSID is empty, STA connection is skipped and the config AP
 * is enabled immediately.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t wifi_manager_init(void);

/**
 * \brief Enable or disable the reward Soft AP.
 *
 * When enabling, the AP is configured with the SSID and password from
 * \c config_manager, subnet \c 192.168.5.0/24, and NAPT is (re-)applied.
 * Guards against redundant enable/disable calls.
 *
 * \param[in] enable  \c true to bring the reward AP up; \c false to take it
 * down.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t wifi_manager_set_reward_ap(bool b_enable);

/**
 * \brief Query whether the STA interface currently has an IP address.
 *
 * \return \c true if connected and an IP has been assigned, \c false otherwise.
 */
bool wifi_manager_is_sta_connected(void);

/**
 * \brief Query whether the reward Soft AP is currently active.
 *
 * \return \c true if the reward AP is up, \c false otherwise.
 */
bool wifi_manager_is_reward_ap_active(void);

/**
 * \brief Return the number of stations currently associated with the reward AP.
 *
 * \return Station count (0 if the reward AP is inactive).
 */
uint8_t wifi_manager_reward_ap_client_count(void);

/**
 * \brief Copy the STA interface IP address (dotted-decimal) into a buffer.
 *
 * Writes an empty string when the STA is not connected.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void wifi_manager_get_sta_ip(char * p_buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H

/*** end of file ***/
