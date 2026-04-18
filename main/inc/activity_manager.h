/**
 * \file
 * \brief Activity Manager - global activity pool, per-user assignments,
 *        daily done-count tracking, credit log, and credit-apply function.
 *
 * Manages a pool of up to #ACT_MNGR_MAX_ACTIVITIES (30) activities that a
 * parent/admin can define.  Each activity can be assigned to up to
 * #DEVICE_REG_MAX_ENTRIES device slots.  Clicking a credit button (or calling
 * the JSON API) runs #act_mngr_activity_credit, which validates the daily
 * limit, adds the credits to the target device's counter via the Device
 * Registry, appends a credit-log entry, and posts
 * #ESPORT_EVENT_ACTIVITY_CREDITED.
 *
 * All in-RAM state is protected by a single spinlock.  NVS writes happen
 * outside the spinlock.
 *
 * NVS Namespace: \c esport_act
 *
 * \date 2026-04-17
 */

#ifndef ACTIVITY_MANAGER_H
#define ACTIVITY_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Maximum number of activities in the global pool. */
#define ACT_MNGR_MAX_ACTIVITIES (30U)

/** Maximum number of activities assignable to one user. */
#define ACT_MNGR_MAX_ASSIGNS_PER_USER (20U)

/** Maximum number of credit-log entries per user (ring buffer). */
#define ACT_MNGR_MAX_CREDIT_LOG (30U)

/** Maximum length of an activity name (excluding NUL terminator). */
#define ACT_MNGR_NAME_MAX_LEN (20U)

/** Reserved activity ID meaning "no activity" / empty slot. */
#define ACT_MNGR_NO_ID (0U)

/** Periodic NVS save interval in seconds (unused currently; reserved). */
#define ACT_MNGR_SAVE_INTERVAL_S (60U)

/**
 * \brief One entry in the global activity pool.
 *
 * Stored as a blob under NVS key \c ac_N in namespace \c esport_act.
 */
typedef struct act_mngr_entry_tag
{
    uint32_t id;                               /**< Auto-generated 1-based unique ID.    */
    char     name[ACT_MNGR_NAME_MAX_LEN + 1U]; /**< Null-terminated name, max 20 chars.  */
    uint32_t credit_s;                         /**< Seconds to credit; 0 = dynamic.     */
    uint32_t time_limit_s;                     /**< Gamification reference duration.     */
    uint8_t  daily_limit;                      /**< Max credits per day; 1–255.          */
} act_mngr_entry_t;

/**
 * \brief Per-user list of assigned activity IDs.
 *
 * Stored as a blob under NVS key \c ua_N in namespace \c esport_act.
 */
typedef struct act_mngr_user_assigns_tag
{
    uint32_t act_ids[ACT_MNGR_MAX_ASSIGNS_PER_USER]; /**< Activity IDs; #ACT_MNGR_NO_ID = empty. */
    uint8_t  count;                                  /**< Number of valid entries.               */
} act_mngr_user_assigns_t;

/**
 * \brief Per-user daily done counts for every pool slot.
 *
 * Stored as a blob under NVS key \c ud_N in namespace \c esport_act.
 * The \c done array is indexed by pool slot (not activity ID) so array
 * positions remain stable when entries are compacted after a deletion.
 */
typedef struct act_mngr_user_daily_tag
{
    uint32_t date_ymd;                      /**< YYYYMMDD; 0 = uninitialized.               */
    uint8_t  done[ACT_MNGR_MAX_ACTIVITIES]; /**< Done count per activity slot (0-based).    */
} act_mngr_user_daily_t;

/**
 * \brief One entry in a per-user credit log ring buffer.
 *
 * Stored as a blob under NVS key \c ul_N_K in namespace \c esport_act.
 */
typedef struct act_credit_log_entry_tag
{
    int64_t  timestamp_utc;     /**< Unix timestamp when the credit was applied.      */
    uint32_t act_id;            /**< Activity ID that was credited.                   */
    uint32_t credits_s;         /**< Seconds awarded.                                 */
    uint32_t completion_time_s; /**< Optional; 0 when not provided (gamification).    */
} act_credit_log_entry_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the activity manager and restore state from NVS.
 *
 * Opens namespace \c esport_act, reads pool metadata (\c ac_cnt, \c ac_nxt),
 * loads each pool blob, and loads per-user assignment and daily blobs.
 * Must be called after #device_reg_init and before #wifi_mngr_init.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t act_mngr_init(void);

