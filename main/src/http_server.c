/**
 * \file
 * \brief HTTP server — Phase 8 full implementation.
 *
 * Starts an \c esp_http_server instance on port 80 and registers all
 * application URI handlers.  Provides a configuration web portal
 * (GET+POST \c /config) and a JSON/CSV data API (\c /api/status,
 * \c /api/sessions, \c /api/sessions/export, \c /api/sessions/daily).
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config_manager.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Maximum number of bytes read from a POST /config request body. */
#define HTTP_SRV_POST_BODY_MAX_LEN (2048U)

/** Heap buffer size for JSON and CSV response bodies. */
#define HTTP_SRV_JSON_BUF_LEN (4096U)

/** Number of calendar days in the daily-aggregates window. */
#define HTTP_SRV_DAILY_WINDOW_DAYS (31U)

/**
 * Buffer size for HTML attribute value encoding used in the config GET handler.
 * Longest string field is 64 chars; worst-case HTML encoding (every char
 * becomes &quot; = 6 bytes) gives 384 bytes.  400 provides a safe margin.
 * Only ONE such buffer is kept on the stack at a time (re-used per field).
 */
#define HTTP_SRV_ATTR_ENC_LEN (400U)

/** Per-entry intermediate buffer for JSON/CSV row formatting. */
#define HTTP_SRV_ENTRY_BUF_LEN (256U)

/** Seconds per day, used for daily-window date arithmetic. */
#define HTTP_SRV_SECS_PER_DAY (86400L)

/** Maximum encoded byte length of a single URL-encoded form field value. */
#define HTTP_SRV_FORM_VALUE_ENC_MAX_LEN (512U)

/** Heap buffer size for the HTML status dashboard response. */
#define HTTP_SRV_HTML_BUF_LEN (16384U)

/** Maximum session entries fetched for the history table. */
#define HTTP_SRV_HIST_MAX (20U)

/** Maximum session entries fetched for graph aggregation. */
#define HTTP_SRV_GRAPH_MAX (SESSION_LOG_MAX_ENTRIES)

/* ---- SVG chart layout constants (used in root handler) ---- */

/** SVG viewport width in pixels. */
#define HTTP_SRV_SVG_W    (620)
/** SVG viewport height in pixels. */
#define HTTP_SRV_SVG_H    (200)
/** Left margin reserved for Y-axis labels. */
#define HTTP_SRV_SVG_LM   (50)
/** Pixel width of the chart plot area. */
#define HTTP_SRV_SVG_CW   (560)
/** Pixel height of the chart plot area. */
#define HTTP_SRV_SVG_CH   (140)
/** Top padding above the chart area. */
#define HTTP_SRV_SVG_TOP  (20)
/** Y-coordinate of the X-axis baseline. */
#define HTTP_SRV_SVG_BOT  (160)
/** Pixel width allocated to each daily bar slot (CW / 31 rounded down). */
#define HTTP_SRV_SVG_SLOT (18)
/** Per-side horizontal padding within each bar slot. */
#define HTTP_SRV_SVG_BPAD (1)

/**
 * \brief Aggregated session data for one calendar day (daily-aggregates window).
 */
typedef struct http_srv_daily_bin_tag
{
    int      year;             /**< tm_year value (year - 1900). */
    int      mon;              /**< tm_mon value (0–11). */
    int      mday;             /**< Day of month (1–31). */
    uint32_t sessions;         /**< Session count for this day. */
    uint64_t speed_sum_x10;    /**< Sum of avg_speed_kmh_x10 for averaging. */
    uint32_t total_duration_s; /**< Sum of duration_s for this day. */
} http_srv_daily_bin_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_server";

/** Handle to the running \c esp_http_server instance, or \c NULL. */
static httpd_handle_t gp_server_handle = NULL;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void      http_srv_url_decode(const char * p_src, char * p_dst, size_t dst_len);
static esp_err_t http_srv_form_field_get(const char * p_body, const char * p_key, char * p_out,
    size_t out_len);
static void      http_srv_html_attr_encode(const char * p_src, char * p_dst, size_t dst_len);

static esp_err_t http_srv_root_get_handler(httpd_req_t * p_req);
static esp_err_t http_srv_config_get_handler(httpd_req_t * p_req);
static esp_err_t http_srv_config_post_handler(httpd_req_t * p_req);
static esp_err_t http_srv_api_status_handler(httpd_req_t * p_req);
static esp_err_t http_srv_api_sessions_handler(httpd_req_t * p_req);
static esp_err_t http_srv_api_sessions_export_handler(httpd_req_t * p_req);
static esp_err_t http_srv_export_csv_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);
static esp_err_t http_srv_export_json_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);
static esp_err_t http_srv_api_sessions_daily_handler(httpd_req_t * p_req);

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Start the HTTP server and register all URI handlers.
 *
 * Creates an \c esp_http_server instance on port 80 and registers
 * handlers for \c GET /config, \c POST /config, \c GET /api/status,
 * \c GET /api/sessions, \c GET /api/sessions/export, and
 * \c GET /api/sessions/daily.  (\c GET / is reserved for Phase 9.)
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_init(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80U;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8U;

    esp_err_t ret = httpd_start(&gp_server_handle, &cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    static const httpd_uri_t sc_uri_root_get = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = http_srv_root_get_handler,
    };
    static const httpd_uri_t sc_uri_config_get = {
        .uri     = "/config",
        .method  = HTTP_GET,
        .handler = http_srv_config_get_handler,
    };
    static const httpd_uri_t sc_uri_config_post = {
        .uri     = "/config",
        .method  = HTTP_POST,
        .handler = http_srv_config_post_handler,
    };
    static const httpd_uri_t sc_uri_api_status = {
        .uri     = "/api/status",
        .method  = HTTP_GET,
        .handler = http_srv_api_status_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions = {
        .uri     = "/api/sessions",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions_export = {
        .uri     = "/api/sessions/export",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_export_handler,
    };
    static const httpd_uri_t sc_uri_api_sessions_daily = {
        .uri     = "/api/sessions/daily",
        .method  = HTTP_GET,
        .handler = http_srv_api_sessions_daily_handler,
    };

    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_root_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_config_get);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_config_post);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_status);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_export);
    (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_api_sessions_daily);

    ESP_LOGI(gp_tag, "started on port %" PRIu16, cfg.server_port);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Decode a URL-encoded string into \p p_dst.
 *
 * Converts '+' to space and '%XX' hex-escape sequences to their byte values.
 * The output is always NUL-terminated and never written past \p dst_len bytes
 * (including the terminator).
 *
 * \param[in]  p_src   NUL-terminated URL-encoded source string.
 * \param[out] p_dst   Destination buffer.
 * \param[in]  dst_len Total size of \p p_dst in bytes including the NUL terminator.
 */
