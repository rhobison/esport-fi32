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

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "activity_manager.h"
#include "config_manager.h"
#include "device_registry.h"
#include "dyn_nonce.h"
#include "http_server_config.h"
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
    bool     b_rew_ap      = wifi_mngr_reward_ap_is_active();
    uint8_t  ap_clients    = wifi_mngr_reward_ap_client_count();
    uint32_t counter_s     = time_ctr_get();
    uint32_t threshold     = config_mngr_internet_gate_threshold_s_get();
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

    session_trk_live_status_t session;
    session_trk_live_status_get(&session);

    /* Get connected station list once for device connection checks. */
    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));
    (void)esp_wifi_ap_get_sta_list(&sta_list);

    int n = snprintf(p_buf, HTTP_SRV_JSON_BUF_LEN,
        "{\n"
        "  \"fw_version\": \"%s\",\n"
        "  \"time_utc\": %" PRId64 ",\n"
        "  \"time_local\": \"%s\",\n"
        "  \"time_synced\": %s,\n"
        "  \"uptime_s\": %" PRId64 ",\n"
        "  \"sta_connected\": %s,\n"
        "  \"sta_ssid\": \"%s\",\n"
        "  \"sta_ip\": \"%s\",\n"
        "  \"reward_ap_active\": %s,\n"
        "  \"reward_ap_ssid\": \"%s\",\n"
        "  \"reward_ap_ip\": \"%s\",\n"
        "  \"reward_ap_clients\": %" PRIu8 ",\n"
        "  \"counter_s\": %" PRIu32 ",\n"
        "  \"inet_gate_threshold_s\": %" PRIu32 ",\n"
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
        esp_app_get_description()->version, (int64_t)now_utc, time_local_str,
        b_synced ? "true" : "false", uptime_s, b_sta ? "true" : "false", sta_ssid, sta_ip,
        b_rew_ap ? "true" : "false", rew_ap_ssid, rew_ap_ip, ap_clients, counter_s, threshold,
        session.p_state_name, session.start_utc, session.duration_s, session.pulse_count,
        session.live_speed_kmh_x10, throughput, speed_x10, b_speed_gated ? "true" : "false",
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

        /* For the current rider, use time_ctr_get() so that in-progress
         * session credits (g_session_credits, SESSION state only) are included
         * in the live counter, not just the flushed device-registry value. */
        bool     b_is_rider        = (rider_idx == i);
        uint32_t display_counter_s = b_is_rider ? time_ctr_get() : entry.counter_s;

        char hms[20];
        snprintf(hms, sizeof(hms), "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
            display_counter_s / 3600U, (display_counter_s % 3600U) / 60U, display_counter_s % 60U);

        bool     b_internet_active = entry.b_enabled && (display_counter_s > 0U);
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
            (0U == i) ? "" : ",", i, entry.nickname, mac_str, display_counter_s, hms,
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
              "\"avg_speed_kmh_x10\":%" PRIu16 ","
              "\"internet_earned_s\":%" PRIu32 "}",
            (0 == i) ? "" : ",", (int64_t)p_rec->start_time_utc, local_str,
            p_rec->b_time_synced ? "true" : "false", p_rec->duration_s, p_rec->pulse_count,
             p_rec->avg_speed_kmh_x10, p_rec->internet_earned_s);

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

