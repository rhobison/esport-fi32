/**
 * \file
 * \brief Exercise session detection and tracking - full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "session_tracker.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "config_manager.h"
#include "event_ids.h"
#include "pulse_input.h"
#include "time_manager.h"
#include "buzzer.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Milliseconds per second, used when converting timer periods. */
#define SESSION_TRK_MS_PER_S (1000U)

/**
 * \brief Internal state of the session tracker state machine.
 */
typedef enum session_trk_state_tag
{
    /** No active session; waiting for the first pulse. */
    SESSION_TRK_STATE_IDLE       = 0,
    /** First pulse received; qualification timer is running. */
    SESSION_TRK_STATE_QUALIFYING = 1,
    /** Session confirmed; idle detection timer is running. */
    SESSION_TRK_STATE_ACTIVE     = 2,
} session_trk_state_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "session_tracker";

/** Current state of the session tracker state machine. */
static session_trk_state_t g_state = SESSION_TRK_STATE_IDLE;

/** Millisecond timestamp (esp_timer_get_time()/1000) captured on the first pulse of the potential
 * session. */
static int64_t g_potential_start_ms = 0;

/** Millisecond timestamp of the most recently accepted pulse. */
static int64_t g_last_pulse_ms = 0;

/** Millisecond timestamp of the pulse immediately before #g_last_pulse_ms.
 * Used to compute the inter-pulse interval for live speed.  Zero until the
 * second pulse of a session has been accepted. */
static int64_t g_prev_pulse_ms = 0;

/** Number of accepted pulses since the current session (or potential session) began. */
static uint32_t g_pulse_count = 0U;

/** FreeRTOS software timer that fires after start_session_interval_s to confirm a session open. */
static TimerHandle_t gp_qualify_timer = NULL;

/** FreeRTOS software timer that fires after idle_session_interval_s to close an active session. */
static TimerHandle_t gp_idle_timer = NULL;

/** Set to true by the qualification timer callback when the session is confirmed open. */
static bool gb_session_confirmed = false;

/** Back-calculated UTC timestamp of the confirmed session start (seconds since Unix epoch). */
static int64_t g_session_start_utc = 0;

/* Config values cached at session start so timer callbacks can use them without re-reading NVS. */

/** Cached start_session_interval_s (seconds); re-read from config at each session start. */
static uint16_t g_start_interval_s = 0U;

/** Cached idle_session_interval_s (seconds); re-read from config at each session start. */
static uint16_t g_idle_interval_s = 0U;

/** Cached centimeters_per_pulse; re-read from config at each session start. */
static uint32_t g_centimeters_per_pulse = 0U;

