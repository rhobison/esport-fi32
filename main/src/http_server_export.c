/**
 * \file
 * \brief HTTP server sessions export handler.
 *
 * \date 2026-03-15
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_export.h"
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

#include "config_manager.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_manager.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_export";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t http_srv_export_csv_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);
static esp_err_t http_srv_export_json_send(httpd_req_t * p_req,
    const session_trk_record_t * p_sessions, uint16_t count, const char * p_date_str);

//==================================================================================================
// Private Functions
//==================================================================================================

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
                                        "avg_speed_kmh,distance_m,internet_earned_s\r\n";

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
             ".%02" PRIu32 ",%" PRIu32 "\r\n",
             (int64_t)p_rec->start_time_utc, local_str, p_rec->b_time_synced ? "true" : "false",
             p_rec->duration_s, p_rec->pulse_count, spd_int, spd_dec, dist_m_i, dist_m_d,
             p_rec->internet_earned_s);

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

//==================================================================================================
// Public Functions
//==================================================================================================

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
esp_err_t http_srv_api_sessions_export_handler(httpd_req_t * p_req)
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
