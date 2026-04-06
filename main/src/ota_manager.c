/**
 * \file
 * \brief OTA firmware update manager implementation.
 *
 * Wraps ESP-IDF esp_ota_ops to provide a simple write-chunk state machine
 * used by the HTTP OTA handler.  Manages the OTA password in NVS namespace
 * \c esport_ota and marks the running firmware valid on every boot to cancel
 * any pending rollback window.
 *
 * \date 2026-04-06
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "ota_manager.h"

#include <string.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** NVS namespace used by the OTA manager. */
#define OTA_MNGR_NVS_NAMESPACE ("esport_ota")

/** NVS key for the OTA Basic Auth password. */
#define OTA_MNGR_NVS_KEY_PWD ("ota_pwd")

/** Default OTA password applied when the NVS key is absent. */
#define OTA_MNGR_DEFAULT_PWD ("esport-fi32")

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "ota_manager";

/** Current OTA state machine state. */
static ota_mngr_state_t g_state = OTA_MNGR_STATE_IDLE;

/** Active OTA write handle (valid only in RECEIVING / VERIFYING states). */
static esp_ota_handle_t g_ota_handle = 0;

/** Target partition for the current update (non-NULL in RECEIVING / VERIFYING / READY). */
static const esp_partition_t * gp_update_partition = NULL;

