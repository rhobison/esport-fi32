/**
 * \file
 * \brief Time counter and reward AP state machine - full implementation.
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
#include "device_registry.h"
#include "event_ids.h"
#include "pulse_input.h"
#include "buzzer.h"

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
    /** No active session; threshold timer not running. */
    TIME_CTR_STATE_IDLE    = 0,
    /** Session confirmed open; threshold timer running; credits accumulating in g_session_credits.
     */
    TIME_CTR_STATE_SESSION = 1,
    /** Session active past threshold; pulses add directly to rider's device_reg counter. */
    TIME_CTR_STATE_EARNING = 2,
} time_ctr_state_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "time_counter";

/** Spinlock protecting #g_state and #g_session_credits against concurrent task access. */
static portMUX_TYPE g_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** Current state machine state; protected by #g_spinlock. */
static time_ctr_state_t g_state = TIME_CTR_STATE_IDLE;

/**
 * \brief Session credit accumulator; accumulates pulse credits during TIME_CTR_STATE_SESSION.
 *
 * Flushed to the current rider's device_reg counter when the threshold fires.
 * Reset to zero if the session closes before the threshold fires.
 * Protected by #g_spinlock.
 */
static volatile uint32_t g_session_credits = 0U;

/** Handle for the 1-second periodic tick timer (created in #time_ctr_init, runs permanently). */
static esp_timer_handle_t gp_tick_timer = NULL;

/** Handle for the one-shot threshold timer that fires after #soft_ap_start_threshold_s seconds. */
static esp_timer_handle_t gp_threshold_timer = NULL;

/**
 * \brief Cached runtime configuration values; loaded at #time_ctr_init() and
 * refreshed by #time_ctr_config_changed_handler() on #ESPORT_EVENT_CONFIG_CHANGED.
 *
 * Protected by #g_spinlock where accessed from the tick callback.
 */
static uint16_t g_cfg_seconds_per_pulse = 1U; /**< Seconds credited per accepted pulse. */
static uint16_t g_cfg_min_speed_kmh_x10 = 0U; /**< Minimum speed gate in km/h x10 (0 = disabled). */
static uint16_t g_cfg_low_speed_bz_thresh_s =
    3U; /**< Consecutive ticks below min speed before speed-low beep starts. */
static uint16_t g_speed_low_ticks = 0U; /**< Consecutive ticks in which speed was below minimum. */

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

