/**
 * \file
 * \brief Time counter and reward AP state machine — full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "time_counter.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "config_manager.h"
#include "event_ids.h"
#include "pulse_input.h"
#include "wifi_manager.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Periodic decrement interval in microseconds (1 second). */
#define TIME_CTR_TICK_PERIOD_US (1000000U)

/**
 * \brief Internal state of the time counter state machine.
 */
typedef enum time_ctr_state_tag
{
    /** No active session; reward AP off; counter is 0. */
    TIME_CTR_STATE_IDLE      = 0,
    /** Session confirmed open; threshold timer running; counter accumulating from pulses. */
    TIME_CTR_STATE_SESSION   = 1,
    /** Session has been active for soft_ap_start_threshold_s; reward AP on; tick decrementing. */
    TIME_CTR_STATE_AP_ACTIVE = 2,
} time_ctr_state_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "time_counter";

/** Time credit counter value in seconds; protected by #g_spinlock. */
static uint32_t g_counter_s = 0U;

/** Spinlock that protects #g_counter_s and #g_state against concurrent task access. */
static portMUX_TYPE g_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** Current reward AP state machine state; protected by #g_spinlock. */
static time_ctr_state_t g_state = TIME_CTR_STATE_IDLE;

/** Handle for the 1-second periodic decrement timer (created in #time_ctr_init). */
static esp_timer_handle_t gp_tick_timer = NULL;

/** Handle for the one-shot threshold timer that fires after #soft_ap_start_threshold_s seconds. */
static esp_timer_handle_t gp_threshold_timer = NULL;

/**
 * \brief Cached runtime configuration values; loaded at #time_ctr_init() and
 * refreshed by #time_ctr_config_changed_handler() on #ESPORT_EVENT_CONFIG_CHANGED.
 *
 * Protected by #g_spinlock where accessed from the tick callback.
 */
static uint16_t g_cfg_spp        = 1U; /**< Seconds credited per accepted pulse. */
static uint16_t g_cfg_min_spd    = 0U; /**< Minimum speed gate in km/h x10 (0 = disabled). */
static uint16_t g_cfg_dec_kbps   = 0U; /**< AP throughput threshold for decrement pause. */
static uint16_t g_cfg_idle_tmo_s = 0U; /**< Ticks of low throughput before pause activates. */

/**
 * \brief Tracks how many consecutive ticks throughput was below threshold.
 *
 * Protected by #g_spinlock; reset to 0 when AP is disabled or throughput
 * rises above threshold.
 */
static uint16_t g_below_ticks = 0U;

/**
 * \brief True when the countdown is currently paused due to low AP traffic;
 * protected by #g_spinlock.
 */
static volatile bool g_paused = false;