/** Open NVS handle for the esport_ota namespace, set by #ota_mngr_init(). */
static nvs_handle_t g_nvs_handle = 0;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Initialise the OTA manager.
 *
 * Marks the running firmware valid (cancels any pending rollback), opens the
 * \c esport_ota NVS namespace, and ensures the default OTA password exists.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t ota_mngr_init(void)
{
    esp_err_t ret;

    /* Mark the running image as valid.  This call is intentionally NOT wrapped
       in ESP_ERROR_CHECK: on a factory partition (before the partition table
       migration) it returns an error that is non-fatal and expected. */
    ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ESP_OK != ret)
    {
        ESP_LOGI(gp_tag, "mark_app_valid: %s (non-fatal on factory partition)",
            esp_err_to_name(ret));
    }

    /* Open the OTA NVS namespace. */
    ret = nvs_open(OTA_MNGR_NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Ensure the default password exists. */
    char buf[OTA_MNGR_PASSWORD_MAX_LEN + 1U];
    ret = nvs_get_str(g_nvs_handle, OTA_MNGR_NVS_KEY_PWD, buf, &(size_t) { sizeof(buf) });
    if (ESP_ERR_NVS_NOT_FOUND == ret)
    {
        ret = nvs_set_str(g_nvs_handle, OTA_MNGR_NVS_KEY_PWD, OTA_MNGR_DEFAULT_PWD);
        if (ESP_OK == ret)
        {
            ret = nvs_commit(g_nvs_handle);
        }
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "failed to write default password: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(gp_tag, "running fw v%s", esp_app_get_description()->version);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Begin an OTA firmware update session.
 *
 * \param[in] image_size  Total image size in bytes, or 0 to use sequential-erase mode.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if an update is already in progress.
 * \retval \c ESP_ERR_NOT_SUPPORTED if no OTA partition exists.
 */
esp_err_t ota_mngr_begin(size_t image_size)
{
    if (OTA_MNGR_STATE_IDLE != g_state)
    {
        ESP_LOGE(gp_tag, "begin: invalid state %d", (int)g_state);
        return ESP_ERR_INVALID_STATE;
    }

    gp_update_partition = esp_ota_get_next_update_partition(NULL);
    if (NULL == gp_update_partition)
    {
        ESP_LOGE(gp_tag, "begin: no OTA partition found (partition table not migrated?)");
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t write_size = (0U == image_size) ? OTA_WITH_SEQUENTIAL_WRITES : image_size;

    esp_err_t ret = esp_ota_begin(gp_update_partition, write_size, &g_ota_handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        gp_update_partition = NULL;
        return ret;
    }

    g_state = OTA_MNGR_STATE_RECEIVING;
    ESP_LOGI(gp_tag, "begin: writing to partition %s", gp_update_partition->label);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Write a chunk of firmware data to the inactive OTA slot.
 *
 * \param[in] p_data  Pointer to firmware data buffer.
 * \param[in] len     Number of bytes to write.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if not in #OTA_MNGR_STATE_RECEIVING.
 */
esp_err_t ota_mngr_write(const void * p_data, size_t len)
{
    if (OTA_MNGR_STATE_RECEIVING != g_state)
    {
        ESP_LOGE(gp_tag, "write: invalid state %d", (int)g_state);
        return ESP_ERR_INVALID_STATE;
    }
    return esp_ota_write(g_ota_handle, p_data, len);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Finalise the OTA write and verify the image header.
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_STATE if not in #OTA_MNGR_STATE_RECEIVING.
 */
esp_err_t ota_mngr_end(void)
{
    if (OTA_MNGR_STATE_RECEIVING != g_state)
    {
        ESP_LOGE(gp_tag, "end: invalid state %d", (int)g_state);
        return ESP_ERR_INVALID_STATE;
    }

    g_state = OTA_MNGR_STATE_VERIFYING;

    esp_err_t ret = esp_ota_end(g_ota_handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_ota_end failed: %s", esp_err_to_name(ret));
        (void)ota_mngr_abort();
        return ret;
    }

    g_state = OTA_MNGR_STATE_READY;
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Abort a firmware update in progress.
 *
 * Idempotent: safe to call in any state.
 *
 * \return \c ESP_OK always.
 */
esp_err_t ota_mngr_abort(void)
{
    if (OTA_MNGR_STATE_IDLE == g_state)
    {
        return ESP_OK;
    }

    if (OTA_MNGR_STATE_RECEIVING == g_state)
    {
        (void)esp_ota_abort(g_ota_handle);
    }

    g_state             = OTA_MNGR_STATE_IDLE;
    g_ota_handle        = 0;
    gp_update_partition = NULL;
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Set the new firmware as the boot partition and reboot.
 *
 * Does not return on success.  On failure, logs the error, calls
 * #ota_mngr_abort(), and returns without rebooting.
 */
void ota_mngr_activate(void)
{
    if (OTA_MNGR_STATE_READY != g_state)
    {
        ESP_LOGE(gp_tag, "activate: not in READY state (state=%d)", (int)g_state);
        return;
    }

    esp_err_t ret = esp_ota_set_boot_partition(gp_update_partition);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        (void)ota_mngr_abort();
        return;
    }

    ESP_LOGI(gp_tag, "OTA update complete, rebooting...");
    esp_restart();
    /* Does not return. */
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Return the current OTA manager state.
 *
 * \return Current #ota_mngr_state_t value.
 */
ota_mngr_state_t ota_mngr_state_get(void)
{
    return g_state;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Copy the running firmware version string into \p p_buf.
 *
 * \param[out] p_buf  Destination buffer.
 * \param[in]  len    Size of \p p_buf in bytes.
 */
void ota_mngr_running_version_get(char * p_buf, size_t len)
{
    (void)snprintf(p_buf, len, "%s", esp_app_get_description()->version);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Check whether \p p_password matches the stored OTA password.
 *
 * \param[in] p_password  Candidate password string.
 *
 * \return \c true if the password matches, \c false otherwise.
 */
bool ota_mngr_credentials_check(const char * p_password)
{
    char stored[OTA_MNGR_PASSWORD_MAX_LEN + 1U];
    ota_mngr_password_get(stored, sizeof(stored));
    return (0 == strcmp(stored, p_password));
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Copy the stored OTA password into \p p_buf.
 *
 * Falls back to the default password if the NVS read fails.
 *
 * \param[out] p_buf  Destination buffer.
 * \param[in]  len    Size of \p p_buf in bytes.
 */
void ota_mngr_password_get(char * p_buf, size_t len)
{
    size_t    out_len = len;
    esp_err_t ret     = nvs_get_str(g_nvs_handle, OTA_MNGR_NVS_KEY_PWD, p_buf, &out_len);
    if (ESP_OK != ret)
    {
        (void)snprintf(p_buf, len, "%s", OTA_MNGR_DEFAULT_PWD);
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Persist a new OTA password to NVS.
 *
 * \param[in] p_password  New password string (1 -- #OTA_MNGR_PASSWORD_MAX_LEN chars).
 *
 * \return \c ESP_OK on success.
 * \retval \c ESP_ERR_INVALID_ARG if \p p_password is empty or too long.
 */
esp_err_t ota_mngr_password_set(const char * p_password)
{
    size_t pwd_len = strlen(p_password);

    if ((0U == pwd_len) || (pwd_len > OTA_MNGR_PASSWORD_MAX_LEN))
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_set_str(g_nvs_handle, OTA_MNGR_NVS_KEY_PWD, p_password);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_set_str failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(g_nvs_handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_commit failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/*** end of file ***/
