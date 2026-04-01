/**
 * \file
 * \brief HTTP server JSON API handlers.
 *
 * \date 2026-03-15
 */

//==================================================================================================
// Includes
//==================================================================================================

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
#include "esp_wifi.h"

#include "config_manager.h"
#include "device_registry.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_api";

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Fill a caller-supplied array of daily aggregation bins for the 31-day window.
 *
 * Determines today's local date from \c time_mngr_utc_get(), fills
 * \p p_bins[0..#HTTP_SRV_DAILY_WINDOW_DAYS-1] with calendar dates (oldest
 * first, ending today), then accumulates every entry in
 * \p p_sessions[0..\p count-1] into the matching bin by local date.
 * The caller must zero \p p_bins before calling this function.
 *
 * \param[out] p_bins     Caller-supplied array of #HTTP_SRV_DAILY_WINDOW_DAYS bins.
 * \param[in]  p_sessions Array of session records to aggregate.
 * \param[in]  count      Number of valid entries in \p p_sessions.
 */
void http_srv_daily_bins_build(http_srv_daily_bin_t * p_bins,
    const session_trk_record_t * p_sessions, uint16_t count)
{
    time_t    now_utc = (time_t)time_mngr_utc_get();
    struct tm today;
    localtime_r(&now_utc, &today);
    today.tm_hour = 0;
    today.tm_min  = 0;
    today.tm_sec  = 0;
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

    for (uint16_t i = 0U; i < count; i++)
    {
        time_t    t = (time_t)p_sessions[i].start_time_utc;
        struct tm sess_tm;
        localtime_r(&t, &sess_tm);

        for (int d = 0; d < (int)HTTP_SRV_DAILY_WINDOW_DAYS; d++)
        {
            if ((p_bins[d].year == sess_tm.tm_year) && (p_bins[d].mon == sess_tm.tm_mon) &&
                (p_bins[d].mday == sess_tm.tm_mday))
            {
                p_bins[d].sessions++;
                p_bins[d].speed_sum_x10 += (uint64_t)p_sessions[i].avg_speed_kmh_x10;
                p_bins[d].total_duration_s += p_sessions[i].duration_s;
                break;
            }
        }
    }
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
esp_err_t http_srv_api_status_handler(httpd_req_t * p_req)
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

    int64_t  uptime_s      = (int64_t)(esp_timer_get_time() / 1000000LL);
    bool     b_synced      = time_mngr_is_synced();
    bool     b_sta         = wifi_mngr_sta_is_connected();
    bool     b_cfg_ap      = wifi_mngr_config_ap_is_active();
    bool     b_rew_ap      = wifi_mngr_reward_ap_is_active();
    uint8_t  ap_clients    = wifi_mngr_reward_ap_client_count();
    uint32_t counter_s     = time_ctr_get();
    uint32_t threshold     = config_mngr_soft_ap_start_threshold_s_get();
    uint32_t throughput    = wifi_mngr_reward_ap_throughput_kbps();
    uint32_t speed_x10     = time_ctr_current_speed_x10_get();
    uint16_t min_spd_cfg   = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    bool     b_speed_gated = (min_spd_cfg > 0U) && (speed_x10 < (uint32_t)min_spd_cfg);
    uint8_t  rider_idx     = device_reg_current_rider_get();

    char sta_ip[20];
    char sta_ssid[33];
    char rew_ap_ssid[33];
    char rew_ap_ip[20];
    wifi_mngr_sta_ip_get(sta_ip, sizeof(sta_ip));
    config_mngr_wifi_ssid_get(sta_ssid, sizeof(sta_ssid));
    config_mngr_soft_ap_ssid_get(rew_ap_ssid, sizeof(rew_ap_ssid));
    wifi_mngr_reward_ap_ip_get(rew_ap_ip, sizeof(rew_ap_ip));

    session_trk_live_status_t sess;
    session_trk_live_status_get(&sess);

    /* Get connected station list once for device connection checks. */
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    (void)esp_wifi_ap_get_sta_list(&sta_list);

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
        "  \"reward_ap_ip\": \"%s\",\n"
        "  \"reward_ap_clients\": %" PRIu8 ",\n"
        "  \"counter_s\": %" PRIu32 ",\n"
        "  \"threshold_s\": %" PRIu32 ",\n"
        "  \"session_state\": \"%s\",\n"
        "  \"session_start_utc\": %" PRId64 ",\n"
        "  \"session_duration_s\": %" PRIu32 ",\n"
        "  \"session_pulse_count\": %" PRIu32 ",\n"
        "  \"live_speed_kmh_x10\": %" PRIu16 ",\n"
        "  \"reward_ap_throughput_kbps\": %" PRIu32 ",\n"
        "  \"current_speed_kmh_x10\": %" PRIu32 ",\n"
        "  \"speed_gate_active\": %s,\n"
        "  \"current_rider_idx\": %" PRIu8 ",\n"
        "  \"devices\": [",
        (int64_t)now_utc, time_local_str, b_synced ? "true" : "false", uptime_s,
        b_sta ? "true" : "false", sta_ssid, sta_ip, b_cfg_ap ? "true" : "false",
        b_rew_ap ? "true" : "false", rew_ap_ssid, rew_ap_ip, ap_clients, counter_s, threshold,
        sess.p_state_name, sess.start_utc, sess.duration_s, sess.pulse_count,
        sess.live_speed_kmh_x10, throughput, speed_x10, b_speed_gated ? "true" : "false",
        rider_idx);

    if (n >= (int)HTTP_SRV_JSON_BUF_LEN)
    {
        ESP_LOGW(gp_tag, "api/status JSON truncated (header)");
    }

    int     pos       = n;
    uint8_t dev_count = device_reg_count_get();

    for (uint8_t i = 0U; i < dev_count; i++)
    {
        device_reg_entry_t entry;
        if (ESP_OK != device_reg_entry_get(i, &entry))
        {
            continue;
        }

        /* Check if connected. */
        bool b_connected = false;
        for (int j = 0; j < (int)sta_list.num; j++)
        {
            if (0 == memcmp(entry.mac, sta_list.sta[j].mac, DEVICE_REG_MAC_LEN))
            {
                b_connected = true;
                break;
            }
        }

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", entry.mac[0],
            entry.mac[1], entry.mac[2], entry.mac[3], entry.mac[4], entry.mac[5]);

        char hms[20];
        snprintf(hms, sizeof(hms), "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, entry.counter_s / 3600U,
            (entry.counter_s % 3600U) / 60U, entry.counter_s % 60U);

        bool     b_internet_active = entry.b_enabled && (entry.counter_s > 0U);
        bool     b_is_rider        = (rider_idx == i);
        uint32_t kbps              = device_reg_entry_throughput_kbps_get(i);
        bool     b_paused          = device_reg_entry_is_paused(i);

        char entry_buf[512];
        int  en = snprintf(entry_buf, sizeof(entry_buf),
             "%s{\"idx\":%" PRIu8 ","
              "\"nickname\":\"%s\","
              "\"mac\":\"%s\","
              "\"counter_s\":%" PRIu32 ","
              "\"counter_hms\":\"%s\","
              "\"enabled\":%s,"
              "\"internet_active\":%s,"
              "\"is_current_rider\":%s,"
              "\"connected\":%s,"
              "\"throughput_kbps\":%" PRIu32 ","
              "\"paused\":%s}",
            (0U == i) ? "" : ",", i, entry.nickname, mac_str, entry.counter_s, hms,
            entry.b_enabled ? "true" : "false", b_internet_active ? "true" : "false",
            b_is_rider ? "true" : "false", b_connected ? "true" : "false", kbps,
            b_paused ? "true" : "false");

        if ((en > 0) && ((pos + en + 4) < (int)HTTP_SRV_JSON_BUF_LEN))
        {
            memcpy(p_buf + pos, entry_buf, (size_t)en);
            pos += en;
        }
        else
        {
            ESP_LOGW(gp_tag, "api/status devices array truncated at index %u", (unsigned)i);
            break;
        }
    }

    /* Close the devices array and the JSON object. */
    int trail = snprintf(p_buf + pos, (size_t)((int)HTTP_SRV_JSON_BUF_LEN - pos), "]\n}\n");
    if (trail > 0)
    {
        pos += trail;
    }
    (void)pos;

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
esp_err_t http_srv_api_sessions_handler(httpd_req_t * p_req)
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
esp_err_t http_srv_api_sessions_daily_handler(httpd_req_t * p_req)
{
    session_trk_record_t * p_sessions = malloc(SESSION_LOG_MAX_ENTRIES * sizeof(*p_sessions));
    if (NULL == p_sessions)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    uint16_t count = session_log_read(p_sessions, SESSION_LOG_MAX_ENTRIES);

    static http_srv_daily_bin_t bins[HTTP_SRV_DAILY_WINDOW_DAYS];
    memset(bins, 0, sizeof(bins));
    http_srv_daily_bins_build(bins, p_sessions, count);

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
