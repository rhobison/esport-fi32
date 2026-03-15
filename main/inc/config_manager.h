/**
 * \file
 * \brief NVS-backed runtime configuration manager public API.
 *
 * Provides typed getters and setters for every esport-fi32 configuration
 * parameter defined in the firmware specification §3.  Values are persisted
 * to NVS namespace \c esport_cfg and survive reboots.  Factory defaults are
 * applied automatically for any key that is absent or could not be read.
 *
 * \date 2026-03-14
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

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
 * \brief Initialise the NVS partition and load all configuration parameters.
 *
 * Calls \c nvs_flash_init().  If the partition is corrupt
 * (\c ESP_ERR_NVS_NO_FREE_PAGES or \c ESP_ERR_NVS_NEW_VERSION_FOUND),
 * the partition is erased and reinitialised so that factory defaults are
 * applied.  Must be called once from #app_main before any getter or setter
 * is used.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t config_mngr_init(void);

/**
 * \brief Copy the home Wi-Fi SSID into a caller-supplied buffer.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void config_mngr_wifi_ssid_get(char * p_buf, size_t len);

/**
 * \brief Copy the home Wi-Fi WPA2 password into a caller-supplied buffer.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void config_mngr_wifi_password_get(char * p_buf, size_t len);

/**
 * \brief Copy the reward Soft AP SSID into a caller-supplied buffer.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void config_mngr_soft_ap_ssid_get(char * p_buf, size_t len);

/**
 * \brief Copy the reward Soft AP WPA2 password into a caller-supplied buffer.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void config_mngr_soft_ap_password_get(char * p_buf, size_t len);

/**
 * \brief Return the number of seconds credited to the counter per accepted
 * pulse.
 *
 * \return Seconds per pulse (range 1–60).
 */
uint16_t config_mngr_seconds_per_pulse_get(void);

/**
 * \brief Return the counter threshold (seconds) at which the reward AP is
 * enabled.
 *
 * \return Threshold in seconds.
 */
uint32_t config_mngr_soft_ap_start_threshold_s_get(void);

/**
 * \brief Return the wheel travel distance in centimetres per pulse.
 *
 * \return Centimetres per pulse (>= 1).
 */
uint32_t config_mngr_centimeters_per_pulse_get(void);

/**
 * \brief Return the idle gap that closes an exercise session.
 *
 * \return Idle interval in seconds (range 5–600).
 */
uint16_t config_mngr_idle_session_interval_s_get(void);

/**
 * \brief Return the continuous-pedalling window required to confirm a session.
 *
 * \return Session-start qualification interval in seconds (range 1–300).
 */
uint16_t config_mngr_start_session_interval_s_get(void);

/**
 * \brief Return the minimum time between two accepted pulses.
 *
 * \return Software debounce time in milliseconds (range 10–5000).
 */
uint16_t config_mngr_pulse_debounce_time_ms_get(void);

/**
 * \brief Copy the POSIX timezone string into a caller-supplied buffer.
 *
 * \param[out] buf  Destination buffer.
 * \param[in]  len  Size of \p p_buf in bytes (including NUL terminator).
 */
void config_mngr_timezone_get(char * p_buf, size_t len);

/**
 * \brief Set and persist the home Wi-Fi SSID.
 *
 * \param[in] val  NUL-terminated SSID string (1–32 characters).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is out of
 *         the allowed length range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_wifi_ssid_set(const char * p_val);

/**
 * \brief Set and persist the home Wi-Fi WPA2 password.
 *
 * \param[in] val  NUL-terminated password string (0–64 characters).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is out of
 *         the allowed length range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_wifi_password_set(const char * p_val);

/**
 * \brief Set and persist the reward Soft AP SSID.
 *
 * \param[in] val  NUL-terminated SSID string (1–32 characters).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is out of
 *         the allowed length range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_soft_ap_ssid_set(const char * p_val);

/**
 * \brief Set and persist the reward Soft AP WPA2 password.
 *
 * \param[in] val  NUL-terminated password string (0–64 characters).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is out of
 *         the allowed length range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_soft_ap_password_set(const char * p_val);

/**
 * \brief Set and persist the seconds credited per accepted pulse.
 *
 * \param[in] val  Seconds per pulse (valid range: 1–60).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is outside
 *         the valid range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_seconds_per_pulse_set(uint16_t val);

/**
 * \brief Set and persist the reward AP enable threshold.
 *
 * \param[in] val  Counter threshold in seconds (range: 0–UINT32_MAX).
 *
 * \return \c ESP_OK on success, or an NVS error code on write failure.
 */
esp_err_t config_mngr_soft_ap_start_threshold_s_set(uint32_t val);

/**
 * \brief Set and persist the wheel travel distance per pulse.
 *
 * \param[in] val  Centimetres per pulse (valid range: 1–UINT32_MAX).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is 0,
 *         or an NVS error code on write failure.
 */
esp_err_t config_mngr_centimeters_per_pulse_set(uint32_t val);

/**
 * \brief Set and persist the idle gap that closes a session.
 *
 * \param[in] val  Idle interval in seconds (valid range: 5–600).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is outside
 *         the valid range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_idle_session_interval_s_set(uint16_t val);

/**
 * \brief Set and persist the session-start qualification window.
 *
 * \param[in] val  Qualification interval in seconds (valid range: 1–300).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is outside
 *         the valid range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_start_session_interval_s_set(uint16_t val);

/**
 * \brief Set and persist the pulse debounce time.
 *
 * \param[in] val  Debounce time in milliseconds (valid range: 10–5000).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is outside
 *         the valid range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_pulse_debounce_time_ms_set(uint16_t val);

/**
 * \brief Set and persist the POSIX timezone string.
 *
 * \param[in] val  NUL-terminated POSIX TZ string (1–63 characters).
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG if \p val is out of
 *         the allowed length range, or an NVS error code on write failure.
 */
esp_err_t config_mngr_timezone_set(const char * p_val);
/**
 * \brief Return the reward AP idle throughput threshold in kbps.
 *
 * If combined RX+TX throughput on the reward AP drops below this value for
 * more than #config_mngr_soft_ap_idle_throughput_timeout_s_get() consecutive
 * seconds, the time counter pauses decrementing.
 *
 * \return Threshold in kbps (range 0\u201365535; 0 = pause immediately).
 */
uint16_t config_mngr_soft_ap_dec_threshold_kbps_get(void);

/**
 * \brief Set and persist the reward AP idle throughput threshold.
 *
 * \param[in] val  Threshold in kbps (range 0\u201365535).
 *
 * \return \c ESP_OK on success, or an NVS error code on write failure.
 */
esp_err_t config_mngr_soft_ap_dec_threshold_kbps_set(uint16_t val);

/**
 * \brief Return the number of consecutive below-threshold seconds before the
 * countdown pauses.
 *
 * \return Timeout in seconds (range 0\u201365535; 0 = pause on first below-threshold
 *         tick).
 */
uint16_t config_mngr_soft_ap_idle_throughput_timeout_s_get(void);

/**
 * \brief Set and persist the below-threshold idle timeout.
 *
 * \param[in] val  Timeout in seconds (range 0\u201365535).
 *
 * \return \c ESP_OK on success, or an NVS error code on write failure.
 */
esp_err_t config_mngr_soft_ap_idle_throughput_timeout_s_set(uint16_t val);
#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H

/*** end of file ***/
