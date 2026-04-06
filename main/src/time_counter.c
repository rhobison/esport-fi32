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
    /** Session confirmed open; internet gate timer running; gate lock active for this rider. */
    TIME_CTR_STATE_SESSION = 1,
    /** Session active past threshold; gate lock cleared; internet access open. */
    TIME_CTR_STATE_EARNING = 2,
} time_ctr_state_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "time_counter";

/** Spinlock protecting #g_state against concurrent task access. */
static portMUX_TYPE g_spinlock = portMUX_INITIALIZER_UNLOCKED;

/** Current state machine state; protected by #g_spinlock. */
static time_ctr_state_t g_state = TIME_CTR_STATE_IDLE;

/** Handle for the 1-second periodic tick timer (created in #time_ctr_init, runs permanently). */
static esp_timer_handle_t gp_tick_timer = NULL;

/** Handle for the one-shot threshold timer that fires after #internet_gate_threshold_s seconds. */
static esp_timer_handle_t gp_inet_gate_timer = NULL;

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

    ret = esp_timer_create(&threshold_args, &gp_inet_gate_timer);
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
    return device_reg_entry_counter_get(rider);
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
 * In both #TIME_CTR_STATE_SESSION and #TIME_CTR_STATE_EARNING: adds
 * \c seconds_per_pulse credits directly to the current rider's device_reg
 * counter (NVS-persisted).  Pulses in #TIME_CTR_STATE_IDLE are ignored.
 *
 * The speed gate (#config_mngr_min_speed_to_increment_time_kmh_x10_get) is
 * applied in both active states.
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
    uint16_t spp = g_cfg_seconds_per_pulse;
    portEXIT_CRITICAL(&g_spinlock);

    if (b_credit)
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
 * Reads the current rider's device-registry counter and the internet gate lock
 * flag to decide the transition:
 *
 * - counter == 0, or (counter > 0 and gate lock set): engage internet gate.
 *   Set the per-device gate lock, transition IDLE->SESSION and start the
 *   one-shot \c internet_gate_threshold_s timer.  Qualifying pulses are
 *   credited directly to the device-registry counter.
 *
 * - counter > 0 and gate lock not set: the rider already has earned internet
 *   access.  Bypass the gate, transition IDLE->EARNING and post
 *   #ESPORT_EVENT_EARNING_STARTED.
 *
 * Events received in non-IDLE states are silently ignored.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_SESSION_OPENED).
 * \param[in] p_event_data   Pointer to \c uint32_t qualification pulse count.
 */
