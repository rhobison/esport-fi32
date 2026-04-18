/**
 * \file
 * \brief HTTP server configuration page handlers.
 *
 * \date 2026-03-15
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_config.h"
#include "http_server_utils.h"
#include "config_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "device_registry.h"
#include "esp_wifi.h"
#include "event_ids.h"
#include "activity_manager.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag __attribute__((unused)) = "http_srv_config";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t parse_mac_address(const char * p_str, uint8_t * p_mac_out);

//==================================================================================================
// Public Functions
//==================================================================================================

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
esp_err_t http_srv_config_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Check for ?saved=1 or ?reset=1 query parameters. */
    bool b_saved = false;
    bool b_reset = false;
    char query_buf[32];
    if (ESP_OK == httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        char saved_val[4];
        if (ESP_OK == httpd_query_key_value(query_buf, "saved", saved_val, sizeof(saved_val)))
        {
            b_saved = (0 == strcmp(saved_val, "1"));
        }
        char reset_val[4];
        if (ESP_OK == httpd_query_key_value(query_buf, "reset", reset_val, sizeof(reset_val)))
        {
            b_reset = (0 == strcmp(reset_val, "1"));
        }
    }

    /* Read all current configuration values. */
    static char wifi_ssid[33];
    static char wifi_pwd[65];
    static char ap_ssid[33];
    static char ap_pwd[65];
    uint16_t    seconds_per_pulse            = config_mngr_seconds_per_pulse_get();
    uint32_t    inet_gate_threshold_s        = config_mngr_internet_gate_threshold_s_get();
    uint32_t    centimeters_per_pulse        = config_mngr_centimeters_per_pulse_get();
    uint16_t    idle_session_interval_s      = config_mngr_idle_session_interval_s_get();
    uint16_t    start_session_interval_s     = config_mngr_start_session_interval_s_get();
    uint16_t    debounce_ms                  = config_mngr_pulse_debounce_time_ms_get();
    uint16_t    ap_dec_threshold_kbps        = config_mngr_soft_ap_dec_threshold_kbps_get();
    uint16_t    ap_idle_throughput_timeout_s = config_mngr_soft_ap_idle_throughput_timeout_s_get();
    uint16_t    min_speed_kmh_x10     = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    uint16_t    low_speed_bz_thresh_s = config_mngr_low_speed_buzzer_threshold_s_get();
    static char tz[64];

    config_mngr_wifi_ssid_get(wifi_ssid, sizeof(wifi_ssid));
    config_mngr_wifi_password_get(wifi_pwd, sizeof(wifi_pwd));
    config_mngr_soft_ap_ssid_get(ap_ssid, sizeof(ap_ssid));
    config_mngr_soft_ap_password_get(ap_pwd, sizeof(ap_pwd));
    config_mngr_timezone_get(tz, sizeof(tz));

    /*
     * Single shared encoding buffer - re-used for each string field so that
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
        "<title>ESPort-fi32 Configuration</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:640px;margin:2em auto;padding:0 1em}"
        "h2{margin-bottom:.5em}"
        "label{display:inline-block;width:230px;font-weight:bold;vertical-align:middle}"
        "input[type=text],input[type=password],input[type=number]"
        "{padding:4px;width:230px;box-sizing:border-box}"
        "p{margin:.4em 0}"
        ".saved{background:#dfd;padding:.5em 1em;border:1px solid #6a6;"
        "border-radius:4px;margin-bottom:1em}"
        ".reset-banner{background:#dff;padding:.5em 1em;border:1px solid #69a;"
        "border-radius:4px;margin-bottom:1em}"
        ".btn-reset{background:#e55;color:#fff;border:none;padding:6px 14px;"
        "cursor:pointer;border-radius:3px;font-size:1em}"
        ".btn-reset:hover{background:#c33}"
        ".btn-right{text-align:right;margin-top:1em}"
        ".sta-status{margin-top:1.5em;padding-top:1em;border-top:1px solid #ccc;font-size:.95em}"
        ".sta-ok{color:#2a2;font-weight:bold}"
        ".sta-err{color:#c00;font-weight:bold}"
        ".card{background:#f9f9f9;border:1px solid #ddd;border-radius:4px;"
        "padding:.6em .9em;margin:.6em 0}"
        "</style></head><body>"
        "<h2>ESPort-fi32 &mdash; Configuration</h2>";
    (void)httpd_resp_sendstr_chunk(p_req, sc_header);

    if (b_saved)
    {
        (void)httpd_resp_sendstr_chunk(p_req,
            "<p class=\"saved\">&#10003; Configuration saved successfully.</p>");
    }

    if (b_reset)
    {
        (void)httpd_resp_sendstr_chunk(p_req,
            "<p class=\"reset-banner\">&#10003; Configuration reset to factory defaults.</p>");
    }

    (void)httpd_resp_sendstr_chunk(p_req,
        "<form method=\"POST\" action=\"/config\">"
        "<div class=\"card\"><h3 style=\"margin-top:0\">General Settings</h3>");

    /* ---- String fields (prefix / encoded-value / suffix chunks) ---- */

    /* wifi_ssid */
    http_srv_html_attr_encode(wifi_ssid, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req, "<p><label>Home Wi-Fi SSID</label>"
                                          "<input type=\"text\" name=\"wifi_ssid\" value=\"");
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"32\"></p>");

    /* wifi_password */
    http_srv_html_attr_encode(wifi_pwd, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Home Wi-Fi Password</label>"
        "<input type=\"password\" name=\"wifi_password\" value=\"");
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"64\"></p>");

    /* soft_ap_ssid */
    http_srv_html_attr_encode(ap_ssid, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req, "<p><label>Reward AP SSID</label>"
                                          "<input type=\"text\" name=\"soft_ap_ssid\" value=\"");
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"32\"></p>");

    /* soft_ap_password */
    http_srv_html_attr_encode(ap_pwd, enc, sizeof(enc));
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Reward AP Password</label>"
        "<input type=\"password\" name=\"soft_ap_password\" value=\"");
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"64\"></p>");

    /* ---- Numeric fields ---- */

    /* seconds_per_pulse */
    snprintf(num, sizeof(num), "%" PRIu16, seconds_per_pulse);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Seconds per Pulse</label>"
        "<input type=\"number\" name=\"seconds_per_pulse\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\" max=\"60\"></p>");

    /* soft_inet_gate_threshold_s */
    snprintf(num, sizeof(num), "%" PRIu32, inet_gate_threshold_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Internet Gate Threshold (s)</label>"
        "<input type=\"number\" name=\"soft_inet_gate_threshold_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\"></p>");

    /* centimeters_per_pulse */
    snprintf(num, sizeof(num), "%" PRIu32, centimeters_per_pulse);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Centimeters per Pulse</label>"
        "<input type=\"number\" name=\"centimeters_per_pulse\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\"></p>");

    /* idle_session_interval_s */
    snprintf(num, sizeof(num), "%" PRIu16, idle_session_interval_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Idle Session Timeout (s)</label>"
        "<input type=\"number\" name=\"idle_session_interval_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"5\" max=\"600\"></p>");

    /* start_session_interval_s */
    snprintf(num, sizeof(num), "%" PRIu16, start_session_interval_s);
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
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"63\"></p>");

    /* soft_ap_dec_time_above_threshold_kbps */
    snprintf(num, sizeof(num), "%" PRIu16, ap_dec_threshold_kbps);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Reward AP idle throughput threshold (kbps)</label>"
        "<input type=\"number\" name=\"soft_ap_dec_time_above_threshold_kbps\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* soft_ap_idle_throughput_timeout_s */
    snprintf(num, sizeof(num), "%" PRIu16, ap_idle_throughput_timeout_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Idle throughput timeout (s)</label>"
        "<input type=\"number\" name=\"soft_ap_idle_throughput_timeout_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* min_speed_to_increment_time_kmh_x10 - displayed as km/h float (1 decimal) */
    snprintf(num, sizeof(num), "%.1f", (double)min_speed_kmh_x10 / 10.0);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Minimum speed to earn credits (km/h)</label>"
        "<input type=\"number\" name=\"min_speed_to_increment_time_kmh_x10\""
        " step=\"0.1\" min=\"0\" max=\"6553.5\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\"></p>");

    /* low_speed_buzzer_threshold_s */
    snprintf(num, sizeof(num), "%" PRIu16, low_speed_bz_thresh_s);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Low-speed beep delay (s)</label>"
        "<input type=\"number\" name=\"low_speed_buzzer_threshold_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* buzzer_enabled */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Buzzer feedback</label>"
        "<input type=\"checkbox\" name=\"buzzer_enabled\" value=\"1\"");
    if (config_mngr_buzzer_enabled_get())
    {
        (void)httpd_resp_sendstr_chunk(p_req, " checked");
    }
    (void)httpd_resp_sendstr_chunk(p_req, "></p>");

    /* activity_credit_buzzer_en */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Activity credit beep</label>"
        "<input type=\"checkbox\" name=\"activity_credit_buzzer_en\" value=\"1\"");
    if (config_mngr_activity_credit_buzzer_en_get())
    {
        (void)httpd_resp_sendstr_chunk(p_req, " checked");
    }
    (void)httpd_resp_sendstr_chunk(p_req, "></p>");

    /* ---- Registered Devices section ---- */
    uint8_t dev_count = device_reg_count_get();
    uint8_t rider_idx = device_reg_current_rider_get();

    (void)httpd_resp_sendstr_chunk(p_req, "</div>"); /* close general settings card */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<div class=\"card\"><h3 style=\"margin-top:0\">Registered Devices</h3>"
        "<table style=\"width:100%;border-collapse:collapse\">"
        "<tr><th>Nickname</th><th>MAC</th><th>Counter</th>"
        "<th>Enabled</th><th>Rider</th><th>Remove</th></tr>");

    for (uint8_t dev_i = 0U; dev_i < dev_count; dev_i++)
    {
        device_reg_entry_t entry;
        if (ESP_OK != device_reg_entry_get(dev_i, &entry))
        {
            continue;
        }

        /* Format MAC. */
        static char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", entry.mac[0],
            entry.mac[1], entry.mac[2], entry.mac[3], entry.mac[4], entry.mac[5]);

        /* Format counter as h:mm:ss. */
        static char ctr_str[20];
        uint32_t    rc_h = entry.counter_s / 3600U;
        uint32_t    rc_m = (entry.counter_s % 3600U) / 60U;
        uint32_t    rc_s = entry.counter_s % 60U;
        snprintf(ctr_str, sizeof(ctr_str), "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, rc_h, rc_m,
            rc_s);

        /* Row open + nickname input. */
        static char row_open[256];
        snprintf(row_open, sizeof(row_open),
            "<tr><td><input type=\"text\" name=\"dev_%u_nickname\" maxlength=\"15\"  "
            "style=\"width:150px\" value=\"",
            (unsigned)dev_i);
        (void)httpd_resp_sendstr_chunk(p_req, row_open);
        http_srv_html_attr_encode(entry.nickname, enc, sizeof(enc));
        (void)httpd_resp_sendstr_chunk(p_req, enc);
        (void)httpd_resp_sendstr_chunk(p_req, "\"></td>");

        /* MAC (read-only). */
        static char mac_cell[64];
        snprintf(mac_cell, sizeof(mac_cell), "<td><span>%s</span></td>", mac_str);
        (void)httpd_resp_sendstr_chunk(p_req, mac_cell);

        /* Counter input - oninput auto-formats as h:mm:ss while typing;
         * server-side parsing rejects malformed values with HTTP 400. */
        static char ctr_open[128];
        snprintf(ctr_open, sizeof(ctr_open),
            "<td><input type=\"text\" name=\"dev_%u_counter\""
            " placeholder=\"0:00:00\" value=\"",
            (unsigned)dev_i);
        (void)httpd_resp_sendstr_chunk(p_req, ctr_open);
        (void)httpd_resp_sendstr_chunk(p_req, ctr_str);
        (void)httpd_resp_sendstr_chunk(p_req,
            "\" maxlength=\"8\" style=\"width:150px\""
            " oninput=\"var d=this.value.replace(/\\D/g,'').slice(0,6);"
            "if(d.length>4)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4);"
            "else if(d.length>2)this.value=d.slice(0,2)+':'+d.slice(2);"
            "else this.value=d;\"></td>");

        /* Enabled checkbox. */
        static char en_cell[128];
        snprintf(en_cell, sizeof(en_cell),
            "<td><input type=\"checkbox\" name=\"dev_%u_enabled\" value=\"1\"%s></td>",
            (unsigned)dev_i, entry.b_enabled ? " checked" : "");
        (void)httpd_resp_sendstr_chunk(p_req, en_cell);

        /* Rider radio. */
        static char rider_cell[128];
        snprintf(rider_cell, sizeof(rider_cell),
            "<td><input type=\"radio\" name=\"current_rider\" value=\"%u\"%s></td>",
            (unsigned)dev_i, (rider_idx == dev_i) ? " checked" : "");
        (void)httpd_resp_sendstr_chunk(p_req, rider_cell);

        /* Remove button. */
        static char rm_cell[128];
        snprintf(rm_cell, sizeof(rm_cell),
            "<td><button type=\"submit\" name=\"dev_%u_remove\" value=\"1\">"
            "Remove</button></td></tr>",
            (unsigned)dev_i);
        (void)httpd_resp_sendstr_chunk(p_req, rm_cell);
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</table>");

    /* No Rider radio option. */
    {
        static char no_rider[128];
        snprintf(no_rider, sizeof(no_rider),
            "<p><label>Current Rider: No rider&nbsp;"
            "<input type=\"radio\" name=\"current_rider\" value=\"255\"%s></label></p>",
            (DEVICE_REG_NO_RIDER == rider_idx) ? " checked" : "");
        (void)httpd_resp_sendstr_chunk(p_req, no_rider);
    }

    /* Add Device sub-form. */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<h4 style=\"margin-top:1em\">Add Device</h4>"
        "<p><label>MAC Address</label>"
        "<input type=\"text\" name=\"new_dev_mac\""
        " placeholder=\"AA:BB:CC:DD:EE:FF\" maxlength=\"17\""
        " oninput=\"var d=this.value.replace(/[^0-9A-Fa-f]/g,'').toUpperCase().slice(0,12);"
        "if(d.length>10)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4,6)+':'"
        "+d.slice(6,8)+':'+d.slice(8,10)+':'+d.slice(10);"
        "else if(d.length>8)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4,6)+':'"
        "+d.slice(6,8)+':'+d.slice(8);"
        "else if(d.length>6)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4,6)+':'"
        "+d.slice(6);"
        "else if(d.length>4)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4);"
        "else if(d.length>2)this.value=d.slice(0,2)+':'+d.slice(2);"
        "else this.value=d;\"></p>"
        "<p><label>Nickname</label>"
        "<input type=\"text\" name=\"new_dev_nickname\" maxlength=\"15\"></p>"
        "<p><button type=\"submit\" name=\"action\" value=\"add_device\">"
        "Add Device</button></p>");

    /* ---- Connected unregistered stations ---- */
    {
        wifi_sta_list_t sta_list;
        memset(&sta_list, 0, sizeof(sta_list));
        (void)esp_wifi_ap_get_sta_list(&sta_list);

        /* Collect unregistered MACs. */
        uint8_t unreg_mac[ESP_WIFI_MAX_CONN_NUM][6];
        int     unreg_count = 0;
        for (int j = 0; j < (int)sta_list.num; j++)
        {
            if ((int8_t)-1 == device_reg_mac_find(sta_list.sta[j].mac))
            {
                memcpy(unreg_mac[unreg_count], sta_list.sta[j].mac, 6U);
                unreg_count++;
            }
        }

        if (unreg_count > 0)
        {
            (void)httpd_resp_sendstr_chunk(p_req,
                "<h4 style=\"margin-top:1em\">Connected Unregistered Stations</h4>"
                "<p style=\"font-size:0.85em;color:#666\">"
                "These devices are connected to the AP but not registered. "
                "Click <b>Add</b> to register with the shown MAC.</p>"
                "<table style=\"width:100%;border-collapse:collapse\">"
                "<tr><th>MAC</th><th>Nickname</th><th></th></tr>");

            for (int u = 0; u < unreg_count; u++)
            {
                static char row[384];
                snprintf(row, sizeof(row),
                    "<tr>"
                    "<td>%02X:%02X:%02X:%02X:%02X:%02X</td>"
                    "<td><input type=\"text\" name=\"unreg_%d_nick\" "
                    "placeholder=\"Enter nickname\" maxlength=\"15\"></td>"
                    "<td><input type=\"hidden\" name=\"unreg_%d_mac\" "
                    "value=\"%02X:%02X:%02X:%02X:%02X:%02X\">"
                    "<button type=\"submit\" name=\"action\" "
                    "value=\"add_unreg_%d\">Add</button></td>"
                    "</tr>",
                    unreg_mac[u][0], unreg_mac[u][1], unreg_mac[u][2], unreg_mac[u][3],
                    unreg_mac[u][4], unreg_mac[u][5], u, u, unreg_mac[u][0], unreg_mac[u][1],
                    unreg_mac[u][2], unreg_mac[u][3], unreg_mac[u][4], unreg_mac[u][5], u);
                (void)httpd_resp_sendstr_chunk(p_req, row);
            }

            (void)httpd_resp_sendstr_chunk(p_req, "</table>");
        }
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</div>"); /* close devices card */

    /* ---- Footer: right-aligned Save + Reset buttons ---- */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\">");
    (void)httpd_resp_sendstr_chunk(p_req,
        "<div class=\"btn-right\">"
        "<input type=\"submit\" value=\"Save Configuration\">"
        "</div>"
        "</form>"
        "<div class=\"sta-status\"></div>"
        "<form method=\"POST\" action=\"/config/reset\""
        " onsubmit=\"return confirm('Reset ALL settings to factory defaults?\\nThis cannot be "
        "undone.')\""
        " style=\"margin-top:.5em\">"
        "<div class=\"btn-right\">"
        "<button type=\"submit\" class=\"btn-reset\">Reset to Factory Defaults</button>"
        "</div>"
        "</form>"
        "<div style='margin:1.5em 0;border-top:1px solid #ccc;'></div>"
        "<div class=\"btn-right\">"
        "<a href='/config/pwd' style='display:inline-block;background:#555;color:#fff;"
        "border:none;padding:6px 14px;border-radius:4px;text-decoration:none;"
        "font-size:inherit;margin-right:0.5em;'>Change Config Password</a>"
        "<a href='/ota' style='display:inline-block;background:#2a6db5;color:#fff;border:none;"
        "padding:6px 14px;border-radius:4px;text-decoration:none;font-size:inherit;'"
        ">Firmware Update</a>"
        "</div>");
    (void)httpd_resp_sendstr_chunk(p_req, "</div>"); /* close footer card */

    /* ---- STA connection status ---- */
    {
        bool        b_sta = wifi_mngr_sta_is_connected();
        static char sta_ip_buf[16];
        wifi_mngr_sta_ip_get(sta_ip_buf, sizeof(sta_ip_buf));

        static char sta_block[256];
        if (b_sta)
        {
            snprintf(sta_block, sizeof(sta_block),
                "<div class=\"sta-status\">"
                "Home Wi-Fi status: <span class=\"sta-ok\">Connected</span><br>"
                "IP: %s"
                "</div>",
                sta_ip_buf);
        }
        else
        {
            snprintf(sta_block, sizeof(sta_block),
                "<div class=\"sta-status\">"
                "Home Wi-Fi status: <span class=\"sta-err\">Disconnected</span>"
                "</div>");
        }
        (void)httpd_resp_sendstr_chunk(p_req, sta_block);
    }

    (void)httpd_resp_sendstr_chunk(p_req, "</body></html>");

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
esp_err_t http_srv_config_post_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

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

    /* soft_inet_gate_threshold_s (uint32) */
    if (ESP_OK ==
        http_srv_form_field_get(body, "soft_inet_gate_threshold_s", num_str, sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if ('\0' != *endptr)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_inet_gate_threshold_s must be a valid integer");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_internet_gate_threshold_s_set((uint32_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid soft_inet_gate_threshold_s");
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

    /* soft_ap_dec_time_above_threshold_kbps (uint16, range 0–65535) */
    if (ESP_OK == http_srv_form_field_get(body, "soft_ap_dec_time_above_threshold_kbps", num_str,
                      sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_ap_dec_time_above_threshold_kbps must be 0-65535");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_soft_ap_dec_threshold_kbps_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "Invalid soft_ap_dec_time_above_threshold_kbps");
            return ESP_FAIL;
        }
    }

    /* soft_ap_idle_throughput_timeout_s (uint16, range 0–65535) */
    if (ESP_OK == http_srv_form_field_get(body, "soft_ap_idle_throughput_timeout_s", num_str,
                      sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "soft_ap_idle_throughput_timeout_s must be 0-65535");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_soft_ap_idle_throughput_timeout_s_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "Invalid soft_ap_idle_throughput_timeout_s");
            return ESP_FAIL;
        }
    }

    /* min_speed_to_increment_time_kmh_x10 - submitted as km/h float, stored x10 */
    if (ESP_OK == http_srv_form_field_get(body, "min_speed_to_increment_time_kmh_x10", num_str,
                      sizeof(num_str)))
    {
        char * endptr;
        float  kmh_f = strtof(num_str, &endptr);
        if (('\0' != *endptr) || (kmh_f < 0.0f) || (kmh_f > 6553.5f))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "min_speed must be a number between 0 and 6553.5 km/h");
            return ESP_FAIL;
        }
        uint16_t val_x10 = (uint16_t)(kmh_f * 10.0f + 0.5f);
        if (ESP_OK != config_mngr_min_speed_to_increment_time_kmh_x10_set(val_x10))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid min_speed value");
            return ESP_FAIL;
        }
    }

    /* buzzer_enabled (checkbox: present = true, absent = false) */
    {
        char bz_val[4];
        bool b_buzzer =
            (ESP_OK == http_srv_form_field_get(body, "buzzer_enabled", bz_val, sizeof(bz_val)));
        (void)config_mngr_buzzer_enabled_set(b_buzzer);
    }

    /* activity_credit_buzzer_en (checkbox: present = true, absent = false) */
    {
        char ac_bz_val[4];
        bool b_ac_bz = (ESP_OK == http_srv_form_field_get(body, "activity_credit_buzzer_en",
                                      ac_bz_val, sizeof(ac_bz_val)));
        (void)config_mngr_activity_credit_buzzer_en_set(b_ac_bz);
    }

    /* low_speed_buzzer_threshold_s */
    {
        char thresh_str[8];
        if (ESP_OK == http_srv_form_field_get(body, "low_speed_buzzer_threshold_s", thresh_str,
                          sizeof(thresh_str)))
        {
            char *   p_end;
            uint32_t thresh_val = (uint32_t)strtoul(thresh_str, &p_end, 10);
            if ((p_end == thresh_str) || ('\0' != *p_end) || (thresh_val > 65535UL))
            {
                httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                    "low_speed_buzzer_threshold_s must be 0-65535");
                return ESP_FAIL;
            }
            if (ESP_OK != config_mngr_low_speed_buzzer_threshold_s_set((uint16_t)thresh_val))
            {
                httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                    "Invalid low_speed_buzzer_threshold_s value");
                return ESP_FAIL;
            }
        }
    }

    /* ---- Device Registry fields ---- */

    /* current_rider */
    {
        char rider_str[8];
        if (ESP_OK == http_srv_form_field_get(body, "current_rider", rider_str, sizeof(rider_str)))
        {
            char *        endptr;
            unsigned long rider_val = strtoul(rider_str, &endptr, 10);
            if ('\0' == *endptr)
            {
                uint8_t rider_idx = (rider_val >= (unsigned long)DEVICE_REG_NO_RIDER) ?
                                        DEVICE_REG_NO_RIDER :
                                        (uint8_t)rider_val;
                if ((DEVICE_REG_NO_RIDER != rider_idx) && (rider_idx >= device_reg_count_get()))
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid rider index");
                    return ESP_FAIL;
                }
                (void)device_reg_current_rider_set(rider_idx);
            }
        }
    }

    /* Per-device fields (nickname, enabled, counter, remove). */
    {
        uint8_t dev_count = device_reg_count_get();
        for (uint8_t dev_i = 0U; dev_i < dev_count; dev_i++)
        {
            /* dev_N_remove */
            char rm_key[20];
            snprintf(rm_key, sizeof(rm_key), "dev_%u_remove", (unsigned)dev_i);
            char rm_val[4];
            if (ESP_OK == http_srv_form_field_get(body, rm_key, rm_val, sizeof(rm_val)))
            {
                if (0 == strcmp(rm_val, "1"))
                {
                    (void)device_reg_entry_remove(dev_i);
                    /* Indices shift after removal - stop processing and let the redirect
                     * re-render the updated form. */
                    break;
                }
            }

            /* dev_N_nickname */
            char nick_key[24];
            snprintf(nick_key, sizeof(nick_key), "dev_%u_nickname", (unsigned)dev_i);
            char nick_val[DEVICE_REG_NICKNAME_MAX_LEN + 2U];
            if (ESP_OK == http_srv_form_field_get(body, nick_key, nick_val, sizeof(nick_val)))
            {
                if ('\0' != nick_val[0])
                {
                    device_reg_entry_t existing;
                    if ((ESP_OK == device_reg_entry_get(dev_i, &existing)) &&
                        (0 != strcmp(existing.nickname, nick_val)))
                    {
                        if (strlen(nick_val) > DEVICE_REG_NICKNAME_MAX_LEN)
                        {
                            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                                "Nickname exceeds maximum length");
                            return ESP_FAIL;
                        }
                        (void)device_reg_entry_nickname_set(dev_i, nick_val);
                    }
                }
            }

            /* dev_N_enabled - checkbox: present = true, absent = false */
            {
                char en_key[24];
                snprintf(en_key, sizeof(en_key), "dev_%u_enabled", (unsigned)dev_i);
                char en_val[4];
                bool b_enabled =
                    (ESP_OK == http_srv_form_field_get(body, en_key, en_val, sizeof(en_val)));
                (void)device_reg_entry_enabled_set(dev_i, b_enabled);
            }

            /* dev_N_counter (h:mm:ss format) */
            char ctr_key[24];
            snprintf(ctr_key, sizeof(ctr_key), "dev_%u_counter", (unsigned)dev_i);
            char ctr_val[20];
            if (ESP_OK == http_srv_form_field_get(body, ctr_key, ctr_val, sizeof(ctr_val)))
            {
                char * colon1 = strchr(ctr_val, ':');
                char * colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;
                if ((NULL == colon1) || (NULL == colon2) || (NULL != strchr(colon2 + 1, ':')))
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Counter must be in h:mm:ss format");
                    return ESP_FAIL;
                }
                *colon1           = '\0';
                *colon2           = '\0';
                const char *  p_h = ctr_val;
                const char *  p_m = colon1 + 1;
                const char *  p_s = colon2 + 1;
                char *        endptr;
                unsigned long h = strtoul(p_h, &endptr, 10);
                if ('\0' != *endptr)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Counter hours must be a valid integer");
                    return ESP_FAIL;
                }
                unsigned long m = strtoul(p_m, &endptr, 10);
                if (('\0' != *endptr) || (m > 59UL))
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Counter minutes must be 0-59");
                    return ESP_FAIL;
                }
                unsigned long s = strtoul(p_s, &endptr, 10);
                if (('\0' != *endptr) || (s > 59UL))
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Counter seconds must be 0-59");
                    return ESP_FAIL;
                }
                if (h > 1193046UL)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Counter value exceeds maximum representable seconds");
                    return ESP_FAIL;
                }
                uint32_t total_s = (uint32_t)(h * 3600UL + m * 60UL + s);
                (void)device_reg_entry_counter_set(dev_i, total_s);
                /* Also update via time_ctr if this is the current rider. */
                if (dev_i == device_reg_current_rider_get())
                {
                    (void)time_ctr_counter_set(total_s);
                }
            }
        }
    }

    /* Add Device action */
    {
        char action_val[24];
        if (ESP_OK == http_srv_form_field_get(body, "action", action_val, sizeof(action_val)))
        {
            if (0 == strcmp(action_val, "add_device"))
            {
                char mac_str[20];
                char nick_str[DEVICE_REG_NICKNAME_MAX_LEN + 2U];
                if ((ESP_OK !=
                        http_srv_form_field_get(body, "new_dev_mac", mac_str, sizeof(mac_str))) ||
                    (ESP_OK != http_srv_form_field_get(body, "new_dev_nickname", nick_str,
                                   sizeof(nick_str))))
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "MAC and nickname are required to add a device");
                    return ESP_FAIL;
                }

                uint8_t   mac_bytes[6];
                esp_err_t mac_ret = parse_mac_address(mac_str, mac_bytes);
                if (ESP_OK != mac_ret)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Invalid MAC address format");
                    return ESP_FAIL;
                }

                esp_err_t add_ret = device_reg_entry_add(mac_bytes, nick_str);
                if (ESP_ERR_NO_MEM == add_ret)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Registry full - max 4 devices");
                    return ESP_FAIL;
                }
                if (ESP_ERR_INVALID_STATE == add_ret)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Device already registered");
                    return ESP_FAIL;
                }
                if (ESP_ERR_INVALID_ARG == add_ret)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                        "Invalid nickname (empty or too long)");
                    return ESP_FAIL;
                }
                if (ESP_OK != add_ret)
                {
                    httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Failed to add device");
                    return ESP_FAIL;
                }
            }

            /* Handle add_unreg_N actions from connected unregistered stations. */
            if (0 == strncmp(action_val, "add_unreg_", 10U))
            {
                char *        endptr;
                unsigned long unreg_idx = strtoul(action_val + 10U, &endptr, 10);
                if ('\0' == *endptr && unreg_idx < (unsigned long)ESP_WIFI_MAX_CONN_NUM)
                {
                    char mac_key[20];
                    char nick_key[20];
                    char mac_str_u[20];
                    char nick_str_u[DEVICE_REG_NICKNAME_MAX_LEN + 2U];
                    snprintf(mac_key, sizeof(mac_key), "unreg_%lu_mac", unreg_idx);
                    snprintf(nick_key, sizeof(nick_key), "unreg_%lu_nick", unreg_idx);

                    if ((ESP_OK !=
                            http_srv_form_field_get(body, mac_key, mac_str_u, sizeof(mac_str_u))) ||
                        (ESP_OK != http_srv_form_field_get(body, nick_key, nick_str_u,
                                       sizeof(nick_str_u))) ||
                        ('\0' == nick_str_u[0]))
                    {
                        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                            "MAC and nickname are required");
                        return ESP_FAIL;
                    }

                    uint8_t mac_bytes_u[6];
                    if (ESP_OK != parse_mac_address(mac_str_u, mac_bytes_u))
                    {
                        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                            "Invalid MAC address format");
                        return ESP_FAIL;
                    }

                    esp_err_t add_ret = device_reg_entry_add(mac_bytes_u, nick_str_u);
                    if (ESP_ERR_NO_MEM == add_ret)
                    {
                        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                            "Registry full - max 4 devices");
                        return ESP_FAIL;
                    }
                    if (ESP_ERR_INVALID_STATE == add_ret)
                    {
                        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                            "Device already registered");
                        return ESP_FAIL;
                    }
                    if (ESP_OK != add_ret)
                    {
                        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Failed to add device");
                        return ESP_FAIL;
                    }
                }
            }
        }
    }

    /* Notify all modules that have cached config values. */
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_CONFIG_CHANGED, NULL, 0U, 0U);

    /* Redirect to /config?saved=1 on success. */
    httpd_resp_set_status(p_req, "302 Found");
    httpd_resp_set_hdr(p_req, "Location", "/config?saved=1");
    httpd_resp_send(p_req, NULL, 0);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c POST /config/reset.
 *
 * Resets every configuration parameter to its factory default by erasing the
 * \c esport_cfg NVS namespace and writing all defaults.  Also applies the
 * default timezone immediately and posts \c ESPORT_EVENT_CONFIG_CHANGED so
 * that all subsystems pick up the new values.  On success, issues an HTTP 302
 * redirect to \c /config?reset=1.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_config_reset_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    if (ESP_OK != config_mngr_reset_to_defaults())
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Failed to reset configuration to defaults");
        return ESP_FAIL;
    }

    /* Apply the default timezone immediately so the live system is in sync. */
    time_mngr_timezone_apply();

    /* Notify all modules that have cached config values. */
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_CONFIG_CHANGED, NULL, 0U, 0U);

    /* Redirect to /config?reset=1 to show the confirmation banner. */
    httpd_resp_set_status(p_req, "302 Found");
    httpd_resp_set_hdr(p_req, "Location", "/config?reset=1");
    httpd_resp_send(p_req, NULL, 0);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handle GET /config/pwd -- serve the config password change form.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_config_pwd_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Check for ?saved=1 query string. */
    char qs[16]  = { 0 };
    bool b_saved = false;
    if (0U < httpd_req_get_url_query_len(p_req))
    {
        (void)httpd_req_get_url_query_str(p_req, qs, sizeof(qs));
        char val[4] = { 0 };
        if (ESP_OK == httpd_query_key_value(qs, "saved", val, sizeof(val)))
        {
            b_saved = (0 == strcmp(val, "1"));
        }
    }

    char max_len_str[8];
    (void)snprintf(max_len_str, sizeof(max_len_str), "%u",
        (unsigned)CONFIG_MNGR_CFG_PASSWORD_MAX_LEN);

    (void)httpd_resp_set_type(p_req, "text/html");

    (void)httpd_resp_sendstr_chunk(p_req,
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESPort-fi32 -- Config Password</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:500px;margin:2em auto;padding:0 1em;}"
        "h1{font-size:1.3em;}h2{font-size:1.1em;margin-top:1.5em;}"
        ".card{border:1px solid #ccc;border-radius:6px;padding:1em;margin:1em 0;}"
        "label{display:block;margin:0.4em 0 0.1em;}"
        "input[type=password]{width:100%;box-sizing:border-box;padding:0.4em;}"
        "button,.btn{background:#2a6db5;color:#fff;border:none;padding:0.5em 1.2em;"
        "border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;"
        "margin-top:0.8em;}"
        "button:hover,.btn:hover{background:#1e5490;}"
        ".ok{color:green;font-weight:bold;}"
        "nav a{margin-right:1em;}"
        "</style></head><body>"
        "<h1>Config Password</h1>");

    if (b_saved)
    {
        (void)httpd_resp_sendstr_chunk(p_req, "<p class=\"ok\">Password saved successfully.</p>");
    }

    static char form_buf[512];
    (void)snprintf(form_buf, sizeof(form_buf),
        "<div class=\"card\"><h2>Change Config Password</h2>"
        "<form method=\"POST\" action=\"/config/pwd\">"
        "<label>Current password</label>"
        "<input type=\"password\" name=\"current_pwd\" required>"
        "<label>New password (max %s chars)</label>"
        "<input type=\"password\" name=\"new_pwd\" maxlength=\"%s\" required>"
        "<label>Confirm new password</label>"
        "<input type=\"password\" name=\"confirm_pwd\" maxlength=\"%s\" required>"
        "<button type=\"submit\">Save</button>"
        "</form></div>",
        max_len_str, max_len_str, max_len_str);
    (void)httpd_resp_sendstr_chunk(p_req, form_buf);

    (void)httpd_resp_sendstr_chunk(p_req,
        "<nav><a class=\"btn\" href=\"/config\">Back to Configuration</a></nav>"
        "</body></html>");

    (void)httpd_resp_sendstr_chunk(p_req, NULL);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handle POST /config/pwd -- save a new config page password.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_config_pwd_post_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Read POST body. */
    char * p_body   = NULL;
    int    body_len = (int)p_req->content_len;

    if ((body_len <= 0) || (body_len >= (int)HTTP_SRV_HTML_BUF_LEN))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Invalid body length");
        return ESP_OK;
    }

    p_body = malloc((size_t)body_len + 1U);
    if (NULL == p_body)
    {
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, "Out of memory");
        return ESP_OK;
    }

    int received = httpd_req_recv(p_req, p_body, (size_t)body_len);
    if (received <= 0)
    {
        free(p_body);
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Body read error");
        return ESP_OK;
    }
    p_body[received] = '\0';

    /* Parse fields. */
    static char current_pwd[CONFIG_MNGR_CFG_PASSWORD_MAX_LEN + 1U];
    static char new_pwd[CONFIG_MNGR_CFG_PASSWORD_MAX_LEN + 1U];
    static char confirm_pwd[CONFIG_MNGR_CFG_PASSWORD_MAX_LEN + 1U];
    static char enc_val[HTTP_SRV_FORM_VALUE_ENC_MAX_LEN + 1U];

    current_pwd[0] = '\0';
    new_pwd[0]     = '\0';
    confirm_pwd[0] = '\0';

    if (ESP_OK == httpd_query_key_value(p_body, "current_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, current_pwd, sizeof(current_pwd));
    }
    if (ESP_OK == httpd_query_key_value(p_body, "new_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, new_pwd, sizeof(new_pwd));
    }
    if (ESP_OK == httpd_query_key_value(p_body, "confirm_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, confirm_pwd, sizeof(confirm_pwd));
    }

    free(p_body);

    /* Validate current password. */
    if (!config_mngr_cfg_credentials_check(current_pwd))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Current password incorrect");
        return ESP_OK;
    }

    /* Validate new password length. */
    size_t new_len = strlen(new_pwd);
    if ((0U == new_len) || (new_len > CONFIG_MNGR_CFG_PASSWORD_MAX_LEN))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Password too short or too long");
        return ESP_OK;
    }

    /* Validate passwords match. */
    if (0 != strcmp(new_pwd, confirm_pwd))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Passwords do not match");
        return ESP_OK;
    }

    esp_err_t ret = config_mngr_cfg_password_set(new_pwd);
    if (ESP_OK != ret)
    {
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, esp_err_to_name(ret));
        return ESP_OK;
    }

    /* Serve the success page inline — a redirect would cause the browser to
     * re-authenticate with the now-stale cached credentials, triggering a 401. */
    char max_len_str[8];
    (void)snprintf(max_len_str, sizeof(max_len_str), "%u",
        (unsigned)CONFIG_MNGR_CFG_PASSWORD_MAX_LEN);

    (void)httpd_resp_set_type(p_req, "text/html");

    (void)httpd_resp_sendstr_chunk(p_req,
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESPort-fi32 -- Config Password</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:500px;margin:2em auto;padding:0 1em;}"
        "h1{font-size:1.3em;}h2{font-size:1.1em;margin-top:1.5em;}"
        ".card{border:1px solid #ccc;border-radius:6px;padding:1em;margin:1em 0;}"
        "label{display:block;margin:0.4em 0 0.1em;}"
        "input[type=password]{width:100%;box-sizing:border-box;padding:0.4em;}"
        "button,.btn{background:#2a6db5;color:#fff;border:none;padding:0.5em 1.2em;"
        "border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;"
        "margin-top:0.8em;}"
        "button:hover,.btn:hover{background:#1e5490;}"
        ".ok{color:green;font-weight:bold;}"
        "nav a{margin-right:1em;}"
        "</style></head><body>"
        "<h1>Config Password</h1>");

    (void)httpd_resp_sendstr_chunk(p_req, "<p class=\"ok\">Password saved successfully.</p>");

    static char form_buf[512];
    (void)snprintf(form_buf, sizeof(form_buf),
        "<div class=\"card\"><h2>Change Config Password</h2>"
        "<form method=\"POST\" action=\"/config/pwd\">"
        "<label>Current password</label>"
        "<input type=\"password\" name=\"current_pwd\" required>"
        "<label>New password (max %s chars)</label>"
        "<input type=\"password\" name=\"new_pwd\" maxlength=\"%s\" required>"
        "<label>Confirm new password</label>"
        "<input type=\"password\" name=\"confirm_pwd\" maxlength=\"%s\" required>"
        "<button type=\"submit\">Save</button>"
        "</form></div>",
        max_len_str, max_len_str, max_len_str);
    (void)httpd_resp_sendstr_chunk(p_req, form_buf);

    (void)httpd_resp_sendstr_chunk(p_req,
        "<nav><a class=\"btn\" href=\"/config\">Back to Configuration</a></nav>"
        "</body></html>");

    (void)httpd_resp_sendstr_chunk(p_req, NULL);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Verify the HTTP Basic Auth header on \p p_req for the config endpoints.
 *
 * Sends a 401 response (with \c WWW-Authenticate header) and returns \c false
 * if credentials are absent, malformed, or incorrect.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c true if authentication passed, \c false otherwise.
 */
