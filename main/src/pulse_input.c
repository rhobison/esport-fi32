/**
 * \file
 * \brief GPIO pulse input module — full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "event_ids.h"
#include "pulse_input.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "config_manager.h"
#include "freertos/FreeRTOS.h"

#include <stdint.h>

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Conversion factor: milliseconds to microseconds. */
#define PULSE_IN_MS_TO_US (1000U)

/**
 * \brief Maximum rise-bounce guard window in microseconds.
 *
 * The actual guard applied at runtime is \c min(g_debounce_us, this value)
 * so it can never be stricter than the user-configured debounce window.
 * See #g_rise_bounce_guard_us.
 */
#define PULSE_IN_RISE_BOUNCE_GUARD_MAX_US (10000U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "pulse_input";

/** Software debounce window in microseconds; set once during #pulse_in_init(). */
static volatile uint64_t g_debounce_us = 0U;

/** Timestamp (µs) of the last accepted pulse; updated by the ISR only. */
static volatile int64_t g_last_accepted_us = 0;

/** Cumulative count of accepted (debounced) pulses; updated by the ISR only. */
static volatile uint32_t g_total_count = 0U;

/** Count of pulses accepted by the ISR but dropped because the event queue was full. */
static volatile uint32_t g_dropped_count = 0U;

/** Last error code returned by esp_event_isr_post; 0 means no failure yet. */
static volatile esp_err_t g_last_post_err = ESP_OK;

/**
 * \brief True when the GPIO was HIGH at the last ISR call.
 *
 * Used together with GPIO_INTR_ANYEDGE to distinguish genuine HIGH→LOW
 * transitions from spurious re-fires.  On the ESP32-C6 the edge detector can
 * re-fire on a rising transition; if gpio_get_level then reads 0 (contact
 * bounce in progress), the na\xefve NEGEDGE + level-check guard incorrectly
 * accepts a spurious pulse.  With ANYEDGE the ISR runs on every edge:
 *  - rising edge (level=1): flag reset to true, no pulse counted;
 *  - falling edge (level=0, was_high=true): genuine transition, apply debounce;
 *  - falling edge (level=0, was_high=false): repeated LOW → reject.
 * A separate short #PULSE_IN_RISE_BOUNCE_GUARD_US window catches bounce that
 * dips back LOW immediately after a rising edge without stamping the main
 * debounce reference, so fast legitimate pulses are never suppressed.
 */
static volatile bool g_pin_was_high = true;

/** Timestamp (µs) of the most-recent confirmed rising edge; used with
 * #g_rise_bounce_guard_us to discard contact bounce on the rising
 * transition.  Zero until the first rising edge; initialised before the ISR
 * handler is registered so the first falling edge is never incorrectly
 * suppressed by an uninitialised value. */
static volatile int64_t g_last_rise_us = 0;

/**
 * \brief Effective rise-bounce guard window in microseconds.
 *
 * Set in #pulse_in_init() to \c min(g_debounce_us, PULSE_IN_RISE_BOUNCE_GUARD_MAX_US).
 * Capping at the debounce value guarantees that the rise-bounce guard is never
 * stricter than the inter-pulse debounce window: a falling edge that would be
 * accepted by the debounce check is never rejected by this guard.
 */
static volatile uint64_t g_rise_bounce_guard_us = 0U;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

/**
 * \brief GPIO any-edge ISR handler for the pulse input pin.
 *
 * Detects genuine HIGH→LOW (falling) transitions, applies software debounce,
 * and posts #ESPORT_EVENT_PULSE on the default event loop when a valid pulse
 * is accepted.  Uses #g_pin_was_high to admit only genuine H→L transitions
 * and #g_last_rise_us to discard contact bounce on the rising transition
 * without disturbing the main debounce reference, so fast consecutive pulses
 * are never suppressed.
 *
 * \param[in] p_arg  Unused user argument passed by the GPIO ISR service.
 */
