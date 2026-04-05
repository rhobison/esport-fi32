/**
 * \file
 * \brief SNTP time synchronisation and timezone management - full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "time_manager.h"

#include <stdlib.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "config_manager.h"
#include "event_ids.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Maximum length of a POSIX TZ string (spec §3, including NUL). */
#define TIME_MNGR_TZ_BUF_LEN (64U)

/** Fallback base epoch: 2000-01-01T00:00:00Z in Unix seconds. */
#define TIME_MNGR_FALLBACK_EPOCH_S (946684800LL)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "time_manager";

/** Set to true on the first successful SNTP synchronisation. */
static bool gb_time_synced = false;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

/**
 * \brief SNTP synchronisation notification callback.
 *
 * Invoked by the SNTP stack after a successful time sync.  Sets the
 * module-level #gb_time_synced flag.
 *
 * \param[in] p_tv  Pointer to the newly synchronised \c timeval; unused.
 */
static void time_mngr_sync_cb(struct timeval * p_tv);

/**
 * \brief Default event loop handler that starts SNTP on STA connection.
 *
 * \param[in] p_arg        Unused user argument.
 * \param[in] p_event_base Event base; expected to be #ESPORT_EVENT_BASE.
 * \param[in] event_id     Event identifier; expected to be
 *                         #ESPORT_EVENT_STA_CONNECTED.
 * \param[in] p_event_data Unused event payload.
 */
static void time_mngr_sta_connected_handler(void * p_arg, esp_event_base_t p_event_base,
    int32_t event_id, void * p_event_data);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t time_mngr_init(void)
{
    time_mngr_timezone_apply();

    esp_err_t ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_STA_CONNECTED,
        time_mngr_sta_connected_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "failed to register STA_CONNECTED handler: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete - awaiting STA connection for SNTP");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

bool time_mngr_is_synced(void)
{
    return gb_time_synced;
}

//--------------------------------------------------------------------------------------------------

time_t time_mngr_utc_get(void)
{
    if (gb_time_synced)
    {
        return time(NULL);
    }

    return (time_t)(TIME_MNGR_FALLBACK_EPOCH_S + (int64_t)(esp_timer_get_time() / 1000000LL));
}

//--------------------------------------------------------------------------------------------------

void time_mngr_timezone_apply(void)
{
    char tz_buf[TIME_MNGR_TZ_BUF_LEN];

    config_mngr_timezone_get(tz_buf, sizeof(tz_buf));
    setenv("TZ", tz_buf, 1);
    tzset();

    ESP_LOGI(gp_tag, "timezone applied: %s", tz_buf);
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

static void time_mngr_sync_cb(struct timeval * p_tv)
{
    (void)p_tv;

    gb_time_synced = true;

    time_t    now = time(NULL);
    struct tm time_info;
    char      time_buf[32];

    localtime_r(&now, &time_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &time_info);
    ESP_LOGI(gp_tag, "SNTP sync complete - local time: %s", time_buf);
}

//--------------------------------------------------------------------------------------------------

static void time_mngr_sta_connected_handler(void * p_arg, esp_event_base_t p_event_base,
    int32_t event_id, void * p_event_data)
{
    (void)p_arg;
    (void)p_event_base;
    (void)event_id;
    (void)p_event_data;

    if (esp_sntp_enabled())
    {
        ESP_LOGD(gp_tag, "SNTP already running - skipping reinit");
        return;
    }

    ESP_LOGI(gp_tag, "STA connected - starting SNTP");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(time_mngr_sync_cb);
    esp_sntp_init();
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
