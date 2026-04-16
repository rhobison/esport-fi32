/**
 * \file
 * \brief Buzzer feedback module - non-blocking pattern engine.
 *
 * Drives an active buzzer via GPIO using an esp_timer at a 50 ms time-base.
 * Four predefined beep patterns are supported; a new pattern immediately
 * interrupts the current one.
 *
 * \date 2026-04-04
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "buzzer.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "config_manager.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Period of the pattern timer in microseconds (one beep unit). */
#define BUZZER_TIMER_PERIOD_US ((uint64_t)BUZZER_UNIT_MS * 1000ULL)

/**
 * \brief One step within a buzzer pattern.
 *
 * Each step defines a HIGH duration (on_units) followed by a LOW duration
 * (off_units).  A trailing step has off_units == 0.
 */
typedef struct buzzer_step_tag
{
    uint8_t on_units;
    uint8_t off_units;
} buzzer_step_t;

/**
 * \brief Playback phase.
 */
typedef enum bz_phase_tag
{
    BZ_PHASE_ON   = 0,
    BZ_PHASE_OFF  = 1,
    BZ_PHASE_IDLE = 2,
} bz_phase_t;

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag __attribute__((unused)) = "buzzer";

/* ---- Pattern tables (const, ROM-eligible) ---- */

static const buzzer_step_t s_pat_qualifying[1] = {
    { BUZZER_PATTERN_QUALIFYING_UNITS, 0U },
};

static const buzzer_step_t s_pat_qualified[1] = {
    { BUZZER_PATTERN_QUALIFIED_UNITS, 0U },
};

static const buzzer_step_t s_pat_closed[BUZZER_PATTERN_CLOSED_BEEP_COUNT] = {
    { BUZZER_PATTERN_CLOSED_BEEP_UNITS, BUZZER_PATTERN_CLOSED_GAP_UNITS },
    { BUZZER_PATTERN_CLOSED_BEEP_UNITS, BUZZER_PATTERN_CLOSED_GAP_UNITS },
    { BUZZER_PATTERN_CLOSED_BEEP_UNITS, 0U },
};

static const buzzer_step_t s_pat_speed_low[1] = {
    { BUZZER_PATTERN_SPEED_LOW_UNITS, 0U },
};

/** 50 units ON = 2.5 s continuous beep for password reset confirmation. */
static const buzzer_step_t s_pat_password_reset[1] = {
    { 50U, 0U },
};

/* ---- Playback state (spinlock-protected) ---- */

/** Spinlock protecting all playback state. */
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

/** Pointer to the current pattern step array. */
static const buzzer_step_t * gp_steps = NULL;

/** Number of steps in the current pattern. */
static uint8_t g_step_count = 0U;

/** Current step index within the pattern. */
static uint8_t g_step_idx = 0U;

/** Remaining ON units for the current step. */
static uint8_t g_remaining_on = 0U;

/** Remaining OFF units for the current step. */
static uint8_t g_remaining_off = 0U;

/** Current playback phase. */
static bz_phase_t g_phase = BZ_PHASE_IDLE;

/** Pattern ID of the currently playing (or last played) pattern. */
static buzzer_pattern_id_t g_current_pat_id = BUZZER_PATTERN_SESSION_QUALIFYING;

/** Handle for the periodic 50 ms timer. */
static esp_timer_handle_t g_timer = NULL;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void buzzer_gpio_set(uint8_t level);
static void buzzer_advance_step_locked(void);
static void buzzer_pattern_start_locked(const buzzer_step_t * p_steps, uint8_t count,
    buzzer_pattern_id_t id);
