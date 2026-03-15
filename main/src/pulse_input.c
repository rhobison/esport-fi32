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

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

/**
 * \brief GPIO falling-edge ISR handler for the pulse input pin.
 *
 * Applies software debounce and posts #ESPORT_EVENT_PULSE on the default
 * event loop when a valid pulse is accepted.  Silently discards pulses that
 * arrive within the debounce window, or if the event queue is full.
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
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
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
       at that moment and then filter any real pulse arriving within 200 ms). */
    uint16_t debounce_ms = config_mngr_pulse_debounce_time_ms_get();
    g_debounce_us        = (uint64_t)debounce_ms * (uint64_t)PULSE_IN_MS_TO_US;

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

    /* Reject noise spikes: if the pin already returned high by the time the
       ISR runs (~1-2 µs latency), the edge was too short to be a real pulse.
       Real sensor pulses are held low for several milliseconds. */
    if (gpio_get_level((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO) != 0)
    {
        return;
    }

    int64_t now_us = esp_timer_get_time();

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
