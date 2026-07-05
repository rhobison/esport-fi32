/**
 * \file
 * \brief Wi-Fi manager (AP+STA mode with NAT) - full implementation.
 *
 * The Config SoftAP has been removed (Feature 4): the Reward AP is always-on
 * from boot, so a separate fallback AP is no longer needed.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "wifi_manager.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"

#include "config_manager.h"
#include "device_registry.h"
#include "event_ids.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Reconnect retry period in microseconds (10 s, hardcoded per spec §5.2). */
#define WIFI_MNGR_RECONNECT_PERIOD_US ((int64_t)10000000)

/** If no IP is obtained within this period after a successful
 *  esp_wifi_connect(), force a disconnect+reconnect ("associated but no IP"). */
#define WIFI_MNGR_CONN_WD_PERIOD_US ((int64_t)30000000)

/** Connectivity-supervisor poll period. */
#define WIFI_MNGR_SUPERVISOR_PERIOD_US ((int64_t)30000000)

/** Continuous STA-down time after which the device reboots as a last
 *  resort.  Only triggers when an SSID is configured. */
#define WIFI_MNGR_STA_DOWN_REBOOT_US ((int64_t)600000000)

/** Maximum number of stations allowed on the reward AP. */
#define WIFI_MNGR_REWARD_AP_MAX_STA (4U)

/** Default channel for reward AP before STA connects. */
#define WIFI_MNGR_REWARD_AP_DEFAULT_CHANNEL (6U)

/** Reward AP subnet gateway IP dotted-decimal. */
#define WIFI_MNGR_REWARD_AP_GW_IP ("192.168.5.1")

/** Reward AP subnet netmask. */
#define WIFI_MNGR_REWARD_AP_NETMASK ("255.255.255.0")

/** DHCP server option flag to offer a DNS server address to clients (DHCP option 6). */
#define DHCPS_OFFER_DNS (0x02U)

/** EtherType value identifying an IPv4 frame (bytes [12..13] of the Ethernet header). */
#define WIFI_MNGR_ETHERTYPE_IPV4 (0x0800U)

/** Minimum frame length that contains a complete Ethernet + IPv4 header
 *  (14-byte Ethernet header + 20-byte IPv4 header). */
#define WIFI_MNGR_MIN_ETHERNET_IPV4_LEN (34U)

/** Byte offset of the source MAC address within an Ethernet frame. */
#define WIFI_MNGR_ETH_SRC_MAC_OFFSET (6U)

/** Byte offset of the destination MAC address within an Ethernet frame. */
#define WIFI_MNGR_ETH_DST_MAC_OFFSET (0U)

/** Byte offset of the EtherType field within an Ethernet frame. */
#define WIFI_MNGR_ETH_TYPE_OFFSET (12U)

/** Byte offset of the IPv4 destination IP field within an Ethernet frame
 *  (14-byte Ethernet header + 16-byte offset inside the IPv4 header). */
#define WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET (30U)

/** Length of a complete Ethernet header (destination MAC + source MAC + EtherType). */
#define WIFI_MNGR_ETH_HEADER_LEN (14U)

/** Reward AP subnet base address packed as a big-endian uint32 (192.168.5.0). */
#define WIFI_MNGR_REWARD_AP_SUBNET_U32 (0xC0A80500U)

/** IPv4 /24 subnet mask as a uint32 bitmask (255.255.255.0). */
#define WIFI_MNGR_IPV4_SUBNET_MASK_24 (0xFFFFFF00U)

/** IPv4 limited broadcast address (255.255.255.255).
 *  DHCP Discover/Request frames use this as the destination; they must always
 *  be passed through so that unregistered devices can obtain an IP address. */
#define WIFI_MNGR_IPV4_LIMITED_BROADCAST (0xFFFFFFFFU)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "wifi_manager";

/** Default network interface for the AP (reward AP). */
static esp_netif_t * gp_netif_ap = NULL;

/** Default network interface for the STA. */
static esp_netif_t * gp_netif_sta = NULL;

/** true once STA has obtained an IP address. */
static volatile bool gb_sta_connected = false;

/** true when the reward AP is currently active. */
static volatile bool gb_reward_ap_active = false;

/** One-shot timer used to trigger STA reconnect attempts. */
static esp_timer_handle_t gp_reconnect_timer = NULL;

/** One-shot timer: fires if a connect attempt never yields an IP. */
static esp_timer_handle_t gp_conn_wd_timer = NULL;

/** Periodic timer: connectivity supervisor / last-resort reboot. */
static esp_timer_handle_t gp_supervisor_timer = NULL;

/** When true, suppress STA connect + supervisor in #wifi_mngr_init (safe mode). */
static volatile bool gb_safe_mode = false;

/** Timestamp (esp_timer_get_time) when the STA went/stayed down, or 0 if up. */
static volatile int64_t g_sta_down_since_us = 0;

/** Total STA (re)connect attempts since boot (telemetry). */
static volatile uint32_t g_reconnect_attempts = 0U;

/** Total STA disconnect events since boot (telemetry). */
static volatile uint32_t g_sta_disconnect_count = 0U;

/** Cached last DNS server forwarded to AP clients (DHCP churn reduction). */
static uint32_t g_last_fwd_dns = 0U;

/** True once a DNS server has been forwarded to the AP DHCP server. */
static bool gb_dns_forwarded = false;

/** true when NAPT should be armed on the next WIFI_EVENT_AP_START. */
static volatile bool gb_napt_pending = false;

