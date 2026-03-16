/**
 * \file
 * \brief Wi-Fi manager (AP+STA mode with NAT) — full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "wifi_manager.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netif.h"

#include "config_manager.h"
#include "event_ids.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Reconnect retry period in microseconds (10 s, hardcoded per spec §5.2). */
#define WIFI_MNGR_RECONNECT_PERIOD_US ((int64_t)10000000)

/** Maximum number of stations allowed on the reward AP. */
#define WIFI_MNGR_REWARD_AP_MAX_STA (4U)

/** Maximum number of stations allowed on the config AP. */
#define WIFI_MNGR_CONFIG_AP_MAX_STA (4U)

/** Channel for the config AP (fixed, spec §5.2). */
#define WIFI_MNGR_CONFIG_AP_CHANNEL (1U)

/** Default channel for reward AP before STA connects. */
#define WIFI_MNGR_REWARD_AP_DEFAULT_CHANNEL (6U)

/** Reward AP subnet gateway IP dotted-decimal. */
#define WIFI_MNGR_REWARD_AP_GW_IP ("192.168.5.1")

/** Reward AP subnet netmask. */
#define WIFI_MNGR_REWARD_AP_NETMASK ("255.255.255.0")

/** DHCP server option flag to offer a DNS server address to clients (DHCP option 6). */
#define DHCPS_OFFER_DNS (0x02U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "wifi_manager";

/** Default network interface for the AP (shared by config AP and reward AP). */
static esp_netif_t * gp_netif_ap = NULL;

/** Default network interface for the STA. */
static esp_netif_t * gp_netif_sta = NULL;

/** true once STA has obtained an IP address. */
static volatile bool gb_sta_connected = false;

/** true when the reward AP is currently active. */
static volatile bool gb_reward_ap_active = false;

/** One-shot timer used to trigger STA reconnect attempts. */
static esp_timer_handle_t gp_reconnect_timer = NULL;

/** true if the config AP is currently enabled. */
static volatile bool gb_config_ap_active = false;

/** true when NAPT should be armed on the next WIFI_EVENT_AP_START. */
static volatile bool gb_napt_pending = false;

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
static void wifi_mngr_ap_dns_forward(void);
static esp_err_t wifi_mngr_config_ap_enable(void);
static esp_err_t wifi_mngr_config_ap_disable(void);
static esp_err_t wifi_mngr_sta_connect(void);
static err_t     wifi_mngr_ap_input_hook(struct pbuf * p, struct netif * inp);
static err_t     wifi_mngr_ap_linkoutput_hook(struct netif * netif, struct pbuf * p);
static void      wifi_mngr_ap_hooks_install(void);
static void      wifi_mngr_ap_hooks_uninstall(void);

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

    /* Start in STA-only mode; AP interface is brought up on demand by
     * wifi_mngr_config_ap_enable() or wifi_mngr_reward_ap_set(true). */
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

    /* Start the WiFi driver. */
    ret = esp_wifi_start();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_start failed: 0x%x", ret);
        return ret;
    }

    /* Attempt STA connection only when an SSID is configured. */
    char ssid[33] = { 0 };
    config_mngr_wifi_ssid_get(ssid, sizeof(ssid));

    if ('\0' == ssid[0])
    {
        ESP_LOGI(gp_tag, "No STA SSID configured — enabling config AP immediately");
        ret = wifi_mngr_config_ap_enable();
    }
    else
    {
        ret = wifi_mngr_sta_connect();
    }

    ESP_LOGI(gp_tag, "initialised");
    return ret;
}

//--------------------------------------------------------------------------------------------------

