/**
 * \file
 * \brief GPIO pulse input module - full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "event_ids.h"
#include "pulse_input.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "config_manager.h"
#include "freertos/FreeRTOS.h"

#include <inttypes.h>
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

/**
 * \brief Maximum age of the last accepted pulse, in microseconds, beyond which
 * #pulse_in_speed_kmh_x10_get() reports zero speed.
 *
 * Derived from: \c interval_ms = cpp_cm * 36 / speed_kmh.
 * At 1 km/h with a 200 cm (standard adult bicycle) wheel circumference:
 * 200 * 36 = 7200 ms → 7 200 000 µs.
 * Anything slower than 1 km/h is treated as stopped for display purposes.
 */
#define PULSE_IN_SPEED_STALE_US (7200000U)

/** Stack size, in bytes, of #pulse_in_forward_task. */
#define PULSE_IN_FORWARD_TASK_STACK_SIZE (3072U)

/**
 * \brief FreeRTOS priority of #pulse_in_forward_task.
 *
 * Above ordinary app-level tasks (e.g. device_reg_nvs_task at 3, httpd at 5)
 * so a debounced pulse is handed to the default event loop with minimal
 * delay, but well below system tasks (tcpip 18, sys_evt 20, esp_timer 22,
 * wifi 23) so it can never contend with networking-critical scheduling.
 */
#define PULSE_IN_FORWARD_TASK_PRIORITY (10U)

/** Name of #pulse_in_forward_task, used for TWDT/debug identification. */
#define PULSE_IN_FORWARD_TASK_NAME ("pulse_fwd")

/**
 * \brief Timeout for the esp_event_post() call made from #pulse_in_forward_task.
 *
 * Small enough to avoid stalling the forward task on a transient default-loop
 * queue burst, generous enough to ride out brief congestion instead of
 * dropping immediately the way the old ISR-post path had to.
 */
#define PULSE_IN_POST_TIMEOUT_TICKS (pdMS_TO_TICKS(20U))

/**
 * \brief Maximum time #pulse_in_forward_task blocks between TWDT feeds.
 *
 * Must stay well under CONFIG_ESP_TASK_WDT_TIMEOUT_S (5 s) so a long idle
 * period between pulses (rider not pedaling) never trips the watchdog; the
 * task notification wakes the task immediately whenever a pulse is actually
 * pending, so this bound never adds latency to pulse delivery.
 */
#define PULSE_IN_FORWARD_TASK_WDT_FEED_MS (2000U)

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

/** Last error code returned by esp_event_post (called from #pulse_in_forward_task,
 *  never from the ISR); 0 means no failure yet. */
static volatile esp_err_t g_last_post_err = ESP_OK;

/** Handle of #pulse_in_forward_task; NULL until created in #pulse_in_init().
 *  The ISR NULL-guards on this before notifying it. */
static TaskHandle_t g_forward_task_handle = NULL;

/**
 * \brief Elapsed time in milliseconds between the two most recent accepted pulses.
 *
 * Set to \c UINT32_MAX on boot (speed indeterminate until two pulses have been
 * accepted).  Updated by the ISR only; safe to read from any task as a volatile.
 */
static volatile uint32_t g_last_interval_ms = UINT32_MAX;

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

/**
 * \brief Wheel distance per pulse in centimetres; cached from config at
 * #pulse_in_init() time so that #pulse_in_speed_kmh_x10_get() never needs to
 * acquire the NVS mutex on the hot path.
 *
 * A reboot is required to pick up a change made via the config portal, which
 * is acceptable because wheel size is a one-time physical calibration value.
 */
static uint32_t g_centimeters_per_pulse = 0U;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void pulse_in_config_changed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data);

