/**
 * \file
 * \brief NVS-backed session ring-buffer log public API.
 *
 * Listens for #ESPORT_EVENT_SESSION_CLOSED events and persists each
 * #session_record_t to NVS namespace \c esport_log as a circular buffer
 * of up to #SESSION_LOG_MAX_ENTRIES (50) entries.  Old entries are
 * silently overwritten when the buffer is full.
 *
 * NVS keys: \c slog_head (uint16), \c slog_count (uint16), and
 * \c slog_0 \u2026 \c slog_49 (blobs of \c session_record_t).
 *
 * On NVS corruption of the log namespace, the namespace is erased and
 * the log is reinitialised from empty.
 *
 * \date 2026-03-14
 */

#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdint.h>

#include "esp_err.h"
#include "session_tracker.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the session log and register the session-closed handler.
 *
 * Opens NVS namespace \c esport_log and reads \c slog_head and
 * \c slog_count.  If metadata is invalid (indices out of range), the
 * namespace is erased and reinitialised.  Registers a handler for
 * #ESPORT_EVENT_SESSION_CLOSED on the default event loop.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t session_log_init(void);

/**
 * \brief Write a session record to the ring buffer.
 *
 * Stores the record at the current head index, advances the head, and
 * increments the count (capped at \c SESSION_LOG_MAX_ENTRIES).  Commits
 * the updated metadata to NVS immediately.
 *
 * \param[in] rec  Pointer to the session record to persist.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on NVS failure.
 */
esp_err_t session_log_write(const session_record_t * p_rec);

/**
 * \brief Return the number of valid entries currently in the ring buffer.
 *
 * \return Entry count in the range [0, #SESSION_LOG_MAX_ENTRIES].
 */
uint16_t session_log_count(void);

/**
 * \brief Read up to \p max_count sessions into \p p_out, newest first.
 *
 * Fills \p p_out in reverse-chronological order (index 0 = most recent).
 * Reads \c min(session_log_count(), max_count) entries.
 *
 * \param[out] out        Destination array of #session_record_t.
 * \param[in]  max_count  Maximum number of entries to copy.
 *
 * \return Actual number of entries written into \p p_out.
 */
uint16_t session_log_read(session_record_t * p_out, uint16_t max_count);

#ifdef __cplusplus
}
#endif

#endif // SESSION_LOG_H

/*** end of file ***/