esp_err_t http_srv_api_activities_get_handler(httpd_req_t * p_req)
{
    /* Parse optional device_idx query parameter. */
    uint8_t dev_idx   = 0xFFU; /* 0xFF = no filter */
    bool    b_has_dev = false;

    char query_buf[32];
    if (ESP_OK == httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        char dev_val[8];
        if (ESP_OK == httpd_query_key_value(query_buf, "device_idx", dev_val, sizeof(dev_val)))
        {
            char * p_end = NULL;
            long   dv    = strtol(dev_val, &p_end, 10);
            if ((p_end != dev_val) && (dv >= 0L) && (dv < (long)DEVICE_REG_MAX_ENTRIES))
            {
                dev_idx   = (uint8_t)dv;
                b_has_dev = true;
            }
        }
    }

    char * p_buf = (char *)malloc(4096U);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t pos = 0U;

#define ACT_APPEND(fmt, ...) pos += (size_t)snprintf(p_buf + pos, 4096U - pos, fmt, ##__VA_ARGS__)

    ACT_APPEND("{\"activities\":[");

    uint8_t pool_cnt   = act_mngr_activity_count();
    bool    first_item = true;

    for (uint8_t s = 0U; s < pool_cnt; s++)
    {
        act_mngr_entry_t entry;
        if (ESP_OK != act_mngr_activity_slot_get(s, &entry))
        {
            continue;
        }

        /* When device_idx specified: only assigned static activities (b_is_dynamic == 0). */
        if (b_has_dev)
        {
            if (!act_mngr_user_is_assigned(dev_idx, entry.id))
            {
                continue;
            }
            if (0U != entry.b_is_dynamic)
            {
                continue;
            }
        }

        if (!first_item)
        {
            ACT_APPEND(",");
        }
        first_item = false;

        /* Format credit and time_limit as h:mm:ss. */
        uint32_t c_h  = entry.credit_s / 3600U;
        uint32_t c_m  = (entry.credit_s % 3600U) / 60U;
        uint32_t c_s  = entry.credit_s % 60U;
        uint32_t tl_h = entry.time_limit_s / 3600U;
        uint32_t tl_m = (entry.time_limit_s % 3600U) / 60U;
        uint32_t tl_s = entry.time_limit_s % 60U;

        ACT_APPEND("{\"id\":%lu,\"slot\":%u,\"name\":\"%s\","
                   "\"credit_s\":%lu,"
                   "\"credit_hms\":\"%lu:%02lu:%02lu\","
                   "\"time_limit_s\":%lu,"
                   "\"time_limit_hms\":\"%lu:%02lu:%02lu\","
                   "\"daily_limit\":%u",
            (unsigned long)entry.id, (unsigned int)s, entry.name, (unsigned long)entry.credit_s,
            (unsigned long)c_h, (unsigned long)c_m, (unsigned long)c_s,
            (unsigned long)entry.time_limit_s, (unsigned long)tl_h, (unsigned long)tl_m,
            (unsigned long)tl_s, (unsigned int)entry.daily_limit);

        if (b_has_dev)
        {
            uint8_t done      = act_mngr_user_daily_done_get(dev_idx, entry.id);
            bool    available = (done < entry.daily_limit);
            ACT_APPEND(",\"done_today\":%u,\"available\":%s", (unsigned int)done,
                available ? "true" : "false");
        }

        ACT_APPEND("}");
    }

    ACT_APPEND("]}");

#undef ACT_APPEND

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_api_activities_credit_handler(httpd_req_t * p_req)
{
    char body[256];
    int  recv_len = httpd_req_recv(p_req, body, sizeof(body) - 1U);
    if (recv_len <= 0)
    {
        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[recv_len] = '\0';

    /* Parse JSON fields with strstr + strtoul (simple fixed-format body). */
    uint32_t device_idx        = 0xFFU;
    uint32_t act_id            = 0U;
    uint32_t credits_s         = 0U;
    uint32_t completion_time_s = 0U;

    const char * p;

    p = strstr(body, "\"device_idx\"");
    if (NULL != p)
    {
        p = strchr(p, ':');
        if (NULL != p)
        {
            device_idx = (uint32_t)strtoul(p + 1, NULL, 10);
        }
    }

    p = strstr(body, "\"act_id\"");
    if (NULL != p)
    {
        p = strchr(p, ':');
        if (NULL != p)
        {
            act_id = (uint32_t)strtoul(p + 1, NULL, 10);
        }
    }

    p = strstr(body, "\"credits_s\"");
    if (NULL != p)
    {
        p = strchr(p, ':');
        if (NULL != p)
        {
            credits_s = (uint32_t)strtoul(p + 1, NULL, 10);
        }
    }

    p = strstr(body, "\"completion_time_s\"");
    if (NULL != p)
    {
        p = strchr(p, ':');
        if (NULL != p)
        {
            completion_time_s = (uint32_t)strtoul(p + 1, NULL, 10);
        }
    }

    /* Parse optional PIN field. */
    char received_pin[DEVICE_REG_PIN_LEN + 1U];
    received_pin[0] = '\0';
    p               = strstr(body, "\"pin\"");
    if (NULL != p)
    {
        /* Locate the opening quote of the value. */
        p = strchr(p + 5, '"');
        if (NULL != p)
        {
            p++; /* step past opening quote */
            size_t pin_idx = 0U;
            while (('\0' != *p) && ('"' != *p) && (pin_idx < DEVICE_REG_PIN_LEN))
            {
                received_pin[pin_idx++] = *p++;
            }
            received_pin[pin_idx] = '\0';
        }
    }

    /* Parse optional one-time token field (required for PIN auth). */
    char received_token[DYN_NONCE_STRLEN + 1U];
    received_token[0] = '\0';
    p                 = strstr(body, "\"token\"");
    if (NULL != p)
    {
        p = strchr(p + 7, '"');
        if (NULL != p)
        {
            p++;
            size_t tok_idx = 0U;
            while (('\0' != *p) && ('"' != *p) && (tok_idx < DYN_NONCE_STRLEN))
            {
                received_token[tok_idx++] = *p++;
            }
            received_token[tok_idx] = '\0';
        }
    }

    /* Dual auth: admin Basic Auth OR device PIN + one-time token. */
    bool b_admin = http_srv_cfg_auth_check_silent(p_req);
    bool b_pin   = false;

    if (!b_admin && ('\0' != received_pin[0]))
    {
        char expected_pin[DEVICE_REG_PIN_LEN + 1U];
        if ((device_idx < (uint32_t)DEVICE_REG_MAX_ENTRIES) &&
            (device_reg_pin_compute((uint8_t)device_idx, expected_pin) == ESP_OK) &&
            (strncmp(received_pin, expected_pin, DEVICE_REG_PIN_LEN) == 0) &&
            dyn_nonce_consume(received_token, (uint8_t)device_idx, act_id))
        {
            b_pin = true;
        }
    }

    if (!b_admin && !b_pin)
    {
        httpd_resp_set_status(p_req, "403 Forbidden");
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_sendstr(p_req, "{\"error\":\"unauthorized\"}");
        return ESP_OK;
    }

    /* Basic input validation. */
    if ((device_idx >= (uint32_t)DEVICE_REG_MAX_ENTRIES) || (ACT_MNGR_NO_ID == act_id) ||
        (0U == credits_s))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"invalid arguments\"}");
        return ESP_OK;
    }

    /* Credit cap for PIN-authenticated calls. */
    if (!b_admin && b_pin)
    {
        act_mngr_entry_t act_entry;
        if ((act_mngr_activity_get(act_id, &act_entry) == ESP_OK) &&
            (credits_s > act_entry.credit_s))
        {
            credits_s = act_entry.credit_s;
        }
    }

    esp_err_t credit_ret =
        act_mngr_activity_credit((uint8_t)device_idx, act_id, credits_s, completion_time_s);

    char resp[128];

    if (ESP_OK == credit_ret)
    {
        uint32_t new_ctr = device_reg_entry_counter_get((uint8_t)device_idx);
        uint32_t h       = new_ctr / 3600U;
        uint32_t m       = (new_ctr % 3600U) / 60U;
        uint32_t s       = new_ctr % 60U;
        (void)snprintf(resp, sizeof(resp),
            "{\"ok\":true,\"new_counter_s\":%lu,\"new_counter_hms\":\"%lu:%02lu:%02lu\"}",
            (unsigned long)new_ctr, (unsigned long)h, (unsigned long)m, (unsigned long)s);
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_sendstr(p_req, resp);
    }
    else if (ESP_ERR_NOT_FOUND == credit_ret)
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"activity not found\"}");
    }
    else if (ESP_ERR_INVALID_STATE == credit_ret)
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"activity not assigned to user\"}");
    }
    else if (ESP_ERR_NOT_ALLOWED == credit_ret)
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "429 Too Many Requests");
        httpd_resp_sendstr(p_req, "{\"error\":\"daily limit reached\"}");
    }
    else
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"invalid arguments\"}");
    }

    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_api_activities_log_get_handler(httpd_req_t * p_req)
{
    char query_buf[32];
    if (ESP_OK != httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx required\"}");
        return ESP_OK;
    }

    char dev_val[8];
    if (ESP_OK != httpd_query_key_value(query_buf, "device_idx", dev_val, sizeof(dev_val)))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx required\"}");
        return ESP_OK;
    }

    char * p_end   = NULL;
    long   dev_int = strtol(dev_val, &p_end, 10);
    if ((p_end == dev_val) || (dev_int < 0L) || (dev_int >= (long)DEVICE_REG_MAX_ENTRIES))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx out of range\"}");
        return ESP_OK;
    }

    uint8_t dev_idx = (uint8_t)dev_int;

    uint8_t log_cnt = act_mngr_credit_log_count(dev_idx);
    if (0U == log_cnt)
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_sendstr(p_req, "{\"log\":[]}");
        return ESP_OK;
    }

    act_credit_log_entry_t * p_log =
        (act_credit_log_entry_t *)malloc(log_cnt * sizeof(act_credit_log_entry_t));
    if (NULL == p_log)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    uint8_t actual = act_mngr_credit_log_read(dev_idx, p_log, log_cnt);

    char * p_buf = (char *)malloc(4096U);
    if (NULL == p_buf)
    {
        free(p_log);
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t pos = 0U;

#define LOG_APPEND(fmt, ...) pos += (size_t)snprintf(p_buf + pos, 4096U - pos, fmt, ##__VA_ARGS__)

    LOG_APPEND("{\"log\":[");

    for (uint8_t i = 0U; i < actual; i++)
    {
        if (i > 0U)
        {
            LOG_APPEND(",");
        }

        const act_credit_log_entry_t * p_e = &p_log[i];

        /* Resolve activity name. */
        act_mngr_entry_t act;
        const char *     p_act_name = "(deleted)";
        char             act_name_buf[ACT_MNGR_NAME_MAX_LEN + 1U];
        if (ESP_OK == act_mngr_activity_get(p_e->act_id, &act))
        {
            (void)strncpy(act_name_buf, act.name, sizeof(act_name_buf) - 1U);
            act_name_buf[sizeof(act_name_buf) - 1U] = '\0';
            p_act_name                              = act_name_buf;
        }

        /* Format local time. */
        time_t    ts = (time_t)p_e->timestamp_utc;
        struct tm local_tm;
        localtime_r(&ts, &local_tm);
        char time_str[32];
        (void)strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", &local_tm);

        /* Format credits as h:mm:ss. */
        uint32_t c_h = p_e->credits_s / 3600U;
        uint32_t c_m = (p_e->credits_s % 3600U) / 60U;
        uint32_t c_s = p_e->credits_s % 60U;

        LOG_APPEND("{\"timestamp_utc\":%" PRId64 ","
                   "\"timestamp_local\":\"%s\","
                   "\"act_id\":%lu,"
                   "\"act_name\":\"%s\","
                   "\"credits_s\":%lu,"
                   "\"credits_hms\":\"%lu:%02lu:%02lu\","
                   "\"completion_time_s\":%lu}",
            (int64_t)p_e->timestamp_utc, time_str, (unsigned long)p_e->act_id, p_act_name,
            (unsigned long)p_e->credits_s, (unsigned long)c_h, (unsigned long)c_m,
            (unsigned long)c_s, (unsigned long)p_e->completion_time_s);
    }

    LOG_APPEND("]}");

#undef LOG_APPEND

    free(p_log);

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

#include "dyn_act_registry.h"

/**
 * \brief Handler for \c GET /api/dyn.
 *
 * Returns the PIN and the list of assigned dynamic activities for a device.
 * Required query parameter: \c device_idx (0\u20133).  No admin auth required.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_api_dyn_get_handler(httpd_req_t * p_req)
{
    /* Parse required device_idx query parameter. */
    char query_buf[32];
    if (ESP_OK != httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx required\"}");
        return ESP_OK;
    }

    char dev_val[8];
    if (ESP_OK != httpd_query_key_value(query_buf, "device_idx", dev_val, sizeof(dev_val)))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx required\"}");
        return ESP_OK;
    }

    char * p_end = NULL;
    long   dv    = strtol(dev_val, &p_end, 10);
    if ((p_end == dev_val) || (dv < 0L) || (dv >= (long)DEVICE_REG_MAX_ENTRIES))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device_idx required\"}");
        return ESP_OK;
    }
    uint8_t dev_idx = (uint8_t)dv;

    /* Validate registration. */
    device_reg_entry_t dev_entry;
    if (ESP_OK != device_reg_entry_get(dev_idx, &dev_entry))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device not registered\"}");
        return ESP_OK;
    }
    static const uint8_t sc_zero_mac[6U] = { 0U, 0U, 0U, 0U, 0U, 0U };
    if (0 == memcmp(dev_entry.mac, sc_zero_mac, 6U))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "400 Bad Request");
        httpd_resp_sendstr(p_req, "{\"error\":\"device not registered\"}");
        return ESP_OK;
    }

    /* Compute PIN. */
    char pin[DEVICE_REG_PIN_LEN + 1U];
    if (ESP_OK != device_reg_pin_compute(dev_idx, pin))
    {
        httpd_resp_set_type(p_req, "application/json");
        httpd_resp_set_status(p_req, "500 Internal Server Error");
        httpd_resp_sendstr(p_req, "{\"error\":\"pin error\"}");
        return ESP_OK;
    }

    /* Build JSON response. */
    char * p_buf = (char *)malloc(1024U);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t pos = 0U;

