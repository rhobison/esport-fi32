/**
 * \file
 * \brief HTTP server telemetry endpoint and client logging page.
 *
 * Implements \c GET /api/telemetry (live diagnostic JSON) and \c GET
 * /telemetry (a self-contained page that polls the endpoint and logs the most
 * recent responses browser-side).  All logging is performed client-side only;
 * the device persists nothing.
 *
 * \date 2026-05-30
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_telemetry.h"
#include "http_server_utils.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_manager.h"
#include "http_server_config.h"
#include "time_manager.h"
#include "wifi_manager.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_telemetry";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static const char * http_srv_reset_reason_str(esp_reset_reason_t reason);
static uint8_t      http_srv_cpu_load_pct(void);

//==================================================================================================
// Internal Functions
//==================================================================================================

/**
 * \brief Map an \c esp_reset_reason_t value to a short human-readable string.
 *
 * \param[in] reason  Reset reason returned by \c esp_reset_reason().
 *
 * \return Constant string describing the reset cause.
 */
static const char * http_srv_reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "other_wdt";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        default:
            return "unknown";
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Compute aggregate CPU load percentage from FreeRTOS run-time stats.
 *
 * Snapshots all tasks via \c uxTaskGetSystemState(), sums the run-time of the
 * idle task(s), and derives busy load as 100 - idle%.  Requires
 * \c CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS.
 *
 * \return CPU busy percentage (0–100), or 0 if stats are unavailable.
 */