static void http_srv_url_decode(const char * p_src, char * p_dst, size_t dst_len)
{
    if ((NULL == p_src) || (NULL == p_dst) || (0U == dst_len))
    {
        return;
    }

    size_t pos = 0U;

    while (('\0' != *p_src) && (pos < (dst_len - 1U)))
    {
        if ('+' == *p_src)
        {
            p_dst[pos] = ' ';
            pos++;
            p_src++;
        }
        else if (('%' == *p_src) && (0 != isxdigit((unsigned char)p_src[1])) &&
                 (0 != isxdigit((unsigned char)p_src[2])))
        {
            char hex[3] = { p_src[1], p_src[2], '\0' };
            p_dst[pos]  = (char)strtol(hex, NULL, 16);
            pos++;
            p_src += 3;
        }
        else
        {
            p_dst[pos] = *p_src;
            pos++;
            p_src++;
        }
    }

    p_dst[pos] = '\0';
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Look up a URL-encoded form field by key and decode its value.
 *
 * Searches \p p_body for "key=value" (fields separated by '&'), URL-decodes
 * the value into \p p_out (bounded by \p out_len), and NUL-terminates the
 * result.
 *
 * \param[in]  p_body   NUL-terminated URL-encoded form body string.
 * \param[in]  p_key    NUL-terminated field name to search for.
 * \param[out] p_out    Destination buffer for the decoded value.
 * \param[in]  out_len  Size of \p p_out in bytes including the NUL terminator.
 *
 * \return \c ESP_OK when the key was found and decoded,
 *         \c ESP_ERR_NOT_FOUND when the key is absent.
 */
static esp_err_t http_srv_form_field_get(const char * p_body, const char * p_key, char * p_out,
    size_t out_len)
{
    if ((NULL == p_body) || (NULL == p_key) || (NULL == p_out) || (0U == out_len))
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t       key_len = strlen(p_key);
    const char * p_pos   = p_body;

    while (NULL != p_pos)
    {
        if ((0 == strncmp(p_pos, p_key, key_len)) && ('=' == p_pos[key_len]))
        {
            const char * p_val_start = p_pos + key_len + 1U;
            const char * p_amp       = strchr(p_val_start, '&');
            size_t raw_len = (NULL == p_amp) ? strlen(p_val_start) : (size_t)(p_amp - p_val_start);

            /* Cap the raw length to protect the shared static decode buffer. */
            static char raw[HTTP_SRV_FORM_VALUE_ENC_MAX_LEN];
            if (raw_len > sizeof(raw) - 1U)
            {
                raw_len = sizeof(raw) - 1U;
            }
            memcpy(raw, p_val_start, raw_len);
            raw[raw_len] = '\0';

            http_srv_url_decode(raw, p_out, out_len);
            return ESP_OK;
        }

        p_pos = strchr(p_pos, '&');
        if (NULL != p_pos)
        {
            p_pos++; /* skip the '&' separator */
        }
    }

    p_out[0] = '\0';
    return ESP_ERR_NOT_FOUND;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief HTML-encode a string value for safe insertion into a double-quoted attribute.
 *
 * Replaces \c & with \c &amp;, \c " with \c &quot;, \c < with \c &lt;,
 * and \c > with \c &gt;.  The output is NUL-terminated and never written
 * past \p dst_len bytes.
 *
 * \param[in]  p_src   NUL-terminated plain text source string.
 * \param[out] p_dst   Destination buffer.
 * \param[in]  dst_len Total size of \p p_dst in bytes including the NUL terminator.
 */
static void http_srv_html_attr_encode(const char * p_src, char * p_dst, size_t dst_len)
{
    if ((NULL == p_src) || (NULL == p_dst) || (0U == dst_len))
    {
        return;
    }

    size_t pos = 0U;

    while (('\0' != *p_src) && (pos < (dst_len - 1U)))
    {
        if ('&' == *p_src)
        {
            if ((pos + 5U) < dst_len)
            {
                memcpy(p_dst + pos, "&amp;", 5U);
                pos += 5U;
            }
            else
            {
                break;
            }
        }
        else if ('"' == *p_src)
        {
            if ((pos + 6U) < dst_len)
            {
                memcpy(p_dst + pos, "&quot;", 6U);
                pos += 6U;
            }
            else
            {
                break;
            }
        }
        else if ('<' == *p_src)
        {
            if ((pos + 4U) < dst_len)
            {
                memcpy(p_dst + pos, "&lt;", 4U);
                pos += 4U;
            }
            else
            {
                break;
            }
        }
        else if ('>' == *p_src)
        {
            if ((pos + 4U) < dst_len)
            {
                memcpy(p_dst + pos, "&gt;", 4U);
                pos += 4U;
            }
            else
            {
                break;
            }
        }
        else
        {
            p_dst[pos] = *p_src;
            pos++;
        }
        p_src++;
    }

    p_dst[pos] = '\0';
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /.
 *
 * Allocates a #HTTP_SRV_HTML_BUF_LEN heap buffer and builds a self-contained
 * HTML status dashboard via multiple #httpd_resp_sendstr_chunk() calls.
 * Dynamic sections are formatted with \c snprintf into the shared buffer
 * before each chunk is sent.  The page auto-refreshes every 5 seconds.
 *
 * Sections included: System (time, NTP, uptime), Wi-Fi, Exercise Counter,
 * Current Session, Session History (last 20), Session Graphs (two SVG bar
 * charts over a 31-day window), Export Controls, and Navigation.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_root_get_handler(httpd_req_t * p_req)
{
    /* ---- Heap allocations ---- */
    char *                 p_buf   = malloc(HTTP_SRV_HTML_BUF_LEN);
    session_trk_record_t * p_hist  = malloc(HTTP_SRV_HIST_MAX * sizeof(*p_hist));
    session_trk_record_t * p_graph = malloc(HTTP_SRV_GRAPH_MAX * sizeof(*p_graph));
    http_srv_daily_bin_t * p_bins  = malloc(HTTP_SRV_DAILY_WINDOW_DAYS * sizeof(*p_bins));

    if ((NULL == p_buf) || (NULL == p_hist) || (NULL == p_graph) || (NULL == p_bins))
    {
        free(p_buf);
        free(p_hist);
        free(p_graph);
        free(p_bins);
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    /* ---- Gather live state ---- */
    time_t    now_utc = (time_t)time_mngr_utc_get();
    struct tm tm_local;
    localtime_r(&now_utc, &tm_local);

    char time_local_str[24];
    strftime(time_local_str, sizeof(time_local_str), "%Y-%m-%dT%H:%M:%S", &tm_local);

    int64_t uptime_s = (int64_t)(esp_timer_get_time() / 1000000LL);
    int32_t up_d     = (int32_t)(uptime_s / 86400LL);
    int32_t up_h     = (int32_t)((uptime_s % 86400LL) / 3600LL);
    int32_t up_m     = (int32_t)((uptime_s % 3600LL) / 60LL);
    int32_t up_s_rem = (int32_t)(uptime_s % 60LL);

    bool    b_synced   = time_mngr_is_synced();
    bool    b_sta      = wifi_mngr_sta_is_connected();
    bool    b_cfg_ap   = wifi_mngr_config_ap_is_active();
    bool    b_rew_ap   = wifi_mngr_reward_ap_is_active();
    uint8_t ap_clients = wifi_mngr_reward_ap_client_count();

    uint32_t counter_s = time_ctr_get();
    uint32_t threshold = config_mngr_soft_ap_start_threshold_s_get();
    uint32_t ctr_h     = counter_s / 3600U;
    uint32_t ctr_m     = (counter_s % 3600U) / 60U;
    uint32_t ctr_s_r   = counter_s % 60U;

    char sta_ip[20];
    char sta_ssid[33];
    char rew_ap_ssid[33];
    wifi_mngr_sta_ip_get(sta_ip, sizeof(sta_ip));
    config_mngr_wifi_ssid_get(sta_ssid, sizeof(sta_ssid));
    config_mngr_soft_ap_ssid_get(rew_ap_ssid, sizeof(rew_ap_ssid));

    session_trk_live_status_t sess;
    session_trk_live_status_get(&sess);

    uint16_t spd_int = (uint16_t)(sess.live_speed_kmh_x10 / 10U);
    uint16_t spd_dec = (uint16_t)(sess.live_speed_kmh_x10 % 10U);
    uint32_t sess_h  = sess.duration_s / 3600U;
    uint32_t sess_m  = (sess.duration_s % 3600U) / 60U;
    uint32_t sess_sr = sess.duration_s % 60U;

    /* ---- Read session log ---- */
    uint16_t hist_count  = session_log_read(p_hist, HTTP_SRV_HIST_MAX);
    uint16_t graph_count = session_log_read(p_graph, HTTP_SRV_GRAPH_MAX);

    /* ---- Build 31-day bins (identical logic to /api/sessions/daily) ---- */
    memset(p_bins, 0, HTTP_SRV_DAILY_WINDOW_DAYS * sizeof(*p_bins));

    struct tm today = tm_local;
    today.tm_hour   = 0;
    today.tm_min    = 0;
    today.tm_sec    = 0;
    (void)mktime(&today);

    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        struct tm day_tm = today;
        day_tm.tm_mday -= (int)(HTTP_SRV_DAILY_WINDOW_DAYS - 1U) - d;
        (void)mktime(&day_tm);
        p_bins[d].year = day_tm.tm_year;
        p_bins[d].mon  = day_tm.tm_mon;
        p_bins[d].mday = day_tm.tm_mday;
    }

    for (uint16_t i = 0U; i < graph_count; i++)
    {
        time_t    t = (time_t)p_graph[i].start_time_utc;
        struct tm sess_tm;
        localtime_r(&t, &sess_tm);

        for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
        {
            if ((p_bins[d].year == sess_tm.tm_year) && (p_bins[d].mon == sess_tm.tm_mon) &&
                (p_bins[d].mday == sess_tm.tm_mday))
            {
                p_bins[d].sessions++;
                p_bins[d].speed_sum_x10 += (uint64_t)p_graph[i].avg_speed_kmh_x10;
                p_bins[d].total_duration_s += p_graph[i].duration_s;
                break;
            }
        }
    }

    /* Find Y-axis maxima for both charts (minimum 1 to avoid div-by-zero). */
    uint32_t max_speed_x10 = 1U;
    uint32_t max_dur_min   = 1U;

    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        uint32_t avg_spd = (0U < p_bins[d].sessions) ?
                               (uint32_t)(p_bins[d].speed_sum_x10 / (uint64_t)p_bins[d].sessions) :
                               0U;
        uint32_t dur_min = p_bins[d].total_duration_s / 60U;
        if (avg_spd > max_speed_x10)
        {
            max_speed_x10 = avg_spd;
        }
        if (dur_min > max_dur_min)
        {
            max_dur_min = dur_min;
        }
    }

    /* ---- Send response via chunks ---- */
    httpd_resp_set_type(p_req, "text/html");

    /* Static HTML header */
    static const char sc_page_hdr[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"5\">"
        "<title>esport-fi32 Dashboard</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:720px;margin:1em auto;padding:0 .8em;"
        "font-size:14px}"
        "h2{margin:.4em 0}h3{margin:.6em 0 .2em}"
        "table{border-collapse:collapse;width:100%}"
        "th,td{border:1px solid #bbb;padding:3px 6px;text-align:left}"
        "th{background:#eee}"
        ".ok{color:#1a7f1a}.err{color:#c0392b}"
        ".card{background:#f9f9f9;border:1px solid #ddd;border-radius:4px;"
        "padding:.5em .8em;margin:.5em 0}"
        "a.btn{display:inline-block;padding:4px 12px;background:#4a90d9;color:#fff;"
        "border-radius:3px;text-decoration:none;margin-right:.5em}"
        "svg{display:block;width:100%;height:auto}"
        "</style></head><body>"
        "<h2>esport-fi32 &mdash; Status Dashboard</h2>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_page_hdr);

    /* System section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>System</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>Time:&nbsp;<b>%s</b></p>"
        "<p>NTP:&nbsp;<span class=\"%s\">%s</span></p>"
        "<p>Uptime:&nbsp;<b>%" PRId32 "d&nbsp;%" PRId32 "h&nbsp;%" PRId32 "m&nbsp;%" PRId32
        "s</b></p></div>",
        time_local_str, b_synced ? "ok" : "err", b_synced ? "Synced" : "Not synced", up_d, up_h,
        up_m, up_s_rem);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Wi-Fi section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Wi-Fi</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>STA:&nbsp;<span class=\"%s\">%s</span>"
        "&nbsp; SSID:&nbsp;<b>%s</b>"
        "&nbsp; IP:&nbsp;<b>%s</b></p>"
        "<p>Config AP:&nbsp;<span class=\"%s\">%s</span></p>"
        "<p>Reward AP:&nbsp;<span class=\"%s\">%s</span>"
        "&nbsp; SSID:&nbsp;<b>%s</b>"
        "&nbsp; Clients:&nbsp;<b>%" PRIu8 "</b></p></div>",
        b_sta ? "ok" : "err", b_sta ? "Connected" : "Disconnected", sta_ssid, sta_ip,
        b_cfg_ap ? "ok" : "err", b_cfg_ap ? "Active" : "Inactive", b_rew_ap ? "ok" : "err",
        b_rew_ap ? "Active" : "Inactive", rew_ap_ssid, ap_clients);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Exercise Counter section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Exercise Counter</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>Counter:&nbsp;<b>%" PRIu32 "s</b>"
        "&nbsp;(%" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ")"
        "&nbsp; Threshold:&nbsp;<b>%" PRIu32 "s</b></p>"
        "<p>Reward AP:&nbsp;<span class=\"%s\">%s</span></p></div>",
        counter_s, ctr_h, ctr_m, ctr_s_r, threshold, b_rew_ap ? "ok" : "err",
        b_rew_ap ? "Active" : "Inactive");
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Current Session section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Current Session</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>State:&nbsp;<b>%s</b></p>"
        "<p>Live Speed:&nbsp;<b>%" PRIu16 ".%" PRIu16 "&nbsp;km/h</b></p>"
        "<p>Duration:&nbsp;<b>%" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "</b></p></div>",
        sess.p_state_name, spd_int, spd_dec, sess_h, sess_m, sess_sr);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Session History table */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Session History (last 20)</h3>"
                                          "<table><tr><th>Start (local)</th><th>Duration</th>"
                                          "<th>Avg Speed (km/h)</th><th>Pulses</th></tr>");

    for (uint16_t i = 0U; i < hist_count; i++)
    {
        const session_trk_record_t * p_rec = &p_hist[i];
        time_t                       t     = (time_t)p_rec->start_time_utc;
        struct tm                    tm_rec;
        localtime_r(&t, &tm_rec);

        char local_str[24];
        strftime(local_str, sizeof(local_str), "%Y-%m-%dT%H:%M:%S", &tm_rec);

        uint32_t d_h = p_rec->duration_s / 3600U;
        uint32_t d_m = (p_rec->duration_s % 3600U) / 60U;
        uint32_t d_s = p_rec->duration_s % 60U;
        uint16_t s_i = (uint16_t)(p_rec->avg_speed_kmh_x10 / 10U);
        uint16_t s_d = (uint16_t)(p_rec->avg_speed_kmh_x10 % 10U);

        snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
            "<tr><td>%s%s</td>"
            "<td>%" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "</td>"
            "<td>%" PRIu16 ".%" PRIu16 "</td>"
            "<td>%" PRIu32 "</td></tr>",
            local_str, p_rec->b_time_synced ? "" : " (*)", d_h, d_m, d_s, s_i, s_d,
            p_rec->pulse_count);
        (void)httpd_resp_sendstr_chunk(p_req, p_buf);
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</table></div>");

    /* ---- SVG Chart 1: Daily Average Speed ---- */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<div class=\"card\"><h3>Daily Average Speed (km/h)</h3>"
        "<svg viewBox=\"0 0 620 200\" xmlns=\"http://www.w3.org/2000/svg\">");

    /* Y-axis and X-axis lines, Y labels */
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#555\" stroke-width=\"1\"/>"
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#555\" stroke-width=\"1\"/>"
        "<text x=\"%d\" y=\"%d\" font-size=\"10\" text-anchor=\"end\""
        " fill=\"#333\">%.1f</text>"
        "<text x=\"%d\" y=\"%d\" font-size=\"10\" text-anchor=\"end\""
        " fill=\"#333\">0</text>",
        HTTP_SRV_SVG_LM, HTTP_SRV_SVG_TOP, HTTP_SRV_SVG_LM, HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM,
        HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM + HTTP_SRV_SVG_CW, HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM - 3,
        HTTP_SRV_SVG_TOP + 4, (float)max_speed_x10 / 10.0f, HTTP_SRV_SVG_LM - 3, HTTP_SRV_SVG_BOT);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        uint32_t avg_spd = (0U < p_bins[d].sessions) ?
                               (uint32_t)(p_bins[d].speed_sum_x10 / (uint64_t)p_bins[d].sessions) :
                               0U;
        int      bar_h   = (int)(avg_spd * (uint32_t)HTTP_SRV_SVG_CH / max_speed_x10);
        int      bx      = HTTP_SRV_SVG_LM + d * HTTP_SRV_SVG_SLOT + HTTP_SRV_SVG_BPAD;
        int      bw      = HTTP_SRV_SVG_SLOT - 2 * HTTP_SRV_SVG_BPAD;
        int      by      = HTTP_SRV_SVG_BOT - bar_h;

        snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
            "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#4a90d9\"/>"
            "<text x=\"%d\" y=\"%d\" font-size=\"8\" text-anchor=\"middle\""
            " fill=\"#555\">%d</text>",
            bx, by, bw, bar_h, bx + bw / 2, HTTP_SRV_SVG_BOT + 11, p_bins[d].mday);
        (void)httpd_resp_sendstr_chunk(p_req, p_buf);
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</svg></div>");

    /* ---- SVG Chart 2: Daily Total Duration (minutes) ---- */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<div class=\"card\"><h3>Daily Total Duration (minutes)</h3>"
        "<svg viewBox=\"0 0 620 200\" xmlns=\"http://www.w3.org/2000/svg\">");

    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#555\" stroke-width=\"1\"/>"
        "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\""
        " stroke=\"#555\" stroke-width=\"1\"/>"
        "<text x=\"%d\" y=\"%d\" font-size=\"10\" text-anchor=\"end\""
        " fill=\"#333\">%" PRIu32 "</text>"
        "<text x=\"%d\" y=\"%d\" font-size=\"10\" text-anchor=\"end\""
        " fill=\"#333\">0</text>",
        HTTP_SRV_SVG_LM, HTTP_SRV_SVG_TOP, HTTP_SRV_SVG_LM, HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM,
        HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM + HTTP_SRV_SVG_CW, HTTP_SRV_SVG_BOT, HTTP_SRV_SVG_LM - 3,
        HTTP_SRV_SVG_TOP + 4, max_dur_min, HTTP_SRV_SVG_LM - 3, HTTP_SRV_SVG_BOT);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        uint32_t dur_min = p_bins[d].total_duration_s / 60U;
        int      bar_h   = (int)(dur_min * (uint32_t)HTTP_SRV_SVG_CH / max_dur_min);
        int      bx      = HTTP_SRV_SVG_LM + d * HTTP_SRV_SVG_SLOT + HTTP_SRV_SVG_BPAD;
        int      bw      = HTTP_SRV_SVG_SLOT - 2 * HTTP_SRV_SVG_BPAD;
        int      by      = HTTP_SRV_SVG_BOT - bar_h;

        snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
            "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#3aa76d\"/>"
            "<text x=\"%d\" y=\"%d\" font-size=\"8\" text-anchor=\"middle\""
            " fill=\"#555\">%d</text>",
            bx, by, bw, bar_h, bx + bw / 2, HTTP_SRV_SVG_BOT + 11, p_bins[d].mday);
        (void)httpd_resp_sendstr_chunk(p_req, p_buf);
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</svg></div>");

    /* Export Controls */
    static const char sc_export[] = "<div class=\"card\"><h3>Export Reports</h3>"
                                    "<a class=\"btn\" href=\"/api/sessions/export?format=csv\">"
                                    "Download CSV</a>"
                                    "<a class=\"btn\" href=\"/api/sessions/export?format=json\">"
                                    "Download JSON</a></div>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_export);

    /* Navigation */
    static const char sc_nav[] = "<div class=\"card\">"
                                 "<a href=\"/config\">&#9881; Go to Configuration</a>"
                                 "</div></body></html>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_nav);

    /* Terminate chunked response */
    (void)httpd_resp_sendstr_chunk(p_req, NULL);

    free(p_buf);
    free(p_hist);
    free(p_graph);
    free(p_bins);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /config.
 *
 * Reads all 11 configuration parameters and sends an HTML form pre-populated
 * with their current values.  If the query string contains \c saved=1 a
 * success banner is prepended to the form body.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_config_get_handler(httpd_req_t * p_req)
{
    /* Check for ?saved=1 query parameter. */
    bool b_saved = false;
    char query_buf[32];
    if (ESP_OK == httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        char saved_val[4];
        if (ESP_OK == httpd_query_key_value(query_buf, "saved", saved_val, sizeof(saved_val)))
        {
            b_saved = (0 == strcmp(saved_val, "1"));
        }
    }

    /* Read all current configuration values. */
    static char wifi_ssid[33];
    static char wifi_pwd[65];
    static char ap_ssid[33];
    static char ap_pwd[65];
    uint16_t    spp         = config_mngr_seconds_per_pulse_get();
    uint32_t    ap_thresh   = config_mngr_soft_ap_start_threshold_s_get();
    uint32_t    cpp         = config_mngr_centimeters_per_pulse_get();
    uint16_t    idle_s      = config_mngr_idle_session_interval_s_get();
    uint16_t    start_s     = config_mngr_start_session_interval_s_get();
    uint16_t    debounce_ms = config_mngr_pulse_debounce_time_ms_get();
    static char tz[64];

    config_mngr_wifi_ssid_get(wifi_ssid, sizeof(wifi_ssid));
    config_mngr_wifi_password_get(wifi_pwd, sizeof(wifi_pwd));
    config_mngr_soft_ap_ssid_get(ap_ssid, sizeof(ap_ssid));
    config_mngr_soft_ap_password_get(ap_pwd, sizeof(ap_pwd));
    config_mngr_timezone_get(tz, sizeof(tz));

    /*
     * Single shared encoding buffer — re-used for each string field so that
     * only HTTP_SRV_ATTR_ENC_LEN bytes are allocated on the stack instead of
     * five separate buffers.  Each field is sent as three chunks:
     *   1. static HTML prefix  2. encoded value  3. static HTML suffix
     */
    static char enc[HTTP_SRV_ATTR_ENC_LEN];
    /* Numeric fields use a small scratch buffer for the snprintf value. */
    static char num[16];

    httpd_resp_set_type(p_req, "text/html");

    /* ---- Static header ---- */
    static const char sc_header[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>esport-fi32 Configuration</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:640px;margin:2em auto;padding:0 1em}"
        "h2{margin-bottom:.5em}"
        "label{display:inline-block;width:230px;font-weight:bold;vertical-align:middle}"
        "input[type=text],input[type=password],input[type=number]"
        "{padding:4px;width:230px;box-sizing:border-box}"
        "p{margin:.4em 0}"
        ".saved{background:#dfd;padding:.5em 1em;border:1px solid #6a6;"
        "border-radius:4px;margin-bottom:1em}"
        "</style></head><body>"
        "<h2>esport-fi32 &mdash; Configuration</h2>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_header);

    if (b_saved)
    {
        (void)httpd_resp_sendstr_chunk(p_req,
            "<p class=\"saved\">&#10003; Configuration saved successfully.</p>");
    }

    (void)httpd_resp_sendstr_chunk(p_req, "<form method=\"POST\" action=\"/config\">");

    /* ---- String fields (prefix / encoded-value / suffix chunks) ---- */

    /* wifi_ssid */
    http_srv_html_attr_encode(wifi_ssid, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req, "<p><label>Home Wi-Fi SSID</label>"
                                          "<input type=\"text\" name=\"wifi_ssid\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, enc);
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"32\"></p>");

    /* wifi_password */
    http_srv_html_attr_encode(wifi_pwd, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Home Wi-Fi Password</label>"
        "<input type=\"password\" name=\"wifi_password\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, enc);
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"64\"></p>");

    /* soft_ap_ssid */
    http_srv_html_attr_encode(ap_ssid, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req, "<p><label>Reward AP SSID</label>"
                                          "<input type=\"text\" name=\"soft_ap_ssid\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, enc);
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"32\"></p>");

    /* soft_ap_password */
    http_srv_html_attr_encode(ap_pwd, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Reward AP Password</label>"
        "<input type=\"password\" name=\"soft_ap_password\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, enc);
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"64\"></p>");

    /* ---- Numeric fields ---- */

    /* seconds_per_pulse */
    snprintf(num, sizeof(num), "%" PRIu16, spp);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Seconds per Pulse</label>"
        "<input type=\"number\" name=\"seconds_per_pulse\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\" max=\"60\"></p>");

    /* soft_ap_start_threshold_s */
    snprintf(num, sizeof(num), "%" PRIu32, ap_thresh);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Counter Threshold (s)</label>"
        "<input type=\"number\" name=\"soft_ap_start_threshold_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\"></p>");

    /* centimeters_per_pulse */
    snprintf(num, sizeof(num), "%" PRIu32, cpp);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Centimeters per Pulse</label>"
        "<input type=\"number\" name=\"centimeters_per_pulse\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\"></p>");

    /* idle_session_interval_s */
    snprintf(num, sizeof(num), "%" PRIu16, idle_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Idle Session Timeout (s)</label>"
        "<input type=\"number\" name=\"idle_session_interval_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"5\" max=\"600\"></p>");

    /* start_session_interval_s */
    snprintf(num, sizeof(num), "%" PRIu16, start_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Session Start Window (s)</label>"
        "<input type=\"number\" name=\"start_session_interval_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\" max=\"300\"></p>");

    /* pulse_debounce_time_ms */
    snprintf(num, sizeof(num), "%" PRIu16, debounce_ms);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Pulse Debounce (ms)</label>"
        "<input type=\"number\" name=\"pulse_debounce_time_ms\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"10\" max=\"5000\"></p>");

    /* timezone */
    http_srv_html_attr_encode(tz, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req, "<p><label>Timezone (POSIX TZ)</label>"
                                          "<input type=\"text\" name=\"timezone\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, enc);
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"63\"></p>");

    /* ---- Footer ---- */
    static const char sc_footer[] = "<p style=\"margin-top:1em\">"
                                    "<input type=\"submit\" value=\"Save Configuration\"></p>"
                                    "</form></body></html>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_footer);

    /* Terminate chunked response. */
    (void)httpd_resp_sendstr_chunk(p_req, NULL);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c POST /config.
 *
 * Reads the URL-encoded request body (up to #HTTP_SRV_POST_BODY_MAX_LEN
 * bytes), validates each of the 11 configuration fields present in the body,
 * and applies them via the config_manager setters.  Rejects string values
 * exceeding the spec-maximum length with HTTP 400.  On success, issues an
 * HTTP 302 redirect to \c /config?saved=1.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_config_post_handler(httpd_req_t * p_req)
{
    /* Read request body into a stack buffer. */
    int content_len = (int)p_req->content_len;
    if (content_len > (int)(HTTP_SRV_POST_BODY_MAX_LEN - 1U))
    {
        content_len = (int)(HTTP_SRV_POST_BODY_MAX_LEN - 1U);
    }

    static char body[HTTP_SRV_POST_BODY_MAX_LEN];
    int         total_read = 0;

    while (total_read < content_len)
    {
        int n = httpd_req_recv(p_req, body + total_read, (size_t)(content_len - total_read));
        if (n <= 0)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Failed to read body");
            return ESP_FAIL;
        }
        total_read += n;
    }
    body[total_read] = '\0';

    /* ---- Process each field ---- */

    /* wifi_ssid (string, max 32) */
    char field_str[65]; /* large enough for any string field */
    if (ESP_OK == http_srv_form_field_get(body, "wifi_ssid", field_str, sizeof(field_str)))
    {
        if (strlen(field_str) > 32U)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "wifi_ssid exceeds maximum length of 32");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_wifi_ssid_set(field_str))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid wifi_ssid");
            return ESP_FAIL;
        }
    }

    /* wifi_password (string, max 64) */
    if (ESP_OK == http_srv_form_field_get(body, "wifi_password", field_str, sizeof(field_str)))
    {
        if (strlen(field_str) > 64U)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "wifi_password exceeds maximum length of 64");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_wifi_password_set(field_str))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid wifi_password");
            return ESP_FAIL;
        }
    }

    /* soft_ap_ssid (string, max 32) */
    if (ESP_OK == http_srv_form_field_get(body, "soft_ap_ssid", field_str, sizeof(field_str)))
    {
        if (strlen(field_str) > 32U)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_ap_ssid exceeds maximum length of 32");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_soft_ap_ssid_set(field_str))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid soft_ap_ssid");
            return ESP_FAIL;
        }
    }

    /* soft_ap_password (string, max 64) */
    if (ESP_OK == http_srv_form_field_get(body, "soft_ap_password", field_str, sizeof(field_str)))
    {
        if (strlen(field_str) > 64U)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_ap_password exceeds maximum length of 64");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_soft_ap_password_set(field_str))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid soft_ap_password");
            return ESP_FAIL;
        }
    }

    /* seconds_per_pulse (uint16, range 1–60) */
    char num_str[16];
    if (ESP_OK == http_srv_form_field_get(body, "seconds_per_pulse", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "seconds_per_pulse must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_seconds_per_pulse_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "seconds_per_pulse out of range (1-60)");
            return ESP_FAIL;
        }
    }

    /* soft_ap_start_threshold_s (uint32) */
    if (ESP_OK ==
        http_srv_form_field_get(body, "soft_ap_start_threshold_s", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if ('\0' != *endptr)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_ap_start_threshold_s must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_soft_ap_start_threshold_s_set((uint32_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid soft_ap_start_threshold_s");
            return ESP_FAIL;
        }
    }

    /* centimeters_per_pulse (uint32, min 1) */
    if (ESP_OK == http_srv_form_field_get(body, "centimeters_per_pulse", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if ('\0' != *endptr)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "centimeters_per_pulse must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_centimeters_per_pulse_set((uint32_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "centimeters_per_pulse out of range (min 1)");
            return ESP_FAIL;
        }
    }

    /* idle_session_interval_s (uint16, range 5–600) */
    if (ESP_OK ==
        http_srv_form_field_get(body, "idle_session_interval_s", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "idle_session_interval_s must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_idle_session_interval_s_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "idle_session_interval_s out of range (5-600)");
            return ESP_FAIL;
        }
    }

    /* start_session_interval_s (uint16, range 1–300) */
    if (ESP_OK ==
        http_srv_form_field_get(body, "start_session_interval_s", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "start_session_interval_s must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_start_session_interval_s_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "start_session_interval_s out of range (1-300)");
            return ESP_FAIL;
        }
    }

    /* pulse_debounce_time_ms (uint16, range 10–5000) */
    if (ESP_OK == http_srv_form_field_get(body, "pulse_debounce_time_ms", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "pulse_debounce_time_ms must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_pulse_debounce_time_ms_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "pulse_debounce_time_ms out of range (10-5000)");
            return ESP_FAIL;
        }
    }

    /* timezone (string, max 63); apply immediately if present */
    bool b_tz_changed = false;
    if (ESP_OK == http_srv_form_field_get(body, "timezone", field_str, sizeof(field_str)))
    {
        if (strlen(field_str) > 63U)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "timezone exceeds maximum length of 63");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_timezone_set(field_str))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid timezone string");
            return ESP_FAIL;
        }
        b_tz_changed = true;
    }

    if (b_tz_changed)
    {
        time_mngr_timezone_apply();
    }

    /* Redirect to /config?saved=1 on success. */
    httpd_resp_set_status(p_req, "302 Found");
    httpd_resp_set_hdr(p_req, "Location", "/config?saved=1");
    httpd_resp_send(p_req, NULL, 0);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /api/status.
 *
 * Allocates a heap buffer, builds a JSON object with live system state from
 * all subsystems, and sends it as an \c application/json response.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_api_status_handler(httpd_req_t * p_req)
{
    char * p_buf = malloc(HTTP_SRV_JSON_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    /* Gather live state. */
    time_t    now_utc = (time_t)time_mngr_utc_get();
    struct tm tm_local;
    localtime_r(&now_utc, &tm_local);

    char time_local_str[24];
    strftime(time_local_str, sizeof(time_local_str), "%Y-%m-%dT%H:%M:%S", &tm_local);

    int64_t  uptime_s   = (int64_t)(esp_timer_get_time() / 1000000LL);
    bool     b_synced   = time_mngr_is_synced();
    bool     b_sta      = wifi_mngr_sta_is_connected();
    bool     b_cfg_ap   = wifi_mngr_config_ap_is_active();
    bool     b_rew_ap   = wifi_mngr_reward_ap_is_active();
    uint8_t  ap_clients = wifi_mngr_reward_ap_client_count();
    uint32_t counter_s  = time_ctr_get();
    uint32_t threshold  = config_mngr_soft_ap_start_threshold_s_get();

    char sta_ip[20];
    char sta_ssid[33];
    char rew_ap_ssid[33];
    wifi_mngr_sta_ip_get(sta_ip, sizeof(sta_ip));
    config_mngr_wifi_ssid_get(sta_ssid, sizeof(sta_ssid));
    config_mngr_soft_ap_ssid_get(rew_ap_ssid, sizeof(rew_ap_ssid));

    session_trk_live_status_t sess;
    session_trk_live_status_get(&sess);

    int n = snprintf(p_buf, HTTP_SRV_JSON_BUF_LEN,
        "{\n"
        "  \"time_utc\": %" PRId64 ",\n"
        "  \"time_local\": \"%s\",\n"
        "  \"time_synced\": %s,\n"
        "  \"uptime_s\": %" PRId64 ",\n"
        "  \"sta_connected\": %s,\n"
        "  \"sta_ssid\": \"%s\",\n"
        "  \"sta_ip\": \"%s\",\n"
        "  \"config_ap_active\": %s,\n"
        "  \"reward_ap_active\": %s,\n"
        "  \"reward_ap_ssid\": \"%s\",\n"
        "  \"reward_ap_clients\": %" PRIu8 ",\n"
        "  \"counter_s\": %" PRIu32 ",\n"
        "  \"threshold_s\": %" PRIu32 ",\n"
        "  \"session_state\": \"%s\",\n"
        "  \"session_start_utc\": %" PRId64 ",\n"
        "  \"session_duration_s\": %" PRIu32 ",\n"
        "  \"session_pulse_count\": %" PRIu32 ",\n"
        "  \"live_speed_kmh_x10\": %" PRIu16 "\n"
        "}\n",
        (int64_t)now_utc, time_local_str, b_synced ? "true" : "false", uptime_s,
        b_sta ? "true" : "false", sta_ssid, sta_ip, b_cfg_ap ? "true" : "false",
        b_rew_ap ? "true" : "false", rew_ap_ssid, ap_clients, counter_s, threshold,
        sess.p_state_name, sess.start_utc, sess.duration_s, sess.pulse_count,
        sess.live_speed_kmh_x10);

    if (n >= (int)HTTP_SRV_JSON_BUF_LEN)
    {
        ESP_LOGW(gp_tag, "api/status JSON truncated");
    }

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /api/sessions.
 *
 * Returns up to #SESSION_LOG_MAX_ENTRIES sessions as a JSON array (newest
 * first).  Each entry includes \c start_utc, \c start_local (ISO-8601 in
 * local time), \c time_synced, \c duration_s, \c pulse_count, and
 * \c avg_speed_kmh_x10.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_api_sessions_handler(httpd_req_t * p_req)
{
    session_trk_record_t * p_sessions = malloc(SESSION_LOG_MAX_ENTRIES * sizeof(*p_sessions));
    if (NULL == p_sessions)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    uint16_t count = session_log_read(p_sessions, SESSION_LOG_MAX_ENTRIES);

    char * p_buf = malloc(HTTP_SRV_JSON_BUF_LEN);
    if (NULL == p_buf)
    {
        free(p_sessions);
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    p_buf[0] = '[';
    int pos  = 1;

    for (uint16_t i = 0U; i < count; i++)
    {
        const session_trk_record_t * p_rec = &p_sessions[i];
        time_t                       t     = (time_t)p_rec->start_time_utc;
        struct tm                    tm_local;
        localtime_r(&t, &tm_local);

        char local_str[24];
        strftime(local_str, sizeof(local_str), "%Y-%m-%dT%H:%M:%S", &tm_local);

        char entry[HTTP_SRV_ENTRY_BUF_LEN];
        int  n = snprintf(entry, sizeof(entry),
             "%s{\"start_utc\":%" PRId64 ","
              "\"start_local\":\"%s\","
              "\"time_synced\":%s,"
              "\"duration_s\":%" PRIu32 ","
              "\"pulse_count\":%" PRIu32 ","
              "\"avg_speed_kmh_x10\":%" PRIu16 "}",
            (0 == i) ? "" : ",", (int64_t)p_rec->start_time_utc, local_str,
            p_rec->b_time_synced ? "true" : "false", p_rec->duration_s, p_rec->pulse_count,
             p_rec->avg_speed_kmh_x10);

        if ((n > 0) && ((pos + n + 2) < (int)HTTP_SRV_JSON_BUF_LEN))
        {
            memcpy(p_buf + pos, entry, (size_t)n);
            pos += n;
        }
        else
        {
            ESP_LOGW(gp_tag, "api/sessions JSON buffer full after %u entries", (unsigned)i);
            break;
        }
    }

    p_buf[pos]     = ']';
    p_buf[pos + 1] = '\0';

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    free(p_sessions);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Build and send a CSV export response for the session log.
 *
 * Writes the spec §6.5 header row followed by one CSV row per entry in
 * \p p_sessions.  Sets \c Content-Type: text/csv and a
 * \c Content-Disposition: attachment header with a date-stamped filename.
 *
 * \param[in] p_req       Incoming HTTP request.
 * \param[in] p_sessions  Array of session records to export.
 * \param[in] count       Number of valid entries in \p p_sessions.
 * \param[in] p_date_str  Date stamp string (YYYYMMDD) for the filename.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_export_csv_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str)
{
    uint32_t cpp = config_mngr_centimeters_per_pulse_get();

    char * p_buf = malloc(HTTP_SRV_JSON_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    char cd_val[72];
    snprintf(cd_val, sizeof(cd_val), "attachment; filename=\"esport-fi32-sessions-%s.csv\"",
        p_date_str);

    static const char sc_csv_header[] = "start_utc,start_local,time_synced,duration_s,pulse_count,"
                                        "avg_speed_kmh,distance_m\r\n";

    int pos = snprintf(p_buf, HTTP_SRV_JSON_BUF_LEN, "%s", sc_csv_header);

    for (uint16_t i = 0U; i < count; i++)
    {
        const session_trk_record_t * p_rec = &p_sessions[i];
        time_t                       t     = (time_t)p_rec->start_time_utc;
        struct tm                    tm_rec;
        localtime_r(&t, &tm_rec);

        char local_str[24];
        strftime(local_str, sizeof(local_str), "%Y-%m-%dT%H:%M:%S", &tm_rec);

        uint16_t spd_int  = (uint16_t)(p_rec->avg_speed_kmh_x10 / 10U);
        uint16_t spd_dec  = (uint16_t)(p_rec->avg_speed_kmh_x10 % 10U);
        uint64_t dist_cm  = (uint64_t)p_rec->pulse_count * (uint64_t)cpp;
        uint32_t dist_m_i = (uint32_t)(dist_cm / 100ULL);
        uint32_t dist_m_d = (uint32_t)(dist_cm % 100ULL);

        char row[HTTP_SRV_ENTRY_BUF_LEN];
        int  n = snprintf(row, sizeof(row),
             "%" PRId64 ",%s,%s,%" PRIu32 ",%" PRIu32 ",%" PRIu16 ".%" PRIu16 ",%" PRIu32
             ".%02" PRIu32 "\r\n",
             (int64_t)p_rec->start_time_utc, local_str, p_rec->b_time_synced ? "true" : "false",
             p_rec->duration_s, p_rec->pulse_count, spd_int, spd_dec, dist_m_i, dist_m_d);

        if ((n > 0) && ((pos + n + 1) < (int)HTTP_SRV_JSON_BUF_LEN))
        {
            memcpy(p_buf + pos, row, (size_t)n);
            pos += n;
        }
        else
        {
            ESP_LOGW(gp_tag, "export CSV buffer full after %u rows", (unsigned)i);
            break;
        }
    }
    p_buf[pos] = '\0';

    httpd_resp_set_type(p_req, "text/csv");
    httpd_resp_set_hdr(p_req, "Content-Disposition", cd_val);
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Build and send a JSON export response for the session log.
 *
 * Formats \p p_sessions as a JSON array matching the schema from spec §6.4
 * and sets a \c Content-Disposition: attachment header with a date-stamped
 * filename.
 *
 * \param[in] p_req       Incoming HTTP request.
 * \param[in] p_sessions  Array of session records to export.
 * \param[in] count       Number of valid entries in \p p_sessions.
 * \param[in] p_date_str  Date stamp string (YYYYMMDD) for the filename.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_export_json_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str)
{
    char * p_buf = malloc(HTTP_SRV_JSON_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    char cd_val[72];
    snprintf(cd_val, sizeof(cd_val), "attachment; filename=\"esport-fi32-sessions-%s.json\"",
        p_date_str);

    p_buf[0] = '[';
    int pos  = 1;

    for (uint16_t i = 0U; i < count; i++)
    {
        const session_trk_record_t * p_rec = &p_sessions[i];
        time_t                       t     = (time_t)p_rec->start_time_utc;
        struct tm                    tm_rec;
        localtime_r(&t, &tm_rec);

        char local_str[24];
        strftime(local_str, sizeof(local_str), "%Y-%m-%dT%H:%M:%S", &tm_rec);

        char entry[HTTP_SRV_ENTRY_BUF_LEN];
        int  n = snprintf(entry, sizeof(entry),
             "%s{\"start_utc\":%" PRId64 ","
              "\"start_local\":\"%s\","
              "\"time_synced\":%s,"
              "\"duration_s\":%" PRIu32 ","
              "\"pulse_count\":%" PRIu32 ","
              "\"avg_speed_kmh_x10\":%" PRIu16 "}",
            (0 == i) ? "" : ",", (int64_t)p_rec->start_time_utc, local_str,
            p_rec->b_time_synced ? "true" : "false", p_rec->duration_s, p_rec->pulse_count,
             p_rec->avg_speed_kmh_x10);

        if ((n > 0) && ((pos + n + 2) < (int)HTTP_SRV_JSON_BUF_LEN))
        {
            memcpy(p_buf + pos, entry, (size_t)n);
            pos += n;
        }
        else
        {
            ESP_LOGW(gp_tag, "export JSON buffer full after %u entries", (unsigned)i);
            break;
        }
    }
    p_buf[pos]     = ']';
    p_buf[pos + 1] = '\0';

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_set_hdr(p_req, "Content-Disposition", cd_val);
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /api/sessions/export.
 *
 * Parses the \c format query parameter (\c csv or \c json; defaults to
 * \c csv), reads the session log, and dispatches to
 * #http_srv_export_csv_send or #http_srv_export_json_send.  Invalid
 * \c format values return HTTP 400 with body \c {"error":"invalid format"}.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_api_sessions_export_handler(httpd_req_t * p_req)
{
    /* Parse "format" query parameter; default to "csv". */
    char format[16] = "csv";
    char query_buf[48];
    if (ESP_OK == httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        char format_val[16];
        if (ESP_OK == httpd_query_key_value(query_buf, "format", format_val, sizeof(format_val)))
        {
            strncpy(format, format_val, sizeof(format) - 1U);
            format[sizeof(format) - 1U] = '\0';
        }
    }

    bool b_is_csv  = (0 == strcmp(format, "csv"));
    bool b_is_json = (0 == strcmp(format, "json"));

    if (!b_is_csv && !b_is_json)
    {
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_sendstr(p_req, "{\"error\":\"invalid format\"}");
        return ESP_OK;
    }

    /* Determine today's local date for filenames. */
    time_t    now = (time_t)time_mngr_utc_get();
    struct tm tm_local;
    localtime_r(&now, &tm_local);

    char date_str[12];
    strftime(date_str, sizeof(date_str), "%Y%m%d", &tm_local);

    session_trk_record_t * p_sessions = malloc(SESSION_LOG_MAX_ENTRIES * sizeof(*p_sessions));
    if (NULL == p_sessions)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    uint16_t count = session_log_read(p_sessions, SESSION_LOG_MAX_ENTRIES);

    esp_err_t ret;
    if (b_is_csv)
    {
        ret = http_srv_export_csv_send(p_req, p_sessions, count, date_str);
    }
    else
    {
        ret = http_srv_export_json_send(p_req, p_sessions, count, date_str);
    }
    free(p_sessions);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /api/sessions/daily.
 *
 * Aggregates session log data into a 31-day window ending today (local date).
 * Days with no sessions are included with zero-filled counts.  Returns a JSON
 * object with a \c "days" array matching spec §6.6.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t http_srv_api_sessions_daily_handler(httpd_req_t * p_req)
{
    session_trk_record_t * p_sessions = malloc(SESSION_LOG_MAX_ENTRIES * sizeof(*p_sessions));
    if (NULL == p_sessions)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    uint16_t count = session_log_read(p_sessions, SESSION_LOG_MAX_ENTRIES);

    /* Build the 31-day window ending today (local time). */
    time_t    now = (time_t)time_mngr_utc_get();
    struct tm today;
    localtime_r(&now, &today);
    today.tm_hour = 0;
    today.tm_min  = 0;
    today.tm_sec  = 0;
    (void)mktime(&today); /* normalise */

    static http_srv_daily_bin_t bins[HTTP_SRV_DAILY_WINDOW_DAYS];
    memset(bins, 0, sizeof(bins));

    /* Fill in calendar dates for each bin (oldest first). */
    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        struct tm day_tm = today;
        day_tm.tm_mday -= (int)(HTTP_SRV_DAILY_WINDOW_DAYS - 1U) - d;
        (void)mktime(&day_tm); /* let libc normalise month/year roll-over */

        bins[d].year = day_tm.tm_year;
        bins[d].mon  = day_tm.tm_mon;
        bins[d].mday = day_tm.tm_mday;
    }

    /* Accumulate sessions into the matching day bin. */
    for (uint16_t i = 0U; i < count; i++)
    {
        time_t    t = (time_t)p_sessions[i].start_time_utc;
        struct tm sess_tm;
        localtime_r(&t, &sess_tm);

        for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
        {
            if ((bins[d].year == sess_tm.tm_year) && (bins[d].mon == sess_tm.tm_mon) &&
                (bins[d].mday == sess_tm.tm_mday))
            {
                bins[d].sessions++;
                bins[d].speed_sum_x10 += (uint64_t)p_sessions[i].avg_speed_kmh_x10;
                bins[d].total_duration_s += p_sessions[i].duration_s;
                break;
            }
        }
    }

    char * p_buf = malloc(HTTP_SRV_JSON_BUF_LEN);
    if (NULL == p_buf)
    {
        free(p_sessions);
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int pos = snprintf(p_buf, HTTP_SRV_JSON_BUF_LEN, "{\"days\":[");

    for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
    {
        uint16_t avg_speed = (0U < bins[d].sessions) ?
                                 (uint16_t)(bins[d].speed_sum_x10 / (uint64_t)bins[d].sessions) :
                                 0U;

        /* Format the "YYYY-MM-DD" date string for this bin. */
        struct tm day_tm = { 0 };
        day_tm.tm_year   = bins[d].year;
        day_tm.tm_mon    = bins[d].mon;
        day_tm.tm_mday   = bins[d].mday;
        (void)mktime(&day_tm);

        char day_str[12];
        strftime(day_str, sizeof(day_str), "%Y-%m-%d", &day_tm);

        char entry[HTTP_SRV_ENTRY_BUF_LEN];
        int  n = snprintf(entry, sizeof(entry),
             "%s{\"day\":\"%s\","
              "\"day_of_month\":%d,"
              "\"sessions\":%" PRIu32 ","
              "\"avg_speed_kmh_x10\":%" PRIu16 ","
              "\"total_duration_s\":%" PRIu32 "}",
            (0 == d) ? "" : ",", day_str, bins[d].mday, bins[d].sessions, avg_speed,
             bins[d].total_duration_s);

        if ((n > 0) && ((pos + n + 4) < (int)HTTP_SRV_JSON_BUF_LEN))
        {
            memcpy(p_buf + pos, entry, (size_t)n);
            pos += n;
        }
        else
        {
            ESP_LOGW(gp_tag, "daily JSON buffer full at day %d", d);
            break;
        }
    }

    int trail = snprintf(p_buf + pos, (size_t)((int)HTTP_SRV_JSON_BUF_LEN - pos), "]}\n");
    if (trail > 0)
    {
        pos += trail;
    }
    (void)pos; /* silence unused-variable warning after final snprintf */

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    free(p_sessions);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
