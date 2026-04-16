/**
 * \file
 * \brief BOOT button long-press password reset module implementation.
 *
 * Polls the BOOT button GPIO at 100 ms intervals.  When the button is held
 * continuously for CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S seconds (default 5 s),
 * both the config page and OTA passwords are reset to their factory defaults
 * and BUZZER_PATTERN_PASSWORD_RESET (2.5 s continuous beep) is played.
 *
 * The re-trigger prevention flag (g_reset_done) is cleared only when the
 * button is released (GPIO reads HIGH), preventing multiple resets from a
 * single long press.
 *
 * \date 2026-04-16
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "button_reset.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "buzzer.h"
#include "config_manager.h"
#include "ota_manager.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/**
 * Number of consecutive LOW polls required to trigger a reset.
 * = CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S * 1000 / BTN_RST_POLL_INTERVAL_MS
 */
#define BTN_RST_HOLD_COUNT                                                 \
    ((uint32_t)((uint32_t)CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S * 1000U / \
                BTN_RST_POLL_INTERVAL_MS))

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "button_reset";

/** Consecutive below-threshold (button pressed) poll count. */
static volatile uint32_t g_held_count = 0U;

/** True after a reset has been triggered; cleared on button release. */
static volatile bool g_reset_done = false;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void btn_rst_timer_cb(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t btn_rst_init(void)
{
    /* Configure BOOT button GPIO as input with internal pull-up.
     * BOOT button is active LOW. */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << (uint32_t)CONFIG_ESPORT_BOOT_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create and start the 100 ms polling timer. */
    const esp_timer_create_args_t timer_args = {
        .callback        = btn_rst_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "btn_rst_poll",
    };

    esp_timer_handle_t timer_handle;
    ret = esp_timer_create(&timer_args, &timer_handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(timer_handle, (uint64_t)BTN_RST_POLL_INTERVAL_MS * 1000ULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_start_periodic failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(gp_tag, "init: GPIO %d, hold=%d s (%lu polls)", CONFIG_ESPORT_BOOT_BUTTON_GPIO,
        CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S, (unsigned long)BTN_RST_HOLD_COUNT);
    return ESP_OK;
}

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief 100 ms periodic timer callback — polls the BOOT button GPIO.
 *
 * Increments g_held_count while the button is pressed (LOW).  Resets the
 * counter and the re-trigger flag when the button is released (HIGH).  When
 * the hold count reaches BTN_RST_HOLD_COUNT and no reset has been done yet
 * in this press sequence, resets both passwords and plays the confirmation
 * beep.
 *
 * \param[in] p_arg  Unused.
 */
static void btn_rst_timer_cb(void * p_arg)
{
    (void)p_arg;

    int level = gpio_get_level((gpio_num_t)CONFIG_ESPORT_BOOT_BUTTON_GPIO);

    if (0 == level)
    {
        /* Button pressed (active LOW). */
        if (g_held_count < UINT32_MAX)
        {
            g_held_count++;
        }

        if (!g_reset_done && (g_held_count >= BTN_RST_HOLD_COUNT))
        {
            g_reset_done = true;
            g_held_count = 0U;

            /* Reset both passwords to factory defaults. */
            esp_err_t ret_cfg = config_mngr_cfg_password_set(CONFIG_MNGR_CFG_PASSWORD_DEFAULT);
            esp_err_t ret_ota = ota_mngr_password_set(OTA_MNGR_PASSWORD_DEFAULT);

            if ((ESP_OK == ret_cfg) && (ESP_OK == ret_ota))
            {
                ESP_LOGW(gp_tag, "password reset: config and OTA passwords restored to defaults");
            }
            else
            {
                ESP_LOGE(gp_tag, "password reset: cfg_ret=0x%x ota_ret=0x%x", ret_cfg, ret_ota);
            }

            /* Play 2.5 s confirmation beep. */
            buzzer_pattern_play(BUZZER_PATTERN_PASSWORD_RESET);
        }
    }
    else
    {
        /* Button released (HIGH) — reset state. */
        g_held_count = 0U;
        g_reset_done = false;
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