static uint8_t http_srv_cpu_load_pct(void)
{
    static TaskStatus_t s_tasks[32];
    uint32_t            total_runtime = 0U;
    UBaseType_t         count         = uxTaskGetSystemState(s_tasks,
                        (UBaseType_t)(sizeof(s_tasks) / sizeof(s_tasks[0])), &total_runtime);

    if ((0U == count) || (0U == total_runtime))
    {
        return 0U;
    }

    uint32_t idle_runtime = 0U;
    for (UBaseType_t i = 0U; i < count; i++)
    {
        if (0 == strncmp(s_tasks[i].pcTaskName, "IDLE", 4))
        {
            idle_runtime += s_tasks[i].ulRunTimeCounter;
        }
    }

    if (idle_runtime >= total_runtime)
    {
        return 0U;
    }

    uint32_t busy = total_runtime - idle_runtime;
    return (uint8_t)((busy * 100U) / total_runtime);
}

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t http_srv_api_telemetry_handler(httpd_req_t * p_req)
{
    char * p_buf = http_srv_scratch_take(HTTP_SRV_SCRATCH_WAIT_MS);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server busy");
        return ESP_FAIL;
    }

    int64_t     uptime_s       = (int64_t)(esp_timer_get_time() / 1000000LL);
    uint32_t    free_heap      = esp_get_free_heap_size();
    uint32_t    min_free_heap  = esp_get_minimum_free_heap_size();
    uint32_t    largest_block  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t    total_internal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint8_t     cpu_load       = http_srv_cpu_load_pct();
    UBaseType_t task_count     = uxTaskGetNumberOfTasks();

    bool     b_sta       = wifi_mngr_sta_is_connected();
    int8_t   rssi        = wifi_mngr_sta_rssi();
    uint32_t reconnects  = wifi_mngr_sta_reconnect_count();
    uint32_t disconnects = wifi_mngr_sta_disconnect_count();
    bool     b_rew_ap    = wifi_mngr_reward_ap_is_active();
    uint8_t  ap_clients  = wifi_mngr_reward_ap_client_count();
    uint32_t throughput  = wifi_mngr_reward_ap_throughput_kbps();
    bool     b_synced    = time_mngr_is_synced();

    char sta_ip[20];
    char sta_ssid[33];
    wifi_mngr_sta_ip_get(sta_ip, sizeof(sta_ip));
    config_mngr_wifi_ssid_get(sta_ssid, sizeof(sta_ssid));

    /* Per-task stack high-water-mark snapshot.  Identifies the most
     * stack-constrained task so low-water alarms can be acted on before the
     * next overflow.  usStackHighWaterMark is in 4-byte words. */
    static TaskStatus_t s_hwm_tasks[32];
    UBaseType_t         hwm_count         = uxTaskGetSystemState(s_hwm_tasks,
                        (UBaseType_t)(sizeof(s_hwm_tasks) / sizeof(s_hwm_tasks[0])), NULL);
    uint16_t            min_hwm_words     = 0xFFFFU;
    const char *        min_hwm_task_name = "?";
    for (UBaseType_t i = 0U; i < hwm_count; i++)
    {
        if (s_hwm_tasks[i].usStackHighWaterMark < min_hwm_words)
        {
            min_hwm_words     = s_hwm_tasks[i].usStackHighWaterMark;
            min_hwm_task_name = s_hwm_tasks[i].pcTaskName;
        }
    }
    if (0xFFFFU == min_hwm_words)
    {
        min_hwm_words     = 0U;
        min_hwm_task_name = "?";
    }

    int n = snprintf(p_buf, HTTP_SRV_JSON_BUF_LEN,
        "{\n"
        "  \"fw_version\": \"%s\",\n"
        "  \"uptime_s\": %" PRId64 ",\n"
        "  \"reset_reason\": \"%s\",\n"
        "  \"time_synced\": %s,\n"
        "  \"free_heap\": %" PRIu32 ",\n"
        "  \"min_free_heap\": %" PRIu32 ",\n"
        "  \"largest_free_block\": %" PRIu32 ",\n"
        "  \"total_internal_heap\": %" PRIu32 ",\n"
        "  \"cpu_load_pct\": %u,\n"
        "  \"task_count\": %u,\n"
        "  \"task_min_stack_hwm_bytes\": %u,\n"
        "  \"task_min_stack_hwm_name\": \"%s\",\n"
        "  \"sta_connected\": %s,\n"
        "  \"sta_ssid\": \"%s\",\n"
        "  \"sta_ip\": \"%s\",\n"
        "  \"sta_rssi_dbm\": %d,\n"
        "  \"sta_reconnects\": %" PRIu32 ",\n"
        "  \"sta_disconnects\": %" PRIu32 ",\n"
        "  \"reward_ap_active\": %s,\n"
        "  \"reward_ap_clients\": %" PRIu8 ",\n"
        "  \"reward_ap_throughput_kbps\": %" PRIu32 "\n"
        "}\n",
        esp_app_get_description()->version, uptime_s, http_srv_reset_reason_str(esp_reset_reason()),
        b_synced ? "true" : "false", free_heap, min_free_heap, largest_block, total_internal,
        (unsigned)cpu_load, (unsigned)task_count, (unsigned)min_hwm_words * 4U, min_hwm_task_name,
        b_sta ? "true" : "false", sta_ssid, sta_ip, (int)rssi, reconnects, disconnects,
        b_rew_ap ? "true" : "false", ap_clients, throughput);

    if (n >= (int)HTTP_SRV_JSON_BUF_LEN)
    {
        ESP_LOGW(gp_tag, "api/telemetry JSON truncated");
    }

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    http_srv_scratch_give();
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_api_coredump_handler(httpd_req_t * p_req)
{
    char * p_buf = http_srv_scratch_take(HTTP_SRV_SCRATCH_WAIT_MS);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server busy");
        return ESP_FAIL;
    }

    /* esp_core_dump_image_check() is always available and is the cheapest way
     * to detect whether a valid core dump is stored in the flash partition. */
    if (ESP_OK != esp_core_dump_image_check())
    {
        snprintf(p_buf, HTTP_SRV_SCRATCH_LEN, "{\"found\":false}\n");
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
        http_srv_scratch_give();
        return ESP_OK;
    }

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t * p_sum = malloc(sizeof(*p_sum));
    if (NULL == p_sum)
    {
        http_srv_scratch_give();
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    char reason[200] = "n/a";
    (void)esp_core_dump_get_panic_reason(reason, sizeof(reason));

    esp_err_t sum_ret = esp_core_dump_get_summary(p_sum);
    if (ESP_OK != sum_ret)
    {
        free(p_sum);
        snprintf(p_buf, HTTP_SRV_SCRATCH_LEN, "{\"found\":false,\"error\":\"%s\"}\n",
            esp_err_to_name(sum_ret));
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
        http_srv_scratch_give();
        return ESP_OK;
    }

    /* Ensure task name is NUL-terminated before use in printf. */
    p_sum->exc_task[sizeof(p_sum->exc_task) - 1U] = '\0';

    /* Build JSON prefix up to the stackdump_hex field. */
    int pos = snprintf(p_buf, HTTP_SRV_SCRATCH_LEN,
        "{\n"
        "  \"found\": true,\n"
        "  \"panic_reason\": \"%s\",\n"
        "  \"task\": \"%s\",\n"
        "  \"pc\": \"0x%08" PRIx32 "\",\n"
        "  \"version\": %" PRIu32 ",\n"
        "  \"app_sha256\": \"%s\",\n"
        "  \"mcause\": \"0x%08" PRIx32 "\",\n"
        "  \"mstatus\": \"0x%08" PRIx32 "\",\n"
        "  \"mtvec\": \"0x%08" PRIx32 "\",\n"
        "  \"mtval\": \"0x%08" PRIx32 "\",\n"
        "  \"ra\": \"0x%08" PRIx32 "\",\n"
        "  \"sp\": \"0x%08" PRIx32 "\",\n"
        "  \"a\": [\"0x%08" PRIx32 "\",\"0x%08" PRIx32 "\","
        "\"0x%08" PRIx32 "\",\"0x%08" PRIx32 "\","
        "\"0x%08" PRIx32 "\",\"0x%08" PRIx32 "\","
        "\"0x%08" PRIx32 "\",\"0x%08" PRIx32 "\"],\n"
        "  \"stackdump_size\": %" PRIu32 ",\n"
        "  \"stackdump_hex\": \"",
        reason, p_sum->exc_task, p_sum->exc_pc, p_sum->core_dump_version,
        (const char *)p_sum->app_elf_sha256, p_sum->ex_info.mcause, p_sum->ex_info.mstatus,
        p_sum->ex_info.mtvec, p_sum->ex_info.mtval, p_sum->ex_info.ra, p_sum->ex_info.sp,
        p_sum->ex_info.exc_a[0], p_sum->ex_info.exc_a[1], p_sum->ex_info.exc_a[2],
        p_sum->ex_info.exc_a[3], p_sum->ex_info.exc_a[4], p_sum->ex_info.exc_a[5],
        p_sum->ex_info.exc_a[6], p_sum->ex_info.exc_a[7], p_sum->exc_bt_info.dump_size);
    if (pos < 0)
    {
        pos = 0;
    }

    /* Append stackdump as lowercase hex, capped to the actual dump size and
     * the buffer limit. */
    uint32_t hex_bytes = p_sum->exc_bt_info.dump_size;
    if (hex_bytes > (uint32_t)sizeof(p_sum->exc_bt_info.stackdump))
    {
        hex_bytes = (uint32_t)sizeof(p_sum->exc_bt_info.stackdump);
    }
    for (uint32_t i = 0U; (i < hex_bytes) && ((pos + 3) < (int)HTTP_SRV_SCRATCH_LEN); i++)
    {
        pos += snprintf(p_buf + pos, (size_t)((int)HTTP_SRV_SCRATCH_LEN - pos), "%02x",
            (unsigned)p_sum->exc_bt_info.stackdump[i]);
    }

    free(p_sum);

    if ((pos + 8) < (int)HTTP_SRV_SCRATCH_LEN)
    {
        pos += snprintf(p_buf + pos, (size_t)((int)HTTP_SRV_SCRATCH_LEN - pos), "\"\n}\n");
    }
#else
    /* Flash ELF coredump not configured — report found but unreadable. */
    snprintf(p_buf, HTTP_SRV_SCRATCH_LEN,
        "{\"found\":true,\"error\":\"summary unavailable (not ELF flash format)\"}\n");
#endif /* CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF */

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    http_srv_scratch_give();
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_api_coredump_download_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    if (ESP_OK != esp_core_dump_image_check())
    {
        httpd_resp_send_err(p_req, HTTPD_404_NOT_FOUND, "No valid core dump stored in flash");
        return ESP_FAIL;
    }

    size_t    image_addr = 0U;
    size_t    image_size = 0U;
    esp_err_t ret        = esp_core_dump_image_get(&image_addr, &image_size);
    if ((ESP_OK != ret) || (image_addr > UINT32_MAX) || (image_size > UINT32_MAX))
    {
        ESP_LOGE(gp_tag, "core dump image lookup failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Unable to locate core dump image");
        return ESP_FAIL;
    }

    char * p_buf = http_srv_scratch_take(HTTP_SRV_SCRATCH_WAIT_MS);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server busy");
        return ESP_FAIL;
    }

    httpd_resp_set_type(p_req, "application/octet-stream");
    httpd_resp_set_hdr(p_req, "Content-Disposition",
        "attachment; filename=\"esport-fi32-coredump.bin\"");
    httpd_resp_set_hdr(p_req, "Cache-Control", "no-store");

    size_t offset = 0U;
    while (offset < image_size)
    {
        size_t chunk_size = image_size - offset;
        if (chunk_size > (size_t)HTTP_SRV_SCRATCH_LEN)
        {
            chunk_size = (size_t)HTTP_SRV_SCRATCH_LEN;
        }

        ret = esp_flash_read(NULL, p_buf, (uint32_t)(image_addr + offset), (uint32_t)chunk_size);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "core dump flash read failed at offset %u: %s", (unsigned)offset,
                esp_err_to_name(ret));
            break;
        }

        ret = httpd_resp_send_chunk(p_req, p_buf, (ssize_t)chunk_size);
        if (ESP_OK != ret)
        {
            ESP_LOGW(gp_tag, "core dump download interrupted at offset %u", (unsigned)offset);
            break;
        }
        offset += chunk_size;
    }

    if (ESP_OK == ret)
    {
        ret = httpd_resp_send_chunk(p_req, NULL, 0U);
    }

    http_srv_scratch_give();
    return ret;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_api_coredump_delete_handler(httpd_req_t * p_req)
{
    char * p_buf = http_srv_scratch_take(HTTP_SRV_SCRATCH_WAIT_MS);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server busy");
        return ESP_FAIL;
    }

    /* Idempotent: erase whenever a valid image is present; otherwise there is
     * nothing to do and the request still succeeds. */
    esp_err_t erase_ret = ESP_OK;
    if (ESP_OK == esp_core_dump_image_check())
    {
        erase_ret = esp_core_dump_image_erase();
    }

    if (ESP_OK != erase_ret)
    {
        ESP_LOGW(gp_tag, "core dump erase failed: %s", esp_err_to_name(erase_ret));
        snprintf(p_buf, HTTP_SRV_SCRATCH_LEN, "{\"ok\":false,\"error\":\"%s\"}\n",
            esp_err_to_name(erase_ret));
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
        http_srv_scratch_give();
        return ESP_OK;
    }

    /* Confirm the partition no longer reports a valid image. */
    bool b_cleared = (ESP_OK != esp_core_dump_image_check());
    snprintf(p_buf, HTTP_SRV_SCRATCH_LEN, "{\"ok\":true,\"cleared\":%s}\n",
        b_cleared ? "true" : "false");
    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    http_srv_scratch_give();
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_telemetry_page_get_handler(httpd_req_t * p_req)
{
    static const char sc_page[] =
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Telemetry</title><style>"
        "body{font-family:system-ui,Arial,sans-serif;margin:0;background:#111;color:#eee}"
        ".bar{display:flex;flex-wrap:wrap;gap:.6em;align-items:center;padding:.8em 1em;"
        "background:#1c1c1c;position:sticky;top:0;border-bottom:1px solid #333}"
        ".bar h3{margin:0 1em 0 0}"
        "label{font-size:.9em}input[type=number]{width:5em;padding:.2em;background:#222;"
        "color:#eee;border:1px solid #444;border-radius:4px}"
        "button,a.btn{padding:.4em .9em;background:#2d6cdf;color:#fff;border:none;"
        "border-radius:5px;cursor:pointer;text-decoration:none;font-size:.9em}"
        "button:disabled{background:#555}"
        "#stat{font-size:.85em;color:#9bd}"
        "#legend{display:none;padding:.8em 1.2em;background:#181818;border-bottom:1px solid #333;"
        "font-size:.85em}"
        "#legend.show{display:block}"
        "#legend dl{margin:0;display:grid;grid-template-columns:auto 1fr;gap:.25em .9em}"
        "#legend dt{color:#9bd;font-family:ui-monospace,Consolas,monospace;white-space:nowrap}"
        "#legend dd{margin:0;color:#ccc}"
        "textarea{width:100%;height:75vh;box-sizing:border-box;background:#0a0a0a;color:#7fd;"
        "border:none;padding:.6em;font-family:ui-monospace,Consolas,monospace;font-size:.8em;"
        "white-space:pre;resize:none}"
        "#coredump{display:none;padding:.6em;background:#0a0a0a;border-bottom:1px solid #333}"
        "#coredump.show{display:block}"
        "#coredump textarea{height:50vh;width:100%;box-sizing:border-box;background:#0a0a0a;"
        "color:#ffa;border:none;padding:.6em;font-family:ui-monospace,Consolas,monospace;"
        "font-size:.78em;white-space:pre;resize:none}"
        "</style></head><body>"
        "<div class=\"bar\"><h3>Telemetry</h3>"
        "<label>Interval (s): <input id=\"iv\" type=\"number\" min=\"1\" step=\"1\" value=\"5\">"
        "</label>"
        "<label><input id=\"auto\" type=\"checkbox\" checked> Auto</label>"
        "<label><input id=\"raw\" type=\"checkbox\"> Raw JSON</label>"
        "<button id=\"now\">Refresh now</button>"
        "<button id=\"clr\">Clear</button>"
        "<button id=\"leg\">&#9432; Fields</button>"
        "<button id=\"cdBtn\">&#128293; Core Dump</button>"
        "<a class=\"btn\" href=\"/api/coredump/raw\" download>Download Raw Dump</a>"
        "<button id=\"cdDelBtn\">&#128465; Delete Core Dump</button>"
        "<a class=\"btn\" href=\"/\">Dashboard</a>"
        "<span id=\"stat\"></span></div>"
        "<div id=\"legend\"><dl>"
        "<dt>up</dt><dd>Uptime since the last boot.</dd>"
        "<dt>heap</dt><dd>Free internal heap. min = lowest ever seen since boot; "
        "blk = largest single allocatable block (low value vs. free heap = fragmentation).</dd>"
        "<dt>cpu</dt><dd>Aggregate CPU load, 100% minus idle time.</dd>"
        "<dt>tasks</dt><dd>Number of live FreeRTOS tasks.</dd>"
        "<dt>STA</dt><dd>Station link to the home router: RSSI in dBm (closer to 0 is stronger), "
        "or \"down\" when not connected.</dd>"
        "<dt>reconn</dt><dd>STA reconnect attempts since boot.</dd>"
        "<dt>disc</dt><dd>STA disconnect events since boot.</dd>"
        "<dt>AP</dt><dd>Reward access point: connected client count and current "
        "throughput in kbps, or \"off\" when inactive.</dd>"
        "<dt>hwm</dt><dd>Stack high-water mark: the task with the <em>least</em> remaining "
        "stack across all FreeRTOS tasks. Bytes left before overflow. "
        "Only the worst offender is shown. Below ~512 b warrants a stack size increase.</dd>"
        "<dt>* line</dt><dd>Static-state change marker, logged at start and whenever one of "
        "these changes: fw (firmware version), rst (last reset reason), ntp (time sync), "
        "link (STA up/down), ssid, ip, ap (reward AP on/off).</dd>"
        "</dl></div>"
        "<div id=\"coredump\"><textarea id=\"cdLog\" readonly></textarea></div>"
        "<textarea id=\"log\" readonly></textarea>"
        "<script>"
        "var MAX=1000,lines=[],timer=null,prev=null,ta=document.getElementById('log'),"
        "stat=document.getElementById('stat');"
        "function ts(){var d=new Date(),p=function(n){return(n<10?'0':'')+n;};"
        "return d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+' '+"
        "p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());}"
        "function kb(n){return(n/1024).toFixed(1)+'K';}"
        "function dur(s){s=s|0;var d=(s/86400)|0,h=((s%86400)/3600)|0,m=((s%3600)/60)|0,"
        "x=s%60,o='';if(d)o+=d+'d ';if(d||h)o+=h+'h ';o+=m+'m '+x+'s';return o;}"
        /* Per-sample line: only the values that move over time. */
        "function metrics(d){var w=d.sta_connected?('STA '+d.sta_rssi_dbm+' dBm'):'STA down';"
        "var ap=d.reward_ap_active?('AP '+d.reward_ap_clients+' cli '+"
        "d.reward_ap_throughput_kbps+' kbps'):'AP off';"
        "var hwm=d.task_min_stack_hwm_bytes!==undefined?"
        "'hwm '+d.task_min_stack_hwm_bytes+'b('+d.task_min_stack_hwm_name+')':"
        "'';"
        "return 'up '+dur(d.uptime_s)+' | heap '+kb(d.free_heap)+' (min '+kb(d.min_free_heap)+"
        "', blk '+kb(d.largest_free_block)+') | cpu '+d.cpu_load_pct+'% | tasks '+d.task_count+"
        "' | '+w+' | reconn '+d.sta_reconnects+' disc '+d.sta_disconnects+' | '+ap+"
        "(hwm?' | '+hwm:'');}"
        /* Static fields, already formatted, so equal values compare equal. */
        "function statics(d){return{fw:d.fw_version,rst:d.reset_reason,"
        "ntp:d.time_synced?'ok':'no',link:d.sta_connected?'up':'down',"
        "ssid:d.sta_ssid||'-',ip:d.sta_ip||'-',ap:d.reward_ap_active?'on':'off'};}"
        /* Event line: emitted once at start, then only for static fields that changed. */
        "function events(cur){var o=[],k;for(k in cur){"
        "if(!prev||prev[k]!==cur[k])o.push(k+'='+cur[k]);}return o;}"
        "function render(){ta.value=lines.join('\\n');ta.scrollTop=ta.scrollHeight;}"
        "function add(t){lines.push(t);if(lines.length>MAX)lines.splice(0,lines.length-MAX);"
        "render();}"
        "function fetchOnce(){var t0=ts();"
        "fetch('/api/telemetry',{cache:'no-store'}).then(function(r){return r.text();})"
        ".then(function(t){if(document.getElementById('raw').checked){var c;"
        "try{c=JSON.stringify(JSON.parse(t));}catch(e){c=t;}add('['+t0+'] '+c);"
        "stat.textContent='last: '+t0;return;}"
        "var d;try{d=JSON.parse(t);}catch(e){add('['+t0+'] '+t);return;}"
        "var cur=statics(d),ev=events(cur);if(ev.length)add('['+t0+'] * '+ev.join(' | '));"
        "prev=cur;add('['+t0+'] '+metrics(d));stat.textContent='last: '+t0;})"
        ".catch(function(e){add('['+t0+'] ERROR: '+e);stat.textContent='error '+t0;});}"
        "function reschedule(){if(timer){clearInterval(timer);timer=null;}"
        "if(document.getElementById('auto').checked){var s=parseInt("
        "document.getElementById('iv').value,10);if(!(s>=1))s=1;"
        "document.getElementById('iv').value=s;timer=setInterval(fetchOnce,s*1000);}}"
        "document.getElementById('now').onclick=fetchOnce;"
        "document.getElementById('clr').onclick=function(){lines=[];prev=null;render();};"
        "var lg=document.getElementById('legend');"
        "function setLeg(on){lg.classList.toggle('show',on);"
        "try{localStorage.setItem('tlmLegend',on?'1':'0');}catch(e){}}"
        "document.getElementById('leg').onclick=function(){setLeg(!lg.classList.contains('show'));}"
        ";"
        "try{setLeg(localStorage.getItem('tlmLegend')==='1');}catch(e){}"
        "document.getElementById('auto').onchange=reschedule;"
        "document.getElementById('iv').onchange=reschedule;"
        "fetchOnce();reschedule();"
        /* ---- Core Dump panel ---- */
        "var cdPanel=document.getElementById('coredump'),cdLog=document.getElementById('cdLog');"
        "function renderCD(d){"
        "if(!d.found)return d.error?'Error: '+d.error:'No core dump stored in flash.';"
        "var L=[];"
        "L.push('=== Core Dump Summary ===');"
        "L.push('Panic reason : '+d.panic_reason);"
        "L.push('Faulting task: '+d.task+'   PC: '+d.pc);"
        "L.push('');"
        "L.push('=== RISC-V Registers ===');"
        "L.push('MCAUSE  '+d.mcause+'   MSTATUS '+d.mstatus);"
        "L.push('MTVEC   '+d.mtvec +'   MTVAL   '+d.mtval);"
        "L.push('RA      '+d.ra    +'   SP      '+d.sp);"
        "if(d.a){for(var i=0;i<d.a.length;i++)L.push('A'+i+'      '+d.a[i]);}"
        "L.push('');"
        "L.push('=== Stack Dump ('+d.stackdump_size+' bytes @ SP '+d.sp+') ===');"
        "var h=d.stackdump_hex||'',sp=parseInt(d.sp,16);"
        "for(var o=0;o<h.length;o+=32){"
        "var chunk=h.slice(o,o+32),addr=sp+(o>>1),hs='',as='';"
        "for(var j=0;j<chunk.length;j+=2){"
        "var b=parseInt(chunk.slice(j,j+2),16);"
        "hs+=chunk.slice(j,j+2)+' ';as+=(b>=32&&b<127)?String.fromCharCode(b):'.';}"
        "L.push('0x'+(addr>>>0).toString(16).padStart(8,'0')+'  '+hs.padEnd(49)+' |'+as+'|');}"
        "L.push('');"
        "L.push('App SHA256   : '+d.app_sha256+'  (core dump v'+d.version+')');"
        "L.push('');"
        "L.push('Decode offline:');"
        "L.push('  idf.py coredump-info');"
        "L.push('  esp-coredump info_corefile -c <coredump.bin> build/esport-fi32.elf');"
        "return L.join('\\n');}"
        "document.getElementById('cdBtn').onclick=function(){"
        "var on=cdPanel.classList.contains('show');"
        "if(on){cdPanel.classList.remove('show');return;}"
        "cdLog.value='Loading\u2026';"
        "cdPanel.classList.add('show');"
        "fetch('/api/coredump',{cache:'no-store'}).then(function(r){return r.json();})"
        ".then(function(d){cdLog.value=renderCD(d);})"
        ".catch(function(e){cdLog.value='Fetch error: '+e;});};"
        "document.getElementById('cdDelBtn').onclick=function(){"
        "if(!window.confirm('Delete the stored core dump from flash? "
        "Make sure you already downloaded/reviewed it \u2014 this cannot be undone.'))return;"
        "cdPanel.classList.add('show');cdLog.value='Deleting\u2026';"
        "fetch('/api/coredump',{method:'DELETE',cache:'no-store'})"
        ".then(function(r){return r.json();})"
        ".then(function(d){cdLog.value=d.ok?"
        "('Core dump erased.'+(d.cleared?'':' (partition still reports data)')):"
        "('Erase failed: '+(d.error||'unknown error'));})"
        ".catch(function(e){cdLog.value='Fetch error: '+e;});};"
        "</script></body></html>";

    httpd_resp_set_type(p_req, "text/html");
    httpd_resp_send(p_req, sc_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/*** end of file ***/