static void pulse_in_forward_task(void * p_arg);

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
    uint16_t debounce_ms    = config_mngr_pulse_debounce_time_ms_get();
    g_debounce_us           = (uint64_t)debounce_ms * (uint64_t)PULSE_IN_MS_TO_US;
    g_centimeters_per_pulse = config_mngr_centimeters_per_pulse_get();

    /* Rise-bounce guard: cap at debounce so it never rejects pulses that the
       main debounce window would have accepted. */
    g_rise_bounce_guard_us = (g_debounce_us < (uint64_t)PULSE_IN_RISE_BOUNCE_GUARD_MAX_US) ?
                                 g_debounce_us :
                                 (uint64_t)PULSE_IN_RISE_BOUNCE_GUARD_MAX_US;

    /* Seed the transition flag so the very first ISR call is handled correctly
       regardless of the initial pin state. */
    g_pin_was_high = (gpio_get_level((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO) != 0);

    /* Create the pulse-forward task BEFORE attaching the ISR: the ISR only
       ever notifies g_forward_task_handle (NULL-guarded regardless), but the
       handle must be valid before the first pulse can possibly arrive. */
    if (pdPASS != xTaskCreate(pulse_in_forward_task, PULSE_IN_FORWARD_TASK_NAME,
                      PULSE_IN_FORWARD_TASK_STACK_SIZE, NULL, PULSE_IN_FORWARD_TASK_PRIORITY,
                      &g_forward_task_handle))
    {
        ESP_LOGE(gp_tag, "xTaskCreate (forward) failed");
        return ESP_ERR_NO_MEM;
    }

    ret = gpio_isr_handler_add((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO, pulse_in_gpio_isr, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_CONFIG_CHANGED,
        pulse_in_config_changed_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGW(gp_tag, "esp_event_handler_register (config_changed) failed: %s",
            esp_err_to_name(ret));
    }

    ESP_LOGI(gp_tag, "init complete - GPIO %d, debounce %u ms, cpp %" PRIu32 " cm",
        CONFIG_ESPORT_PULSE_GPIO, (unsigned)debounce_ms, g_centimeters_per_pulse);
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

uint32_t pulse_in_last_interval_ms_get(void)
{
    return g_last_interval_ms;
}

//--------------------------------------------------------------------------------------------------

uint32_t pulse_in_speed_kmh_x10_get(void)
{
    uint32_t interval_ms = g_last_interval_ms;
    if ((UINT32_MAX == interval_ms) || (0U == interval_ms))
    {
        return 0U;
    }
    /* Return 0 when the last pulse is older than PULSE_IN_SPEED_STALE_US (rider stopped). */
    if ((uint64_t)(esp_timer_get_time() - g_last_accepted_us) > (uint64_t)PULSE_IN_SPEED_STALE_US)
    {
        return 0U;
    }
    return (g_centimeters_per_pulse * 360U) / interval_ms;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Event handler that refreshes #g_centimeters_per_pulse after a portal save.
 *
 * \param[in] p_handler_arg  Unused context pointer.
 * \param[in] base           Event base (always #ESPORT_EVENT_BASE).
 * \param[in] event_id       Event identifier (always #ESPORT_EVENT_CONFIG_CHANGED).
 * \param[in] p_event_data   Unused (no payload).
 */
static void pulse_in_config_changed_handler(void * p_handler_arg, esp_event_base_t base,
    int32_t event_id, void * p_event_data)
{
    (void)p_handler_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    g_centimeters_per_pulse = config_mngr_centimeters_per_pulse_get();
    ESP_LOGI(gp_tag, "config reloaded: cpp=%" PRIu32 " cm", g_centimeters_per_pulse);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Pulse-forward task: dispatches one #ESPORT_EVENT_PULSE per accepted
 * pulse, from task context.
 *
 * #pulse_in_gpio_isr() runs with \c ESP_INTR_FLAG_IRAM and must never touch
 * anything that can be flash-cache-backed (see docs/6-pulse-isr-issue.md for
 * the crash this caused when it called esp_event_isr_post() directly). This
 * task is the only place that calls esp_event_post(); that is safe here
 * because ESP-IDF itself suspends ordinary tasks for the (brief) duration of
 * any flash cache-disable window instead of letting them run into a fault.
 *
 * ulTaskNotifyTake(pdFALSE, ...) is used as a lightweight counting semaphore:
 * each call decrements the task's notification value by one and returns the
 * pre-decrement value, so every "give" from the ISR results in exactly one
 * esp_event_post() call here, in order. This preserves an exact 1:1 mapping
 * between physical pulses and posted events - both time_ctr_pulse_handler and
 * session_trk_pulse_handler credit/count per call, so coalescing pulses here
 * would silently under-count rider credit and qualifying pulses.
 *
 * The wait is time-bounded (not portMAX_DELAY) purely so this task can
 * periodically call esp_task_wdt_reset() even when the rider is not
 * pedaling; the notification wakes it immediately whenever a pulse is
 * actually pending, so the bound never adds latency to pulse delivery.
 *
 * \param[in] p_arg  Unused.
 */
static void pulse_in_forward_task(void * p_arg)
{
    (void)p_arg;

    if (ESP_OK != esp_task_wdt_add(NULL))
    {
        ESP_LOGW(gp_tag, "esp_task_wdt_add (forward task) failed");
    }

    for (;;)
    {
        uint32_t pending =
            ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(PULSE_IN_FORWARD_TASK_WDT_FEED_MS));
        (void)esp_task_wdt_reset();

        if (0U != pending)
        {
            esp_err_t err = esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE, NULL, 0,
                PULSE_IN_POST_TIMEOUT_TICKS);
            if (ESP_OK != err)
            {
                g_dropped_count++;
                g_last_post_err = err;
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------

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
           reset the transition flag.  Do NOT touch g_last_accepted_us - the
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

    /* Compute inter-pulse interval before updating g_last_accepted_us. */
    if (0 != g_last_accepted_us)
    {
        uint64_t interval_us = (uint64_t)(now_us - g_last_accepted_us);
        g_last_interval_ms   = (interval_us > ((uint64_t)UINT32_MAX * 1000ULL)) ?
                                   UINT32_MAX :
                                   (uint32_t)(interval_us / 1000U);
    }
    /* When g_last_accepted_us == 0 (first pulse ever), leave g_last_interval_ms = UINT32_MAX. */

    g_last_accepted_us = now_us;
    g_total_count++;

    /* Hand off to pulse_in_forward_task via a counting task-notification
       instead of posting the event directly from ISR context (see
       docs/6-pulse-isr-issue.md).  vTaskNotifyGiveFromISR is confirmed
       IRAM-resident in this build, so it remains safe to call even while the
       flash cache is disabled; esp_event_post() itself is deferred to task
       context, where the OS - not this ISR - absorbs any cache-disable
       window. NULL-guarded because pulse_in_init() creates the task before
       attaching this ISR, but this defends against any future reordering. */
    BaseType_t hp_task_awoken = pdFALSE;
    if (NULL != g_forward_task_handle)
    {
        vTaskNotifyGiveFromISR(g_forward_task_handle, &hp_task_awoken);
    }

    if (pdTRUE == hp_task_awoken)
    {
        portYIELD_FROM_ISR();
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
