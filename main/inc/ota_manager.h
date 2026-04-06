/**
 * \file
 * \brief OTA firmware update manager public API.
 *
 * Wraps ESP-IDF esp_ota_ops to provide a simple write-chunk interface
 * used by the HTTP OTA handler.  Manages password storage in NVS
 * namespace \c esport_ota.  Marks the running firmware valid at init
 * time to cancel any pending rollback.
 *
 * \date 2026-04-06
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Maximum length of the OTA password (excluding null terminator). */
#define OTA_MNGR_PASSWORD_MAX_LEN (63U)

/** Fixed HTTP Basic Auth username for the OTA endpoint. */
#define OTA_MNGR_HTTP_USERNAME ("admin")

/**
 * \brief OTA manager state.
 */
typedef enum ota_mngr_state_tag
{
    OTA_MNGR_STATE_IDLE      = 0, /**< No update in progress. */
    OTA_MNGR_STATE_RECEIVING = 1, /**< Receiving firmware chunks. */
    OTA_MNGR_STATE_VERIFYING = 2, /**< Finalising write, verifying header. */
    OTA_MNGR_STATE_READY     = 3, /**< Image verified; ready to activate. */
} ota_mngr_state_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the OTA manager.
 *
 * Marks the running firmware valid (cancels any pending rollback), opens the
 * \c esport_ota NVS namespace, and ensures the default OTA password exists.
 * Must be called after \c nvs_flash_init() and before \c http_srv_init().
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t ota_mngr_init(void);

/**
 * \brief Begin an OTA firmware update session.
 *
 * Selects the inactive OTA partition slot and opens an OTA write handle.
 * The caller must be in #OTA_MNGR_STATE_IDLE.
 *
 * \param[in] image_size  Total image size in bytes, or 0 to use sequential-erase mode.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if an update is already in progress.
 * \retval \c ESP_ERR_NOT_SUPPORTED if no OTA partition exists in the partition table.
 */
esp_err_t ota_mngr_begin(size_t image_size);

/**
 * \brief Write a chunk of firmware data to the inactive OTA slot.
 *
 * \param[in] p_data  Pointer to firmware data buffer.
 * \param[in] len     Number of bytes to write.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if not in #OTA_MNGR_STATE_RECEIVING.
 */
esp_err_t ota_mngr_write(const void * p_data, size_t len);

/**
 * \brief Finalise the OTA write and verify the image header.
 *
 * Transitions state to #OTA_MNGR_STATE_READY on success, or calls
 * #ota_mngr_abort() and returns an error on failure.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if not in #OTA_MNGR_STATE_RECEIVING.
 */
esp_err_t ota_mngr_end(void);

/**
 * \brief Abort a firmware update in progress.
 *
 * Idempotent: safe to call in any state, including #OTA_MNGR_STATE_IDLE.
 *
 * \return \c ESP_OK always.
 */
esp_err_t ota_mngr_abort(void);

/**
 * \brief Set the new firmware as the boot partition and reboot.
 *
 * Does not return on success.  If the activation fails, logs the error,
 * calls #ota_mngr_abort(), and returns without rebooting.
 */
void ota_mngr_activate(void);

/**
 * \brief Return the current OTA manager state.
 *
 * \return Current #ota_mngr_state_t value.
 */
ota_mngr_state_t ota_mngr_state_get(void);

/**
 * \brief Copy the running firmware version string into \p p_buf.
 *
 * \param[out] p_buf  Destination buffer.
 * \param[in]  len    Size of \p p_buf in bytes.
 */
void ota_mngr_running_version_get(char * p_buf, size_t len);

/**
 * \brief Check whether \p p_password matches the stored OTA password.
 *
 * The supplied password is never logged.
 *
 * \param[in] p_password  Candidate password string.
 *
 * \return \c true if the password matches, \c false otherwise.
 */
bool ota_mngr_credentials_check(const char * p_password);

/**
 * \brief Copy the stored OTA password into \p p_buf.
 *
 * Falls back to the default password if the NVS read fails.
 *
 * \param[out] p_buf  Destination buffer.
 * \param[in]  len    Size of \p p_buf in bytes.
 */
void ota_mngr_password_get(char * p_buf, size_t len);

/**
 * \brief Persist a new OTA password to NVS.
 *
 * \param[in] p_password  New password string (1 -- #OTA_MNGR_PASSWORD_MAX_LEN chars).
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_ARG if \p p_password is empty or too long.
 */
esp_err_t ota_mngr_password_set(const char * p_password);

#ifdef __cplusplus
}
#endif

#endif // OTA_MANAGER_H

/*** end of file ***/
