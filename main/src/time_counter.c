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
    /** Counter is below the threshold; reward AP is off; tick timer is stopped. */
    TIME_CTR_STATE_IDLE   = 0,
    /** Counter is at or above the threshold; reward AP is on; tick timer is running. */
    TIME_CTR_STATE_ACTIVE = 1,
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

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void time_ctr_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data);
static void time_ctr_tick_cb(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t time_ctr_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback        = time_ctr_tick_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "time_ctr_tick",
    };

    esp_err_t ret = esp_timer_create(&timer_args, &gp_tick_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE, time_ctr_pulse_handler,
        NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_event_handler_register failed: %s", esp_err_to_name(ret));
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

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief ESP event loop handler called for each accepted debounced pulse.
 *
 * Adds #config_mngr_seconds_per_pulse_get() credits to #g_counter_s and
 * initiates an IDLE→ACTIVE transition if the threshold is crossed for the
 * first time since the counter was last at zero.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_PULSE).
 * \param[in] p_event_data   Pointer to the \c int64_t pulse timestamp payload.
 */
static void time_ctr_pulse_handler(void * p_handler_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    uint16_t spp       = config_mngr_seconds_per_pulse_get();
    uint32_t threshold = config_mngr_soft_ap_start_threshold_s_get();

    portENTER_CRITICAL(&g_spinlock);
    g_counter_s += (uint32_t)spp;
    uint32_t counter_snapshot = g_counter_s;
    bool     activate         = (TIME_CTR_STATE_IDLE == g_state) && (counter_snapshot >= threshold);
    if (activate)
    {
        g_state = TIME_CTR_STATE_ACTIVE;
    }
    portEXIT_CRITICAL(&g_spinlock);

    if (activate)
    {
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
        ESP_LOGI(gp_tag, "IDLE→ACTIVE: reward AP on, counter = %" PRIu32 " s", counter_snapshot);
    }

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_snapshot,
        sizeof(counter_snapshot), 0U);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief 1-second periodic timer callback that decrements #g_counter_s by one.
 *
 * Decrements the time credit counter (floored at zero) and, when the counter
 * reaches zero, stops the tick timer and initiates an ACTIVE→IDLE transition.
 *
 * \param[in] p_arg  Unused context pointer passed by the timer subsystem.
 */
static void time_ctr_tick_cb(void * p_arg)
{
    (void)p_arg;

    portENTER_CRITICAL(&g_spinlock);
    if (0U < g_counter_s)
    {
        g_counter_s--;
    }
    uint32_t counter_snapshot = g_counter_s;
    bool     reached_zero     = (0U == g_counter_s);
    if (reached_zero)
    {
        g_state = TIME_CTR_STATE_IDLE;
    }
    portEXIT_CRITICAL(&g_spinlock);

    (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED, &counter_snapshot,
        sizeof(counter_snapshot), 0U);

    if (reached_zero)
    {
        (void)esp_timer_stop(gp_tick_timer);

        esp_err_t ret = wifi_mngr_reward_ap_set(false);
        if (ESP_OK != ret)
        {
            ESP_LOGW(gp_tag, "wifi_mngr_reward_ap_set(false) failed: %s", esp_err_to_name(ret));
        }

        (void)esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_OFF, NULL, 0U, 0U);
        ESP_LOGI(gp_tag, "ACTIVE→IDLE: reward AP off, counter reached 0");
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