/** Set when a config change should trigger an immediate reconnect instead of the 10 s delay.
 *
 * Written by the #ESPORT_EVENT_CONFIG_CHANGED handler (event loop task) before calling
 * \c esp_wifi_disconnect().  Read and cleared by the \c WIFI_EVENT_STA_DISCONNECTED
 * handler (event loop task).  Both accesses run in the same task, so no spinlock is needed. */
static volatile bool gb_reconnect_immediate = false;

/** True until the first successful STA IP assignment.
 *
 * Allows the first failed connect attempt on boot to retry immediately rather
 * than waiting the full #WIFI_MNGR_RECONNECT_PERIOD_US backoff. */
static volatile bool gb_first_connect_attempt = true;

/** Previous RX byte count snapshot for throughput measurement. */
static uint32_t g_prev_rx_bytes = 0U;

/** Previous TX byte count snapshot for throughput measurement. */
static uint32_t g_prev_tx_bytes = 0U;

/**
 * Cumulative AP RX byte counter, incremented by #wifi_mngr_ap_input_hook on every
 * frame received from an AP client.  Updated from the WiFi driver task.
 */
static volatile uint32_t g_ap_rx_bytes = 0U;

/**
 * Cumulative AP TX byte counter, incremented by #wifi_mngr_ap_linkoutput_hook on
 * every frame sent to an AP client.  Updated from the lwIP core task.
 */
static volatile uint32_t g_ap_tx_bytes = 0U;

/** Saved AP netif \c input function pointer, replaced by #wifi_mngr_ap_input_hook. */
static netif_input_fn gp_orig_ap_input = NULL;

/** Saved AP netif \c linkoutput function pointer, replaced by #wifi_mngr_ap_linkoutput_hook. */
static netif_linkoutput_fn gp_orig_ap_linkoutput = NULL;

/** Spinlock protecting #g_ap_rx_bytes and #g_ap_tx_bytes against concurrent access. */
static portMUX_TYPE g_ap_bytes_mux = portMUX_INITIALIZER_UNLOCKED;

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static void wifi_mngr_event_handler(void * p_arg, esp_event_base_t event_base, int32_t event_id,
    void * p_event_data);
static void wifi_mngr_reconnect_timer_cb(void * p_arg);
static void wifi_mngr_conn_wd_timer_cb(void * p_arg);
static void wifi_mngr_supervisor_timer_cb(void * p_arg);
static void wifi_mngr_schedule_reconnect(void);
static void wifi_mngr_ap_dns_forward(bool b_force);
static void wifi_mngr_config_changed_handler(void * p_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data);
static esp_err_t wifi_mngr_sta_connect(void);
static err_t     wifi_mngr_ap_input_hook(struct pbuf * p, struct netif * inp);
static err_t     wifi_mngr_ap_linkoutput_hook(struct netif * netif, struct pbuf * p);
static void      wifi_mngr_ap_hooks_install(void);
static esp_err_t wifi_mngr_ap_hooks_install_cb(void * ctx);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t wifi_mngr_init(void)
{
    esp_err_t ret;

    /* Initialise TCP/IP stack and create default netif instances. */
    ret = esp_netif_init();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_netif_init failed: 0x%x", ret);
        return ret;
    }

    gp_netif_ap  = esp_netif_create_default_wifi_ap();
    gp_netif_sta = esp_netif_create_default_wifi_sta();

    if ((NULL == gp_netif_ap) || (NULL == gp_netif_sta))
    {
        ESP_LOGE(gp_tag, "esp_netif_create_default_wifi_ap/sta returned NULL");
        return ESP_FAIL;
    }

    /* Pre-configure the AP netif to 192.168.5.0/24 before the WiFi driver
     * starts.  The event loop task has higher priority than app_main, so
     * every WIFI_EVENT_AP_START (including the very first one fired from
     * esp_wifi_set_mode(APSTA)) is processed before wifi_mngr_reward_ap_set
     * can run esp_netif_set_ip_info.  By setting the IP here we ensure the
     * AP always uses 192.168.5.1, even on the first boot DHCP start.
     * DHCP is left stopped; the WIFI_EVENT_AP_START handler starts it with
     * the correct DNS option so the custom option is never lost on restarts. */
    {
        esp_netif_ip_info_t ap_ip;
        memset(&ap_ip, 0, sizeof(ap_ip));
        ip4addr_aton(WIFI_MNGR_REWARD_AP_GW_IP, (ip4_addr_t *)&ap_ip.ip);
        ip4addr_aton(WIFI_MNGR_REWARD_AP_GW_IP, (ip4_addr_t *)&ap_ip.gw);
        ip4addr_aton(WIFI_MNGR_REWARD_AP_NETMASK, (ip4_addr_t *)&ap_ip.netmask);
        (void)esp_netif_dhcps_stop(gp_netif_ap);
        (void)esp_netif_set_ip_info(gp_netif_ap, &ap_ip);
    }

    /* Initialise WiFi driver with default config. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret                    = esp_wifi_init(&cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_init failed: 0x%x", ret);
        return ret;
    }

    /* Register event handlers (WIFI_EVENT and IP_EVENT). */
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_mngr_event_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "register WIFI_EVENT handler failed: 0x%x", ret);
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_mngr_event_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "register IP_EVENT handler failed: 0x%x", ret);
        return ret;
    }

    ret = esp_event_handler_register(ESPORT_EVENT_BASE, ESPORT_EVENT_CONFIG_CHANGED,
        wifi_mngr_config_changed_handler, NULL);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "register CONFIG_CHANGED handler failed: 0x%x", ret);
        return ret;
    }

    /* Start in STA-only mode; AP interface is brought up by wifi_mngr_reward_ap_set(true). */
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_set_mode(STA) failed: 0x%x", ret);
        return ret;
    }

    /* Create the reconnect timer (one-shot, created here, started on disconnect). */
    esp_timer_create_args_t timer_args = {
        .callback        = wifi_mngr_reconnect_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_reconnect",
    };
    ret = esp_timer_create(&timer_args, &gp_reconnect_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create failed: 0x%x", ret);
        return ret;
    }

    /* Connection watchdog (one-shot, armed on each connect attempt). */
    esp_timer_create_args_t conn_wd_args = {
        .callback        = wifi_mngr_conn_wd_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_conn_wd",
    };
    ret = esp_timer_create(&conn_wd_args, &gp_conn_wd_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create(conn_wd) failed: 0x%x", ret);
        return ret;
    }

    /* Connectivity supervisor (periodic). */
    esp_timer_create_args_t supervisor_args = {
        .callback        = wifi_mngr_supervisor_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_supervisor",
    };
    ret = esp_timer_create(&supervisor_args, &gp_supervisor_timer);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_timer_create(supervisor) failed: 0x%x", ret);
        return ret;
    }

    /* Start the WiFi driver. */
    ret = esp_wifi_start();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_start failed: 0x%x", ret);
        return ret;
    }

    /* Disable Wi-Fi modem power-save.  In concurrent AP+STA the default
     * WIFI_PS_MIN_MODEM causes missed beacons / downlink frames and long-run STA
     * disconnects; the device is mains-powered so power-save offers no benefit. */
    (void)esp_wifi_set_ps(WIFI_PS_NONE);

    /* Attempt STA connection only when an SSID is configured. */
    char ssid[33] = { 0 };
    config_mngr_wifi_ssid_get(ssid, sizeof(ssid));

    if (gb_safe_mode)
    {
        ESP_LOGW(gp_tag, "SAFE MODE: skipping STA connection and connectivity supervisor");
    }
    else if ('\0' == ssid[0])
    {
        ESP_LOGI(gp_tag, "No STA SSID configured - skipping STA connection");
    }
    else
    {
        ret = wifi_mngr_sta_connect();
    }

    /* Always start the reward AP from boot (Feature 4: always-on). */
    esp_err_t ap_ret = wifi_mngr_reward_ap_set(true);
    if (ESP_OK != ap_ret)
    {
        ESP_LOGW(gp_tag, "wifi_mngr_init: reward AP start failed: %s", esp_err_to_name(ap_ret));
    }

    /* Start the connectivity supervisor.  When an SSID is configured
     * but the STA stays disconnected for WIFI_MNGR_STA_DOWN_REBOOT_US, the
     * device reboots as a last resort.  Seed the down-since timestamp so a
     * device that never manages to connect is still recovered.
     *
     * In safe mode the supervisor is NOT started: the device is already in a
     * reboot loop and must stay up so an operator can reach the dashboard. */
    if (!gb_safe_mode)
    {
        if ('\0' != ssid[0])
        {
            g_sta_down_since_us = esp_timer_get_time();
        }
        (void)esp_timer_start_periodic(gp_supervisor_timer,
            (uint64_t)WIFI_MNGR_SUPERVISOR_PERIOD_US);
    }

    ESP_LOGI(gp_tag, "initialised");
    return ret;
}

