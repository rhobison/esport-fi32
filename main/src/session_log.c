/**
 * \file
 * \brief NVS-backed session ring-buffer log - full implementation.
 *
 * Stores up to #SESSION_LOG_MAX_ENTRIES completed session records in NVS
 * namespace \c esport_log as a circular (ring) buffer.  Old entries are
 * silently overwritten when the buffer is full.  The ring-buffer head and
 * fill count are mirrored in module-level variables so that
 * session_log_count() never touches NVS.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "session_log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"

#include "event_ids.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Maximum number of session entries stored in the NVS ring buffer. */
#define SESSION_LOG_MAX_ENTRIES (50U)

/** NVS namespace used by this module. */
#define SESSION_LOG_NAMESPACE ("esport_log")

/** NVS key for the ring-buffer head index. */
#define SESSION_LOG_KEY_HEAD ("slog_head")

/** NVS key for the entry fill count. */
#define SESSION_LOG_KEY_COUNT ("slog_count")

/** Buffer size for a slot key string (e.g. "slog_49\0" fits in 9 bytes; 16 is generous). */
#define SESSION_LOG_KEY_BUF_LEN (16U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "session_log";

/**
 * \brief Index of the next slot to write into, in
 *        [0, #SESSION_LOG_MAX_ENTRIES).
 *
 * Mirrors the \c slog_head NVS key and is updated on every write so
 * read/count operations never need to open NVS.
 */
static uint16_t g_next_write_slot = 0U;

/**
 * \brief Number of valid entries currently stored, capped at
 *        #SESSION_LOG_MAX_ENTRIES.
 *
 * Mirrors the \c slog_count NVS key.
 */
