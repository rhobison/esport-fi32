/**
 * \file
 * \brief Device registry — per-device internet access control and NVS persistence.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "device_registry.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "config_manager.h"
#include "event_ids.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** NVS namespace used by this module. */
#define DEVICE_REG_NVS_NS ("esport_dev")

/** NVS key for the device count (uint8). */
#define DEVICE_REG_NVS_KEY_COUNT ("dev_count")

/** NVS key prefix for device blobs (uint8 index appended). */
#define DEVICE_REG_NVS_KEY_DEV_FMT ("dev_%u")

/** NVS key for the current rider index (uint8). */
#define DEVICE_REG_NVS_KEY_RIDER ("dev_rider")

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "device_reg";

/** Array of registered device entries. */
static device_reg_entry_t g_entries[DEVICE_REG_MAX_ENTRIES];

/** Number of valid entries in #g_entries. */
static uint8_t g_count = 0U;

/** Index of the currently selected rider (or #DEVICE_REG_NO_RIDER). */
static uint8_t g_rider = DEVICE_REG_NO_RIDER;

/** Spinlock protecting all in-RAM device state. */
static portMUX_TYPE g_dev_mux = portMUX_INITIALIZER_UNLOCKED;

/** Tick counter used to trigger periodic NVS saves. */
static uint16_t g_tick_count = 0U;

/* ---------- Per-device traffic state (RAM only) ---------- */

/** Cumulative RX bytes per device since last tick (indexed by slot). */
static volatile uint32_t g_rx_bytes[DEVICE_REG_MAX_ENTRIES];

/** Cumulative TX bytes per device since last tick (indexed by slot). */
static volatile uint32_t g_tx_bytes[DEVICE_REG_MAX_ENTRIES];

/** Last computed RX+TX kbps per device (updated in tick). */
static uint32_t g_throughput_kbps[DEVICE_REG_MAX_ENTRIES];

/** Consecutive below-threshold ticks per device. */
static uint16_t g_below_ticks[DEVICE_REG_MAX_ENTRIES];

