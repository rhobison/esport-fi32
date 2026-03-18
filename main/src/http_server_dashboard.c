/**
 * \file
 * \brief HTTP server status dashboard handler.
 *
 * \date 2026-03-15
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_dashboard.h"
#include "http_server_api.h"
#include "http_server_utils.h"

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
static const char * gp_tag = "http_srv_dashboard";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Handler for \c GET /.
 *
 * Allocates a #HTTP_SRV_HTML_BUF_LEN heap buffer and builds a self-contained
 * HTML status dashboard via multiple \c httpd_resp_sendstr_chunk() calls.
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
esp_err_t http_srv_root_get_handler(httpd_req_t * p_req)
{
    /* ---- Heap allocations ---- */
    char *                 p_buf   = malloc(HTTP_SRV_HTML_BUF_LEN);
    session_trk_record_t * p_hist  = malloc(HTTP_SRV_HIST_MAX * sizeof(*p_hist));
    session_trk_record_t * p_graph = malloc(HTTP_SRV_GRAPH_MAX * sizeof(*p_graph));
    http_srv_daily_bin_t * p_bins  = malloc(HTTP_SRV_DAILY_WINDOW_DAYS * sizeof(*p_bins));

    if ((NULL == p_buf) || (NULL == p_hist) || (NULL == p_graph) || (NULL == p_bins))
    {
        ESP_LOGE(gp_tag, "heap alloc failed");
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

    uint32_t counter_s     = time_ctr_get();
    uint32_t threshold     = config_mngr_soft_ap_start_threshold_s_get();
    bool     b_paused      = time_ctr_is_paused();
    uint32_t throughput    = wifi_mngr_reward_ap_throughput_kbps();
    uint32_t speed_x10     = time_ctr_current_speed_x10_get();
    uint32_t spd_ctr_int   = speed_x10 / 10U;
    uint32_t spd_ctr_dec   = speed_x10 % 10U;
    uint16_t min_spd_cfg   = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    bool     b_speed_gated = (min_spd_cfg > 0U) && (speed_x10 < (uint32_t)min_spd_cfg);
    uint32_t ctr_h         = counter_s / 3600U;
    uint32_t ctr_m         = (counter_s % 3600U) / 60U;
    uint32_t ctr_s_r       = counter_s % 60U;

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
        "<p>Time:&nbsp;<b><span id=\"sys-time\">%s</span></b></p>"
        "<p>NTP:&nbsp;<span id=\"sys-ntp\" class=\"%s\">%s</span></p>"
        "<p>Uptime:&nbsp;<b><span id=\"sys-uptime\">%" PRId32 "d&nbsp;%" PRId32 "h&nbsp;%" PRId32
        "m&nbsp;%" PRId32 "s</span></b></p></div>",
        time_local_str, b_synced ? "ok" : "err", b_synced ? "Synced" : "Not synced", up_d, up_h,
        up_m, up_s_rem);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Wi-Fi section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Wi-Fi</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>STA:&nbsp;<span id=\"wifi-sta\" class=\"%s\">%s</span>"
        "&nbsp; SSID:&nbsp;<b><span id=\"wifi-sta-ssid\">%s</span></b>"
        "&nbsp; IP:&nbsp;<b><span id=\"wifi-sta-ip\">%s</span></b></p>"
        "<p>Config AP:&nbsp;<span id=\"wifi-cfg-ap\" class=\"%s\">%s</span></p>"
        "<p>Reward AP:&nbsp;<span id=\"wifi-rew-ap\" class=\"%s\">%s</span>"
        "&nbsp; SSID:&nbsp;<b><span id=\"wifi-rew-ap-ssid\">%s</span></b>"
        "&nbsp; Clients:&nbsp;<b><span id=\"wifi-rew-ap-clients\">%" PRIu8 "</span></b></p></div>",
        b_sta ? "ok" : "err", b_sta ? "Connected" : "Disconnected", sta_ssid, sta_ip,
        b_cfg_ap ? "ok" : "err", b_cfg_ap ? "Active" : "Inactive", b_rew_ap ? "ok" : "err",
        b_rew_ap ? "Active" : "Inactive", rew_ap_ssid, ap_clients);
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Exercise Counter section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Exercise Counter</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>Counter:&nbsp;<b><span id=\"ctr-seconds\">%" PRIu32 "s</span></b>"
        "&nbsp;(<span id=\"ctr-hms\">%" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "</span>)"
        "&nbsp; Threshold:&nbsp;<b><span id=\"ctr-threshold\">%" PRIu32 "s</span></b></p>"
        "<p>Reward AP:&nbsp;<span id=\"ctr-rew-ap\" class=\"%s\">%s</span></p>"
        "<p>Traffic:&nbsp;<span id=\"ap-throughput\"><b>%" PRIu32 "</b></span>&nbsp;kbps</p>"
        "<p>Current speed:&nbsp;<span id=\"current-speed\"><b>%" PRIu32 ".%" PRIu32
        "</b></span>&nbsp;km/h</p>"
        "<p>Pulse crediting:&nbsp;"
        "<span id=\"speed-gate-indicator\" style=\"display:%s;\">&#8856; Gated (speed too "
        "low)</span>"
        "<span id=\"speed-credit-indicator\" style=\"display:%s;\">Crediting</span>"
        "</p>"
        "<p>Countdown:&nbsp;"
        "<span id=\"pause-indicator\" style=\"display:%s;\">&#9208; Paused (low traffic)</span>"
        "<span id=\"decrement-indicator\" style=\"display:%s;\">Decrementing</span>"
        "</p></div>",
        counter_s, ctr_h, ctr_m, ctr_s_r, threshold, b_rew_ap ? "ok" : "err",
        b_rew_ap ? "Active" : "Inactive", throughput, spd_ctr_int, spd_ctr_dec,
        b_speed_gated ? "inline" : "none", b_speed_gated ? "none" : "inline",
        b_paused ? "inline" : "none", b_paused ? "none" : "inline");
    (void)httpd_resp_sendstr_chunk(p_req, p_buf);

    /* Current Session section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h3>Current Session</h3>");
    snprintf(p_buf, HTTP_SRV_HTML_BUF_LEN,
        "<p>State:&nbsp;<b><span id=\"sess-state\">%s</span></b></p>"
        "<p>Live Speed:&nbsp;<b><span id=\"sess-speed\">%" PRIu16 ".%" PRIu16
        "&nbsp;km/h</span></b></p>"
        "<p>Duration:&nbsp;<b><span id=\"sess-duration\">%" PRIu32 ":%02" PRIu32 ":%02" PRIu32
        "</span></b></p></div>",
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

    /* Navigation and JS auto-refresh for pause indicator */
    static const char sc_nav[] =
        "<div class=\"card\">"
        "<a href=\"/config\">&#9881; Go to Configuration</a>"
        "</div>"
        "<script>"
        "(function(){"
        "function pad2(n){return('0'+n).slice(-2);}"
        "function fmtHms(s){return "
        "Math.floor(s/3600)+':'+pad2(Math.floor((s%3600)/60))+':'+pad2(s%60);}"
        "function refresh(){"
        "fetch('/api/status').then(function(r){return r.json();}).then(function(d){"
        "var e;"
        "e=document.getElementById('sys-time');if(e)e.textContent=d.time_local;"
        "e=document.getElementById('sys-ntp');"
        "if(e){e.textContent=d.time_synced?'Synced':'Not "
        "synced';e.className=d.time_synced?'ok':'err';}"
        "e=document.getElementById('sys-uptime');"
        "if(e){var u=d.uptime_s;"
        "e.textContent=Math.floor(u/86400)+'d '+Math.floor((u%86400)/3600)+'h "
        "'+Math.floor((u%3600)/60)+'m '+(u%60)+'s';}"
        "e=document.getElementById('wifi-sta');"
        "if(e){e.textContent=d.sta_connected?'Connected':'Disconnected';e.className=d.sta_"
        "connected?'ok':'err';}"
        "e=document.getElementById('wifi-sta-ssid');if(e)e.textContent=d.sta_ssid;"
        "e=document.getElementById('wifi-sta-ip');if(e)e.textContent=d.sta_ip;"
        "e=document.getElementById('wifi-cfg-ap');"
        "if(e){e.textContent=d.config_ap_active?'Active':'Inactive';e.className=d.config_ap_active?"
        "'ok':'err';}"
        "e=document.getElementById('wifi-rew-ap');"
        "if(e){e.textContent=d.reward_ap_active?'Active':'Inactive';e.className=d.reward_ap_active?"
        "'ok':'err';}"
        "e=document.getElementById('wifi-rew-ap-ssid');if(e)e.textContent=d.reward_ap_ssid;"
        "e=document.getElementById('wifi-rew-ap-clients');if(e)e.textContent=d.reward_ap_clients;"
        "e=document.getElementById('ctr-seconds');if(e)e.textContent=d.counter_s+'s';"
        "e=document.getElementById('ctr-hms');if(e)e.textContent=fmtHms(d.counter_s);"
        "e=document.getElementById('ctr-threshold');if(e)e.textContent=d.threshold_s+'s';"
        "e=document.getElementById('ctr-rew-ap');"
        "if(e){e.textContent=d.reward_ap_active?'Active':'Inactive';e.className=d.reward_ap_active?"
        "'ok':'err';}"
        "e=document.getElementById('ap-throughput');"
        "if(e)e.innerHTML='<b>'+d.reward_ap_throughput_kbps+'</b>';"
        "e=document.getElementById('current-speed');"
        "if(e)e.innerHTML='<b>'+(d.current_speed_kmh_x10/10).toFixed(1)+'</b>';"
        "var sg=document.getElementById('speed-gate-indicator');"
        "var sci=document.getElementById('speed-credit-indicator');"
        "if(sg&&sci){sg.style.display=d.speed_gate_active?'inline':'none';sci.style.display=d."
        "speed_gate_active?'none':'inline';}"
        "var pi=document.getElementById('pause-indicator');"
        "var di=document.getElementById('decrement-indicator');"
        "if(pi&&di){pi.style.display=d.countdown_paused?'inline':'none';di.style.display=d."
        "countdown_paused?'none':'inline';}"
        "e=document.getElementById('sess-state');if(e)e.textContent=d.session_state;"
        "e=document.getElementById('sess-speed');"
        "if(e)e.innerHTML=(d.live_speed_kmh_x10/10).toFixed(1)+'&nbsp;km/h';"
        "e=document.getElementById('sess-duration');if(e)e.textContent=fmtHms(d.session_duration_s)"
        ";"
        "}).catch(function(){});"
        "}"
        "refresh();"
        "setInterval(refresh,2000);"
        "})();"
        "</script>"
        "</body></html>";
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

//==================================================================================================
// Private Functions
//==================================================================================================

/*** end of file ***/