/** Cached seconds_per_pulse; re-read from config at each session start. */
static uint16_t g_seconds_per_pulse = 0U;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void session_trk_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data);
static void session_trk_qualify_timer_cb(TimerHandle_t p_timer);
static void session_trk_idle_timer_cb(TimerHandle_t p_timer);
static void session_trk_state_reset(void);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t session_trk_init(void)
{
    gp_qualify_timer = xTimerCreate("session_qualify",
        1U, /* placeholder period; set via xTimerChangePeriod before each start */
        pdFALSE, NULL, session_trk_qualify_timer_cb);

    if (NULL == gp_qualify_timer)
    {
        ESP_LOGE(gp_tag, "xTimerCreate failed for qualify timer");
        return ESP_ERR_NO_MEM;
    }

    gp_idle_timer = xTimerCreate("session_idle",
        1U, /* placeholder period; set via xTimerChangePeriod before each start */
        pdFALSE, NULL, session_trk_idle_timer_cb);

    if (NULL == gp_idle_timer)
    {
        ESP_LOGE(gp_tag, "xTimerCreate failed for idle timer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE,
        session_trk_pulse_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete - IDLE, timers created");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Fill a #session_trk_live_status_t with the current session state.
 *
 * Reads module-level state variables to produce a point-in-time snapshot.
 * Intended for display purposes only; no locking is applied since a slightly
 * stale snapshot is acceptable for the HTTP status API.
 *
 * \param[out] p_out  Destination status structure to populate.
 */
void session_trk_live_status_get(session_trk_live_status_t * p_out)
{
    static const char * const sc_state_names[] = { "idle", "qualifying", "active" };

    p_out->p_state_name = sc_state_names[g_state];
    p_out->start_utc    = g_session_start_utc;
    p_out->pulse_count  = (SESSION_TRK_STATE_IDLE == g_state) ? 0U : g_pulse_count;

    if (SESSION_TRK_STATE_ACTIVE == g_state)
    {
        int64_t now_s     = (int64_t)time_mngr_utc_get();
        int64_t elapsed   = now_s - g_session_start_utc;
        p_out->duration_s = (elapsed > 0LL) ? (uint32_t)elapsed : 0U;

        /* Live speed - pulse_input is the single source of truth and handles
         * staleness internally (returns 0 when last pulse > 3 s ago). */
        uint32_t raw              = pulse_in_speed_kmh_x10_get();
        p_out->live_speed_kmh_x10 = (raw > (uint32_t)UINT16_MAX) ? UINT16_MAX : (uint16_t)raw;
    }
    else
    {
        p_out->duration_s         = 0U;
        p_out->live_speed_kmh_x10 = 0U;
    }
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Reset all session state variables and stop both FreeRTOS timers.
 *
 * Shared by the idle timer callback (both ACTIVE close and QUALIFYING gap paths)
 * to avoid duplicating teardown logic.
 */
static void session_trk_state_reset(void)
{
    (void)xTimerStop(gp_qualify_timer, 0U);
    (void)xTimerStop(gp_idle_timer, 0U);

    g_state              = SESSION_TRK_STATE_IDLE;
    g_pulse_count        = 0U;
    g_potential_start_ms = 0;
    g_last_pulse_ms      = 0;
    g_prev_pulse_ms      = 0;
    g_session_start_utc  = 0;
    gb_session_confirmed = false;
    g_seconds_per_pulse  = 0U;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Default event loop handler invoked for each accepted debounced pulse.
 *
 * Updates the last-pulse timestamp and pulse count, then applies the per-state
 * transition logic:
 *   - #SESSION_TRK_STATE_IDLE: re-reads config, transitions to QUALIFYING, starts
 *     both FreeRTOS timers.
 *   - #SESSION_TRK_STATE_QUALIFYING: resets the idle timer to extend the window.
 *   - #SESSION_TRK_STATE_ACTIVE: resets the idle timer to prevent premature close.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_PULSE).
 * \param[in] p_event_data   Pointer to the \c int64_t pulse timestamp payload.
 */
static void session_trk_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data; /* no payload; timing derived from esp_timer_get_time() below */

    int64_t timestamp_ms = esp_timer_get_time() / 1000LL;

    g_prev_pulse_ms = g_last_pulse_ms;
    g_last_pulse_ms = timestamp_ms;
    g_pulse_count++;

    ESP_LOGI(gp_tag, "Pulse: %" PRIu16 " (%d ms)", g_pulse_count,
        g_last_pulse_ms - g_prev_pulse_ms);

    if (SESSION_TRK_STATE_IDLE == g_state)
    {
        /* Re-read config so changes take effect on the next session without a reboot. */
        g_start_interval_s      = config_mngr_start_session_interval_s_get();
        g_idle_interval_s       = config_mngr_idle_session_interval_s_get();
        g_centimeters_per_pulse = config_mngr_centimeters_per_pulse_get();
        g_seconds_per_pulse     = config_mngr_seconds_per_pulse_get();

        g_potential_start_ms = timestamp_ms;
        g_pulse_count        = 1U;
        g_state              = SESSION_TRK_STATE_QUALIFYING;

        buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFYING);

        TickType_t qualify_ticks =
            pdMS_TO_TICKS((uint32_t)g_start_interval_s * SESSION_TRK_MS_PER_S);
        TickType_t idle_ticks = pdMS_TO_TICKS((uint32_t)g_idle_interval_s * SESSION_TRK_MS_PER_S);

        (void)xTimerChangePeriod(gp_qualify_timer, qualify_ticks, 0U);
        (void)xTimerChangePeriod(gp_idle_timer, idle_ticks, 0U);

        ESP_LOGI(gp_tag, "IDLE→QUALIFYING: qualify=%" PRIu16 "s idle=%" PRIu16 "s",
            g_start_interval_s, g_idle_interval_s);
    }
    else if (SESSION_TRK_STATE_QUALIFYING == g_state)
    {
        (void)xTimerReset(gp_idle_timer, 0U);
    }
    else /* SESSION_TRK_STATE_ACTIVE */
    {
        (void)xTimerReset(gp_idle_timer, 0U);
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief FreeRTOS timer callback that fires after #g_start_interval_s to confirm
 * the session open.
 *
 * Back-calculates the session start UTC timestamp from the elapsed time since
 * #g_potential_start_ms so that #session_trk_record_t.start_time_utc reflects
 * the actual first-pulse time, not the confirmation moment.
 *
 * \param[in] p_timer  Handle of the timer that expired; unused.
 */
static void session_trk_qualify_timer_cb(TimerHandle_t p_timer)
{
    (void)p_timer;

    g_state = SESSION_TRK_STATE_ACTIVE;

    buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFIED);

    int64_t elapsed_ms   = esp_timer_get_time() / 1000LL - g_potential_start_ms;
    int64_t elapsed_s    = elapsed_ms / 1000LL;
    g_session_start_utc  = (int64_t)time_mngr_utc_get() - elapsed_s;
    gb_session_confirmed = true;

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_SESSION_OPENED, NULL, 0U, 0U);

    ESP_LOGI(gp_tag, "QUALIFYING→ACTIVE: session open, start_utc=%" PRId64, g_session_start_utc);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief FreeRTOS timer callback that fires after #g_idle_interval_s with no pulse.
 *
 * If the session was confirmed (#SESSION_TRK_STATE_ACTIVE), computes session
 * statistics, fills a #session_trk_record_t on the stack, and posts
 * #ESPORT_EVENT_SESSION_CLOSED with the record copied by value.
 *
 * If the timeout fires during #SESSION_TRK_STATE_QUALIFYING (gap before
 * confirmation), the potential session is silently discarded and state is
 * reset to #SESSION_TRK_STATE_IDLE.
 *
 * \param[in] p_timer  Handle of the timer that expired; unused.
 */
static void session_trk_idle_timer_cb(TimerHandle_t p_timer)
{
    (void)p_timer;

    if (SESSION_TRK_STATE_ACTIVE == g_state)
    {
        buzzer_pattern_play(BUZZER_PATTERN_SESSION_CLOSED);

        int64_t  raw_duration = (g_last_pulse_ms - g_potential_start_ms) / 1000LL;
        uint32_t duration_s =
            (raw_duration > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)raw_duration;

        uint64_t total_cm = (uint64_t)g_pulse_count * (uint64_t)g_centimeters_per_pulse;
        /* avg speed (km/h x10) = total_cm * 36 / (duration_s * 100)
         * Derivation: speed_km_h = total_cm/100000 / (duration_s/3600)
         *           = total_cm * 36 / (duration_s * 1000)  →  x10: / 100 */
        uint16_t avg_speed_kmh_x10 =
            (0U < duration_s) ? (uint16_t)(total_cm * 36ULL / ((uint64_t)duration_s * 100ULL)) : 0U;

        uint32_t internet_earned_s = (uint32_t)g_pulse_count * (uint32_t)g_seconds_per_pulse;

        session_trk_record_t record = {
            .start_time_utc    = g_session_start_utc,
            .b_time_synced     = time_mngr_is_synced(),
            .duration_s        = duration_s,
            .pulse_count       = g_pulse_count,
            .avg_speed_kmh_x10 = avg_speed_kmh_x10,
            .internet_earned_s = internet_earned_s,
        };

        ESP_LOGI(gp_tag,
            "session closed: duration=%" PRIu32 "s pulses=%" PRIu32 " speed=%" PRIu16
            " (x10 km/h) earned=%" PRIu32 "s",
            duration_s, g_pulse_count, avg_speed_kmh_x10, internet_earned_s);

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_SESSION_CLOSED, &record,
            sizeof(record), 0U);
    }
    else /* SESSION_TRK_STATE_QUALIFYING - gap during qualification, discard */
    {
        ESP_LOGI(gp_tag, "QUALIFYING→IDLE: idle gap before confirmation, discarding");
    }

    session_trk_state_reset();
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
