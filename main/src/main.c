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
#include "ota_manager.h"
#include "pulse_input.h"
#include "session_log.h"
#include "session_tracker.h"
#include "time_counter.h"
#include "time_manager.h"
#include "wifi_manager.h"
#include "buzzer.h"
#include "button_reset.h"
#include "activity_manager.h"

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Magic value validating the RTC-retained reboot-loop guard counter. */
#define MAIN_BOOT_GUARD_MAGIC (0xB007C0DEU)

/** Consecutive fast reboots that trigger safe mode. */
#define MAIN_BOOT_GUARD_MAX_FAILS (5U)

/** Uptime (microseconds) after which a boot is considered healthy and the
 *  reboot-loop counter is cleared. */
#define MAIN_BOOT_GUARD_STABLE_US (60ULL * 1000000ULL)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Application-wide esport event base (declared in event_ids.h). */
ESP_EVENT_DEFINE_BASE(ESPORT_EVENT_BASE);

/** Module log tag. */
static const char * gp_tag = "main";

/**
 * \brief RTC-retained magic + consecutive-fast-reboot counter.
 *
 * Placed in RTC_NOINIT memory so the value survives software resets, panics,
 * and watchdog reboots (but is undefined after a true power-on, which the
 * magic guards against).  Each boot increments #g_boot_fail_count; a one-shot
 * timer clears it once the device has been up for #MAIN_BOOT_GUARD_STABLE_US,
 * so only \e rapid reboot loops accumulate.
 */
RTC_NOINIT_ATTR static uint32_t g_boot_guard_magic;
RTC_NOINIT_ATTR static uint32_t g_boot_fail_count;

/** One-shot timer that clears #g_boot_fail_count after a stable uptime. */
static esp_timer_handle_t gp_boot_guard_timer = NULL;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void on_counter_changed(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);
static void on_earning_started(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);
static void on_earning_stopped(void * p_arg, esp_event_base_t base, int32_t id, void * p_data);
static bool boot_guard_evaluate(void);
static void boot_guard_clear_cb(void * p_arg);

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Application entry point - initialises all firmware modules.
 *
 * Follows the boot sequence defined in the esport-fi32 firmware specification
 * §7.1.  Every call is wrapped with \c ESP_ERROR_CHECK so that any
 * initialisation failure triggers an immediate reboot with a diagnostic log
 * message.
 */
void app_main(void)
{
    esp_err_t ret;

    /* Step 0: Reboot-loop guard.  Evaluate BEFORE touching any subsystem so a
     * device stuck in a crash/WDT loop falls back to a minimal "safe mode"
     * (reward AP + dashboard only) that an operator can reach to read the
     * captured core dump and reset reason, instead of looping forever. */
    bool b_safe_mode = boot_guard_evaluate();
    if (b_safe_mode)
    {
        ESP_LOGE(gp_tag, "SAFE MODE active after %u consecutive fast reboots (reset_reason=%d)",
            (unsigned)g_boot_fail_count, (int)esp_reset_reason());
    }

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

    /* Step 2aa: Initialise activity manager (after device registry). */
    ESP_ERROR_CHECK(act_mngr_init());

    /* Step 2b: Initialise buzzer GPIO and pattern timer. */
    ESP_ERROR_CHECK(buzzer_init());

    /* Step 2c: Initialise BOOT button long-press password reset module. */
    ESP_ERROR_CHECK(btn_rst_init());

    /* Step 2d: Mark firmware valid (cancel rollback); open esport_ota NVS namespace. */
    ESP_ERROR_CHECK(ota_mngr_init());

    /* Step 3: Create the default event loop before any module that posts. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(gp_tag, "esport-fi32 starting. Build: " __DATE__ " " __TIME__);

    /* Step 4: Start Wi-Fi - AP+STA mode; reward AP always-on from init.
     * In safe mode the STA connect and connectivity supervisor are suppressed
     * so the device cannot reboot itself out of the recovery state. */
    wifi_mngr_safe_mode_set(b_safe_mode);
    ESP_ERROR_CHECK(wifi_mngr_init());

    /* Step 5: HTTP server reachable via reward AP (192.168.5.1) and STA IP. */
    ESP_ERROR_CHECK(http_srv_init());

    /* Step 6: Register SNTP sync callback; fires once STA has an IP. */
    ESP_ERROR_CHECK(time_mngr_init());

    /* Step 10: Start the NVS-backed session ring-buffer log (read-only at boot). */
    ESP_ERROR_CHECK(session_log_init());

    if (b_safe_mode)
    {
        /* Skip the runtime generators most likely to drive a reboot loop:
         * the pulse ISR (edge storms), the 1 s tick (periodic NVS commits),
         * and session tracking.  The dashboard remains fully reachable. */
        ESP_LOGW(gp_tag, "SAFE MODE: pulse input, time counter and session tracker disabled");
    }
    else
    {
        /* Step 7: Configure pulse GPIO interrupt and debounce filter. */
        ESP_ERROR_CHECK(pulse_in_init());

        /* Step 8: Start the time counter and reward AP state machine. */
        ESP_ERROR_CHECK(time_ctr_init());

        /* Step 9: Start two-phase exercise session detection. */
        ESP_ERROR_CHECK(session_trk_init());
    }

    /* Register cross-module event handlers from main to avoid circular deps. */
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_COUNTER_CHANGED,
        on_counter_changed, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_EARNING_STARTED,
        on_earning_started, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_EARNING_STOPPED,
        on_earning_stopped, NULL));

    /* Arm the boot-guard clear timer last: reaching this point with the
     * scheduler running means initialisation succeeded.  After a stable
     * uptime the consecutive-fast-reboot counter is reset to zero. */
    const esp_timer_create_args_t guard_args = {
        .callback        = boot_guard_clear_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "boot_guard",
    };
    if (ESP_OK == esp_timer_create(&guard_args, &gp_boot_guard_timer))
    {
        (void)esp_timer_start_once(gp_boot_guard_timer, (uint64_t)MAIN_BOOT_GUARD_STABLE_US);
    }

    ESP_LOGI(gp_tag, "Config: threshold=%" PRIu32 " spp=%" PRIu16 " cpp=%" PRIu32,
        config_mngr_internet_gate_threshold_s_get(), config_mngr_seconds_per_pulse_get(),
        config_mngr_centimeters_per_pulse_get());
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
 * \brief Event handler for #ESPORT_EVENT_EARNING_STARTED.
 *
 * \param[in] p_arg  Unused handler argument.
 * \param[in] base   Event base (unused).
 * \param[in] id     Event ID (unused).
 * \param[in] p_data Unused (no payload).
 */