//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void time_ctr_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data);
static void time_ctr_session_opened_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data);
static void time_ctr_session_closed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data);
static void time_ctr_config_changed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data);
static void time_ctr_threshold_cb(void * p_arg);
static void time_ctr_tick_cb(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

/** Load (or reload) all cached config values; call within or outside spinlock — all are plain
 * reads. */
static void time_ctr_config_cache_refresh(void)
{
    g_cfg_spp        = config_mngr_seconds_per_pulse_get();
    g_cfg_min_spd    = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    g_cfg_dec_kbps   = config_mngr_soft_ap_dec_threshold_kbps_get();
    g_cfg_idle_tmo_s = config_mngr_soft_ap_idle_throughput_timeout_s_get();
}

//--------------------------------------------------------------------------------------------------

esp_err_t time_ctr_init(void)
{
    time_ctr_config_cache_refresh();

    const esp_timer_create_args_t tick_args = {
        .callback        = time_ctr_tick_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "time_ctr_tick",
    };

    esp_err_t ret = esp_timer_create(&tick_args, &gp_tick_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create (tick) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const esp_timer_create_args_t threshold_args = {
        .callback        = time_ctr_threshold_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "time_ctr_thresh",
    };

    ret = esp_timer_create(&threshold_args, &gp_threshold_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create (threshold) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE, time_ctr_pulse_handler,
        NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register (pulse) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_SESSION_OPENED,
        time_ctr_session_opened_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register (session_opened) failed: %s",
            esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_SESSION_CLOSED,
        time_ctr_session_closed_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register (session_closed) failed: %s",
            esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_CONFIG_CHANGED,
        time_ctr_config_changed_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register (config_changed) failed: %s",
            esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete — counter 0, reward AP off");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint32_t time_ctr_get(void)
{
    portENTER_CRITICAL(&g_spinlock);
    uint32_t counter_snapshot = g_counter_s;
    portEXIT_CRITICAL(&g_spinlock);
    return counter_snapshot;
}

//--------------------------------------------------------------------------------------------------

bool time_ctr_is_paused(void)
{
    portENTER_CRITICAL(&g_spinlock);
    bool b_paused = g_paused;
    portEXIT_CRITICAL(&g_spinlock);
    return b_paused;
}

//--------------------------------------------------------------------------------------------------

uint32_t time_ctr_current_speed_x10_get(void)
{
    return pulse_in_speed_kmh_x10_get();
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief ESP event loop handler called for each accepted debounced pulse.
 *
 * Adds #config_mngr_seconds_per_pulse_get() credits to #g_counter_s when the
 * state machine is in #TIME_CTR_STATE_SESSION or #TIME_CTR_STATE_AP_ACTIVE
 * and the rider's instantaneous speed meets or exceeds
 * #config_mngr_min_speed_to_increment_time_kmh_x10_get().  When the gate
 * threshold is 0 all pulses earn credits (original behaviour).  Pulses in
 * #TIME_CTR_STATE_IDLE are ignored.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_PULSE).
 * \param[in] p_event_data   Unused (no payload).
 */
static void time_ctr_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    /* Instantaneous speed (km/h × 10) — single source of truth in pulse_input. */
    uint32_t speed_x10 = pulse_in_speed_kmh_x10_get();

    portENTER_CRITICAL(&g_spinlock);
    bool b_credit = (TIME_CTR_STATE_SESSION == g_state) || (TIME_CTR_STATE_AP_ACTIVE == g_state);
    /* Apply speed gate: skip credit if threshold > 0 and speed is below it. */
    if (b_credit && (g_cfg_min_spd > 0U) && (speed_x10 < (uint32_t)g_cfg_min_spd))
    {
        b_credit = false;
    }
    if (b_credit)
    {
        g_counter_s += (uint32_t)g_cfg_spp;
    }
    uint32_t counter_snapshot = g_counter_s;
    portEXIT_CRITICAL(&g_spinlock);

    /* Always post COUNTER_CHANGED so consumers stay up to date. */
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_snapshot,
        sizeof(counter_snapshot), 0U);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP event loop handler called when an exercise session is confirmed open.
 *
 * Transitions from #TIME_CTR_STATE_IDLE to #TIME_CTR_STATE_SESSION and starts
 * the one-shot threshold timer for #config_mngr_soft_ap_start_threshold_s_get()
 * seconds.  When the timer fires, the reward AP is enabled.  Events received
 * in non-IDLE states are silently ignored (AP already active or session already
 * in progress).
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_SESSION_OPENED).
 * \param[in] p_event_data   Unused (no payload).
 */
static void time_ctr_session_opened_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    portENTER_CRITICAL(&g_spinlock);
    bool b_start = (TIME_CTR_STATE_IDLE == g_state);
    if (b_start)
    {
        g_state = TIME_CTR_STATE_SESSION;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_start)
    {
        return; /* Already in SESSION or AP_ACTIVE — ignore. */
    }

    uint32_t threshold    = config_mngr_soft_ap_start_threshold_s_get();
    uint64_t threshold_us = (uint64_t)threshold * 1000000ULL;

    if (0U == threshold)
    {
        /* Threshold of zero means enable AP immediately. */
        time_ctr_threshold_cb(NULL);
    }
    else
    {
        esp_err_t ret = esp_timer_start_once(gp_threshold_timer, threshold_us);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_timer_start_once failed: %s", esp_err_to_name(ret));
            portENTER_CRITICAL(&g_spinlock);
            g_state = TIME_CTR_STATE_IDLE;
            portEXIT_CRITICAL(&g_spinlock);
        }
    }

    ESP_LOGI(gp_tag, "IDLE→SESSION: session opened, AP threshold %" PRIu32 "s", threshold);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP event loop handler called when an exercise session closes.
 *
 * If in #TIME_CTR_STATE_SESSION (before the threshold timer has fired), the
 * threshold timer is cancelled, the counter is reset to zero, and the state
 * returns to #TIME_CTR_STATE_IDLE.  If in #TIME_CTR_STATE_AP_ACTIVE the close
 * is silently ignored — the reward AP continues until the counter drains.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_SESSION_CLOSED).
 * \param[in] p_event_data   Unused (no payload consumed here).
 */
static void time_ctr_session_closed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    portENTER_CRITICAL(&g_spinlock);
    bool b_cancel = (TIME_CTR_STATE_SESSION == g_state);
    if (b_cancel)
    {
        g_state     = TIME_CTR_STATE_IDLE;
        g_counter_s = 0U;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (b_cancel)
    {
        (void)esp_timer_stop(gp_threshold_timer);
        uint32_t zero = 0U;
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &zero, sizeof(zero),
            0U);
        ESP_LOGI(gp_tag, "SESSION→IDLE: session closed before threshold, counter reset");
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief One-shot timer callback that fires after #soft_ap_start_threshold_s seconds.
 *
 * Transitions from #TIME_CTR_STATE_SESSION to #TIME_CTR_STATE_AP_ACTIVE, starts
 * the 1-second decrement timer, and enables the reward AP via
 * #wifi_mngr_reward_ap_set().  If the state is no longer SESSION when the timer
 * fires (e.g. session closed just before expiry), the callback exits without
 * activating the AP.
 *
 * \param[in] p_arg  Unused context pointer passed by the timer subsystem.
 */
static void time_ctr_threshold_cb(void * p_arg)
{
    (void)p_arg;

    portENTER_CRITICAL(&g_spinlock);
    bool b_activate = (TIME_CTR_STATE_SESSION == g_state);
    if (b_activate)
    {
        g_state = TIME_CTR_STATE_AP_ACTIVE;
    }
    uint32_t counter_snapshot = g_counter_s;
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_activate)
    {
        return; /* State changed before timer fired — session likely closed. */
    }

    esp_err_t ret = esp_timer_start_periodic(gp_tick_timer, TIME_CTR_TICK_PERIOD_US);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_start_periodic failed: %s", esp_err_to_name(ret));
    }

    ret = wifi_mngr_reward_ap_set(true);
    if (ESP_OK != ret)
    {
        ESP_LOGW(gp_tag, "wifi_mngr_reward_ap_set(true) failed: %s", esp_err_to_name(ret));
    }

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_ON, NULL, 0U, 0U);
    ESP_LOGI(gp_tag, "SESSION→AP_ACTIVE: reward AP on, counter = %" PRIu32 " s", counter_snapshot);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Event handler that refreshes cached config values after a portal save.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_CONFIG_CHANGED).
 * \param[in] p_event_data   Unused (no payload).
 */
