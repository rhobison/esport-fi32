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
#include "http_server_utils.h"
#include "http_server_config.h"
#include "http_server_api.h"

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

static esp_err_t http_srv_root_get_handler(httpd_req_t * p_req);
static esp_err_t http_srv_api_sessions_export_handler(httpd_req_t * p_req);
static esp_err_t http_srv_export_csv_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);
static esp_err_t http_srv_export_json_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);

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

    /* ---- Build 31-day bins ---- */
    memset(p_bins, 0, HTTP_SRV_DAILY_WINDOW_DAYS * sizeof(*p_bins));
    http_srv_daily_bins_build(p_bins, p_graph, graph_count);

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

/*** end of file ***/