static void on_earning_started(void * p_arg, esp_event_base_t base, int32_t id, void * p_data)
{
    (void)p_arg;
    (void)base;
    (void)id;
    (void)p_data;
    ESP_LOGI(gp_tag, "earning started");
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Event handler for #ESPORT_EVENT_EARNING_STOPPED.
 *
 * \param[in] p_arg  Unused handler argument.
 * \param[in] base   Event base (unused).
 * \param[in] id     Event ID (unused).
 * \param[in] p_data Unused (no payload).
 */
static void on_earning_stopped(void * p_arg, esp_event_base_t base, int32_t id, void * p_data)
{
    (void)p_arg;
    (void)base;
    (void)id;
    (void)p_data;
    ESP_LOGI(gp_tag, "earning stopped");
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Evaluate the RTC-retained reboot-loop guard and decide on safe mode.
 *
 * Validates the RTC magic (re-seeding the counter on a true power-on, where
 * RTC_NOINIT memory is undefined), increments the consecutive-fast-reboot
 * counter, and returns whether the threshold for safe mode has been reached.
 * The counter is later cleared by #boot_guard_clear_cb after a stable uptime,
 * so only rapid reboot loops accumulate.
 *
 * \return \c true if the device should boot in safe mode.
 */
static bool boot_guard_evaluate(void)
{
    if (MAIN_BOOT_GUARD_MAGIC != g_boot_guard_magic)
    {
        /* Cold boot / RTC memory invalid: start a fresh count. */
        g_boot_guard_magic = MAIN_BOOT_GUARD_MAGIC;
        g_boot_fail_count  = 0U;
    }

    if (g_boot_fail_count < UINT32_MAX)
    {
        g_boot_fail_count++;
    }

    return (g_boot_fail_count >= MAIN_BOOT_GUARD_MAX_FAILS);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief One-shot timer callback that clears the reboot-loop guard counter.
 *
 * Fires #MAIN_BOOT_GUARD_STABLE_US after a successful boot.  Reaching this
 * point means the device stayed up long enough to be considered healthy, so
 * the consecutive-fast-reboot counter is reset to zero.
 *
 * \param[in] p_arg  Unused timer argument.
 */
static void boot_guard_clear_cb(void * p_arg)
{
    (void)p_arg;
    g_boot_fail_count = 0U;
    ESP_LOGI(gp_tag, "boot guard: uptime stable, reboot counter cleared");
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