bool http_srv_cfg_auth_check(httpd_req_t * p_req)
{
    size_t hdr_len = httpd_req_get_hdr_value_len(p_req, "Authorization");

    if (0U == hdr_len)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    static char hdr_buf[HTTP_SRV_CFG_AUTH_HDR_MAX];
    if (ESP_OK != httpd_req_get_hdr_value_str(p_req, "Authorization", hdr_buf, sizeof(hdr_buf)))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    /* Verify "Basic " prefix. */
    if (0 != strncmp(hdr_buf, "Basic ", HTTP_SRV_CFG_BASIC_PREFIX_LEN))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    /* Base64-decode the credential portion. */
    static char decoded[HTTP_SRV_CFG_DECODED_MAX + 1U];
    int         dec_len =
        http_srv_base64_decode(hdr_buf + HTTP_SRV_CFG_BASIC_PREFIX_LEN, decoded, sizeof(decoded));

    if (dec_len < 0)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }
    decoded[dec_len] = '\0';

    /* Split on first ':'. */
    char * p_colon = strchr(decoded, ':');
    if (NULL == p_colon)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }
    *p_colon                = '\0';
    const char * p_user     = decoded;
    const char * p_password = p_colon + 1;

    /* Check username and password. */
    if ((0 != strcmp(p_user, CONFIG_MNGR_CFG_HTTP_USERNAME)) ||
        !config_mngr_cfg_credentials_check(p_password))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 Config\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Parse a MAC address string into a 6-byte array.
 *
 * Accepts colon-separated format \c "AA:BB:CC:DD:EE:FF" (17 characters) and
 * packed hex format \c "AABBCCDDEEFF" (12 hex characters without separators).
 *
 * \param[in]  p_str      Null-terminated input string.
 * \param[out] p_mac_out  Caller-supplied 6-byte array to fill.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG on any malformed input.
 */