static void time_ctr_config_changed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    portENTER_CRITICAL(&g_spinlock);
    time_ctr_config_cache_refresh();
    portEXIT_CRITICAL(&g_spinlock);

    ESP_LOGI(gp_tag, "config reloaded: spp=%u min_spd=%u dec_kbps=%u idle_tmo=%u",
        (unsigned)g_cfg_spp, (unsigned)g_cfg_min_spd, (unsigned)g_cfg_dec_kbps,
        (unsigned)g_cfg_idle_tmo_s);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief 1-second periodic timer callback that applies the traffic-gated decrement.
 *
 * Evaluates the sliding-window pause logic using current reward AP throughput
 * from #wifi_mngr_reward_ap_throughput_kbps() against the configured threshold
 * and timeout.  Decrements #g_counter_s only when throughput is above threshold
 * or the below-threshold streak has not yet reached the timeout.  When the
 * counter reaches zero, stops the tick timer and transitions back to
 * #TIME_CTR_STATE_IDLE.
 *
 * \\param[in] p_arg  Unused context pointer passed by the timer subsystem.
 */
static void time_ctr_tick_cb(void * p_arg)
{
    (void)p_arg;

    /* Read throughput outside the spinlock (this call may sleep). */
    uint32_t throughput = wifi_mngr_reward_ap_throughput_kbps();

    portENTER_CRITICAL(&g_spinlock);

    uint16_t threshold      = g_cfg_dec_kbps;
    uint16_t timeout        = g_cfg_idle_tmo_s;
    bool     b_reached_zero = false;

    if (throughput > (uint32_t)threshold)
    {
        /* Traffic above threshold — reset streak, clear pause, decrement. */
        g_below_ticks = 0U;
        g_paused      = false;
        if (0U < g_counter_s)
        {
            g_counter_s--;
        }
        b_reached_zero = (0U == g_counter_s);
        if (b_reached_zero)
        {
            g_state = TIME_CTR_STATE_IDLE;
        }
    }
    else
    {
        /* Traffic at or below threshold — advance streak counter. */
        if (g_below_ticks < UINT16_MAX)
        {
            g_below_ticks++;
        }
        /* Pause if streak >= timeout, or immediately when timeout == 0. */
        if ((0U == timeout) || (g_below_ticks >= (uint32_t)timeout))
        {
            g_paused = true;
        }
        /* Do NOT decrement when paused. */
    }

    uint32_t counter_snapshot = g_counter_s;

    portEXIT_CRITICAL(&g_spinlock);

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_snapshot,
        sizeof(counter_snapshot), 0U);

    if (b_reached_zero)
    {
        (void)esp_timer_stop(gp_tick_timer);

        /* Reset sliding-window state when leaving AP_ACTIVE. */
        portENTER_CRITICAL(&g_spinlock);
        g_below_ticks = 0U;
        g_paused      = false;
        portEXIT_CRITICAL(&g_spinlock);

        esp_err_t ret = wifi_mngr_reward_ap_set(false);
        if (ESP_OK != ret)
        {
            ESP_LOGW(gp_tag, "wifi_mngr_reward_ap_set(false) failed: %s", esp_err_to_name(ret));
        }

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_OFF, NULL, 0U, 0U);
        ESP_LOGI(gp_tag, "AP_ACTIVE→IDLE: reward AP off, counter reached 0");
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
