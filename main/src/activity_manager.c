/**
 * \file
 * \brief Activity Manager - global pool, per-user assignments, daily tracking,
 *        credit log, and credit-apply logic.
 *
 * \date 2026-04-17
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "activity_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include "buzzer.h"
#include "config_manager.h"
#include "device_registry.h"
#include "dyn_act_registry.h"
#include "event_ids.h"
#include "time_manager.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** NVS namespace for all activity-manager data. */
#define ACT_MNGR_NAMESPACE ("esport_act")

/** NVS key for pool entry count. */
#define ACT_MNGR_KEY_POOL_CNT ("ac_cnt")

/** NVS key for the auto-increment next ID. */
#define ACT_MNGR_KEY_NEXT_ID ("ac_nxt")

/** Buffer size for a pool-blob NVS key (e.g. "ac_29\0" = 6 bytes; 12 is safe). */
#define ACT_MNGR_KEY_BUF_LEN (12U)

/** Buffer size for user-daily and user-assign NVS keys (e.g. "ul_3_29\0"). */
#define ACT_MNGR_KEY_LOG_BUF_LEN (16U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "act_mngr";

/** Spinlock protecting all in-RAM state. */
static portMUX_TYPE g_act_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- Pool ---- */
static act_mngr_entry_t g_pool[ACT_MNGR_MAX_ACTIVITIES];
static uint8_t          g_pool_count = 0U;
static uint32_t         g_next_id    = 1U;

/* ---- Per-user assignments and daily counts ---- */
static act_mngr_user_assigns_t g_assigns[DEVICE_REG_MAX_ENTRIES];
static act_mngr_user_daily_t   g_daily[DEVICE_REG_MAX_ENTRIES];

/* ---- Per-user credit log ring buffers ---- */
static uint8_t g_log_head[DEVICE_REG_MAX_ENTRIES];
static uint8_t g_log_count[DEVICE_REG_MAX_ENTRIES];

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static int8_t    act_mngr_pool_slot_find(uint32_t id);
static esp_err_t act_mngr_pool_save(uint8_t slot);
static esp_err_t act_mngr_pool_meta_save(void);
static esp_err_t act_mngr_assigns_save(uint8_t dev_idx);
static esp_err_t act_mngr_daily_save(uint8_t dev_idx);
static esp_err_t act_mngr_log_save(uint8_t dev_idx, uint8_t slot,
    const act_credit_log_entry_t * p_entry);
