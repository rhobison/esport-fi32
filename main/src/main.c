/**
 * \file
 * \brief esport-fi32 application entry point and module initialisation sequence.
 *
 * Initialises all firmware modules in the boot order specified by the
 * esport-fi32 firmware specification §7.1.  Every initialisation call is
 * wrapped with \c ESP_ERROR_CHECK so that a failed module causes an
 * immediate reset with a diagnostic message rather than silent undefined
 * behaviour.  Cross-module event handlers for counter and reward-AP
 * transitions are registered from here to avoid circular dependencies.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config_manager.h"
#include "device_registry.h"
#include "event_ids.h"
#include "http_server.h"
#include "pulse_input.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Application-wide esport event base (declared in event_ids.h). */
ESP_EVENT_DEFINE_BASE(ESPORT_EVENT_BASE);

/** Module log tag. */
static const char * gp_tag = "main";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void on_counter_changed(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);
static void on_reward_ap_on(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);
static void on_reward_ap_off(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Application entry point — initialises all firmware modules.
 *
 * Follows the boot sequence defined in the esport-fi32 firmware specification
 * §7.1.  Every call is wrapped with \c ESP_ERROR_CHECK so that any
 * initialisation failure triggers an immediate reboot with a diagnostic log
 * message.
 */
void app_main(void)
{
    esp_err_t ret;

    /* Step 1: Initialise NVS; erase on corruption so factory defaults apply. */
    ret = nvs_flash_init();
    if ((ESP_ERR_NVS_NO_FREE_PAGES == ret) || (ESP_ERR_NVS_NEW_VERSION_FOUND == ret))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Step 2: Load runtime configuration from NVS; apply factory defaults. */
    ESP_ERROR_CHECK(config_mngr_init());

    /* Step 2a: Initialise device registry before WiFi so the MAC filter is ready. */
    ESP_ERROR_CHECK(device_reg_init());

    /* Step 3: Create the default event loop before any module that posts. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(gp_tag, "esport-fi32 starting. Build: " __DATE__ " " __TIME__);

    /* Step 4: Start Wi-Fi — AP+STA mode; config AP enabled at first STA miss. */
    ESP_ERROR_CHECK(wifi_mngr_init());

    /* Step 5: HTTP server reachable immediately via config AP. */
    ESP_ERROR_CHECK(http_srv_init());

    /* Step 6: Register SNTP sync callback; fires once STA has an IP. */
    ESP_ERROR_CHECK(time_mngr_init());

    /* Step 7: Configure pulse GPIO interrupt and debounce filter. */
    ESP_ERROR_CHECK(pulse_in_init());

    /* Step 8: Start the time counter and reward AP state machine. */
    ESP_ERROR_CHECK(time_ctr_init());

    /* Step 9: Start two-phase exercise session detection. */
    ESP_ERROR_CHECK(session_trk_init());

    /* Step 10: Start the NVS-backed session ring-buffer log. */
    ESP_ERROR_CHECK(session_log_init());

    /* Register cross-module event handlers from main to avoid circular deps. */
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED,
        on_counter_changed, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_ON,
        on_reward_ap_on, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_REWARD_AP_OFF,
        on_reward_ap_off, NULL));

    ESP_LOGI(gp_tag, "Config: threshold=%" PRIu32 " spp=%" PRIu16 " cpp=%" PRIu32,
        config_mngr_soft_ap_start_threshold_s_get(), config_mngr_seconds_per_pulse_get(),
        config_mngr_centimeters_per_pulse_get());

    /* DEBUG: poll GPIO input level every second to verify pin state changes.
       If level stays 1 when grounded the HW wiring is suspect; if it toggles
       but interrupts never fire the ISR service is the issue.
       Remove before production release. */
    // for (;;)
    // {
    //     int       level    = gpio_get_level((gpio_num_t)CONFIG_ESPORT_PULSE_GPIO);
    //     uint32_t  count    = pulse_in_total_count_get();
    //     uint32_t  dropped  = pulse_in_dropped_count_get();
    //     esp_err_t post_err = pulse_in_last_post_err_get();
    //     ESP_LOGI(gp_tag,
    //         "[DBG] GPIO%d = %d  isr_count=%" PRIu32 "  dropped=%" PRIu32 "  post_err=0x%x (%s)",
    //         CONFIG_ESPORT_PULSE_GPIO, level, count, dropped, post_err,
    //         esp_err_to_name(post_err));
    //     vTaskDelay(pdMS_TO_TICKS(1000U));
    // }
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Event handler for #ESPORT_EVENT_COUNTER_CHANGED.
 *
 * Logs the new counter value at DEBUG level.
 *
 * \param[in] p_arg  Unused handler argument.
 * \param[in] base   Event base (unused).
 * \param[in] id     Event ID (unused).
 * \param[in] p_data Pointer to \c uint32_t counter value in seconds.
 */
static void on_counter_changed(void * p_arg, esp_event_base_t base, int32_t id, void * p_data)
{
    (void)p_arg;
    (void)base;
    (void)id;
    uint32_t counter_s = *(const uint32_t *)p_data;
    ESP_LOGD(gp_tag, "counter=%" PRIu32 " s", counter_s);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Event handler for #ESPORT_EVENT_REWARD_AP_ON.
 *
 * \param[in] p_arg  Unused handler argument.
 * \param[in] base   Event base (unused).
 * \param[in] id     Event ID (unused).
 * \param[in] p_data Unused (no payload).
 */
static void on_reward_ap_on(void * p_arg, esp_event_base_t base, int32_t id, void * p_data)
{
    (void)p_arg;
    (void)base;
    (void)id;
    (void)p_data;
    ESP_LOGI(gp_tag, "reward AP on");
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Event handler for #ESPORT_EVENT_REWARD_AP_OFF.
 *
 * \param[in] p_arg  Unused handler argument.
 * \param[in] base   Event base (unused).
 * \param[in] id     Event ID (unused).
 * \param[in] p_data Unused (no payload).
 */
static void on_reward_ap_off(void * p_arg, esp_event_base_t base, int32_t id, void * p_data)
{
    (void)p_arg;
    (void)base;
    (void)id;
    (void)p_data;
    ESP_LOGI(gp_tag, "reward AP off");
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
