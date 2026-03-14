/**
 * \file
 * \brief esport-fi32 application entry point and module initialisation
 * sequence.
 *
 * Initialises all firmware modules in the boot order specified by the
 * esport-fi32 firmware specification §7.1.  Every initialisation call is
 * wrapped with \c ESP_ERROR_CHECK so that a failed module causes an
 * immediate reset with a diagnostic message rather than silent undefined
 * behaviour.
 *
 * \note This is the Phase 0 skeleton.  Module init functions are stubs that
 *       return \c ESP_OK without performing real work.  Each module will be
 *       fully implemented in its corresponding phase.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "config_manager.h"
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

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Application entry point — initialises all firmware modules.
 *
 * Follows the boot sequence defined in the esport-fi32 firmware
 * specification §7.1.  Every call is wrapped with \c ESP_ERROR_CHECK
 * so that any initialisation failure triggers an immediate reboot with
 * a diagnostic log message.
 */
void app_main(void)
{
    esp_err_t ret;

    /* Step 1: Initialise the NVS flash partition.
     * On partition corruption, erase and reinitialise so that factory
     * defaults are applied by config_mngr_init(). */
    ret = nvs_flash_init();
    if ((ESP_ERR_NVS_NO_FREE_PAGES == ret) || (ESP_ERR_NVS_NEW_VERSION_FOUND == ret))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Step 2: Load runtime configuration from NVS; apply factory defaults
     * for any missing key. */
    ESP_ERROR_CHECK(config_mngr_init());

    /* Step 3: Create the default event loop used for all inter-module
     * messaging via ESPORT_EVENT_BASE events. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Step 4: Start Wi-Fi in AP+STA mode.  Attempts STA connection; enables
     * the config AP immediately if STA is unavailable. */
    ESP_ERROR_CHECK(wifi_mngr_init());

    /* Step 5: Start the HTTP server so the config portal is reachable
     * immediately via the config AP (before STA connects). */
    ESP_ERROR_CHECK(http_srv_init());

    /* Step 6: Register SNTP sync callback; will fire once STA has an IP. */
    ESP_ERROR_CHECK(time_mngr_init());

    /* Step 7: Configure the pulse GPIO interrupt and debounce filter. */
    ESP_ERROR_CHECK(pulse_in_init());

    /* Step 8: Start the time counter and reward AP state machine. */
    ESP_ERROR_CHECK(time_ctr_init());

    /* Step 9: Start the two-phase exercise session detection logic. */
    ESP_ERROR_CHECK(session_trk_init());

    /* Step 10: Start the NVS-backed session ring-buffer log. */
    ESP_ERROR_CHECK(session_log_init());

    ESP_LOGI(gp_tag, "esport-fi32 init complete");
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/*** end of file ***/