esp_err_t wifi_mngr_reward_ap_set(bool b_enable)
{
    esp_err_t ret = ESP_OK;

    if (b_enable == gb_reward_ap_active)
    {
        /* Already in the requested state — guard against double-enable/disable. */
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

        /* Stop DHCP server before changing IP configuration. */
        esp_netif_dhcps_stop(gp_netif_ap);

        /* Set subnet/gateway for the reward AP (192.168.5.0/24). */
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        ip4addr_aton(WIFI_MNGR_REWARD_AP_GW_IP, (ip4_addr_t *)&ip_info.ip);
        ip4addr_aton(WIFI_MNGR_REWARD_AP_GW_IP, (ip4_addr_t *)&ip_info.gw);
        ip4addr_aton(WIFI_MNGR_REWARD_AP_NETMASK, (ip4_addr_t *)&ip_info.netmask);

        ret = esp_netif_set_ip_info(gp_netif_ap, &ip_info);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_netif_set_ip_info for reward AP failed: 0x%x", ret);
            esp_netif_dhcps_start(gp_netif_ap);
            return ret;
        }

        /* Restart DHCP server with new pool. */
        ret = esp_netif_dhcps_start(gp_netif_ap);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_netif_dhcps_start for reward AP failed: 0x%x", ret);
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

        ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "esp_wifi_set_config(AP) for reward AP failed: 0x%x", ret);
            return ret;
        }

        gb_reward_ap_active = true;
        ESP_LOGI(gp_tag, "Reward AP enabled: SSID='%s'", ap_ssid);
    }
    else
    {
        /* Disarm any pending NAPT request and disable NAPT.  The AP netif is
         * still up here so esp_netif_napt_disable() will succeed. */
        gb_napt_pending = false;
        esp_netif_napt_disable(gp_netif_ap);

        /* Uninstall the byte-count netif hooks before tearing down the AP. */
        wifi_mngr_ap_hooks_uninstall();

        /* Restore default AP config (config AP may take over). */
        esp_netif_dhcps_stop(gp_netif_ap);

        /* 192.168.4.1 — lwip default for AP interface. */
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        ip4addr_aton("192.168.4.1", (ip4_addr_t *)&ip_info.ip);
        ip4addr_aton("192.168.4.1", (ip4_addr_t *)&ip_info.gw);
        ip4addr_aton("255.255.255.0", (ip4_addr_t *)&ip_info.netmask);
        esp_netif_set_ip_info(gp_netif_ap, &ip_info);

        esp_netif_dhcps_start(gp_netif_ap);

        gb_reward_ap_active = false;
        /* Reset throughput measurement counters so the next enable starts clean. */
        portENTER_CRITICAL(&g_ap_bytes_mux);
        g_ap_rx_bytes = 0U;
        g_ap_tx_bytes = 0U;
        portEXIT_CRITICAL(&g_ap_bytes_mux);
        g_prev_rx_bytes = 0U;
        g_prev_tx_bytes = 0U;

        if (!gb_config_ap_active)
        {
            /* No AP needed at all — revert to STA-only mode. */
            (void)esp_wifi_set_mode(WIFI_MODE_STA);
        }

        ESP_LOGI(gp_tag, "Reward AP disabled");
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

/**
 * \brief Query whether the config (fallback) Soft AP is currently active.
 *
 * \return \c true if the config AP is up, \c false otherwise.
 */
bool wifi_mngr_config_ap_is_active(void)
{
    return gb_config_ap_active;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Return the combined RX+TX throughput on the reward AP in kbps.
 *
 * Reads cumulative byte counters maintained by the netif input/linkoutput
 * hooks (#wifi_mngr_ap_input_hook and #wifi_mngr_ap_linkoutput_hook),
 * computes the delta since the previous call, and converts to kbps.
 * Designed to be called exactly once per second from the tick callback.
 * Returns \c 0 when the reward AP is inactive or on the first call after
 * activation.
 *
 * \return Combined RX+TX throughput in kbps.
 */
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

    /* Convert bytes to kbps: multiply by 8 (bits) then divide by 1000 (kilo). */
    uint32_t kbps = delta_bytes * 8U / 1000U;
    ESP_LOGI(gp_tag,
        "throughput: rx=%" PRIu32 " tx=%" PRIu32 " delta=%" PRIu32 " bytes -> %" PRIu32 " kbps",
        cur_rx, cur_tx, delta_bytes, kbps);
    return kbps;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Enable the config (fallback) SoftAP.
 *
 * Configures the AP interface with Kconfig-defined SSID/password,
 * channel 1, max #WIFI_MNGR_CONFIG_AP_MAX_STA stations.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t wifi_mngr_config_ap_enable(void)
{
    if (gb_config_ap_active)
    {
        return ESP_OK;
    }

    /* Bring the AP interface up if it is not already running. */
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_set_mode(APSTA) for config AP failed: 0x%x", ret);
        return ret;
    }

    wifi_config_t ap_cfg;
    memset(&ap_cfg, 0, sizeof(ap_cfg));

    strncpy((char *)ap_cfg.ap.ssid, CONFIG_ESPORT_CONFIG_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1U);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(CONFIG_ESPORT_CONFIG_AP_SSID);
    strncpy((char *)ap_cfg.ap.password, CONFIG_ESPORT_CONFIG_AP_PASSWORD,
        sizeof(ap_cfg.ap.password) - 1U);
    ap_cfg.ap.channel        = WIFI_MNGR_CONFIG_AP_CHANNEL;
    ap_cfg.ap.max_connection = WIFI_MNGR_CONFIG_AP_MAX_STA;
    ap_cfg.ap.authmode = (ap_cfg.ap.password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_set_config(AP) for config AP failed: 0x%x", ret);
        return ret;
    }

    gb_config_ap_active = true;
    ESP_LOGI(gp_tag, "Config AP enabled: SSID='%s'", CONFIG_ESPORT_CONFIG_AP_SSID);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Disable the config (fallback) SoftAP.
 *
 * Clears the AP SSID so it is no longer visible; does not stop the WiFi
 * driver (AP+STA mode is maintained for the reward AP).
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
static esp_err_t wifi_mngr_config_ap_disable(void)
{
    if (!gb_config_ap_active)
    {
        return ESP_OK;
    }

    gb_config_ap_active = false;

    if (!gb_reward_ap_active)
    {
        /* No AP needed at all — revert to STA-only mode so the AP interface
         * stops transmitting entirely (no ESP_XXXXXX default beacon). */
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
    }

    ESP_LOGI(gp_tag, "Config AP disabled");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

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

    wifi_config_t sta_cfg;
    memset(&sta_cfg, 0, sizeof(sta_cfg));
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1U);
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1U);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_set_config(STA) failed: 0x%x", ret);
        return ret;
    }

    ret = esp_wifi_connect();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "esp_wifi_connect failed: 0x%x", ret);
    }

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
 * \brief Forward the STA's primary DNS server into the AP DHCP server response.
 *
 * Reads the DNS address obtained from the home network via the STA interface
 * and injects it into the AP DHCP server (DHCP option 6) so that clients
 * connected to the reward AP receive a working name server.  The DHCP server
 * is stopped and restarted to pick up the new option value.
 */