static uint32_t  act_mngr_date_ymd_get(void);
static void      act_mngr_daily_reset_if_needed(uint8_t dev_idx);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t act_mngr_init(void)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ---- pool metadata ---- */
    uint8_t cnt = 0U;
    (void)nvs_get_u8(handle, ACT_MNGR_KEY_POOL_CNT, &cnt);
    if (cnt > (uint8_t)ACT_MNGR_MAX_ACTIVITIES)
    {
        cnt = 0U;
    }

    uint32_t nxt = 1U;
    (void)nvs_get_u32(handle, ACT_MNGR_KEY_NEXT_ID, &nxt);
    if (0U == nxt)
    {
        nxt = 1U;
    }

    /* ---- pool blobs ---- */
    for (uint8_t s = 0U; s < cnt; s++)
    {
        char key[ACT_MNGR_KEY_BUF_LEN];
        (void)snprintf(key, sizeof(key), "ac_%u", (unsigned int)s);

        /* Backward compatibility: query the stored blob size first.
         * Old blobs (pre-Feature-8) may be smaller than sizeof(act_mngr_entry_t).
         * Zero-init the struct so missing fields default to 0 (b_is_dynamic = 0). */
        size_t loaded_size = 0U;
        (void)nvs_get_blob(handle, key, NULL, &loaded_size);
        if (0U == loaded_size)
        {
            /* Slot unreadable – truncate pool to last good slot. */
            cnt = s;
            break;
        }
        (void)memset(&g_pool[s], 0, sizeof(act_mngr_entry_t));
        size_t read_size =
            (loaded_size < sizeof(act_mngr_entry_t)) ? loaded_size : sizeof(act_mngr_entry_t);
        if (ESP_OK != nvs_get_blob(handle, key, &g_pool[s], &read_size))
        {
            /* Slot unreadable – truncate pool to last good slot. */
            cnt = s;
            break;
        }
    }

    /* ---- per-user data ---- */
    for (uint8_t d = 0U; d < (uint8_t)DEVICE_REG_MAX_ENTRIES; d++)
    {
        /* assignments */
        char akey[ACT_MNGR_KEY_BUF_LEN];
        (void)snprintf(akey, sizeof(akey), "ua_%u", (unsigned int)d);
        size_t alen = sizeof(act_mngr_user_assigns_t);
        if (ESP_OK != nvs_get_blob(handle, akey, &g_assigns[d], &alen))
        {
            (void)memset(&g_assigns[d], 0, sizeof(g_assigns[d]));
        }

        /* daily */
        char dkey[ACT_MNGR_KEY_BUF_LEN];
        (void)snprintf(dkey, sizeof(dkey), "ud_%u", (unsigned int)d);
        size_t dlen = sizeof(act_mngr_user_daily_t);
        if (ESP_OK != nvs_get_blob(handle, dkey, &g_daily[d], &dlen))
        {
            (void)memset(&g_daily[d], 0, sizeof(g_daily[d]));
        }

        /* credit log indices */
        char lhkey[ACT_MNGR_KEY_LOG_BUF_LEN];
        char lckey[ACT_MNGR_KEY_LOG_BUF_LEN];
        (void)snprintf(lhkey, sizeof(lhkey), "ul_%u_hd", (unsigned int)d);
        (void)snprintf(lckey, sizeof(lckey), "ul_%u_cnt", (unsigned int)d);
        (void)nvs_get_u8(handle, lhkey, &g_log_head[d]);
        (void)nvs_get_u8(handle, lckey, &g_log_count[d]);
        if (g_log_head[d] >= (uint8_t)ACT_MNGR_MAX_CREDIT_LOG)
        {
            g_log_head[d] = 0U;
        }
        if (g_log_count[d] > (uint8_t)ACT_MNGR_MAX_CREDIT_LOG)
        {
            g_log_count[d] = 0U;
        }
    }

    nvs_close(handle);

    /* Auto-heal: any entry whose name matches a compiled-in dynamic activity but
     * has b_is_dynamic=0 (old NVS blob or clobbered by a previous Update) is
     * corrected in RAM and immediately re-saved so the fix survives the next boot. */
    for (uint8_t s = 0U; s < cnt; s++)
    {
        if (0U != g_pool[s].b_is_dynamic)
        {
            continue;
        }
        for (uint8_t di = 0U; di < g_dyn_act_count; di++)
        {
            if (0 == strcmp(g_pool[s].name, g_dyn_act_registry[di].p_name))
            {
                g_pool[s].b_is_dynamic = 1U;
                (void)act_mngr_pool_save(s);
                ESP_LOGI(gp_tag, "auto-healed b_is_dynamic for activity id=%lu name='%s'",
                    (unsigned long)g_pool[s].id, g_pool[s].name);
                break;
            }
        }
    }

    g_pool_count = cnt;
    g_next_id    = nxt;

    ESP_LOGI(gp_tag, "init complete - pool_count=%u next_id=%lu", (unsigned int)g_pool_count,
        (unsigned long)g_next_id);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_add(const char * p_name, uint32_t credit_s, uint32_t time_limit_s,
    uint8_t daily_limit, uint8_t b_is_dynamic, uint32_t * p_id_out)
{
    if ((NULL == p_name) || (0U == strlen(p_name)) || (strlen(p_name) > ACT_MNGR_NAME_MAX_LEN) ||
        (0U == daily_limit))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (0U == credit_s)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    bool b_full = (g_pool_count >= (uint8_t)ACT_MNGR_MAX_ACTIVITIES);
    portEXIT_CRITICAL(&g_act_mux);

    if (b_full)
    {
        return ESP_ERR_NO_MEM;
    }

    uint8_t  slot;
    uint32_t new_id;

    portENTER_CRITICAL(&g_act_mux);
    slot            = g_pool_count;
    new_id          = g_next_id++;
    g_pool[slot].id = new_id;
    (void)strncpy(g_pool[slot].name, p_name, ACT_MNGR_NAME_MAX_LEN);
    g_pool[slot].name[ACT_MNGR_NAME_MAX_LEN] = '\0';
    g_pool[slot].credit_s                    = credit_s;
    g_pool[slot].time_limit_s                = time_limit_s;
    g_pool[slot].daily_limit                 = daily_limit;
    g_pool[slot].b_is_dynamic                = b_is_dynamic;
    g_pool_count++;
    portEXIT_CRITICAL(&g_act_mux);

    (void)act_mngr_pool_save(slot);
    (void)act_mngr_pool_meta_save();

    if (NULL != p_id_out)
    {
        *p_id_out = new_id;
    }

    ESP_LOGI(gp_tag, "added activity id=%lu slot=%u name='%s' is_dynamic=%u", (unsigned long)new_id,
        (unsigned int)slot, p_name, (unsigned int)b_is_dynamic);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_remove(uint32_t id)
{
    portENTER_CRITICAL(&g_act_mux);
    int8_t slot = act_mngr_pool_slot_find(id);
    if (-1 == slot)
    {
        portEXIT_CRITICAL(&g_act_mux);
        return ESP_ERR_NOT_FOUND;
    }

    /* Compact pool: shift entries left. */
    uint8_t old_count = g_pool_count;
    for (uint8_t s = (uint8_t)slot; s < (old_count - 1U); s++)
    {
        g_pool[s] = g_pool[s + 1U];
    }
    g_pool_count--;

    /* Compact user assignments. */
    for (uint8_t d = 0U; d < (uint8_t)DEVICE_REG_MAX_ENTRIES; d++)
    {
        uint8_t wcnt = 0U;
        for (uint8_t i = 0U; i < g_assigns[d].count; i++)
        {
            if (g_assigns[d].act_ids[i] != id)
            {
                g_assigns[d].act_ids[wcnt++] = g_assigns[d].act_ids[i];
            }
        }
        g_assigns[d].count = wcnt;
    }
    portEXIT_CRITICAL(&g_act_mux);

    /* Rewrite all pool blobs and delete the now-stale last slot. */
    nvs_handle_t handle;
    if (ESP_OK == nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle))
    {
        for (uint8_t s = 0U; s < g_pool_count; s++)
        {
            char key[ACT_MNGR_KEY_BUF_LEN];
            (void)snprintf(key, sizeof(key), "ac_%u", (unsigned int)s);
            (void)nvs_set_blob(handle, key, &g_pool[s], sizeof(act_mngr_entry_t));
        }
        /* Erase the slot that no longer exists. */
        char stale_key[ACT_MNGR_KEY_BUF_LEN];
        (void)snprintf(stale_key, sizeof(stale_key), "ac_%u", (unsigned int)old_count - 1U);
        (void)nvs_erase_key(handle, stale_key);
        (void)nvs_set_u8(handle, ACT_MNGR_KEY_POOL_CNT, g_pool_count);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }

    /* Rewrite all user assignment blobs. */
    for (uint8_t d = 0U; d < (uint8_t)DEVICE_REG_MAX_ENTRIES; d++)
    {
        (void)act_mngr_assigns_save(d);
    }

    ESP_LOGI(gp_tag, "removed activity id=%lu", (unsigned long)id);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_update(uint32_t id, const char * p_name, uint32_t credit_s,
    uint32_t time_limit_s, uint8_t daily_limit, uint8_t b_is_dynamic)
{
    if ((NULL == p_name) || (0U == strlen(p_name)) || (strlen(p_name) > ACT_MNGR_NAME_MAX_LEN) ||
        (0U == daily_limit))
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    int8_t slot = act_mngr_pool_slot_find(id);
    if (-1 == slot)
    {
        portEXIT_CRITICAL(&g_act_mux);
        return ESP_ERR_NOT_FOUND;
    }
    (void)strncpy(g_pool[slot].name, p_name, ACT_MNGR_NAME_MAX_LEN);
    g_pool[slot].name[ACT_MNGR_NAME_MAX_LEN] = '\0';
    g_pool[slot].credit_s                    = credit_s;
    g_pool[slot].time_limit_s                = time_limit_s;
    g_pool[slot].daily_limit                 = daily_limit;
    g_pool[slot].b_is_dynamic                = b_is_dynamic;
    portEXIT_CRITICAL(&g_act_mux);

    (void)act_mngr_pool_save((uint8_t)slot);

    ESP_LOGI(gp_tag, "updated activity id=%lu is_dynamic=%u", (unsigned long)id,
        (unsigned int)b_is_dynamic);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_get(uint32_t id, act_mngr_entry_t * p_out)
{
    if (NULL == p_out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    int8_t slot = act_mngr_pool_slot_find(id);
    if (-1 == slot)
    {
        portEXIT_CRITICAL(&g_act_mux);
        return ESP_ERR_NOT_FOUND;
    }
    *p_out = g_pool[slot];
    portEXIT_CRITICAL(&g_act_mux);

    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_slot_get(uint8_t slot, act_mngr_entry_t * p_out)
{
    if (NULL == p_out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    bool b_valid = (slot < g_pool_count);
    if (b_valid)
    {
        *p_out = g_pool[slot];
    }
    portEXIT_CRITICAL(&g_act_mux);

    return b_valid ? ESP_OK : ESP_ERR_INVALID_ARG;
}

//--------------------------------------------------------------------------------------------------

uint8_t act_mngr_activity_count(void)
{
    portENTER_CRITICAL(&g_act_mux);
    uint8_t cnt = g_pool_count;
    portEXIT_CRITICAL(&g_act_mux);
    return cnt;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_user_assign(uint8_t dev_idx, uint32_t act_id)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    int8_t pool_slot = act_mngr_pool_slot_find(act_id);
    if (-1 == pool_slot)
    {
        portEXIT_CRITICAL(&g_act_mux);
        return ESP_ERR_NOT_FOUND;
    }

    /* Check for duplicate. */
    for (uint8_t i = 0U; i < g_assigns[dev_idx].count; i++)
    {
        if (g_assigns[dev_idx].act_ids[i] == act_id)
        {
            portEXIT_CRITICAL(&g_act_mux);
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (g_assigns[dev_idx].count >= (uint8_t)ACT_MNGR_MAX_ASSIGNS_PER_USER)
    {
        portEXIT_CRITICAL(&g_act_mux);
        return ESP_ERR_NO_MEM;
    }

    g_assigns[dev_idx].act_ids[g_assigns[dev_idx].count] = act_id;
    g_assigns[dev_idx].count++;
    portEXIT_CRITICAL(&g_act_mux);

    (void)act_mngr_assigns_save(dev_idx);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_user_unassign(uint8_t dev_idx, uint32_t act_id)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    uint8_t wcnt = 0U;
    for (uint8_t i = 0U; i < g_assigns[dev_idx].count; i++)
    {
        if (g_assigns[dev_idx].act_ids[i] != act_id)
        {
            g_assigns[dev_idx].act_ids[wcnt++] = g_assigns[dev_idx].act_ids[i];
        }
    }
    g_assigns[dev_idx].count = wcnt;
    portEXIT_CRITICAL(&g_act_mux);

    (void)act_mngr_assigns_save(dev_idx);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint8_t act_mngr_user_assign_count(uint8_t dev_idx)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return 0U;
    }

    portENTER_CRITICAL(&g_act_mux);
    uint8_t cnt = g_assigns[dev_idx].count;
    portEXIT_CRITICAL(&g_act_mux);
    return cnt;
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_user_assigns_get(uint8_t dev_idx, act_mngr_user_assigns_t * p_out)
{
    if ((dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES) || (NULL == p_out))
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_act_mux);
    *p_out = g_assigns[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

bool act_mngr_user_is_assigned(uint8_t dev_idx, uint32_t act_id)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return false;
    }

    portENTER_CRITICAL(&g_act_mux);
    bool b_found = false;
    for (uint8_t i = 0U; i < g_assigns[dev_idx].count; i++)
    {
        if (g_assigns[dev_idx].act_ids[i] == act_id)
        {
            b_found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&g_act_mux);
    return b_found;
}

//--------------------------------------------------------------------------------------------------

uint8_t act_mngr_user_daily_done_get(uint8_t dev_idx, uint32_t act_id)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return 0U;
    }

    act_mngr_daily_reset_if_needed(dev_idx);

    portENTER_CRITICAL(&g_act_mux);
    int8_t  slot = act_mngr_pool_slot_find(act_id);
    uint8_t cnt  = 0U;
    if (-1 != slot)
    {
        cnt = g_daily[dev_idx].done[(uint8_t)slot];
    }
    portEXIT_CRITICAL(&g_act_mux);
    return cnt;
}

//--------------------------------------------------------------------------------------------------

bool act_mngr_user_daily_limit_reached(uint8_t dev_idx, uint32_t act_id)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return true;
    }

    act_mngr_daily_reset_if_needed(dev_idx);

    portENTER_CRITICAL(&g_act_mux);
    int8_t slot      = act_mngr_pool_slot_find(act_id);
    bool   b_reached = true;
    if (-1 != slot)
    {
        b_reached = (g_daily[dev_idx].done[(uint8_t)slot] >= g_pool[(uint8_t)slot].daily_limit);
    }
    portEXIT_CRITICAL(&g_act_mux);
    return b_reached;
}

//--------------------------------------------------------------------------------------------------

void act_mngr_daily_reset_check(void)
{
    for (uint8_t d = 0U; d < (uint8_t)DEVICE_REG_MAX_ENTRIES; d++)
    {
        act_mngr_daily_reset_if_needed(d);
    }
}

//--------------------------------------------------------------------------------------------------

esp_err_t act_mngr_activity_credit(uint8_t dev_idx, uint32_t act_id, uint32_t credits_s,
    uint32_t completion_time_s)
{
    if ((dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES) || (0U == credits_s) ||
        (ACT_MNGR_NO_ID == act_id))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Verify activity exists. */
    portENTER_CRITICAL(&g_act_mux);
    int8_t slot = act_mngr_pool_slot_find(act_id);
    portEXIT_CRITICAL(&g_act_mux);

    if (-1 == slot)
    {
        return ESP_ERR_NOT_FOUND;
    }

    /* Verify assignment. */
    if (!act_mngr_user_is_assigned(dev_idx, act_id))
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Lazy daily reset. */
    act_mngr_daily_reset_if_needed(dev_idx);

    /* Check daily limit. */
    portENTER_CRITICAL(&g_act_mux);
    uint8_t done        = g_daily[dev_idx].done[(uint8_t)slot];
    uint8_t daily_limit = g_pool[(uint8_t)slot].daily_limit;
    bool    b_at_limit  = (done >= daily_limit);
    if (!b_at_limit)
    {
        g_daily[dev_idx].done[(uint8_t)slot]++;
    }
    portEXIT_CRITICAL(&g_act_mux);

    if (b_at_limit)
    {
        return ESP_ERR_NOT_ALLOWED;
    }

    /* Save daily state. */
    (void)act_mngr_daily_save(dev_idx);

    /* Credit the device counter. */
    uint32_t old_ctr = device_reg_entry_counter_get(dev_idx);
    (void)device_reg_entry_counter_set(dev_idx, old_ctr + credits_s);

    /* Append credit log entry. */
    act_credit_log_entry_t entry;
    entry.timestamp_utc     = (int64_t)time_mngr_utc_get();
    entry.act_id            = act_id;
    entry.credits_s         = credits_s;
    entry.completion_time_s = completion_time_s;

    portENTER_CRITICAL(&g_act_mux);
    uint8_t log_slot = g_log_head[dev_idx];
    g_log_head[dev_idx] =
        (uint8_t)(((uint16_t)g_log_head[dev_idx] + 1U) % (uint16_t)ACT_MNGR_MAX_CREDIT_LOG);
    if (g_log_count[dev_idx] < (uint8_t)ACT_MNGR_MAX_CREDIT_LOG)
    {
        g_log_count[dev_idx]++;
    }
    uint8_t new_head  = g_log_head[dev_idx];
    uint8_t new_count = g_log_count[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);

    (void)act_mngr_log_save(dev_idx, log_slot, &entry);

    /* Persist updated log indices. */
    nvs_handle_t handle;
    if (ESP_OK == nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle))
    {
        char lhkey[ACT_MNGR_KEY_LOG_BUF_LEN];
        char lckey[ACT_MNGR_KEY_LOG_BUF_LEN];
        (void)snprintf(lhkey, sizeof(lhkey), "ul_%u_hd", (unsigned int)dev_idx);
        (void)snprintf(lckey, sizeof(lckey), "ul_%u_cnt", (unsigned int)dev_idx);
        (void)nvs_set_u8(handle, lhkey, new_head);
        (void)nvs_set_u8(handle, lckey, new_count);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }

    /* Buzzer feedback. */
    if (config_mngr_activity_credit_buzzer_en_get())
    {
        buzzer_pattern_play(BUZZER_PATTERN_ACTIVITY_CREDIT);
    }

    /* Post event (no payload). */
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_ACTIVITY_CREDITED, NULL, 0U, 0U);

    ESP_LOGI(gp_tag, "credited dev=%u act_id=%lu credits=%lu", (unsigned int)dev_idx,
        (unsigned long)act_id, (unsigned long)credits_s);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint8_t act_mngr_credit_log_count(uint8_t dev_idx)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return 0U;
    }

    portENTER_CRITICAL(&g_act_mux);
    uint8_t cnt = g_log_count[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);
    return cnt;
}

