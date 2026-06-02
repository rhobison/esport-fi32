/**
 * \file
 * \brief Device registry - per-device internet access control and NVS persistence.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "device_registry.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
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

/** NVS key format for the metadata blob (mac, nickname, b_enabled). */
#define DEVICE_REG_NVS_KEY_META_FMT ("dev_%u_m")
/** NVS key format for the counter value (uint32_t). */
#define DEVICE_REG_NVS_KEY_CTR_FMT  ("dev_%u_c")

/** NVS key for the current rider index (uint8). */
#define DEVICE_REG_NVS_KEY_RIDER ("dev_rider")

/** Depth of the asynchronous NVS-save request queue. */
#define DEVICE_REG_NVS_QUEUE_LEN (8U)

/** Queue message value requesting a full counter save. */
#define DEVICE_REG_NVS_SAVE_ALL (0xFFU)

//==================================================================================================
// Internal Type Definitions
//==================================================================================================

/** Internal struct holding only the static per-device metadata (no counter). */
typedef struct device_reg_meta_tag
{
    uint8_t mac[DEVICE_REG_MAC_LEN];
    char    nickname[DEVICE_REG_NICKNAME_MAX_LEN + 1U];
    bool    b_enabled;
} device_reg_meta_t;

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

/** Queue of asynchronous NVS-save requests serviced by #device_reg_nvs_task.
 *  Flash commits must not run in the high-priority esp_timer task. */
static QueueHandle_t g_nvs_queue = NULL;

/** Tick counter used to trigger periodic NVS saves. */
static uint16_t g_tick_count = 0U;

/** Set when any counter changes in RAM; cleared after the periodic NVS save so
 * NVS is never written when no counter has changed since the last checkpoint. */
static bool gb_counters_dirty = false;

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

/**
 * \brief Internet gate lock flag per device (RAM only, not persisted).
 *
 * When \c true for a device slot, that device cannot access the internet
 * (\c device_reg_mac_internet_allowed() returns \c false) and its counter is
 * not decremented by \c device_reg_tick().  Defaults to \c false on every
 * boot: if a counter survived in NVS the child may use internet immediately
 * after a reboot.  Set to \c true by \c time_counter when a session opens
 * and the rider's counter is zero; cleared when the internet gate threshold
 * fires.
 */