/**
 * \brief Add a new activity to the global pool.
 *
 * \param[in]  p_name       Activity name (1–#ACT_MNGR_NAME_MAX_LEN chars).
 * \param[in]  credit_s     Seconds to credit; 0 = dynamic (API-provided).
 * \param[in]  time_limit_s Gamification reference duration in seconds.
 * \param[in]  daily_limit  Max credits per day (1–255).
 * \param[out] p_id_out     Receives the auto-generated activity ID on success.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p p_name is invalid or \p daily_limit is 0.
 * \return \c ESP_ERR_NO_MEM when the pool is already full.
 */
esp_err_t act_mngr_activity_add(const char * p_name, uint32_t credit_s, uint32_t time_limit_s,
    uint8_t daily_limit, uint32_t * p_id_out);

/**
 * \brief Remove an activity from the global pool by ID.
 *
 * Compacts the pool array and removes the ID from all user assignment lists.
 * The auto-increment counter is never reset; the deleted ID is never reused.
 *
 * \param[in] id  Activity ID to delete.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NOT_FOUND when no activity with \p id exists.
 */
esp_err_t act_mngr_activity_remove(uint32_t id);

/**
 * \brief Update the mutable fields of an existing activity.
 *
 * \param[in] id           Activity ID to update.
 * \param[in] p_name       New name (1–#ACT_MNGR_NAME_MAX_LEN chars).
 * \param[in] credit_s     New credit in seconds.
 * \param[in] time_limit_s New gamification reference in seconds.
 * \param[in] daily_limit  New daily limit (1–255).
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NOT_FOUND when no activity with \p id exists.
 * \return \c ESP_ERR_INVALID_ARG when \p p_name is invalid or \p daily_limit is 0.
 */
esp_err_t act_mngr_activity_update(uint32_t id, const char * p_name, uint32_t credit_s,
    uint32_t time_limit_s, uint8_t daily_limit);

/**
 * \brief Copy the activity entry for the given ID into \p p_out.
 *
 * \param[in]  id     Activity ID to look up.
 * \param[out] p_out  Destination #act_mngr_entry_t.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NOT_FOUND when no activity with \p id exists.
 */
esp_err_t act_mngr_activity_get(uint32_t id, act_mngr_entry_t * p_out);

/**
 * \brief Copy the activity entry at pool slot \p slot into \p p_out.
 *
 * \param[in]  slot   Zero-based pool slot index (< #act_mngr_activity_count()).
 * \param[out] p_out  Destination #act_mngr_entry_t.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p slot is out of range.
 */
esp_err_t act_mngr_activity_slot_get(uint8_t slot, act_mngr_entry_t * p_out);

/** \return Number of activities currently in the global pool (0–30). */
uint8_t act_mngr_activity_count(void);

/**
 * \brief Assign an activity to a user (device slot).
 *
 * \param[in] dev_idx  Device registry index (0–#DEVICE_REG_MAX_ENTRIES-1).
 * \param[in] act_id   Activity ID to assign.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NOT_FOUND when \p act_id does not exist in the pool.
 * \return \c ESP_ERR_INVALID_STATE when the activity is already assigned.
 * \return \c ESP_ERR_NO_MEM when the user already has #ACT_MNGR_MAX_ASSIGNS_PER_USER assignments.
 */
esp_err_t act_mngr_user_assign(uint8_t dev_idx, uint32_t act_id);

/**
 * \brief Remove a user-activity assignment.
 *
 * \param[in] dev_idx  Device registry index.
 * \param[in] act_id   Activity ID to unassign.
 *
 * \return \c ESP_OK on success (or when the activity was not assigned — idempotent).
 */
esp_err_t act_mngr_user_unassign(uint8_t dev_idx, uint32_t act_id);

/**
 * \brief Return the number of activities assigned to a user.
 *
 * \param[in] dev_idx  Device registry index.
 *
 * \return Assignment count (0–#ACT_MNGR_MAX_ASSIGNS_PER_USER).
 */