//--------------------------------------------------------------------------------------------------

uint8_t act_mngr_credit_log_read(uint8_t dev_idx, act_credit_log_entry_t * p_out, uint8_t max_count)
{
    if ((dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES) || (NULL == p_out) || (0U == max_count))
    {
        return 0U;
    }

    portENTER_CRITICAL(&g_act_mux);
    uint8_t total = g_log_count[dev_idx];
    uint8_t head  = g_log_head[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);

    uint8_t actual = (total < max_count) ? total : max_count;
    if (0U == actual)
    {
        return 0U;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_OK != ret)
    {
        return 0U;
    }

    uint8_t copied = 0U;
    for (uint8_t i = 0U; i < actual; i++)
    {
        /* Walk backward from (head - 1). */
        uint8_t slot =
            (uint8_t)(((uint16_t)head + (uint16_t)ACT_MNGR_MAX_CREDIT_LOG - 1U - (uint16_t)i) %
                      (uint16_t)ACT_MNGR_MAX_CREDIT_LOG);
        char key[ACT_MNGR_KEY_LOG_BUF_LEN];
        (void)snprintf(key, sizeof(key), "ul_%u_%u", (unsigned int)dev_idx, (unsigned int)slot);
        size_t len = sizeof(act_credit_log_entry_t);
        if (ESP_OK == nvs_get_blob(handle, key, &p_out[copied], &len))
        {
            copied++;
        }
    }

    nvs_close(handle);
    return copied;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Find the pool slot for activity ID \p id.
 *
 * Must be called under the spinlock.
 *
 * \param[in] id  Activity ID to search for.
 *
 * \return Zero-based slot index, or -1 when not found.
 */
static int8_t act_mngr_pool_slot_find(uint32_t id)
{
    for (uint8_t s = 0U; s < g_pool_count; s++)
    {
        if (g_pool[s].id == id)
        {
            return (int8_t)s;
        }
    }
    return -1;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save pool slot \p slot to NVS.
 *
 * \param[in] slot  Zero-based pool slot index.
 *
 * \return \c ESP_OK on success.
 */
static esp_err_t act_mngr_pool_save(uint8_t slot)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        return ret;
    }

    char key[ACT_MNGR_KEY_BUF_LEN];
    (void)snprintf(key, sizeof(key), "ac_%u", (unsigned int)slot);
    ret = nvs_set_blob(handle, key, &g_pool[slot], sizeof(act_mngr_entry_t));
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save pool metadata (count and next ID) to NVS.
 *
 * \return \c ESP_OK on success.
 */
static esp_err_t act_mngr_pool_meta_save(void)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        return ret;
    }

    portENTER_CRITICAL(&g_act_mux);
    uint8_t  cnt = g_pool_count;
    uint32_t nxt = g_next_id;
    portEXIT_CRITICAL(&g_act_mux);

    ret = nvs_set_u8(handle, ACT_MNGR_KEY_POOL_CNT, cnt);
    if (ESP_OK == ret)
    {
        ret = nvs_set_u32(handle, ACT_MNGR_KEY_NEXT_ID, nxt);
    }
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save user \p dev_idx assignment blob to NVS.
 *
 * \param[in] dev_idx  Device registry index.
 *
 * \return \c ESP_OK on success.
 */