/** Load (or reload) cached config values; call within or outside spinlock - all are plain reads. */
static void time_ctr_config_cache_refresh(void)
{
    g_cfg_seconds_per_pulse     = config_mngr_seconds_per_pulse_get();
    g_cfg_min_speed_kmh_x10     = config_mngr_min_speed_to_increment_time_kmh_x10_get();
    g_cfg_low_speed_bz_thresh_s = config_mngr_low_speed_buzzer_threshold_s_get();
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

    /* One-time migration from Feature 3 global counter to per-device counter. */
    uint32_t legacy_val = config_mngr_reward_counter_s_get();
    if (legacy_val > 0U)
    {
        uint8_t rider = device_reg_current_rider_get();
        if ((DEVICE_REG_NO_RIDER != rider) && (0U == device_reg_entry_counter_get(rider)))
        {
            esp_err_t set_ret = device_reg_entry_counter_set(rider, legacy_val);
            if (ESP_OK == set_ret)
            {
                (void)config_mngr_reward_counter_s_set(0U);
                ESP_LOGI(gp_tag,
                    "init: migrated legacy counter %" PRIu32 "s to rider %u; legacy key cleared",
                    legacy_val, (unsigned)rider);
            }
        }
    }

    /* Start the tick timer permanently - runs for the lifetime of the firmware. */
    ret = esp_timer_start_periodic(gp_tick_timer, TIME_CTR_TICK_PERIOD_US);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_start_periodic (tick) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete - tick timer started permanently");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint32_t time_ctr_get(void)
{
    uint8_t rider = device_reg_current_rider_get();
    if (DEVICE_REG_NO_RIDER == rider)
    {
        return 0U;
    }

    /* Include in-progress session credits so the dashboard shows live
     * accumulation during SESSION state (before the threshold fires).
     * Credits in EARNING state are already in the device registry counter. */
    portENTER_CRITICAL(&g_spinlock);
    uint32_t session_credits = (TIME_CTR_STATE_SESSION == g_state) ? g_session_credits : 0U;
    portEXIT_CRITICAL(&g_spinlock);

    return device_reg_entry_counter_get(rider) + session_credits;
}

//--------------------------------------------------------------------------------------------------

uint32_t time_ctr_current_speed_x10_get(void)
{
    return pulse_in_speed_kmh_x10_get();
}

//--------------------------------------------------------------------------------------------------

esp_err_t time_ctr_counter_set(uint32_t val)
{
    uint8_t rider = device_reg_current_rider_get();
    if (DEVICE_REG_NO_RIDER == rider)
    {
        ESP_LOGW(gp_tag, "time_ctr_counter_set: no rider selected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = device_reg_entry_counter_set(rider, val);
    if (ESP_OK == ret)
    {
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &val, sizeof(val),
            0U);
    }
    return ret;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief ESP event loop handler called for each accepted debounced pulse.
 *
 * In #TIME_CTR_STATE_SESSION: accumulates credits in #g_session_credits.
 * In #TIME_CTR_STATE_EARNING: adds credits directly to the current rider's
 * device_reg counter.  Pulses in #TIME_CTR_STATE_IDLE are ignored.
 *
 * The speed gate (#config_mngr_min_speed_to_increment_time_kmh_x10_get) is
 * applied in both earning states.
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

    /* Speed gate (outside spinlock - single-source read from pulse_input). */
    uint32_t speed_x10 = pulse_in_speed_kmh_x10_get();

    portENTER_CRITICAL(&g_spinlock);
    bool b_credit = (TIME_CTR_STATE_SESSION == g_state) || (TIME_CTR_STATE_EARNING == g_state);
    if (b_credit && (g_cfg_min_speed_kmh_x10 > 0U) &&
        (speed_x10 < (uint32_t)g_cfg_min_speed_kmh_x10))
    {
        b_credit = false;
    }

    time_ctr_state_t state_snap = g_state;
    uint16_t         spp        = g_cfg_seconds_per_pulse;

    if (b_credit && (TIME_CTR_STATE_SESSION == state_snap))
    {
        g_session_credits += (uint32_t)spp;
    }

    /* Read rider index under spinlock so we can call device_reg outside. */
    portEXIT_CRITICAL(&g_spinlock);

    if (b_credit && (TIME_CTR_STATE_EARNING == state_snap))
    {
        uint8_t rider = device_reg_current_rider_get();
        if (DEVICE_REG_NO_RIDER != rider)
        {
            uint32_t current = device_reg_entry_counter_get(rider);
            (void)device_reg_entry_counter_set(rider, current + (uint32_t)spp);
        }
    }

    uint32_t counter_val = time_ctr_get();
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_val,
        sizeof(counter_val), 0U);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP event loop handler called when an exercise session is confirmed open.
 *
 * Transitions from #TIME_CTR_STATE_IDLE to #TIME_CTR_STATE_SESSION and starts
 * the one-shot threshold timer.  Events received in non-IDLE states are
 * silently ignored.
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
        g_state           = TIME_CTR_STATE_SESSION;
        g_session_credits = 0U;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_start)
    {
        return; /* Already in SESSION or EARNING - ignore. */
    }

    uint32_t threshold    = config_mngr_soft_ap_start_threshold_s_get();
    uint64_t threshold_us = (uint64_t)threshold * 1000000ULL;

    if (0U == threshold)
    {
        /* Threshold of zero means transition to EARNING immediately. */
        time_ctr_threshold_cb(NULL);
    }
    else
    {
        esp_err_t ret = esp_timer_start_once(gp_threshold_timer, threshold_us);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_timer_start_once failed: %s", esp_err_to_name(ret));
            portENTER_CRITICAL(&g_spinlock);
            g_state           = TIME_CTR_STATE_IDLE;
            g_session_credits = 0U;
            portEXIT_CRITICAL(&g_spinlock);
        }
    }

    ESP_LOGI(gp_tag, "IDLE->SESSION: session opened, threshold %" PRIu32 "s", threshold);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP event loop handler called when an exercise session closes.
 *
 * In #TIME_CTR_STATE_SESSION: cancels the threshold timer, resets
 * #g_session_credits to zero, and returns to #TIME_CTR_STATE_IDLE (no credits
 * are applied).  In #TIME_CTR_STATE_EARNING: transitions to IDLE and posts
 * #ESPORT_EVENT_REWARD_AP_OFF (device counters continue to drain normally via
 * the always-running tick timer).
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
    time_ctr_state_t prev_state       = g_state;
    uint32_t         credits_to_flush = 0U;
    if ((TIME_CTR_STATE_SESSION == prev_state) || (TIME_CTR_STATE_EARNING == prev_state))
    {
        g_state           = TIME_CTR_STATE_IDLE;
        credits_to_flush  = g_session_credits;
        g_session_credits = 0U;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (TIME_CTR_STATE_SESSION == prev_state)
    {
        (void)esp_timer_stop(gp_threshold_timer);

        buzzer_speed_low_update(false);

        /* Flush accumulated session credits to the current rider's counter.
         * Credits are earned from session start; the threshold only gates
         * when internet access opens (ESPORT_EVENT_REWARD_AP_ON). */
        if (credits_to_flush > 0U)
        {
            uint8_t rider = device_reg_current_rider_get();
            if (DEVICE_REG_NO_RIDER != rider)
            {
                uint32_t current = device_reg_entry_counter_get(rider);
                (void)device_reg_entry_counter_set(rider, current + credits_to_flush);
            }
        }

        uint32_t counter_val = time_ctr_get();
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_val,
            sizeof(counter_val), 0U);
        ESP_LOGI(gp_tag,
            "SESSION->IDLE: session closed before threshold, %" PRIu32 " credits flushed to rider",
            credits_to_flush);
    }
    else if (TIME_CTR_STATE_EARNING == prev_state)
    {
        buzzer_speed_low_update(false);

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_OFF, NULL, 0U, 0U);
        ESP_LOGI(gp_tag, "EARNING->IDLE: session closed, device counters continue");
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief One-shot timer callback that fires after #soft_ap_start_threshold_s seconds.
 *
 * Transitions from #TIME_CTR_STATE_SESSION to #TIME_CTR_STATE_EARNING, flushes
 * the accumulated session credits to the current rider's device_reg counter,
 * and posts #ESPORT_EVENT_REWARD_AP_ON.  If the state is no longer SESSION when
 * the timer fires (e.g. session closed just before expiry), the callback exits
 * without awarding credits.
 *
 * \param[in] p_arg  Unused context pointer passed by the timer subsystem.
 */