static uint16_t g_entry_count = 0U;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void session_log_session_closed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data);

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Initialise the session log and register the session-closed handler.
 *
 * Opens NVS namespace \c esport_log and reads \c slog_head and
 * \c slog_count.  If either value is out of range or an unexpected NVS
 * error occurs, the namespace is erased and both counters are reset to 0.
 * Registers a handler for #ESPORT_EVENT_SESSION_CLOSED on the default
 * event loop.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t session_log_init(void)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(SESSION_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t head    = 0U;
    uint16_t count   = 0U;
    bool     b_valid = true;

    esp_err_t head_ret  = nvs_get_u16(handle, SESSION_LOG_KEY_HEAD, &head);
    esp_err_t count_ret = nvs_get_u16(handle, SESSION_LOG_KEY_COUNT, &count);

    /* Only ESP_OK (key found) and ESP_ERR_NVS_NOT_FOUND (first boot) are expected. */
    if (((ESP_OK != head_ret) && (ESP_ERR_NVS_NOT_FOUND != head_ret)) ||
        ((ESP_OK != count_ret) && (ESP_ERR_NVS_NOT_FOUND != count_ret)))
    {
        b_valid = false;
    }
    else if (head >= (uint16_t)SESSION_LOG_MAX_ENTRIES)
    {
        b_valid = false;
    }
    else if (count > (uint16_t)SESSION_LOG_MAX_ENTRIES)
    {
        b_valid = false;
    }
    else
    {
        /* Values are within valid range; nothing to do. */
    }

    if (!b_valid)
    {
        ESP_LOGW(gp_tag, "invalid NVS metadata - erasing log namespace");
        ret = nvs_erase_all(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "nvs_erase_all failed: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }
        head  = 0U;
        count = 0U;

        ret = nvs_set_u16(handle, SESSION_LOG_KEY_HEAD, head);
        if (ESP_OK == ret)
        {
            ret = nvs_set_u16(handle, SESSION_LOG_KEY_COUNT, count);
        }
        if (ESP_OK == ret)
        {
            ret = nvs_commit(handle);
        }
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "failed to commit fresh metadata: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }
    }

    g_next_write_slot = head;
    g_entry_count     = count;
    nvs_close(handle);

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_SESSION_CLOSED,
        session_log_session_closed_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete - next_write_slot=%u entry_count=%u",
        (unsigned int)g_next_write_slot, (unsigned int)g_entry_count);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Write a session record to the ring buffer.
 *
 * Stores the record at the current head slot, advances the head (wrapping
 * at #SESSION_LOG_MAX_ENTRIES), increments the fill count (capped at
 * #SESSION_LOG_MAX_ENTRIES), and commits the updated metadata to NVS.
 *
 * \param[in] p_rec  Pointer to the completed session record to persist.
 *
 * \return \c ESP_OK on success, or the first non-\c ESP_OK error encountered.
 */
esp_err_t session_log_write(const session_trk_record_t * p_rec)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(SESSION_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    char key[SESSION_LOG_KEY_BUF_LEN];
    (void)snprintf(key, sizeof(key), "slog_%u", (unsigned int)g_next_write_slot);

    ret = nvs_set_blob(handle, key, p_rec, sizeof(session_trk_record_t));
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_set_blob(%s) failed: %s", key, esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    g_next_write_slot =
        (uint16_t)(((uint32_t)g_next_write_slot + 1U) % (uint32_t)SESSION_LOG_MAX_ENTRIES);
    if (g_entry_count < (uint16_t)SESSION_LOG_MAX_ENTRIES)
    {
        g_entry_count++;
    }

    ret = nvs_set_u16(handle, SESSION_LOG_KEY_HEAD, g_next_write_slot);
    if (ESP_OK == ret)
    {
        ret = nvs_set_u16(handle, SESSION_LOG_KEY_COUNT, g_entry_count);
    }
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "failed to commit metadata: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Return the number of valid entries currently in the ring buffer.
 *
 * Reads the cached module variable; no NVS access is performed.
 *
 * \return Entry count in the range [0, #SESSION_LOG_MAX_ENTRIES].
 */
uint16_t session_log_count(void)
{
    return g_entry_count;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Read up to \p max_count sessions into \p p_out, newest first.
 *
 * Copies \c min(session_log_count(), max_count) entries into \p p_out
 * in reverse-chronological order (index 0 = most recent session).  If a
 * single NVS read fails the slot is skipped and the loop continues.
 *
 * \param[out] p_out       Destination array of \c session_trk_record_t.
 * \param[in]  max_count   Maximum number of entries to copy.
 *
 * \return Actual number of entries successfully written into \p p_out.
 */
uint16_t session_log_read(session_trk_record_t * p_out, uint16_t max_count)
{
    uint16_t actual = (g_entry_count < max_count) ? g_entry_count : max_count;
    if (0U == actual)
    {
        return 0U;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(SESSION_LOG_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return 0U;
    }

    uint16_t copied = 0U;
    for (uint16_t i = 0U; i < actual; i++)
    {
        /* Traverse newest-first: idx points to the entry written i steps ago. */
        uint16_t idx = (uint16_t)(((uint32_t)g_next_write_slot + (uint32_t)SESSION_LOG_MAX_ENTRIES -
                                      1U - (uint32_t)i) %
                                  (uint32_t)SESSION_LOG_MAX_ENTRIES);

        char key[SESSION_LOG_KEY_BUF_LEN];
        (void)snprintf(key, sizeof(key), "slog_%u", (unsigned int)idx);

        size_t    len    = sizeof(session_trk_record_t);
        esp_err_t rd_ret = nvs_get_blob(handle, key, &p_out[copied], &len);
        if (ESP_OK != rd_ret)
        {
            ESP_LOGW(gp_tag, "nvs_get_blob(%s) failed: %s - skipping slot", key,
                esp_err_to_name(rd_ret));
            continue;
        }
        copied++;
    }

    nvs_close(handle);
    return copied;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Event handler for #ESPORT_EVENT_SESSION_CLOSED.
 *
 * Extracts the #session_trk_record_t payload from the event data pointer
 * and forwards it to session_log_write().
 *
 * \param[in] p_handler_arg  Unused handler argument.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event ID (always #ESPORT_EVENT_SESSION_CLOSED).
 * \param[in] p_event_data   Pointer to the #session_trk_record_t payload.
 */
static void session_log_session_closed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;

    const session_trk_record_t * p_rec = (const session_trk_record_t *)p_event_data;
    esp_err_t                    ret   = session_log_write(p_rec);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "session_log_write failed: %s", esp_err_to_name(ret));
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