static bool g_inet_gate_locked[DEVICE_REG_MAX_ENTRIES];

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t device_reg_entry_save(uint8_t idx);
static esp_err_t device_reg_entry_meta_save(uint8_t idx);
static esp_err_t device_reg_entry_ctr_save(uint8_t idx);
static esp_err_t device_reg_meta_save(void);
static void      device_reg_counters_save_all(void);
static void      device_reg_nvs_task(void * p_arg);
static void      device_reg_nvs_request(uint8_t msg);

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
    memset(g_inet_gate_locked, 0, sizeof(g_inet_gate_locked));
    memset(g_entries, 0, sizeof(g_entries));
    g_count           = 0U;
    g_rider           = DEVICE_REG_NO_RIDER;
    g_tick_count      = 0U;
    gb_counters_dirty = false;

    /* Create the asynchronous NVS-save worker so that periodic flash
     * commits never run in the esp_timer task (which also hosts the Wi-Fi
     * reconnect timer and the 1 s tick). */
    if (NULL == g_nvs_queue)
    {
        g_nvs_queue = xQueueCreate(DEVICE_REG_NVS_QUEUE_LEN, sizeof(uint8_t));
        if (NULL == g_nvs_queue)
        {
            ESP_LOGE(gp_tag, "init: xQueueCreate(nvs) failed");
            return ESP_ERR_NO_MEM;
        }
        if (pdPASS !=
            xTaskCreate(device_reg_nvs_task, "dev_reg_nvs", 4096, NULL, 3, NULL))
        {
            ESP_LOGE(gp_tag, "init: xTaskCreate(nvs) failed");
            return ESP_ERR_NO_MEM;
        }
    }

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
        /* First boot - write 0 and continue. */
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

    /* Load each device entry from split keys. */
    for (uint8_t i = 0U; i < count; i++)
    {
        char key[16];

        device_reg_meta_t meta;
        size_t            meta_size = sizeof(meta);
        snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_META_FMT, (unsigned)i);
        esp_err_t blob_ret = nvs_get_blob(handle, key, &meta, &meta_size);

        if (ESP_OK == blob_ret)
        {
            memcpy(g_entries[i].mac, meta.mac, DEVICE_REG_MAC_LEN);
            memcpy(g_entries[i].nickname, meta.nickname, sizeof(g_entries[i].nickname));
            g_entries[i].b_enabled = meta.b_enabled;

            /* Read counter (missing counter defaults to 0 — acceptable). */
            snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_CTR_FMT, (unsigned)i);
            uint32_t ctr = 0U;
            (void)nvs_get_u32(handle, key, &ctr);
            g_entries[i].counter_s = ctr;

            ESP_LOGD(gp_tag, "dev %u: loaded (meta+ctr)", (unsigned)i);
        }
        else if (ESP_ERR_NVS_NOT_FOUND == blob_ret)
        {
            /* No entry for this slot — zero it. */
            memset(&g_entries[i], 0, sizeof(g_entries[i]));
            ESP_LOGD(gp_tag, "dev %u: no NVS data (empty slot)", (unsigned)i);
        }
        else
        {
            memset(&g_entries[i], 0, sizeof(g_entries[i]));
            ESP_LOGW(gp_tag, "dev %u: meta read error %s, slot zeroed", (unsigned)i,
                esp_err_to_name(blob_ret));
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
        /* Invalid rider index - reset. */
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
        g_entries[i]          = g_entries[i + 1U];
        g_throughput_kbps[i]  = g_throughput_kbps[i + 1U];
        g_below_ticks[i]      = g_below_ticks[i + 1U];
        g_dev_paused[i]       = g_dev_paused[i + 1U];
        g_inet_gate_locked[i] = g_inet_gate_locked[i + 1U];
        g_rx_bytes[i]         = g_rx_bytes[i + 1U];
        g_tx_bytes[i]         = g_tx_bytes[i + 1U];
    }

    /* Zero the now-unused last slot. */
    uint8_t old_count = g_count;
    g_count--;
    memset(&g_entries[g_count], 0, sizeof(g_entries[g_count]));
    g_throughput_kbps[g_count]  = 0U;
    g_below_ticks[g_count]      = 0U;
    g_dev_paused[g_count]       = false;
    g_inet_gate_locked[g_count] = false;
    g_rx_bytes[g_count]         = 0U;
    g_tx_bytes[g_count]         = 0U;

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

    /* Rewrite all compacted entries using the split-key helpers, then erase the
     * stale last slot keys and update count/rider. */
    for (uint8_t i = 0U; i < g_count; i++)
    {
        (void)device_reg_entry_save(i);
    }

    nvs_handle_t handle;
    if (ESP_OK == nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle))
    {
        uint8_t old_last = old_count - 1U;
        char    stale_key[16];

        /* Erase split keys for the vacated last slot. */
        snprintf(stale_key, sizeof(stale_key), DEVICE_REG_NVS_KEY_META_FMT, (unsigned)old_last);
        (void)nvs_erase_key(handle, stale_key);
        snprintf(stale_key, sizeof(stale_key), DEVICE_REG_NVS_KEY_CTR_FMT, (unsigned)old_last);
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

esp_err_t device_reg_entry_mac_set(uint8_t idx, const uint8_t * p_mac)
{
    if (NULL == p_mac)
    {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    /* Reject the new MAC if it is already registered in another slot. */
    for (uint8_t i = 0U; i < g_count; i++)
    {
        if ((i != idx) && (0 == memcmp(g_entries[i].mac, p_mac, DEVICE_REG_MAC_LEN)))
        {
            portEXIT_CRITICAL(&g_dev_mux);
            return ESP_ERR_INVALID_STATE;
        }
    }

    memcpy(g_entries[idx].mac, p_mac, DEVICE_REG_MAC_LEN);
    portEXIT_CRITICAL(&g_dev_mux);

    (void)device_reg_entry_meta_save(idx);
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
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

    (void)device_reg_entry_meta_save(idx);
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

    (void)device_reg_entry_meta_save(idx);
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
    gb_counters_dirty        = true;
    portEXIT_CRITICAL(&g_dev_mux);

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

esp_err_t device_reg_entry_inet_gate_lock_set(uint8_t idx, bool b_locked)
{
    portENTER_CRITICAL(&g_dev_mux);

    if (idx >= g_count)
    {
        portEXIT_CRITICAL(&g_dev_mux);
        return ESP_ERR_INVALID_ARG;
    }

    g_inet_gate_locked[idx] = b_locked;
    portEXIT_CRITICAL(&g_dev_mux);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

bool device_reg_entry_inet_gate_lock_get(uint8_t idx)
{
    portENTER_CRITICAL(&g_dev_mux);
    bool b = (idx < g_count) && g_inet_gate_locked[idx];
    portEXIT_CRITICAL(&g_dev_mux);
    return b;
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
            b_allowed =
                (g_entries[i].b_enabled && (g_entries[i].counter_s > 0U) && !g_inet_gate_locked[i]);
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
    /* Read global gate parameters (outside spinlock - plain function calls). */
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

        /* Drain and reset byte accumulators every tick regardless of
         * connection status so stale bytes from background AP netif
         * traffic (DHCP, ARP probes) do not accumulate. */
        uint32_t delta = g_rx_bytes[i] + g_tx_bytes[i];
        g_rx_bytes[i]  = 0U;
        g_tx_bytes[i]  = 0U;

        /* Throughput and traffic gate only apply to connected devices.
         * Disconnected devices always read 0 kbps / paused. */
        if (!b_connected)
        {
            g_throughput_kbps[i] = 0U;
            g_below_ticks[i]     = 0U;
            g_dev_paused[i]      = true;
            continue;
        }

        g_throughput_kbps[i] = (uint32_t)(delta * 8U / 1000U);

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

        /* Decrement if eligible: connected, enabled, has credits, gate unlocked. */
        if (!g_dev_paused[i] && g_entries[i].b_enabled && (g_entries[i].counter_s > 0U) &&
            !g_inet_gate_locked[i])
        {
            g_entries[i].counter_s--;
            b_changed         = true;
            gb_counters_dirty = true;
            if (0U == g_entries[i].counter_s)
            {
                b_zero[i] = true;
            }
        }
    }

    portEXIT_CRITICAL(&g_dev_mux);

    /* Post-tick saves (outside spinlock).  Queued to the worker task so
     * the flash commit never blocks the esp_timer task. */
    for (uint8_t i = 0U; i < count_snap; i++)
    {
        if (b_zero[i])
        {
            device_reg_nvs_request(i);
        }
    }

    if (b_changed)
    {
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_DEVICE_REGISTRY_CHANGED, NULL, 0U, 0U);
    }

    /* Periodic full save — only when counters have actually changed since the
     * last checkpoint.  ESP-IDF NVS does not skip writes on identical data, so
     * we guard with a dirty flag to avoid unnecessary flash wear. */
    g_tick_count++;
    if (g_tick_count >= (uint16_t)DEVICE_REG_SAVE_INTERVAL_S)
    {
        if (gb_counters_dirty)
        {
            device_reg_nvs_request(DEVICE_REG_NVS_SAVE_ALL);
            gb_counters_dirty = false;
        }
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
 * Delegates to \c device_reg_entry_meta_save() and \c device_reg_entry_ctr_save()
 * so callers such as \c device_reg_entry_add() continue to write both keys.
 *
 * \param[in] idx  Entry index (caller must ensure it is valid).
 *
 * \return \c ESP_OK on success; NVS error code on failure.
 */
static esp_err_t device_reg_entry_save(uint8_t idx)
{
    (void)device_reg_entry_meta_save(idx);
    (void)device_reg_entry_ctr_save(idx);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save the static metadata for entry \p idx (MAC, nickname, b_enabled) to NVS.
 *
 * \param[in] idx  Entry index (caller must ensure it is valid).
 *
 * \return \c ESP_OK on success; NVS error code on failure.
 */
static esp_err_t device_reg_entry_meta_save(uint8_t idx)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "entry_meta_save(idx=%u): nvs_open failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
        return ret;
    }

    device_reg_meta_t meta;
    portENTER_CRITICAL(&g_dev_mux);
    memcpy(meta.mac, g_entries[idx].mac, DEVICE_REG_MAC_LEN);
    memcpy(meta.nickname, g_entries[idx].nickname, sizeof(meta.nickname));
    meta.b_enabled = g_entries[idx].b_enabled;
    portEXIT_CRITICAL(&g_dev_mux);

    char key[16];
    snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_META_FMT, (unsigned)idx);

    ret = nvs_set_blob(handle, key, &meta, sizeof(meta));
    if (ESP_OK != ret)
    {
        ESP_LOGW(gp_tag, "entry_meta_save(idx=%u): nvs_set_blob failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
    }
    else
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGW(gp_tag, "entry_meta_save(idx=%u): nvs_commit failed: %s", (unsigned)idx,
                esp_err_to_name(ret));
        }
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Save the counter for entry \p idx to NVS as a uint32.
 *
 * \param[in] idx  Entry index (caller must ensure it is valid).
 *
 * \return \c ESP_OK on success; NVS error code on failure.
 */
static esp_err_t device_reg_entry_ctr_save(uint8_t idx)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(DEVICE_REG_NVS_NS, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "entry_ctr_save(idx=%u): nvs_open failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&g_dev_mux);
    uint32_t counter_s = g_entries[idx].counter_s;
    portEXIT_CRITICAL(&g_dev_mux);

    char key[16];
    snprintf(key, sizeof(key), DEVICE_REG_NVS_KEY_CTR_FMT, (unsigned)idx);

    ret = nvs_set_u32(handle, key, counter_s);
    if (ESP_OK != ret)
    {
        ESP_LOGW(gp_tag, "entry_ctr_save(idx=%u): nvs_set_u32 failed: %s", (unsigned)idx,
            esp_err_to_name(ret));
    }
    else
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGW(gp_tag, "entry_ctr_save(idx=%u): nvs_commit failed: %s", (unsigned)idx,
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
        (void)device_reg_entry_ctr_save(i);
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Worker task that performs queued NVS counter saves off the timer task.
 *
 * NVS commits block on flash erase/write and must not run in the
 * high-priority esp_timer task (which also dispatches the Wi-Fi reconnect
 * timer).  This low-priority task drains #g_nvs_queue and performs the actual
 * flash writes.
 *
 * \param[in] p_arg  Unused.
 */
static void device_reg_nvs_task(void * p_arg)
{
    (void)p_arg;

    for (;;)
    {
        uint8_t msg = 0U;
        if (pdTRUE == xQueueReceive(g_nvs_queue, &msg, portMAX_DELAY))
        {
            if (DEVICE_REG_NVS_SAVE_ALL == msg)
            {
                device_reg_counters_save_all();
            }
            else if (msg < (uint8_t)DEVICE_REG_MAX_ENTRIES)
            {
                (void)device_reg_entry_ctr_save(msg);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Enqueue an NVS-save request for the worker task (non-blocking).
 *
 * Drops the request if the queue is full; the next periodic save will persist
 * the latest in-RAM state, so no data is lost.
 *
 * \param[in] msg  Entry index to save, or #DEVICE_REG_NVS_SAVE_ALL.
 */
static void device_reg_nvs_request(uint8_t msg)
{
    if (NULL != g_nvs_queue)
    {
        (void)xQueueSend(g_nvs_queue, &msg, 0);
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Compute a deterministic 8-hex-char PIN from a device's registered MAC address.
 *
 * \param[in]  dev_idx    Entry index (0 to #DEVICE_REG_MAX_ENTRIES-1).
 * \param[out] p_pin_out  Caller-supplied buffer of at least #DEVICE_REG_PIN_LEN + 1 bytes.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p dev_idx is out of range or the slot is unregistered.
 */
esp_err_t device_reg_pin_compute(uint8_t dev_idx, char * p_pin_out)
{
    if ((dev_idx >= (uint8_t)DEVICE_REG_MAX_ENTRIES) || (NULL == p_pin_out))
    {
        return ESP_ERR_INVALID_ARG;
    }

    device_reg_entry_t entry;
    esp_err_t          ret = device_reg_entry_get(dev_idx, &entry);
    if (ESP_OK != ret)
    {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t sc_zero_mac[6U] = { 0U, 0U, 0U, 0U, 0U, 0U };
    if (0 == memcmp(entry.mac, sc_zero_mac, sizeof(sc_zero_mac)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t crc = esp_rom_crc32_be(0U, entry.mac, (uint32_t)DEVICE_REG_MAC_LEN);
    (void)snprintf(p_pin_out, DEVICE_REG_PIN_LEN + 1U, "%08" PRIX32, crc);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
