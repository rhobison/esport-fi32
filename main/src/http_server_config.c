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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "config_manager.h"
#include "event_ids.h"
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
    uint16_t    spp         = config_mngr_seconds_per_pulse_get();
    uint32_t    ap_thresh   = config_mngr_soft_ap_start_threshold_s_get();
    uint32_t    cpp         = config_mngr_centimeters_per_pulse_get();
    uint16_t    idle_s      = config_mngr_idle_session_interval_s_get();
    uint16_t    start_s     = config_mngr_start_session_interval_s_get();
    uint16_t    debounce_ms = config_mngr_pulse_debounce_time_ms_get();
    uint16_t    ap_thr_kbps = config_mngr_soft_ap_dec_threshold_kbps_get();
    uint16_t    ap_idle_tmo = config_mngr_soft_ap_idle_throughput_timeout_s_get();
    uint16_t    min_spd_x10 = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    uint32_t    reward_ctr  = time_ctr_get();
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
        ".reset-banner{background:#dff;padding:.5em 1em;border:1px solid #69a;"
        "border-radius:4px;margin-bottom:1em}"
        ".btn-reset{background:#e55;color:#fff;border:none;padding:6px 14px;"
        "cursor:pointer;border-radius:3px;font-size:1em}"
        ".btn-reset:hover{background:#c33}"
        "</style></head><body>"
        "<h2>esport-fi32 &mdash; Configuration</h2>";
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

    (void)httpd_resp_sendstr_chunk(p_req, "<form method=\"POST\" action=\"/config\">");

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
    snprintf(num, sizeof(num), "%" PRIu16, spp);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Seconds per Pulse</label>"
        "<input type=\"number\" name=\"seconds_per_pulse\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"1\" max=\"60\"></p>");

    /* soft_ap_start_threshold_s */
    snprintf(num, sizeof(num), "%" PRIu32, ap_thresh);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>AP Start Threshold (s)</label>"
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
    if ('\0' != enc[0])
    {
        (void)httpd_resp_sendstr_chunk(p_req, enc);
    }
    (void)httpd_resp_sendstr_chunk(p_req, "\" maxlength=\"63\"></p>");

    /* soft_ap_dec_time_above_threshold_kbps */
    snprintf(num, sizeof(num), "%" PRIu16, ap_thr_kbps);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Reward AP idle throughput threshold (kbps)</label>"
        "<input type=\"number\" name=\"soft_ap_dec_time_above_threshold_kbps\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* soft_ap_idle_throughput_timeout_s */
    snprintf(num, sizeof(num), "%" PRIu16, ap_idle_tmo);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Idle throughput timeout (s)</label>"
        "<input type=\"number\" name=\"soft_ap_idle_throughput_timeout_s\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* min_speed_to_increment_time_kmh_x10 */
    snprintf(num, sizeof(num), "%" PRIu16, min_spd_x10);
    (void)httpd_resp_sendstr_chunk(p_req,
        "<p><label>Minimum speed to earn credits (km/h &times; 10)</label>"
        "<input type=\"number\" name=\"min_speed_to_increment_time_kmh_x10\" value=\"");
    (void)httpd_resp_sendstr_chunk(p_req, num);
    (void)httpd_resp_sendstr_chunk(p_req, "\" min=\"0\" max=\"65535\"></p>");

    /* reward_counter_s — displayed in hh:mm:ss format */
    {
        uint32_t rc_h = reward_ctr / 3600U;
        uint32_t rc_m = (reward_ctr % 3600U) / 60U;
        uint32_t rc_s = reward_ctr % 60U;
        /* Allocate a small buffer on the stack — fits any uint32 hours value. */
        char     rc_str[20];
        snprintf(rc_str, sizeof(rc_str), "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32, rc_h, rc_m,
            rc_s);
        (void)httpd_resp_sendstr_chunk(p_req,
            "<p><label>Reward Counter (hh:mm:ss)</label>"
            "<input type=\"text\" name=\"reward_counter_s\" value=\"");
        (void)httpd_resp_sendstr_chunk(p_req, rc_str);
        (void)httpd_resp_sendstr_chunk(p_req,
            "\" placeholder=\"hh:mm:ss\" maxlength=\"8\""
            " oninput=\"var d=this.value.replace(/\\D/g,'').slice(0,6);"
            "if(d.length>4)this.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4);"
            "else if(d.length>2)this.value=d.slice(0,2)+':'+d.slice(2);"
            "else this.value=d;\"></p>");
    }

    /* ---- Footer ---- */
    static const char sc_footer[] =
        "<p style=\"margin-top:1em\">"
        "<input type=\"submit\" value=\"Save Configuration\"></p>"
        "</form>"
        "<form method=\"POST\" action=\"/config/reset\""
        " onsubmit=\"return confirm('Reset ALL settings to factory defaults?\\nThis cannot be "
        "undone.')\""
        " style=\"margin-top:1.5em;padding-top:1em;border-top:1px solid #ccc\">"
        "<p><button type=\"submit\" class=\"btn-reset\">Reset to Factory Defaults</button></p>"
        "</form>"
        "</body></html>";
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
esp_err_t http_srv_config_post_handler(httpd_req_t * p_req)
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

    /* min_speed_to_increment_time_kmh_x10 (uint16, range 0–65535) */
    if (ESP_OK == http_srv_form_field_get(body, "min_speed_to_increment_time_kmh_x10", num_str,
                      sizeof(num_str)))
    {
        char *        endptr;
        unsigned long val = strtoul(num_str, &endptr, 10);
        if (('\0' != *endptr) || (val > 65535UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "min_speed_to_increment_time_kmh_x10 must be 0-65535");
            return ESP_FAIL;
        }
        if (ESP_OK != config_mngr_min_speed_to_increment_time_kmh_x10_set((uint16_t)val))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "Invalid min_speed_to_increment_time_kmh_x10");
            return ESP_FAIL;
        }
    }

    /* reward_counter_s — parsed from hh:mm:ss text field */
    char rc_str[20];
    if (ESP_OK == http_srv_form_field_get(body, "reward_counter_s", rc_str, sizeof(rc_str)))
    {
        /* Expect at least one ':' separating h from mm:ss. */
        char * colon1 = strchr(rc_str, ':');
        char * colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;

        if ((NULL == colon1) || (NULL == colon2) || (strchr(colon2 + 1, ':') != NULL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "reward_counter_s must be in hh:mm:ss format");
            return ESP_FAIL;
        }

        *colon1          = '\0';
        *colon2          = '\0';
        const char * p_h = rc_str;
        const char * p_m = colon1 + 1;
        const char * p_s = colon2 + 1;

        char *        endptr;
        unsigned long h = strtoul(p_h, &endptr, 10);
        if ('\0' != *endptr)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "reward_counter_s: hours must be a valid integer");
            return ESP_FAIL;
        }
        unsigned long m = strtoul(p_m, &endptr, 10);
        if (('\0' != *endptr) || (m > 59UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "reward_counter_s: minutes must be 0-59");
            return ESP_FAIL;
        }
        unsigned long s = strtoul(p_s, &endptr, 10);
        if (('\0' != *endptr) || (s > 59UL))
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "reward_counter_s: seconds must be 0-59");
            return ESP_FAIL;
        }

        /* Overflow check: UINT32_MAX / 3600 ≈ 1193046. */
        if (h > 1193046UL)
        {
            httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST,
                "reward_counter_s: value exceeds maximum representable seconds");
            return ESP_FAIL;
        }

        uint32_t total_s = (uint32_t)(h * 3600UL + m * 60UL + s);
        /* Apply immediately — also saves to NVS; do NOT call config_mngr_reward_counter_s_set. */
        (void)time_ctr_counter_set(total_s);
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

/*** end of file ***/