static void wifi_mngr_ap_dns_forward(void)
{
    esp_netif_dns_info_t dns;

    if (ESP_OK != esp_netif_get_dns_info(gp_netif_sta, ESP_NETIF_DNS_MAIN, &dns))
    {
        ESP_LOGW(gp_tag, "ap_dns_forward: could not read STA DNS");
        return;
    }

    uint8_t dhcps_offer_dns = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(gp_netif_ap);
    esp_netif_dhcps_option(gp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &dhcps_offer_dns, sizeof(dhcps_offer_dns));
    esp_netif_set_dns_info(gp_netif_ap, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(gp_netif_ap);

    ESP_LOGI(gp_tag, "AP DNS forwarded from STA (" IPSTR ")", IP2STR(&dns.ip.u_addr.ip4));
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Netif input hook that counts inbound bytes from AP clients.
 *
 * Intercepts #gp_netif_ap 's \c input function pointer.  Adds the pbuf length
 * to #g_ap_rx_bytes under #g_ap_bytes_mux, then delegates to the original
 * input function saved in #gp_orig_ap_input.
 *
 * \param[in] p    Received Ethernet frame as a pbuf chain.
 * \param[in] inp  Netif the frame arrived on (the AP lwIP netif).
 *
 * \return Error code from the original input handler.
 */
static err_t wifi_mngr_ap_input_hook(struct pbuf * p, struct netif * inp)
{
    portENTER_CRITICAL(&g_ap_bytes_mux);
    g_ap_rx_bytes += (uint32_t)p->tot_len;
    portEXIT_CRITICAL(&g_ap_bytes_mux);
    return gp_orig_ap_input(p, inp);
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Netif linkoutput hook that counts outbound bytes to AP clients.
 *
 * Intercepts #gp_netif_ap 's \c linkoutput function pointer.  Adds the pbuf
 * length to #g_ap_tx_bytes under #g_ap_bytes_mux, then delegates to the
 * original linkoutput function saved in #gp_orig_ap_linkoutput.
 *
 * \param[in] netif  Netif sending the frame (the AP lwIP netif).
 * \param[in] p      Ethernet frame to transmit as a pbuf chain.
 *
 * \return Error code from the original linkoutput handler.
 */
static err_t wifi_mngr_ap_linkoutput_hook(struct netif * netif, struct pbuf * p)
{
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
    struct netif * p_netif = (struct netif *)esp_netif_get_netif_impl(gp_netif_ap);
    if (NULL == p_netif)
    {
        ESP_LOGW(gp_tag, "hooks_install: AP lwIP netif unavailable");
        return;
    }

    /* Guard against double-install: if our hook is already in place, skip. */
    if (p_netif->input == wifi_mngr_ap_input_hook)
    {
        return;
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
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Uninstall byte-count hooks from the AP lwIP netif.
 *
 * Restores \c netif->input and \c netif->linkoutput to the functions saved by
 * the last call to #wifi_mngr_ap_hooks_install.  Safe to call if hooks were
 * never installed (no-op in that case).
 */
static void wifi_mngr_ap_hooks_uninstall(void)
{
    if (NULL == gp_orig_ap_input)
    {
        return; /* Hooks were never installed. */
    }

    struct netif * p_netif = (struct netif *)esp_netif_get_netif_impl(gp_netif_ap);
    if ((NULL != p_netif) && (p_netif->input == wifi_mngr_ap_input_hook))
    {
        p_netif->input      = gp_orig_ap_input;
        p_netif->linkoutput = gp_orig_ap_linkoutput;
    }

    gp_orig_ap_input      = NULL;
    gp_orig_ap_linkoutput = NULL;
    ESP_LOGI(gp_tag, "AP byte-count hooks uninstalled");
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
             * when the reward AP is supposed to be active, not just the first. */
            if (gb_reward_ap_active)
            {
                /* Forward DNS from STA to AP DHCP so clients receive a working
                 * name server on their first lease. */
                if (gb_sta_connected)
                {
                    wifi_mngr_ap_dns_forward();
                }
                /* Assert STA as the default netif so that the lwIP routing layer
                 * sends outbound traffic (incl. NATted AP-client traffic) through
                 * the home network. */
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
                /* Install byte-count hooks on the AP netif after each (re-)start so that
                 * wifi_mngr_reward_ap_throughput_kbps() receives actual traffic data.
                 * The lwIP struct netif is re-created on every AP restart, so we must
                 * re-hook on every WIFI_EVENT_AP_START. */
                wifi_mngr_ap_hooks_install();
            }
            /* Consume the pending flag regardless. */
            gb_napt_pending = false;
        }
        else if (WIFI_EVENT_STA_DISCONNECTED == event_id)
        {
            gb_sta_connected = false;

            /* Enable config AP immediately — no retry counter threshold. */
            wifi_mngr_config_ap_enable();

            /* Schedule reconnect attempt in 10 s. */
            esp_timer_stop(gp_reconnect_timer);
            esp_timer_start_once(gp_reconnect_timer, WIFI_MNGR_RECONNECT_PERIOD_US);

            /* Notify application. */
            esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_STA_DISCONNECTED, NULL, 0,
                pdMS_TO_TICKS(10));

            ESP_LOGI(gp_tag, "STA disconnected — config AP enabled, reconnect in 10 s");
        }
    }
    else if (IP_EVENT == event_base)
    {
        if (IP_EVENT_STA_GOT_IP == event_id)
        {
            gb_sta_connected = true;

            /* Make STA the default netif so that the lwIP routing layer sends
             * outbound traffic (including NATted AP-client traffic) through
             * the home network.  Must be set before NAPT can route correctly. */
            esp_netif_set_default_netif(gp_netif_sta);

            /* Stop pending reconnect timer. */
            esp_timer_stop(gp_reconnect_timer);

            /* Disable config AP — STA now connected. */
            wifi_mngr_config_ap_disable();

            /* If the reward AP is already active (edge case: STA reconnected
             * while AP was up), re-apply DNS and re-assert the default netif
             * so NAPT resumes routing correctly. */
            if (gb_reward_ap_active)
            {
                wifi_mngr_ap_dns_forward();
                esp_netif_set_default_netif(gp_netif_sta);
            }

            /* Notify application. */
            esp_event_post(ESPORT_EVENT_BASE, ESPORT_EVENT_STA_CONNECTED, NULL, 0,
                pdMS_TO_TICKS(10));

            ESP_LOGI(gp_tag, "STA connected and got IP");
        }
    }
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