static esp_err_t parse_mac_address(const char * p_str, uint8_t * p_mac_out)
{
    if ((NULL == p_str) || (NULL == p_mac_out))
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(p_str);

    if (17U == len)
    {
        /* Colon-separated: "AA:BB:CC:DD:EE:FF" */
        for (int i = 0; i < 6; i++)
        {
            char          tok[3] = { p_str[i * 3], p_str[i * 3 + 1], '\0' };
            char *        endptr;
            unsigned long byte_val = strtoul(tok, &endptr, 16);
            if (('\0' != *endptr) || (byte_val > 0xFFUL))
            {
                return ESP_ERR_INVALID_ARG;
            }
            /* Verify separator except after last byte. */
            if ((i < 5) && (':' != p_str[i * 3 + 2]))
            {
                return ESP_ERR_INVALID_ARG;
            }
            p_mac_out[i] = (uint8_t)byte_val;
        }
        return ESP_OK;
    }

    if (12U == len)
    {
        /* No-colon format: "AABBCCDDEEFF" */
        for (int i = 0; i < 6; i++)
        {
            char          tok[3] = { p_str[i * 2], p_str[i * 2 + 1], '\0' };
            char *        endptr;
            unsigned long byte_val = strtoul(tok, &endptr, 16);
            if (('\0' != *endptr) || (byte_val > 0xFFUL))
            {
                return ESP_ERR_INVALID_ARG;
            }
            p_mac_out[i] = (uint8_t)byte_val;
        }
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