/** Current traffic-gate pause state per device. */
static bool g_dev_paused[DEVICE_REG_MAX_ENTRIES];

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t device_reg_entry_save(uint8_t idx);
static esp_err_t device_reg_meta_save(void);
static void      device_reg_counters_save_all(void);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t device_reg_init(void)
{
    /* Zero all per-device RAM traffic state. */
    memset((void *)g_rx_bytes, 0, sizeof(g_rx_bytes));
    memset((void *)g_tx_bytes, 0, sizeof(g_tx_bytes));
    memset(g_throughput_kbps, 0, sizeof(g_throughput_kbps));
    memset(g_below_ticks, 0, sizeof(g_below_ticks));
    memset(g_dev_paused, 0, sizeof(g_dev_paused));
    memset(g_entries, 0, sizeof(g_entries));
    g_count      = 0U;
    g_rider      = DEVICE_REG_NO_RIDER;
    g_tick_count = 0U;

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Read device count. */
    uint8_t count = 0U;
    ret           = nvs_get_u8(handle, DEVICE_REG_NVS_KEY_COUNT, &count);
    if (ESP_ERR_NVS_NOT_FOUND == ret)
    {
        /* First boot — write 0 and continue. */
        (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_COUNT, 0U);
        (void)nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(gp_tag, "init: first boot, no devices");
        return ESP_OK;
    }
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_get_u8(dev_count) failed: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    /* Validate count. */
    if (count > DEVICE_REG_MAX_ENTRIES)
    {
        ESP_LOGW(gp_tag, "NVS count %u out of range, resetting", (unsigned)count);
        count = 0U;
        (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_COUNT, 0U);
        (void)nvs_commit(handle);
        nvs_close(handle);
        return ESP_OK;
    }

    /* Load each device blob. */
    for (uint8_t i = 0U; i < count; i++)
    {
        char key[12];
        snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_DEV_FMT, (unsigned)i);

        size_t    blob_size = sizeof(device_reg_entry_t);
        esp_err_t blob_ret  = nvs_get_blob(handle, key, &g_entries[i], &blob_size);
        if (ESP_OK != blob_ret)
        {
            ESP_LOGW(gp_tag, "nvs_get_blob(%s) failed: %s — zeroing slot", key,
                esp_err_to_name(blob_ret));
            memset(&g_entries[i], 0, sizeof(g_entries[i]));
        }
    }
    g_count = count;

    /* Read current rider. */
    uint8_t rider = DEVICE_REG_NO_RIDER;
    ret           = nvs_get_u8(handle, DEVICE_REG_NVS_KEY_RIDER, &rider);
    if ((ESP_OK == ret) && ((rider < g_count) || (DEVICE_REG_NO_RIDER == rider)))
    {
        g_rider = rider;
    }
    else if (ESP_OK != ret && ESP_ERR_NVS_NOT_FOUND != ret)
    {
        ESP_LOGW(gp_tag, "nvs_get_u8(dev_rider) failed: %s", esp_err_to_name(ret));
    }
    else
    {
        /* Invalid rider index — reset. */
        g_rider = DEVICE_REG_NO_RIDER;
    }

    nvs_close(handle);
    ESP_LOGI(gp_tag, "init: loaded %u device(s), rider=%u", (unsigned)g_count, (unsigned)g_rider);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint8_t device_reg_count_get(void)
{
    portENTER_CRITICAL(&g_dev_mux);
    uint8_t count = g_count;
    portEXIT_CRITICAL(&g_dev_mux);
    return count;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_add(const uint8_t * p_mac, const char * p_nickname)
{
    if ((NULL == p_mac) || (NULL == p_nickname) || ('\0' == p_nickname[0]))
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t nick_len = strlen(p_nickname);
    if (nick_len > DEVICE_REG_NICKNAME_MAX_LEN)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_dev_mux);

    if (DEVICE_REG_MAX_ENTRIES == g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_NO_MEM;
    }

    /* Check for duplicate MAC. */
    for (uint8_t i = 0U; i < g_count; i++)
    {
        if (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN))
        {
            portEXIT_CRITICAL(&g_dev_mux);
            return ESP_ERR_INVALID_STATE;
        }
    }

    uint8_t new_idx = g_count;
    memcpy(g_entries[new_idx].mac, p_mac, DEVICE_REG_MAC_LEN);
    strncpy(g_entries[new_idx].nickname, p_nickname, DEVICE_REG_NICKNAME_MAX_LEN);
    g_entries[new_idx].nickname[DEVICE_REG_NICKNAME_MAX_LEN] = '\0';
    g_entries[new_idx].counter_s                             = 0U;
    g_entries[new_idx].b_enabled                             = true;
    g_count++;

    portEXIT_CRITICAL(&g_dev_mux);

    /* Persist. */
    (void)device_reg_entry_save(new_idx);
    (void)device_reg_meta_save();

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    ESP_LOGI(gp_tag, "entry_add: slot %u MAC %02X:%02X:%02X:%02X:%02X:%02X nick='%s'",
        (unsigned)new_idx, p_mac[0], p_mac[1], p_mac[2], p_mac[3], p_mac[4], p_mac[5], p_nickname);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_remove(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    /* Compact the array: shift entries [idx+1 … g_count-1] left by one. */
    for (uint8_t i = idx; i < (g_count - 1U); i++)
    {
        g_entries[i]         = g_entries[i + 1U];
        g_throughput_kbps[i] = g_throughput_kbps[i + 1U];
        g_below_ticks[i]     = g_below_ticks[i + 1U];
        g_dev_paused[i]      = g_dev_paused[i + 1U];
        g_rx_bytes[i]        = g_rx_bytes[i + 1U];
        g_tx_bytes[i]        = g_tx_bytes[i + 1U];
    }

    /* Zero the now-unused last slot. */
    uint8_t old_count = g_count;
    g_count--;
    memset(&g_entries[g_count], 0, sizeof(g_entries[g_count]));
    g_throughput_kbps[g_count] = 0U;
    g_below_ticks[g_count]     = 0U;
    g_dev_paused[g_count]      = false;
    g_rx_bytes[g_count]        = 0U;
    g_tx_bytes[g_count]        = 0U;

    /* Adjust rider index. */
    if (DEVICE_REG_NO_RIDER != g_rider)
    {
        if (g_rider == idx)
        {
            g_rider = DEVICE_REG_NO_RIDER;
        }
        else if (g_rider > idx)
        {
            g_rider--;
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);

    /* Rewrite all blobs plus metadata, then erase the stale last key. */
    nvs_handle_t handle;
    if (ESP_OK == nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle))
    {
        for (uint8_t i = 0U; i < g_count; i++)
        {
            char key[12];
            snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_DEV_FMT, (unsigned)i);
            (void)nvs_set_blob(handle, key, &g_entries[i], sizeof(device_reg_entry_t));
        }

        /* Erase the slot that no longer exists (old_count - 1). */
        char stale_key[12];
        snprintf(stale_key, sizeof(stale_key), DEVICE_REG_NVS_KEY_DEV_FMT,
            (unsigned)(old_count - 1U));
        (void)nvs_erase_key(handle, stale_key);

        (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_COUNT, g_count);
        (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_RIDER, g_rider);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    ESP_LOGI(gp_tag, "entry_remove: idx=%u, count now %u", (unsigned)idx, (unsigned)g_count);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_get(uint8_t idx, device_reg_entry_t * p_out)
{
    if (NULL == p_out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    *p_out = g_entries[idx];
    portEXIT_CRITICAL(&g_dev_mux);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_nickname_set(uint8_t idx, const char * p_nickname)
{
    if ((NULL == p_nickname) || ('\0' == p_nickname[0]))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(p_nickname) > DEVICE_REG_NICKNAME_MAX_LEN)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(g_entries[idx].nickname, p_nickname, DEVICE_REG_NICKNAME_MAX_LEN);
    g_entries[idx].nickname[DEVICE_REG_NICKNAME_MAX_LEN] = '\0';
    portEXIT_CRITICAL(&g_dev_mux);

    (void)device_reg_entry_save(idx);
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_enabled_set(uint8_t idx, bool b_enabled)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    g_entries[idx].b_enabled = b_enabled;
    portEXIT_CRITICAL(&g_dev_mux);

    (void)device_reg_entry_save(idx);
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_entry_counter_set(uint8_t idx, uint32_t counter_s)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    g_entries[idx].counter_s = counter_s;
    portEXIT_CRITICAL(&g_dev_mux);

    (void)device_reg_entry_save(idx);
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint32_t device_reg_entry_counter_get(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return 0U;
    }

    uint32_t val = g_entries[idx].counter_s;
    portEXIT_CRITICAL(&g_dev_mux);
    return val;
}

//--------------------------------------------------------------------------------------------------

int8_t device_reg_mac_find(const uint8_t * p_mac)
{
    if (NULL == p_mac)
    {
        return (int8_t)-1;
    }

    portENTER_CRITICAL(&g_dev_mux);

    int8_t found = (int8_t)-1;
    for (uint8_t i = 0U; i < g_count; i++)
    {
        if (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN))
        {
            found = (int8_t)i;
            break;
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);
    return found;
}

//--------------------------------------------------------------------------------------------------

bool device_reg_mac_internet_allowed(const uint8_t * p_mac)
{
    if (NULL == p_mac)
    {
        return false;
    }

    portENTER_CRITICAL(&g_dev_mux);

    bool b_allowed = false;
    for (uint8_t i = 0U; i < g_count; i++)
    {
        if (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN))
        {
            b_allowed = (g_entries[i].b_enabled && (g_entries[i].counter_s > 0U));
            break;
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);
    return b_allowed;
}

//--------------------------------------------------------------------------------------------------

uint8_t device_reg_current_rider_get(void)
{
    portENTER_CRITICAL(&g_dev_mux);
    uint8_t rider = g_rider;
    portEXIT_CRITICAL(&g_dev_mux);
    return rider;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_current_rider_set(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);

    if ((DEVICE_REG_NO_RIDER != idx) && (idx >= g_count))
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    g_rider = idx;
    portEXIT_CRITICAL(&g_dev_mux);

    (void)device_reg_meta_save();
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t device_reg_tick(void)
{
    /* Read global gate parameters (outside spinlock — plain function calls). */
    uint32_t threshold = (uint32_t)config_mngr_soft_ap_dec_threshold_kbps_get();
    uint32_t timeout   = (uint32_t)config_mngr_soft_ap_idle_throughput_timeout_s_get();

    /* Get connected station list outside spinlock. */
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    (void)esp_wifi_ap_get_sta_list(&sta_list);

    /* Per-device arrays for post-spinlock work. */
    bool    b_zero[DEVICE_REG_MAX_ENTRIES];
    bool    b_changed = false;
    uint8_t count_snap;

    memset(b_zero, 0, sizeof(b_zero));

    portENTER_CRITICAL(&g_dev_mux);

    count_snap = g_count;

    for (uint8_t i = 0U; i < count_snap; i++)
    {
        /* Throughput computation. */
        uint32_t delta       = g_rx_bytes[i] + g_tx_bytes[i];
        g_throughput_kbps[i] = (uint32_t)(delta * 8U / 1000U);
        g_rx_bytes[i]        = 0U;
        g_tx_bytes[i]        = 0U;

        /* Sliding-window traffic gate. */
        if (g_throughput_kbps[i] > threshold)
        {
            g_below_ticks[i] = 0U;
            g_dev_paused[i]  = false;
        }
        else
        {
            if (g_below_ticks[i] < UINT16_MAX)
            {
                g_below_ticks[i]++;
            }
            if ((0U == timeout) || (g_below_ticks[i] >= (uint16_t)timeout))
            {
                g_dev_paused[i] = true;
            }
        }

        /* Decrement if eligible. */
        if (!g_dev_paused[i] && g_entries[i].b_enabled && (g_entries[i].counter_s > 0U))
        {
            /* Check if the device's MAC is in the AP station list. */
            bool b_connected = false;
            for (int j = 0; j < (int)sta_list.num; j++)
            {
                if (0 == memcmp(g_entries[i].mac, sta_list.sta[j].mac, DEVICE_REG_MAC_LEN))
                {
                    b_connected = true;
                    break;
                }
            }

            if (b_connected)
            {
                g_entries[i].counter_s--;
                b_changed = true;
                if (0U == g_entries[i].counter_s)
                {
                    b_zero[i] = true;
                }
            }
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);

    /* Post-tick saves (outside spinlock). */
    for (uint8_t i = 0U; i < count_snap; i++)
    {
        if (b_zero[i])
        {
            (void)device_reg_entry_save(i);
        }
    }

    if (b_changed)
    {
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    }

    /* Periodic full save. */
    g_tick_count++;
    if (g_tick_count >= (uint16_t)DEVICE_REG_SAVE_INTERVAL_S)
    {
        device_reg_counters_save_all();
        g_tick_count = 0U;
    }

    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

void device_reg_mac_rx_bytes_add(const uint8_t * p_mac, uint32_t bytes)
{
    if (NULL == p_mac)
    {
        return;
    }

    portENTER_CRITICAL(&g_dev_mux);

    for (uint8_t i = 0U; i < g_count; i++)
    {
        if (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN))
        {
            g_rx_bytes[i] += bytes;
            break;
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);
}

//--------------------------------------------------------------------------------------------------

void device_reg_mac_tx_bytes_add(const uint8_t * p_mac, uint32_t bytes)
{
    if (NULL == p_mac)
    {
        return;
    }

    portENTER_CRITICAL(&g_dev_mux);

    for (uint8_t i = 0U; i < g_count; i++)
    {
        if (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN))
        {
            g_tx_bytes[i] += bytes;
            break;
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);
}

//--------------------------------------------------------------------------------------------------

uint32_t device_reg_entry_throughput_kbps_get(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return 0U;
    }

    uint32_t kbps = g_throughput_kbps[idx];
    portEXIT_CRITICAL(&g_dev_mux);
    return kbps;
}

//--------------------------------------------------------------------------------------------------

bool device_reg_entry_is_paused(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return false;
    }

    bool b_paused = g_dev_paused[idx];
    portEXIT_CRITICAL(&g_dev_mux);
    return b_paused;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Save the entry at \p idx as a blob to the \c esport_dev NVS namespace.
 *
 * \param[in] idx  Entry index (caller must ensure it is valid).
 *
 * \return \c ESP_OK on success; NVS error code on failure.
 */
static esp_err_t device_reg_entry_save(uint8_t idx)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "entry_save(idx=%u): nvs_open failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
        return ret;
    }

    char key[12];
    snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_DEV_FMT, (unsigned)idx);

    portENTER_CRITICAL(&g_dev_mux);
    device_reg_entry_t entry_copy = g_entries[idx];
    portEXIT_CRITICAL(&g_dev_mux);

    ret = nvs_set_blob(handle, key, &entry_copy, sizeof(device_reg_entry_t));
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "entry_save(idx=%u): nvs_set_blob failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
    }
    else
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "entry_save(idx=%u): nvs_commit failed: %s", (unsigned)idx,
                esp_err_to_name(ret));
        }
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save the device count and current rider index to NVS.
 *
 * \return \c ESP_OK on success; NVS error code on failure.
 */
static esp_err_t device_reg_meta_save(void)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "meta_save: nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&g_dev_mux);
    uint8_t count = g_count;
    uint8_t rider = g_rider;
    portEXIT_CRITICAL(&g_dev_mux);

    (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_COUNT, count);
    (void)nvs_set_u8(handle, DEVICE_REG_NVS_KEY_RIDER, rider);
    ret = nvs_commit(handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "meta_save: nvs_commit failed: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save all device entry blobs to NVS.
 *
 * Called periodically from #device_reg_tick() every #DEVICE_REG_SAVE_INTERVAL_S seconds.
 */
static void device_reg_counters_save_all(void)
{
    portENTER_CRITICAL(&g_dev_mux);
    uint8_t count = g_count;
    portEXIT_CRITICAL(&g_dev_mux);

    for (uint8_t i = 0U; i < count; i++)
    {
        (void)device_reg_entry_save(i);
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