uint8_t act_mngr_user_assign_count(uint8_t dev_idx);

/**
 * \brief Copy the user's assignment record into \p p_out.
 *
 * \param[in]  dev_idx  Device registry index.
 * \param[out] p_out    Destination #act_mngr_user_assigns_t.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p dev_idx is out of range or \p p_out is NULL.
 */
esp_err_t act_mngr_user_assigns_get(uint8_t dev_idx, act_mngr_user_assigns_t * p_out);

/**
 * \brief Return whether activity \p act_id is assigned to user \p dev_idx.
 *
 * \param[in] dev_idx  Device registry index.
 * \param[in] act_id   Activity ID to test.
 *
 * \return \c true if the activity is assigned.
 */
bool act_mngr_user_is_assigned(uint8_t dev_idx, uint32_t act_id);

/**
 * \brief Return how many times today the user has credited activity \p act_id.
 *
 * Applies the daily-date reset lazily before reading the count.
 *
 * \param[in] dev_idx  Device registry index.
 * \param[in] act_id   Activity ID.
 *
 * \return Done count for today (0 if the daily reset ran, or activity not found).
 */
uint8_t act_mngr_user_daily_done_get(uint8_t dev_idx, uint32_t act_id);

/**
 * \brief Return whether the user has reached the daily limit for an activity.
 *
 * \param[in] dev_idx  Device registry index.
 * \param[in] act_id   Activity ID.
 *
 * \return \c true when done count >= daily limit.
 */
bool act_mngr_user_daily_limit_reached(uint8_t dev_idx, uint32_t act_id);

/**
 * \brief Lazily reset all users' daily done counts when the calendar day has changed.
 *
 * Compares each user's stored \c date_ymd with today's YYYYMMDD; if they
 * differ, zeroes the done array, updates the date, and saves to NVS.
 * Intended to be called approximately once per minute from the time-counter
 * tick.
 */
void act_mngr_daily_reset_check(void);

/**
 * \brief Credit an activity to a user — the single write path.
 *
 * Validates the request, applies the daily-reset check, verifies the daily
 * limit has not been reached, adds \p credits_s to the target device's
 * counter via #device_reg_entry_counter_set, increments the daily done
 * count, appends a credit-log entry, optionally plays the activity-credit
 * buzzer, and posts #ESPORT_EVENT_ACTIVITY_CREDITED.
 *
 * \param[in] dev_idx            Device registry index (0–3).
 * \param[in] act_id             Activity ID (must exist and be assigned to \p dev_idx).
 * \param[in] credits_s          Seconds to award (must be > 0).
 * \param[in] completion_time_s  Optional analytics field; 0 when unused.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p dev_idx is out of range or \p credits_s is 0.
 * \return \c ESP_ERR_NOT_FOUND when \p act_id does not exist in the pool.
 * \return \c ESP_ERR_INVALID_STATE when \p act_id is not assigned to \p dev_idx.
 * \return \c ESP_ERR_NOT_ALLOWED when the daily limit is already exhausted.
 */
esp_err_t act_mngr_activity_credit(uint8_t dev_idx, uint32_t act_id, uint32_t credits_s,
    uint32_t completion_time_s);

/**
 * \brief Return the number of credit-log entries for a user.
 *
 * \param[in] dev_idx  Device registry index.
 *
 * \return Entry count (0–#ACT_MNGR_MAX_CREDIT_LOG).
 */
uint8_t act_mngr_credit_log_count(uint8_t dev_idx);

/**
 * \brief Read up to \p max_count credit-log entries for a user, newest first.
 *
 * \param[in]  dev_idx    Device registry index.
 * \param[out] p_out      Destination array of #act_credit_log_entry_t.
 * \param[in]  max_count  Maximum number of entries to copy.
 *
 * \return Actual number of entries written into \p p_out.
 */
uint8_t act_mngr_credit_log_read(uint8_t dev_idx, act_credit_log_entry_t * p_out,
    uint8_t max_count);

#ifdef __cplusplus
}
#endif

#endif // ACTIVITY_MANAGER_H

/*** end of file ***/