//--------------------------------------------------------------------------------------------------

void wifi_mngr_safe_mode_set(bool b_enable)
{
    gb_safe_mode = b_enable;
}

//--------------------------------------------------------------------------------------------------

esp_err_t wifi_mngr_reward_ap_set(bool b_enable)
{
    esp_err_t ret = ESP_OK;

    if (!b_enable)
    {
        /* Feature 4: reward AP is always-on; disable requests are ignored. */
        ESP_LOGD(gp_tag, "reward AP always-on: disable request ignored");
        return ESP_OK;
    }

    if (b_enable == gb_reward_ap_active)
    {
        /* Already in the requested state - guard against double-enable. */
        return ESP_OK;
    }

    if (b_enable)
    {
        char ap_ssid[33]     = { 0 };
        char ap_password[65] = { 0 };

        config_mngr_soft_ap_ssid_get(ap_ssid, sizeof(ap_ssid));
        config_mngr_soft_ap_password_get(ap_password, sizeof(ap_password));

        /* Arm NAPT before triggering the mode change so the flag is visible
         * to the WIFI_EVENT_AP_START handler regardless of task scheduling. */
        gb_napt_pending = true;

        /* Bring the AP interface up if it is not already running. */
        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ESP_OK != ret)
        {
            gb_napt_pending = false;
            ESP_LOGE(gp_tag, "esp_wifi_set_mode(APSTA) for reward AP failed: 0x%x", ret);
            return ret;
        }

        /* Configure the AP interface. */
        wifi_config_t ap_cfg;
        memset(&ap_cfg, 0, sizeof(ap_cfg));
        strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1U);
        ap_cfg.ap.ssid_len = (uint8_t)strnlen(ap_ssid, sizeof(ap_cfg.ap.ssid));
        strncpy((char *)ap_cfg.ap.password, ap_password, sizeof(ap_cfg.ap.password) - 1U);
        ap_cfg.ap.channel        = WIFI_MNGR_REWARD_AP_DEFAULT_CHANNEL;
        ap_cfg.ap.max_connection = WIFI_MNGR_REWARD_AP_MAX_STA;
        ap_cfg.ap.authmode       = (ap_password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

        /* Set the active flag BEFORE esp_wifi_set_config so that the
         * WIFI_EVENT_AP_START which fires from the AP's config-change restart
         * finds gb_reward_ap_active == true and enables NAPT.  Without this,
         * the event-loop task can process that AP_START before the calling
         * task returns from esp_wifi_set_config and sets the flag, leaving
         * gb_napt_pending=false and gb_reward_ap_active=false → NAPT skipped. */
        gb_reward_ap_active = true;

        ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (ESP_OK != ret)
        {
            gb_reward_ap_active = false; /* rollback */
            ESP_LOGE(gp_tag, "esp_wifi_set_config(AP) for reward AP failed: 0x%x", ret);
            return ret;
        }

        ESP_LOGI(gp_tag, "Reward AP enabled: SSID='%s'", ap_ssid);
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

bool wifi_mngr_sta_is_connected(void)
{
    return gb_sta_connected;
}

//--------------------------------------------------------------------------------------------------

bool wifi_mngr_reward_ap_is_active(void)
{
    return gb_reward_ap_active;
}

//--------------------------------------------------------------------------------------------------

uint8_t wifi_mngr_reward_ap_client_count(void)
{
    if (!gb_reward_ap_active)
    {
        return 0U;
    }

    wifi_sta_list_t sta_list;
    memset(&sta_list, 0, sizeof(sta_list));

    if (ESP_OK != esp_wifi_ap_get_sta_list(&sta_list))
    {
        return 0U;
    }

    return (uint8_t)sta_list.num;
}

//--------------------------------------------------------------------------------------------------

void wifi_mngr_sta_ip_get(char * p_buf, size_t len)
{
    if ((NULL == p_buf) || (0U == len))
    {
        return;
    }

    p_buf[0] = '\0';

    if (!gb_sta_connected)
    {
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (ESP_OK == esp_netif_get_ip_info(gp_netif_sta, &ip_info))
    {
        esp_ip4addr_ntoa(&ip_info.ip, p_buf, (int)len);
    }
}

//--------------------------------------------------------------------------------------------------

void wifi_mngr_reward_ap_ip_get(char * p_buf, size_t len)
{
    if ((NULL == p_buf) || (0U == len))
    {
        return;
    }

    p_buf[0] = '\0';

    if (!gb_reward_ap_active)
    {
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (ESP_OK == esp_netif_get_ip_info(gp_netif_ap, &ip_info))
    {
        esp_ip4addr_ntoa(&ip_info.ip, p_buf, (int)len);
    }
}

//--------------------------------------------------------------------------------------------------

uint32_t wifi_mngr_reward_ap_throughput_kbps(void)
{
    if (!gb_reward_ap_active)
    {
        return 0U;
    }

    /* Read hook-maintained byte counters under spinlock. */
    portENTER_CRITICAL(&g_ap_bytes_mux);
    uint32_t cur_rx = g_ap_rx_bytes;
    uint32_t cur_tx = g_ap_tx_bytes;
    portEXIT_CRITICAL(&g_ap_bytes_mux);

    /* Guard against 32-bit counter wrap: treat wrapped values as 0 delta. */
    uint32_t delta_bytes = 0U;
    if ((cur_rx >= g_prev_rx_bytes) && (cur_tx >= g_prev_tx_bytes))
    {
        delta_bytes = (cur_rx - g_prev_rx_bytes) + (cur_tx - g_prev_tx_bytes);
    }

    g_prev_rx_bytes = cur_rx;
    g_prev_tx_bytes = cur_tx;

    /* When no clients are associated with the AP, discard any background
     * netif traffic (DHCP server, ARP probes) so the dashboard reads 0. */
    if (0U == wifi_mngr_reward_ap_client_count())
    {
        return 0U;
    }

    /* Convert bytes to kbps: multiply by 8 (bits) then divide by 1000 (kilo). */
    uint32_t kbps = delta_bytes * 8U / 1000U;

    // ESP_LOGI(gp_tag,
    //     "throughput: rx=%" PRIu32 " tx=%" PRIu32 " delta=%" PRIu32 " bytes -> %" PRIu32 " kbps",
    //     cur_rx, cur_tx, delta_bytes, kbps);

    return kbps;
}

//--------------------------------------------------------------------------------------------------

int8_t wifi_mngr_sta_rssi(void)
{
    if (!gb_sta_connected)
    {
        return 0;
    }
    wifi_ap_record_t ap;
    if (ESP_OK != esp_wifi_sta_get_ap_info(&ap))
    {
        return 0;
    }
    return ap.rssi;
}

//--------------------------------------------------------------------------------------------------

uint32_t wifi_mngr_sta_reconnect_count(void)
{
    return g_reconnect_attempts;
}

//--------------------------------------------------------------------------------------------------

uint32_t wifi_mngr_sta_disconnect_count(void)
{
    return g_sta_disconnect_count;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Configure and initiate a STA connection attempt.
 *
 * Reads credentials from #config_manager and calls \c esp_wifi_connect().
 *
 * \return \c ESP_OK if \c esp_wifi_connect() was called successfully,
 *         or a non-zero \c esp_err_t on failure.
 */
static esp_err_t wifi_mngr_sta_connect(void)
{
    char ssid[33]     = { 0 };
    char password[65] = { 0 };

    config_mngr_wifi_ssid_get(ssid, sizeof(ssid));
    config_mngr_wifi_password_get(password, sizeof(password));

    g_reconnect_attempts++;

    wifi_config_t sta_cfg;
    memset(&sta_cfg, 0, sizeof(sta_cfg));
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1U);
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1U);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_set_config(STA) failed: 0x%x", ret);
        /* A synchronous failure produces no STA_DISCONNECTED event, so
         * the reconnect chain would otherwise die here.  Re-arm it explicitly. */
        wifi_mngr_schedule_reconnect();
        return ret;
    }

    ret = esp_wifi_connect();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_connect failed: 0x%x", ret);
        /* Same as above - guarantee a future retry. */
        wifi_mngr_schedule_reconnect();
        return ret;
    }

    /* Association started; guard against "associated but never gets an
     * IP" by arming a watchdog that forces disconnect+reconnect if no
     * IP_EVENT_STA_GOT_IP arrives in time. */
    (void)esp_timer_stop(gp_conn_wd_timer);
    (void)esp_timer_start_once(gp_conn_wd_timer, (uint64_t)WIFI_MNGR_CONN_WD_PERIOD_US);

    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief One-shot timer callback: retry STA connection.
 *
 * \param[in] p_arg  Unused timer argument.
 */
static void wifi_mngr_reconnect_timer_cb(void * p_arg)
{
    (void)p_arg;
    ESP_LOGI(gp_tag, "Retrying STA connection...");
    wifi_mngr_sta_connect();
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief (Re)arm the 10 s STA reconnect one-shot timer.
 */
static void wifi_mngr_schedule_reconnect(void)
{
    (void)esp_timer_stop(gp_reconnect_timer);
    (void)esp_timer_start_once(gp_reconnect_timer, (uint64_t)WIFI_MNGR_RECONNECT_PERIOD_US);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Connection watchdog: force a reconnect if association never
 *        produced an IP within #WIFI_MNGR_CONN_WD_PERIOD_US.
 *
 * \param[in] p_arg  Unused timer argument.
 */
static void wifi_mngr_conn_wd_timer_cb(void * p_arg)
{
    (void)p_arg;
    if (!gb_sta_connected)
    {
        ESP_LOGW(gp_tag, "Connect watchdog: no IP obtained, forcing reconnect");
        (void)esp_wifi_disconnect();
        wifi_mngr_schedule_reconnect();
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Connectivity supervisor: reboot as a last resort when the STA
 *        has been disconnected for too long while an SSID is configured.
 *
 * \param[in] p_arg  Unused timer argument.
 */
static void wifi_mngr_supervisor_timer_cb(void * p_arg)
{
    (void)p_arg;

    char ssid[33] = { 0 };
    config_mngr_wifi_ssid_get(ssid, sizeof(ssid));
    if ('\0' == ssid[0])
    {
        /* Unconfigured device: keep the AP up for setup, never reboot. */
        return;
    }

    if (gb_sta_connected)
    {
        return;
    }

    int64_t since = g_sta_down_since_us;
    if (0 == since)
    {
        g_sta_down_since_us = esp_timer_get_time();
        return;
    }

    if ((esp_timer_get_time() - since) >= WIFI_MNGR_STA_DOWN_REBOOT_US)
    {
        ESP_LOGE(gp_tag, "STA down > %lld s with SSID configured - rebooting (last resort)",
            (long long)(WIFI_MNGR_STA_DOWN_REBOOT_US / 1000000));
        esp_restart();
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Forward the STA's primary DNS server into the AP DHCP server response.
 *
 * Reads the DNS address obtained from the home network via the STA interface
 * and injects it into the AP DHCP server (DHCP option 6) so that clients
 * connected to the reward AP receive a working name server.  The DHCP server
 * is stopped and restarted to pick up the new option value.
 */
static void wifi_mngr_ap_dns_forward(bool b_force)
{
    esp_netif_dns_info_t dns;

    if (ESP_OK != esp_netif_get_dns_info(gp_netif_sta, ESP_NETIF_DNS_MAIN, &dns))
    {
        ESP_LOGW(gp_tag, "ap_dns_forward: could not read STA DNS");
        return;
    }

    /* Avoid needless DHCP-server stop/restart churn on every STA flap.
     * Only re-forward (which restarts the AP DHCP server) when forced (AP just
     * (re)started and lost its option) or when the STA's DNS actually changed. */
    uint32_t dns_ip = dns.ip.u_addr.ip4.addr;
    if (!b_force && gb_dns_forwarded && (dns_ip == g_last_fwd_dns))
    {
        return;
    }

    uint8_t dhcps_offer_dns = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(gp_netif_ap);
    esp_netif_dhcps_option(gp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &dhcps_offer_dns, sizeof(dhcps_offer_dns));
    esp_netif_set_dns_info(gp_netif_ap, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(gp_netif_ap);

    g_last_fwd_dns   = dns_ip;
    gb_dns_forwarded = true;

    ESP_LOGI(gp_tag, "AP DNS forwarded from STA (" IPSTR ")", IP2STR(&dns.ip.u_addr.ip4));
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Netif input hook that counts inbound bytes from AP clients and enforces
 * per-device internet access control at the IP layer.
 *
 * Intercepts #gp_netif_ap 's \c input function pointer.  Accumulates per-device
 * byte counts via #device_reg_mac_rx_bytes_add, updates the global #g_ap_rx_bytes
 * counter, then applies an IPv4 drop rule for internet-destined frames from
 * unregistered or expired devices.
 *
 * Guards every payload dereference behind a minimum-length check first: a
 * runt pbuf (shorter than a full Ethernet header) is counted and passed
 * through untouched, so no out-of-bounds read of \c p->payload can occur.
 *
 * \param[in] p    Received Ethernet frame as a pbuf chain.
 * \param[in] inp  Netif the frame arrived on (the AP lwIP netif).
 *
 * \return Error code from the original input handler, or \c ERR_OK if the frame
 *         was silently dropped.
 */
static err_t wifi_mngr_ap_input_hook(struct pbuf * p, struct netif * inp)
{
    /* Global RX byte count - safe unconditionally, does not touch the payload. */
    portENTER_CRITICAL(&g_ap_bytes_mux);
    g_ap_rx_bytes += (uint32_t)p->tot_len;
    portEXIT_CRITICAL(&g_ap_bytes_mux);

    /* Guard against runt frames BEFORE any payload dereference below: a valid
     * Ethernet header (dest MAC + src MAC + EtherType) requires at least
     * WIFI_MNGR_ETH_HEADER_LEN bytes in the first pbuf segment. Without this
     * check first, the source-MAC read below could run past the end of a
     * short/malformed pbuf. */
    if (p->len < WIFI_MNGR_ETH_HEADER_LEN)
    {
        return gp_orig_ap_input(p, inp);
    }

    /* Per-device RX byte count - source MAC starts at byte WIFI_MNGR_ETH_SRC_MAC_OFFSET. */
    device_reg_mac_rx_bytes_add((const uint8_t *)p->payload + WIFI_MNGR_ETH_SRC_MAC_OFFSET,
        (uint32_t)p->tot_len);

    /* Per-device internet access filter. */
    if (p->len >= WIFI_MNGR_MIN_ETHERNET_IPV4_LEN)
    {
        /* Extract EtherType from WIFI_MNGR_ETH_TYPE_OFFSET. */
        const uint8_t * p_eth     = (const uint8_t *)p->payload;
        uint16_t        ethertype = (uint16_t)(((uint16_t)p_eth[WIFI_MNGR_ETH_TYPE_OFFSET] << 8U) |
                                        p_eth[WIFI_MNGR_ETH_TYPE_OFFSET + 1U]);

        if (WIFI_MNGR_ETHERTYPE_IPV4 == ethertype)
        {
            /* IPv4: extract destination IP from WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET (big-endian). */
            uint32_t dst_ip = ((uint32_t)p_eth[WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET] << 24U) |
                              ((uint32_t)p_eth[WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET + 1U] << 16U) |
                              ((uint32_t)p_eth[WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET + 2U] << 8U) |
                              (uint32_t)p_eth[WIFI_MNGR_ETH_IPV4_DST_IP_OFFSET + 3U];

            /* Drop internet-destined packets from devices without access.
             * Two destination classes are ALWAYS passed through regardless of
             * registration status:
             *   1. Subnet-local destinations (192.168.5.0/24): covers traffic to
             *      the gateway (status dashboard) and DNS on 192.168.5.1.
             *   2. Limited broadcast (255.255.255.255): used by DHCP Discover and
             *      DHCP Request frames - without this, unregistered devices can
             *      never obtain an IP address and cannot reach the gateway. */
            bool b_local =
                (WIFI_MNGR_REWARD_AP_SUBNET_U32 == (dst_ip & WIFI_MNGR_IPV4_SUBNET_MASK_24)) ||
                (WIFI_MNGR_IPV4_LIMITED_BROADCAST == dst_ip);

            if (!b_local)
            {
                /* Destination outside 192.168.5.0/24 - check device allowance. */
                if (!device_reg_mac_internet_allowed(p_eth + WIFI_MNGR_ETH_SRC_MAC_OFFSET))
                {
                    pbuf_free(p);
                    return ERR_OK;
                }
            }
        }
    }

    return gp_orig_ap_input(p, inp);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Netif linkoutput hook that counts outbound bytes to AP clients.
 *
 * Intercepts #gp_netif_ap 's \c linkoutput function pointer.  Accumulates per-device
 * byte counts via #device_reg_mac_tx_bytes_add, updates the global #g_ap_tx_bytes
 * counter, then delegates to the original linkoutput function.
 *
 * \param[in] netif  Netif sending the frame (the AP lwIP netif).
 * \param[in] p      Ethernet frame to transmit as a pbuf chain.
 *
 * \return Error code from the original linkoutput handler.
 */
static err_t wifi_mngr_ap_linkoutput_hook(struct netif * netif, struct pbuf * p)
{
    /* Per-device TX byte count - destination MAC starts at byte WIFI_MNGR_ETH_DST_MAC_OFFSET. */
    device_reg_mac_tx_bytes_add((const uint8_t *)p->payload + WIFI_MNGR_ETH_DST_MAC_OFFSET,
        (uint32_t)p->tot_len);

    /* Global TX byte count. */
    portENTER_CRITICAL(&g_ap_bytes_mux);
    g_ap_tx_bytes += (uint32_t)p->tot_len;
    portEXIT_CRITICAL(&g_ap_bytes_mux);
    return gp_orig_ap_linkoutput(netif, p);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Install byte-count hooks on the AP lwIP netif.
 *
 * Replaces \c netif->input and \c netif->linkoutput with
 * #wifi_mngr_ap_input_hook and #wifi_mngr_ap_linkoutput_hook respectively,
 * saving the originals in #gp_orig_ap_input and #gp_orig_ap_linkoutput.
 * Must be called after every #WIFI_EVENT_AP_START because the lwIP
 * \c struct \c netif is removed and re-added on each AP restart, resetting
 * the function pointers to their defaults.  Resets #g_ap_rx_bytes and
 * #g_ap_tx_bytes so the new interval starts clean.
 */
static void wifi_mngr_ap_hooks_install(void)
{
    /* lwIP netif pointers must only be mutated from the tcpip task when
     * CONFIG_LWIP_TCPIP_CORE_LOCKING is disabled.  Run the actual swap inside the
     * tcpip context. */
    (void)esp_netif_tcpip_exec(wifi_mngr_ap_hooks_install_cb, NULL);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief tcpip-context callback that performs the AP netif pointer swap.
 *
 * \param[in] ctx  Unused.
 *
 * \return \c ESP_OK on success, or an error if the AP netif is unavailable.
 */
static esp_err_t wifi_mngr_ap_hooks_install_cb(void * ctx)
{
    (void)ctx;

    struct netif * p_netif = (struct netif *)esp_netif_get_netif_impl(gp_netif_ap);
    if (NULL == p_netif)
    {
        ESP_LOGW(gp_tag, "hooks_install: AP lwIP netif unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    /* Guard against double-install: if our hook is already in place, skip. */
    if (p_netif->input == wifi_mngr_ap_input_hook)
    {
        return ESP_OK;
    }

    gp_orig_ap_input      = p_netif->input;
    gp_orig_ap_linkoutput = p_netif->linkoutput;
    p_netif->input        = wifi_mngr_ap_input_hook;
    p_netif->linkoutput   = wifi_mngr_ap_linkoutput_hook;

    portENTER_CRITICAL(&g_ap_bytes_mux);
    g_ap_rx_bytes = 0U;
    g_ap_tx_bytes = 0U;
    portEXIT_CRITICAL(&g_ap_bytes_mux);
    g_prev_rx_bytes = 0U;
    g_prev_tx_bytes = 0U;

    ESP_LOGI(gp_tag, "AP byte-count hooks installed (input=%p linkoutput=%p)",
        (void *)gp_orig_ap_input, (void *)gp_orig_ap_linkoutput);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Unified event handler for WIFI_EVENT and IP_EVENT.
 *
 * \param[in] p_arg        Unused handler argument.
 * \param[in] event_base   Event base (WIFI_EVENT or IP_EVENT).
 * \param[in] event_id     Event identifier.
 * \param[in] p_event_data Pointer to event-specific data (may be NULL).
 */
static void wifi_mngr_event_handler(void * p_arg, esp_event_base_t event_base, int32_t event_id,
    void * p_event_data)
{
    (void)p_arg;
    (void)p_event_data;

    if (WIFI_EVENT == event_base)
    {
        if (WIFI_EVENT_AP_START == event_id)
        {
            /* The AP netif is now UP.  This event fires once when
             * esp_wifi_set_mode(APSTA) starts the AP, and AGAIN when
             * esp_wifi_set_config(WIFI_IF_AP,...) causes the WiFi driver to
             * restart the AP (AP_STOP + AP_START).  On the second restart,
             * esp_netif_stop_api() calls esp_netif_lwip_remove() which removes
             * the lwIP struct netif and re-adds it on the following start, which
             * resets napt=0.  Therefore we must re-enable NAPT on every AP_START
             * when the reward AP is supposed to be active, not just the first.
             *
             * Use (gb_napt_pending || gb_reward_ap_active) rather than
             * gb_reward_ap_active alone to avoid a race condition: when the AP is
             * first enabled (e.g. counter set via the config page from a low-priority
             * HTTP task), the event loop task may process WIFI_EVENT_AP_START before
             * the caller has set gb_reward_ap_active = true.  gb_napt_pending is
             * set before any mode/config call, so it is always visible here. */
            if (gb_napt_pending || gb_reward_ap_active)
            {
                /* Forward DNS and enable NAPT only when STA is connected.
                 * NAPT requires a valid STA IP to route AP-client traffic to
                 * the internet; enabling it when STA is disconnected causes
                 * `esp_netif_napt_enable` to read the wrong AP IP (the default
                 * 192.168.4.1, set before our explicit 192.168.5.x assignment)
                 * and registers that address as the NAPT-local address.  Any
                 * packets to 192.168.5.1 are then mis-identified as external
                 * and forwarded to STA (not connected) → silently dropped,
                 * preventing ALL traffic to the gateway including the web UI. */
                if (gb_sta_connected)
                {
                    wifi_mngr_ap_dns_forward(true);
                    /* Assert STA as the default netif so that the lwIP routing
                     * layer sends NATted AP-client traffic through the home
                     * network. */
                    esp_netif_set_default_netif(gp_netif_sta);
                    esp_err_t napt_err = esp_netif_napt_enable(gp_netif_ap);
                    if (ESP_OK != napt_err)
                    {
                        ESP_LOGE(gp_tag, "esp_netif_napt_enable failed: 0x%x", napt_err);
                    }
                    else
                    {
                        ESP_LOGI(gp_tag, "NAPT enabled on reward AP");
                    }
                }
                else
                {
                    /* STA not yet connected: (re)start the DHCP server.
                     * Must run on every WIFI_EVENT_AP_START because the
                     * ESP-IDF netif glue resets DHCP state on each AP restart. */
                    (void)esp_netif_dhcps_stop(gp_netif_ap);
                    (void)esp_netif_dhcps_start(gp_netif_ap);
                }
                /* Always re-install byte-count hooks because the lwIP struct
                 * netif is re-created on every AP restart. */
                wifi_mngr_ap_hooks_install();
            }
            /* Consume the pending flag regardless. */
            gb_napt_pending = false;
        }
        else if (WIFI_EVENT_STA_DISCONNECTED == event_id)
        {
            gb_sta_connected = false;
            g_sta_disconnect_count++;
            (void)esp_timer_stop(gp_conn_wd_timer);
            if (0 == g_sta_down_since_us)
            {
                g_sta_down_since_us = esp_timer_get_time(); /* mark STA down */
            }

            if (gb_reconnect_immediate)
            {
                /* Config was just saved with new credentials: reconnect without delay. */
                gb_reconnect_immediate = false;
                esp_timer_stop(gp_reconnect_timer);
                wifi_mngr_sta_connect();
                ESP_LOGI(gp_tag, "STA disconnected - reconnecting immediately (config changed)");
            }
            else if (gb_first_connect_attempt)
            {
                /* First boot attempt failed: retry immediately, no point waiting 10 s. */
                esp_timer_stop(gp_reconnect_timer);
                wifi_mngr_sta_connect();
                ESP_LOGI(gp_tag, "STA first connect failed - retrying immediately");
            }
            else
            {
                /* Normal path: schedule reconnect attempt in 10 s. */
                esp_timer_stop(gp_reconnect_timer);
                esp_timer_start_once(gp_reconnect_timer, WIFI_MNGR_RECONNECT_PERIOD_US);
                ESP_LOGI(gp_tag, "STA disconnected - reconnect in 10 s");
            }

            /* Notify application. */
            esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_STA_DISCONNECTED, NULL, 0,
                pdMS_TO_TICKS(10));
        }
    }
    else if (IP_EVENT == event_base)
    {
        if (IP_EVENT_STA_GOT_IP == event_id)
        {
            gb_sta_connected         = true;
            gb_first_connect_attempt = false;
            g_sta_down_since_us      = 0;           /* STA is up */
            (void)esp_timer_stop(gp_conn_wd_timer); /* got IP in time */

            /* Make STA the default netif so that the lwIP routing layer sends
             * outbound traffic (including NATted AP-client traffic) through
             * the home network.  Must be set before NAPT can route correctly. */
            esp_netif_set_default_netif(gp_netif_sta);

            /* Stop pending reconnect timer. */
            esp_timer_stop(gp_reconnect_timer);

            /* If the reward AP is already active: forward DNS, assert the
             * default netif, and enable NAPT.  NAPT is intentionally deferred
             * to this point (not enabled on WIFI_EVENT_AP_START) so that
             * esp_netif_napt_enable always reads the correct AP IP (192.168.5.1)
             * and a valid STA IP exists for the routing layer. */
            if (gb_reward_ap_active)
            {
                wifi_mngr_ap_dns_forward(false);
                esp_netif_set_default_netif(gp_netif_sta);
                esp_err_t napt_err = esp_netif_napt_enable(gp_netif_ap);
                if (ESP_OK != napt_err)
                {
                    ESP_LOGE(gp_tag, "esp_netif_napt_enable on STA connect failed: 0x%x", napt_err);
                }
                else
                {
                    ESP_LOGI(gp_tag, "NAPT enabled on reward AP (STA connected)");
                }
            }

            /* Notify application. */
            esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_STA_CONNECTED, NULL, 0,
                pdMS_TO_TICKS(10));

            ESP_LOGI(gp_tag, "STA connected and got IP");
        }
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief ESP-IDF event loop handler for #ESPORT_EVENT_CONFIG_CHANGED.
 *
 * Called when the user saves new configuration via the web UI.  If an SSID is
 * configured and the STA is not yet connected, initiates a connection attempt
 * immediately (bypassing the 10-second reconnect timer).  If the STA is already
 * connected (e.g. user changed SSID/password), disconnects first; the
 * \c WIFI_EVENT_STA_DISCONNECTED handler detects #gb_reconnect_immediate and
 * reconnects with the fresh credentials without delay.
 *
 * \param[in] p_arg       Unused.
 * \param[in] base        Event base (unused).
 * \param[in] event_id    Event identifier (unused).
 * \param[in] p_event_data Unused.
 */
static void wifi_mngr_config_changed_handler(void * p_arg, esp_event_base_t base, int32_t event_id,
    void * p_event_data)
{
    (void)p_arg;
    (void)base;
    (void)event_id;
    (void)p_event_data;

    char new_ssid[33] = { 0 };
    char new_pwd[65]  = { 0 };
    config_mngr_wifi_ssid_get(new_ssid, sizeof(new_ssid));
    config_mngr_wifi_password_get(new_pwd, sizeof(new_pwd));

    if ('\0' == new_ssid[0])
    {
        /* No SSID configured - nothing to connect to. */
        return;
    }

    /* Read what credentials the WiFi driver is currently using.
     * Only reconnect if SSID or password actually changed to avoid
     * disrupting the STA connection on every unrelated config save
     * (e.g. session interval, pulse debounce, device nicknames). */
    wifi_config_t cur_cfg;
    memset(&cur_cfg, 0, sizeof(cur_cfg));
    (void)esp_wifi_get_config(WIFI_IF_STA, &cur_cfg);

    bool b_ssid_changed =
        (0 != strncmp(new_ssid, (const char *)cur_cfg.sta.ssid, sizeof(cur_cfg.sta.ssid)));
    bool b_pwd_changed =
        (0 != strncmp(new_pwd, (const char *)cur_cfg.sta.password, sizeof(cur_cfg.sta.password)));

    if (!b_ssid_changed && !b_pwd_changed)
    {
        /* Credentials unchanged - other config fields were saved; no reconnect needed. */
        return;
    }

    if (gb_sta_connected)
    {
        /* STA is up with old credentials: arm immediate-reconnect flag then disconnect.
         * WIFI_EVENT_STA_DISCONNECTED will call wifi_mngr_sta_connect() immediately
         * (no 10 s delay) using the newly saved credentials. */
        gb_reconnect_immediate = true;
        esp_wifi_disconnect();
        ESP_LOGI(gp_tag, "Config changed: STA credentials updated, reconnecting");
    }
    else
    {
        /* STA is not connected: connect now using the new credentials. */
        esp_timer_stop(gp_reconnect_timer);
        wifi_mngr_sta_connect();
        ESP_LOGI(gp_tag, "Config changed: initiating STA connection with new credentials");
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file */