static void time_ctr_threshold_cb(void * p_arg)
{
    (void)p_arg;

    portENTER_CRITICAL(&g_spinlock);
    bool     b_activate     = (TIME_CTR_STATE_SESSION == g_state);
    uint32_t credits_to_add = g_session_credits;
    if (b_activate)
    {
        g_state           = TIME_CTR_STATE_EARNING;
        g_session_credits = 0U;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_activate)
    {
        return; /* State changed before timer fired - session likely closed. */
    }

    /* Flush accumulated session credits to the current rider's counter. */
    if (credits_to_add > 0U)
    {
        uint8_t rider = device_reg_current_rider_get();
        if (DEVICE_REG_NO_RIDER != rider)
        {
            uint32_t current = device_reg_entry_counter_get(rider);
            (void)device_reg_entry_counter_set(rider, current + credits_to_add);
        }
    }

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_ON, NULL, 0U, 0U);
    ESP_LOGI(gp_tag, "SESSION->EARNING: threshold fired, %" PRIu32 " session credits flushed",
        credits_to_add);
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

    ESP_LOGI(gp_tag, "config reloaded: seconds_per_pulse=%u min_speed_kmh_x10=%u",
        (unsigned)g_cfg_seconds_per_pulse, (unsigned)g_cfg_min_speed_kmh_x10);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief 1-second periodic timer callback that delegates per-device decrement to device_registry.
 *
 * Called unconditionally every second for the lifetime of the firmware.
 * Calls #device_reg_tick() which applies the per-device sliding-window traffic
 * gate and decrements eligible device counters.
 *
 * \param[in] p_arg  Unused context pointer passed by the timer subsystem.
 */
static void time_ctr_tick_cb(void * p_arg)
{
    (void)p_arg;

    (void)device_reg_tick();

    /* Speed-low beep: fire when SESSION or EARNING, speed gate enabled,
     * rider moving but below threshold, and the speed has been continuously
     * low for at least low_speed_buzzer_threshold_s consecutive ticks. */
    bool b_speed_low = false;
    if ((TIME_CTR_STATE_SESSION == g_state) || (TIME_CTR_STATE_EARNING == g_state))
    {
        uint32_t speed_x10 = time_ctr_current_speed_x10_get();
        uint16_t min_spd   = g_cfg_min_speed_kmh_x10;
        b_speed_low        = (min_spd > 0U) && (speed_x10 > 0U) && (speed_x10 < (uint32_t)min_spd);
    }

    /* Accumulate consecutive below-threshold ticks; reset when not low. */
    if (b_speed_low)
    {
        if (g_speed_low_ticks < UINT16_MAX)
        {
            g_speed_low_ticks++;
        }
    }
    else
    {
        g_speed_low_ticks = 0U;
    }
    buzzer_speed_low_update(b_speed_low && (g_speed_low_ticks >= g_cfg_low_speed_bz_thresh_s));

    uint32_t counter_val = time_ctr_get();
    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_val,
        sizeof(counter_val), 0U);
}

//--------------------------------------------------------------------------------------------------