static void pulse_in_gpio_isr(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t pulse_in_init(void)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ESPORT_PULSE_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = CONFIG_ESPORT_PULSE_PULLUP_EN ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en =
            CONFIG_ESPORT_PULSE_PULLDOWN_EN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if ((ESP_OK != ret) && (ESP_ERR_INVALID_STATE != ret))
    {
        ESP_LOGE(gp_tag, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set debounce window BEFORE adding the ISR handler so the first edge is
       never accepted with g_debounce_us == 0 (which would record g_last_accepted_us
       at that moment and then filter any real pulse arriving within the debounce window). */
    uint16_t debounce_ms = config_mngr_pulse_debounce_time_ms_get();
    g_debounce_us        = (uint64_t)debounce_ms * (uint64_t)PULSE_IN_MS_TO_US;

    /* Rise-bounce guard: cap at debounce so it never rejects pulses that the
       main debounce window would have accepted. */
    g_rise_bounce_guard_us = (g_debounce_us < (uint64_t)PULSE_IN_RISE_BOUNCE_GUARD_MAX_US) ?
                                 g_debounce_us :
                                 (uint64_t)PULSE_IN_RISE_BOUNCE_GUARD_MAX_US;

    /* Seed the transition flag so the very first ISR call is handled correctly
       regardless of the initial pin state. */
    g_pin_was_high = (gpio_get_level((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO) != 0);

    ret = gpio_isr_handler_add((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO, pulse_in_gpio_isr, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init complete — GPIO %d, debounce %u ms", CONFIG_ESPORT_PULSE_GPIO,
        (unsigned)debounce_ms);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

uint32_t pulse_in_total_count_get(void)
{
    return g_total_count;
}

//--------------------------------------------------------------------------------------------------

uint32_t pulse_in_dropped_count_get(void)
{
    return g_dropped_count;
}

//--------------------------------------------------------------------------------------------------

esp_err_t pulse_in_last_post_err_get(void)
{
    return g_last_post_err;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

static void IRAM_ATTR pulse_in_gpio_isr(void * p_arg)
{
    (void)p_arg;

    int64_t now_us   = esp_timer_get_time();
    int     level    = gpio_get_level((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO);
    bool    was_high = g_pin_was_high;
    g_pin_was_high   = (level != 0);

    if (level != 0)
    {
        /* Confirmed rising edge: record the rise time for the bounce guard and
           reset the transition flag.  Do NOT touch g_last_accepted_us — the
           inter-pulse debounce window must be measured from the previous
           accepted falling edge, not from the rising edge, so that fast
           consecutive pulses whose LOW period is shorter than g_debounce_us
           are still correctly accepted. */
        g_last_rise_us = now_us;
        return;
    }

    /* Falling edge.  Reject if the pin was already LOW at the last ISR call.
     * This covers the ESP32-C6 hardware quirk where the edge detector re-fires
     * on the rising transition but gpio_get_level still reads 0 due to
     * propagation delay, as well as repeated ISR calls with no intervening
     * HIGH. */
    if (!was_high)
    {
        return;
    }

    /* Reject contact bounce that dips LOW immediately after a rising edge.
     * PULSE_IN_RISE_BOUNCE_GUARD_US (10 ms) is chosen to cover worst-case
     * mechanical bounce while being negligible vs any real inter-pulse period. */
    if ((uint64_t)(now_us - g_last_rise_us) < g_rise_bounce_guard_us)
    {
        return;
    }

    /* Valid HIGH→LOW transition past all guards.  Apply the software debounce
     * window measured from the previous accepted pulse. */
    if ((uint64_t)(now_us - g_last_accepted_us) < g_debounce_us)
    {
        return;
    }

    g_last_accepted_us = now_us;
    g_total_count++;

    /* esp_event_isr_post copies payload inline into a uint32_t-sized field (max 4 bytes).
       No handler needs the exact ISR timestamp — all consumers derive timing from
       esp_timer_get_time() in handler context, where the sub-ms latency is negligible
       for second-resolution outputs. */
    BaseType_t hp_task_awoken = pdFALSE;
    esp_err_t  err =
        esp_event_isr_post(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE, NULL, 0, &hp_task_awoken);
    if (ESP_OK != err)
    {
        g_dropped_count++;
        g_last_post_err = err;
    }

    if (pdTRUE == hp_task_awoken)
    {
        portYIELD_FROM_ISR();
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