static esp_err_t act_mngr_assigns_save(uint8_t dev_idx)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        return ret;
    }

    char key[ACT_MNGR_KEY_BUF_LEN];
    (void)snprintf(key, sizeof(key), "ua_%u", (unsigned int)dev_idx);

    portENTER_CRITICAL(&g_act_mux);
    act_mngr_user_assigns_t assigns_copy = g_assigns[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);

    ret = nvs_set_blob(handle, key, &assigns_copy, sizeof(act_mngr_user_assigns_t));
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save user \p dev_idx daily blob to NVS.
 *
 * \param[in] dev_idx  Device registry index.
 *
 * \return \c ESP_OK on success.
 */
static esp_err_t act_mngr_daily_save(uint8_t dev_idx)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        return ret;
    }

    char key[ACT_MNGR_KEY_BUF_LEN];
    (void)snprintf(key, sizeof(key), "ud_%u", (unsigned int)dev_idx);

    portENTER_CRITICAL(&g_act_mux);
    act_mngr_user_daily_t daily_copy = g_daily[dev_idx];
    portEXIT_CRITICAL(&g_act_mux);

    ret = nvs_set_blob(handle, key, &daily_copy, sizeof(act_mngr_user_daily_t));
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save one credit-log entry blob to NVS.
 *
 * \param[in] dev_idx  Device registry index.
 * \param[in] slot     Ring-buffer slot index (0–#ACT_MNGR_MAX_CREDIT_LOG-1).
 * \param[in] p_entry  Entry to save.
 *
 * \return \c ESP_OK on success.
 */
static esp_err_t act_mngr_log_save(uint8_t dev_idx, uint8_t slot,
    const act_credit_log_entry_t * p_entry)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(ACT_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        return ret;
    }

    char key[ACT_MNGR_KEY_LOG_BUF_LEN];
    (void)snprintf(key, sizeof(key), "ul_%u_%u", (unsigned int)dev_idx, (unsigned int)slot);

    ret = nvs_set_blob(handle, key, p_entry, sizeof(act_credit_log_entry_t));
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Return the current local date as YYYYMMDD.
 *
 * \return Date as a 32-bit unsigned integer (e.g. 20260417).
 */
static uint32_t act_mngr_date_ymd_get(void)
{
    time_t    now_utc = (time_t)time_mngr_utc_get();
    struct tm local_tm;
    localtime_r(&now_utc, &local_tm);
    uint32_t year  = (uint32_t)(local_tm.tm_year + 1900);
    uint32_t month = (uint32_t)(local_tm.tm_mon + 1);
    uint32_t day   = (uint32_t)local_tm.tm_mday;
    return year * 10000U + month * 100U + day;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Reset daily done counts for \p dev_idx when the calendar day has changed.
 *
 * Compares the stored \c date_ymd with today; if they differ, zeroes all done
 * counts, updates the date, and saves to NVS.  Called outside the spinlock.
 *
 * \param[in] dev_idx  Device registry index.
 */
static void act_mngr_daily_reset_if_needed(uint8_t dev_idx)
{
    if (dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES)
    {
        return;
    }

    uint32_t today_ymd = act_mngr_date_ymd_get();

    portENTER_CRITICAL(&g_act_mux);
    bool b_reset = (g_daily[dev_idx].date_ymd != today_ymd);
    if (b_reset)
    {
        (void)memset(g_daily[dev_idx].done, 0, sizeof(g_daily[dev_idx].done));
        g_daily[dev_idx].date_ymd = today_ymd;
    }
    portEXIT_CRITICAL(&g_act_mux);

    if (b_reset)
    {
        (void)act_mngr_daily_save(dev_idx);
        ESP_LOGI(gp_tag, "daily reset for dev=%u (new day %lu)", (unsigned int)dev_idx,
            (unsigned long)today_ymd);
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