#define DYN_APPEND(fmt, ...) pos += (size_t)snprintf(p_buf + pos, 1024U - pos, fmt, ##__VA_ARGS__)

    DYN_APPEND("{\"pin\":\"%s\",\"device_idx\":%u,\"activities\":[", pin, (unsigned int)dev_idx);

    act_mngr_user_assigns_t assigns;
    (void)act_mngr_user_assigns_get(dev_idx, &assigns);

    bool first = true;
    for (uint8_t i = 0U; i < assigns.count; i++)
    {
        uint32_t         aid = assigns.act_ids[i];
        act_mngr_entry_t act_entry;
        if (ACT_MNGR_NO_ID == aid)
        {
            continue;
        }
        if (ESP_OK != act_mngr_activity_get(aid, &act_entry))
        {
            continue;
        }
        if (0U == act_entry.b_is_dynamic)
        {
            continue; /* Only dynamic activities. */
        }

        uint8_t done      = act_mngr_user_daily_done_get(dev_idx, aid);
        bool    available = !act_mngr_user_daily_limit_reached(dev_idx, aid);

        uint32_t c_h  = act_entry.credit_s / 3600U;
        uint32_t c_m  = (act_entry.credit_s % 3600U) / 60U;
        uint32_t c_s  = act_entry.credit_s % 60U;
        uint32_t tl_h = act_entry.time_limit_s / 3600U;
        uint32_t tl_m = (act_entry.time_limit_s % 3600U) / 60U;
        uint32_t tl_s = act_entry.time_limit_s % 60U;

        if (!first)
        {
            DYN_APPEND(",");
        }
        first = false;

        char tok[DYN_NONCE_STRLEN + 1U];
        dyn_nonce_generate((uint8_t)dev_idx, aid, tok);

        DYN_APPEND("{\"act_id\":%lu,\"name\":\"%s\","
                   "\"credit_s\":%lu,"
                   "\"credits_hms\":\"%lu:%02lu:%02lu\","
                   "\"time_limit_s\":%lu,"
                   "\"time_limit_hms\":\"%lu:%02lu:%02lu\","
                   "\"done_today\":%u,\"available\":%s,"
                   "\"token\":\"%s\"}",
            (unsigned long)aid, act_entry.name, (unsigned long)act_entry.credit_s,
            (unsigned long)c_h, (unsigned long)c_m, (unsigned long)c_s,
            (unsigned long)act_entry.time_limit_s, (unsigned long)tl_h, (unsigned long)tl_m,
            (unsigned long)tl_s, (unsigned int)done, available ? "true" : "false", tok);
    }

    DYN_APPEND("]}");

#undef DYN_APPEND

    httpd_resp_set_type(p_req, "application/json");
    httpd_resp_send(p_req, p_buf, HTTPD_RESP_USE_STRLEN);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