static void buzzer_timer_cb(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t buzzer_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ESPORT_BUZZER_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    (void)gpio_set_level((gpio_num_t)CONFIG_ESPORT_BUZZER_GPIO, 0);

    const esp_timer_create_args_t timer_args = {
        .callback        = buzzer_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "buzzer_pat",
    };

    ret = esp_timer_create(&timer_args, &g_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "buzzer init: GPIO %d", CONFIG_ESPORT_BUZZER_GPIO);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

void buzzer_pattern_play(buzzer_pattern_id_t pattern)
{
    if (!config_mngr_buzzer_enabled_get())
    {
        return;
    }

    const buzzer_step_t * p_steps = NULL;
    uint8_t               count   = 0U;

    ESP_LOGI(gp_tag, "pattern %d", (int)pattern);

    switch (pattern)
    {
        case BUZZER_PATTERN_SESSION_QUALIFYING:
            p_steps = s_pat_qualifying;
            count   = 1U;
            break;
        case BUZZER_PATTERN_SESSION_QUALIFIED:
            p_steps = s_pat_qualified;
            count   = 1U;
            break;
        case BUZZER_PATTERN_SESSION_CLOSED:
            p_steps = s_pat_closed;
            count   = BUZZER_PATTERN_CLOSED_BEEP_COUNT;
            break;
        case BUZZER_PATTERN_SPEED_LOW:
            p_steps = s_pat_speed_low;
            count   = 1U;
            break;
        case BUZZER_PATTERN_PASSWORD_RESET:
            p_steps = s_pat_password_reset;
            count   = 1U;
            break;
        default:
            ESP_LOGW(gp_tag, "unknown pattern %d", (int)pattern);
            return;
    }

    portENTER_CRITICAL(&g_mux);
    buzzer_pattern_start_locked(p_steps, count, pattern);
    portEXIT_CRITICAL(&g_mux);
}

//--------------------------------------------------------------------------------------------------

void buzzer_stop(void)
{
    portENTER_CRITICAL(&g_mux);
    g_phase = BZ_PHASE_IDLE;
    portEXIT_CRITICAL(&g_mux);

    buzzer_gpio_set(0U);
    (void)esp_timer_stop(g_timer);
}

//--------------------------------------------------------------------------------------------------

void buzzer_speed_low_update(bool b_active)
{
    if (b_active)
    {
        buzzer_pattern_play(BUZZER_PATTERN_SPEED_LOW);
        return;
    }

    /* b_active == false: only stop if SPEED_LOW is currently playing. */
    bool b_stop = false;

    portENTER_CRITICAL(&g_mux);
    if ((BUZZER_PATTERN_SPEED_LOW == g_current_pat_id) && (BZ_PHASE_IDLE != g_phase))
    {
        b_stop = true;
    }
    portEXIT_CRITICAL(&g_mux);

    if (b_stop)
    {
        buzzer_stop();
    }
}

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Set the buzzer GPIO level.
 *
 * Thin wrapper around \c gpio_set_level.  Safe to call from within a
 * critical section on single-core ESP32-C6 (O(1) register write).
 *
 * \param[in] level  0 = OFF, 1 = ON.
 */
static void buzzer_gpio_set(uint8_t level)
{
    (void)gpio_set_level((gpio_num_t)CONFIG_ESPORT_BUZZER_GPIO, (uint32_t)level);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Advance to the next step in the pattern.
 *
 * Must be called under the g_mux spinlock.  If no more steps remain, sets
 * the phase to IDLE and the GPIO to LOW.
 */
static void buzzer_advance_step_locked(void)
{
    g_step_idx++;
    if (g_step_idx >= g_step_count)
    {
        buzzer_gpio_set(0U);
        g_phase = BZ_PHASE_IDLE;
    }
    else
    {
        buzzer_gpio_set(1U);
        g_remaining_on = gp_steps[g_step_idx].on_units;
        g_phase        = BZ_PHASE_ON;
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Load and start a pattern.
 *
 * Must be called under the g_mux spinlock.  Interrupts any pattern already
 * in progress.  Sets the GPIO HIGH for the first ON unit and starts the
 * timer if it is not already running.
 *
 * \param[in] p_steps  Pointer to the pattern step array.
 * \param[in] count    Number of steps.
 * \param[in] id       Pattern ID (for identification in speed-low stop logic).
 */
static void buzzer_pattern_start_locked(const buzzer_step_t * p_steps, uint8_t count,
    buzzer_pattern_id_t id)
{
    gp_steps         = p_steps;
    g_step_count     = count;
    g_step_idx       = 0U;
    g_remaining_on   = p_steps[0U].on_units;
    g_remaining_off  = 0U;
    g_phase          = BZ_PHASE_ON;
    g_current_pat_id = id;

    buzzer_gpio_set(1U);

    /* Start the periodic timer if not already running. */
    (void)esp_timer_stop(g_timer);
    (void)esp_timer_start_periodic(g_timer, BUZZER_TIMER_PERIOD_US);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Periodic timer callback (50 ms).
 *
 * Runs in the esp_timer task.  All operations are O(1), spinlock-guarded,
 * and perform only GPIO writes - no heap, NVS, or logging.
 *
 * \param[in] p_arg  Unused.
 */
static void buzzer_timer_cb(void * p_arg)
{
    (void)p_arg;

    portENTER_CRITICAL(&g_mux);

    if (BZ_PHASE_IDLE == g_phase)
    {
        portEXIT_CRITICAL(&g_mux);
        (void)esp_timer_stop(g_timer);
        return;
    }

    if (BZ_PHASE_ON == g_phase)
    {
        g_remaining_on--;
        if (0U == g_remaining_on)
        {
            if (gp_steps[g_step_idx].off_units > 0U)
            {
                buzzer_gpio_set(0U);
                g_remaining_off = gp_steps[g_step_idx].off_units;
                g_phase         = BZ_PHASE_OFF;
            }
            else
            {
                buzzer_advance_step_locked();
            }
        }
    }
    else /* BZ_PHASE_OFF */
    {
        g_remaining_off--;
        if (0U == g_remaining_off)
        {
            buzzer_advance_step_locked();
        }
    }

    portEXIT_CRITICAL(&g_mux);
}

/*** end of file ***/