static void time_ctr_session_opened_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;

    portENTER_CRITICAL(&g_spinlock);
    bool b_start = (TIME_CTR_STATE_IDLE == g_state);
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_start)
    {
        return; /* Already in SESSION or EARNING - ignore. */
    }

    uint32_t qualify_pulses = 0U;
    if (NULL != p_event_data)
    {
        qualify_pulses = *(const uint32_t *)p_event_data;
    }

    uint8_t  rider = device_reg_current_rider_get();
    uint32_t current_counter =
        (DEVICE_REG_NO_RIDER != rider) ? device_reg_entry_counter_get(rider) : 0U;
    bool gate_locked = device_reg_entry_inet_gate_lock_get(rider);

    /* Credit qualifying pulses directly to the device-registry counter (persisted). */
    if ((qualify_pulses > 0U) && (DEVICE_REG_NO_RIDER != rider))
    {
        uint16_t spp = config_mngr_seconds_per_pulse_get();
        (void)device_reg_entry_counter_set(rider, current_counter + qualify_pulses * (uint32_t)spp);
    }

    /* Bypass the gate only when the rider already has internet time AND the
     * gate has not been locked in a previous incomplete session. */
    bool b_bypass_gate = (current_counter > 0U) && !gate_locked;

    if (b_bypass_gate)
    {
        portENTER_CRITICAL(&g_spinlock);
        g_state = TIME_CTR_STATE_EARNING;
        portEXIT_CRITICAL(&g_spinlock);

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_EARNING_STARTED, NULL, 0U, 0U);
        ESP_LOGI(gp_tag, "IDLE->EARNING: gate bypassed (counter %" PRIu32 "s, lock=false)",
            current_counter);
        return;
    }

    /* Engage the internet gate lock for this rider. */
    if (DEVICE_REG_NO_RIDER != rider)
    {
        (void)device_reg_entry_inet_gate_lock_set(rider, true);
    }

    portENTER_CRITICAL(&g_spinlock);
    g_state = TIME_CTR_STATE_SESSION;
    portEXIT_CRITICAL(&g_spinlock);

    uint32_t threshold    = config_mngr_internet_gate_threshold_s_get();
    uint64_t threshold_us = (uint64_t)threshold * 1000000ULL;

    if (0U == threshold)
    {
        /* Threshold of zero means transition to EARNING immediately. */
        time_ctr_threshold_cb(NULL);
    }
    else
    {
        esp_err_t ret = esp_timer_start_once(gp_inet_gate_timer, threshold_us);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_timer_start_once failed: %s", esp_err_to_name(ret));
            if (DEVICE_REG_NO_RIDER != rider)
            {
                (void)device_reg_entry_inet_gate_lock_set(rider, false);
            }
            portENTER_CRITICAL(&g_spinlock);
            g_state = TIME_CTR_STATE_IDLE;
            portEXIT_CRITICAL(&g_spinlock);
        }
    }

    ESP_LOGI(gp_tag,
        "IDLE->SESSION: gate locked, threshold %" PRIu32 "s, qualify_pulses=%" PRIu32
        ", counter=%" PRIu32 "s",
        threshold, qualify_pulses, device_reg_entry_counter_get(rider));
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP event loop handler called when an exercise session closes.
 *
 * In #TIME_CTR_STATE_SESSION: cancels the gate timer and returns to
 * #TIME_CTR_STATE_IDLE.  The per-device gate lock remains set so internet
 * access is still blocked.  All credits earned this session are already
 * persisted in the device-registry counter; they will be accessible (internet
 * unblocked) when the gate timer completes in a future session or on the next
 * boot (gate lock is RAM-only and resets to \c false).
 *
 * In #TIME_CTR_STATE_EARNING: clears the per-device gate lock, transitions to
 * IDLE and posts #ESPORT_EVENT_EARNING_STOPPED.
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
    time_ctr_state_t prev_state = g_state;
    if ((TIME_CTR_STATE_SESSION == prev_state) || (TIME_CTR_STATE_EARNING == prev_state))
    {
        g_state = TIME_CTR_STATE_IDLE;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (TIME_CTR_STATE_SESSION == prev_state)
    {
        (void)esp_timer_stop(gp_inet_gate_timer);
        buzzer_speed_low_update(false);

        /* Gate lock stays true: internet remains blocked until the gate threshold
         * is completed in a future session.  Credits are already in the device-
         * registry counter (NVS-persisted).  They will be accessible after the
         * next successful gate completion or after a device reboot. */
        uint32_t counter_val = time_ctr_get();
        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_val,
            sizeof(counter_val), 0U);
        ESP_LOGI(gp_tag,
            "SESSION->IDLE: closed before threshold, gate lock retained,"
            " counter=%" PRIu32 "s",
            counter_val);
    }
    else if (TIME_CTR_STATE_EARNING == prev_state)
    {
        buzzer_speed_low_update(false);

        /* Clear the gate lock: the rider completed the gate this session. */
        uint8_t rider = device_reg_current_rider_get();
        if (DEVICE_REG_NO_RIDER != rider)
        {
            (void)device_reg_entry_inet_gate_lock_set(rider, false);
        }

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_EARNING_STOPPED, NULL, 0U, 0U);
        ESP_LOGI(gp_tag, "EARNING->IDLE: session closed, gate lock cleared");
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief One-shot timer callback that fires after #internet_gate_threshold_s seconds.
 *
 * Transitions from #TIME_CTR_STATE_SESSION to #TIME_CTR_STATE_EARNING, clears
 * the per-device internet gate lock so internet access becomes available, and
 * posts #ESPORT_EVENT_EARNING_STARTED.  If the state is no longer SESSION when
 * the timer fires (e.g. session closed just before expiry), the callback exits
 * without any side effects.
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
        g_state = TIME_CTR_STATE_EARNING;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (!b_activate)
    {
        return; /* State changed before timer fired - session likely closed. */
    }

    /* Clear the gate lock so internet access opens for this rider. */
    uint8_t rider = device_reg_current_rider_get();
    if (DEVICE_REG_NO_RIDER != rider)
    {
        (void)device_reg_entry_inet_gate_lock_set(rider, false);
    }

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_EARNING_STARTED, NULL, 0U, 0U);
    ESP_LOGI(gp_tag, "SESSION->EARNING: gate threshold reached, internet unlocked");
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
