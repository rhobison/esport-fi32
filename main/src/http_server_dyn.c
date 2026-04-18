/**
 * \file
 * \brief HTTP server dynamic activities page and file server.
 *
 * Implements GET /dyn (user-facing launch page) and GET /dyn_activities/\*
 * (wildcard handler that serves embedded HTML mini-game files).
 *
 * Device identification on GET /dyn is performed by resolving the HTTP
 * connection's source IP address against the AP station list (via
 * `esp_wifi_ap_get_sta_list_with_ip()`), which is more reliable than an
 * ARP table lookup and works correctly even when the ARP entry has not yet
 * been populated.
 *
 * \date 2026-04-18
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_dyn.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"

#include "device_registry.h"
#include "dyn_act_registry.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Prefix length of "/dyn_activities/" (16 chars). */
#define HTTP_SRV_DYN_FILE_PREFIX_LEN (16U)

/** Heap buffer size for the /dyn page (bytes). */
#define HTTP_SRV_DYN_PAGE_BUF_LEN (2048U)

/** Heap buffer size for the "device not registered" error page (bytes). */
#define HTTP_SRV_DYN_ERR_BUF_LEN (512U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_dyn";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t http_srv_dyn_detect_device(httpd_req_t * p_req, uint8_t * p_dev_idx_out);

//==================================================================================================
// Internal Functions
//==================================================================================================

/**
 * \brief Identify the requesting device by matching peer IP against the AP
 *        station list, then looking up the MAC in the device registry.
 *
 * Uses \c esp_wifi_ap_get_sta_list_with_ip() to map IP → MAC for all
 * currently associated stations.  This is more reliable than an ARP table
 * lookup because the DHCP lease table is always current and does not depend
 * on ARP timing.
 *
 * \note ESP-IDF creates AF_INET6 dual-stack sockets by default
 *       (\c CONFIG_LWIP_IPV6=y).  IPv4 clients therefore present as
 *       IPv4-mapped IPv6 addresses (\c ::ffff:a.b.c.d).  A union large
 *       enough for \c sockaddr_in6 is used and the IPv4 part is extracted.
 *
 * \param[in]  p_req         HTTP request handle.
 * \param[out] p_dev_idx_out Receives the device registry index on success.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NOT_FOUND when the peer IP is not in the AP station list
 *         or the MAC is not registered.
 */
static esp_err_t http_srv_dyn_detect_device(httpd_req_t * p_req, uint8_t * p_dev_idx_out)
{
    /* 1. Get the underlying socket and peer address.
     *
     * The ESP-IDF HTTP server uses AF_INET6 dual-stack sockets when IPv6 is
     * enabled (default).  IPv4 clients appear as IPv4-mapped IPv6 addresses
     * (::ffff:a.b.c.d).  A union large enough for sockaddr_in6 is used so
     * getpeername() never truncates the result. */
    int sock = httpd_req_to_sockfd(p_req);

    union
    {
        struct sockaddr     sa;
        struct sockaddr_in  sin;
        struct sockaddr_in6 sin6;
    } peer_u;

    socklen_t addrlen = (socklen_t)sizeof(peer_u);
    (void)memset(&peer_u, 0, sizeof(peer_u));

    if (0 != getpeername(sock, &peer_u.sa, &addrlen))
    {
        ESP_LOGW(gp_tag, "getpeername() failed on fd %d", sock);
        return ESP_ERR_NOT_FOUND;
    }

    /* 2. Extract IPv4 address (network byte order).
     *
     * AF_INET  : address is directly in sin.sin_addr.s_addr.
     * AF_INET6 : check for IPv4-mapped prefix (first 10 bytes 0x00,
     *            bytes 10-11 0xFF); bytes 12-15 are the IPv4 address. */
    uint32_t peer_ipv4 = 0U;

    if (AF_INET == peer_u.sa.sa_family)
    {
        peer_ipv4 = peer_u.sin.sin_addr.s_addr;
    }
    else if (AF_INET6 == peer_u.sa.sa_family)
    {
        static const uint8_t sc_v4mapped_pfx[12U] =
            { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0xFFU, 0xFFU };
        const uint8_t * b = peer_u.sin6.sin6_addr.s6_addr;

        if (0 == memcmp(b, sc_v4mapped_pfx, 12U))
        {
            (void)memcpy(&peer_ipv4, b + 12U, 4U);
        }
        else
        {
            ESP_LOGW(gp_tag, "Peer is native IPv6 (not IPv4-mapped); unsupported");
            return ESP_ERR_NOT_FOUND;
        }
    }
    else
    {
        ESP_LOGW(gp_tag, "Unexpected peer address family %d on fd %d",
            (int)peer_u.sa.sa_family, sock);
        return ESP_ERR_NOT_FOUND;
    }

    /* 3. Get the AP station list with IP addresses. */
    wifi_sta_list_t        sta_list;
    wifi_sta_mac_ip_list_t sta_ip_list;

    if (ESP_OK != esp_wifi_ap_get_sta_list(&sta_list))
    {
        ESP_LOGW(gp_tag, "esp_wifi_ap_get_sta_list failed");
        return ESP_ERR_NOT_FOUND;
    }
    if (ESP_OK != esp_wifi_ap_get_sta_list_with_ip(&sta_list, &sta_ip_list))
    {
        ESP_LOGW(gp_tag, "esp_wifi_ap_get_sta_list_with_ip failed");
        return ESP_ERR_NOT_FOUND;
    }

    /* 4. Find the station whose IP matches the peer address. */
    uint8_t mac[6U];
    bool    b_found = false;

    for (int i = 0; i < sta_ip_list.num; i++)
    {
        if (sta_ip_list.sta[i].ip.addr == peer_ipv4)
        {
            (void)memcpy(mac, sta_ip_list.sta[i].mac, 6U);
            b_found = true;
            break;
        }
    }

    if (!b_found)
    {
        const uint8_t * b = (const uint8_t *)&peer_ipv4;
        ESP_LOGW(gp_tag, "Peer IP %d.%d.%d.%d not in AP station list (%d stations)",
            (int)b[0], (int)b[1], (int)b[2], (int)b[3], sta_ip_list.num);
        return ESP_ERR_NOT_FOUND;
    }

    /* 5. Search device registry for this MAC. */
    static const uint8_t sc_zero_mac[6U] = { 0U, 0U, 0U, 0U, 0U, 0U };
    for (uint8_t i = 0U; i < (uint8_t)DEVICE_REG_MAX_ENTRIES; i++)
    {
        device_reg_entry_t entry;
        if (ESP_OK != device_reg_entry_get(i, &entry))
        {
            continue;
        }
        if ((0 != memcmp(entry.mac, sc_zero_mac, 6U)) && (0 == memcmp(entry.mac, mac, 6U)))
        {
            *p_dev_idx_out = i;
            return ESP_OK;
        }
    }

    ESP_LOGW(gp_tag, "MAC %02X:%02X:%02X:%02X:%02X:%02X not in device registry",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_ERR_NOT_FOUND;
}

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Handler for \c GET /dyn.
 *
 * User-facing dynamic activities launch page.  No admin auth required.
 * Auto-detects the requesting device; shows an error page if unregistered.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_dyn_page_get_handler(httpd_req_t * p_req)
{
    uint8_t   dev_idx = 0U;
    esp_err_t det_ret = http_srv_dyn_detect_device(p_req, &dev_idx);

    if (ESP_OK != det_ret)
    {
        /* Device not recognised — serve a friendly error page. */
        char * p_err = (char *)malloc(HTTP_SRV_DYN_ERR_BUF_LEN);
        if (NULL == p_err)
        {
            httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
            return ESP_FAIL;
        }
        size_t pos = 0U;
#define EAPPEND(fmt, ...) \
    pos += (size_t)snprintf(p_err + pos, HTTP_SRV_DYN_ERR_BUF_LEN - pos, fmt, ##__VA_ARGS__)

        EAPPEND("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>ESPort - Not Registered</title>"
                "<style>body{font-family:sans-serif;text-align:center;padding:2em;}</style>"
                "</head><body>");
        EAPPEND("<h1>Device not registered</h1>");
        EAPPEND("<p>Your device is not registered on this network.</p>");
        EAPPEND("<p>Please ask the administrator to add it.</p>");
        EAPPEND("<p><a href='/'>&#x2190; Home</a></p>");
        EAPPEND("</body></html>");

#undef EAPPEND

        httpd_resp_set_type(p_req, "text/html");
        httpd_resp_send(p_req, p_err, (ssize_t)pos);
        free(p_err);
        return ESP_OK;
    }

    /* Device found — build the launch page. */
    device_reg_entry_t dev_entry;
    if (ESP_OK != device_reg_entry_get(dev_idx, &dev_entry))
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Registry error");
        return ESP_FAIL;
    }

    char * p_buf = (char *)malloc(HTTP_SRV_DYN_PAGE_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t pos = 0U;
#define APPEND(fmt, ...) \
    pos += (size_t)snprintf(p_buf + pos, HTTP_SRV_DYN_PAGE_BUF_LEN - pos, fmt, ##__VA_ARGS__)

    APPEND("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>My Activities</title>"
           "<style>body{font-family:sans-serif;margin:1em 2em;max-width:600px}"
           "h1{color:#333}button{font-size:1em;padding:0.5em 1.2em;cursor:pointer;"
           "border-radius:5px;border:none;background:#4a90d9;color:#fff;display:block;"
           "width:100%%;margin:0.5em 0}"
           "button:disabled{background:#aaa;cursor:default}"
           "a.home{display:inline-block;margin-top:1em;color:#4a90d9}"
           "</style></head><body>");

    APPEND("<h1>My Activities</h1>");
    APPEND("<p>Hello, <b>%s</b></p>", dev_entry.nickname);
    APPEND("<div id='activity-list'><p><em>Loading activities&hellip;</em></p></div>");
    APPEND("<a class='home' href='/'>&#x2190; Home</a>");

    /* Embed device index as a JS constant at render time. */
    APPEND("<script>\n");
    APPEND("var DEVICE_IDX = %u;\n", (unsigned int)dev_idx);
    APPEND("function formatHMS(s){"
           "var h=Math.floor(s/3600);"
           "var m=Math.floor((s%%3600)/60);"
           "var sec=s%%60;"
           "return h+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');}\n");

    APPEND("document.addEventListener('DOMContentLoaded',function(){"
           "fetch('/api/dyn?device_idx='+DEVICE_IDX)"
           ".then(function(r){return r.json();})"
           ".then(function(d){"
           "var div=document.getElementById('activity-list');"
           "if(!d.activities||d.activities.length===0){"
           "div.innerHTML='<p><em>No activities assigned.</em></p>';return;}"
           "var html='';"
           "d.activities.forEach(function(a){"
           "var dis=a.available?'':' disabled';"
           "var lbl=a.available"
           "?('Play: '+a.name+' ('+formatHMS(a.credit_s)+')')"
           ":('(done today) '+a.name);"
           "html+='<button'+dis"
           "+\" onclick=\\\"location.href='/dyn_activities/\"+a.name"
           "+\"?pin=\"+d.pin"
           "+\"&device_idx=\"+DEVICE_IDX"
           "+\"&act_id=\"+a.act_id"
           "+\"&credits_s=\"+a.credit_s+\"'\\\">\"+lbl+'</button>';"
           "});"
           "div.innerHTML=html;"
           "})"
           ".catch(function(){"
           "document.getElementById('activity-list').innerHTML="
           "'<p><em>Failed to load activities.</em></p>';"
           "});"
           "});\n");
    APPEND("</script>");
    APPEND("</body></html>");

#undef APPEND

    httpd_resp_set_type(p_req, "text/html");
    httpd_resp_send(p_req, p_buf, (ssize_t)pos);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handler for \c GET /dyn_activities/\*.
 *
 * Serves an embedded dynamic activity HTML file by logical name.  Strips a
 * trailing \c .html extension before looking up the name in
 * \c g_dyn_act_registry[].  Returns HTTP 404 when not found.
 *
 * \param[in] p_req  Incoming HTTP request.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t http_srv_dyn_file_get_handler(httpd_req_t * p_req)
{
    /* Extract the filename after "/dyn_activities/" (16 chars). */
    const char * p_uri = p_req->uri;
    if (strlen(p_uri) <= HTTP_SRV_DYN_FILE_PREFIX_LEN)
    {
        httpd_resp_send_err(p_req, HTTPD_404_NOT_FOUND, "Dynamic activity not found");
        return ESP_OK;
    }

    /* Copy the name portion (after the prefix) into a local buffer. */
    char         name_buf[DYN_ACT_NAME_MAX_LEN + 8U]; /* +8 for ".html\0" headroom */
    const char * p_name_start = p_uri + HTTP_SRV_DYN_FILE_PREFIX_LEN;
    size_t       name_len     = strlen(p_name_start);
    if (name_len >= sizeof(name_buf))
    {
        httpd_resp_send_err(p_req, HTTPD_404_NOT_FOUND, "Dynamic activity not found");
        return ESP_OK;
    }
    (void)strncpy(name_buf, p_name_start, sizeof(name_buf) - 1U);
    name_buf[sizeof(name_buf) - 1U] = '\0';

    /* Strip trailing ".html" extension if present. */
    char * p_dot = strrchr(name_buf, '.');
    if (NULL != p_dot)
    {
        *p_dot = '\0';
    }

    /* Search the registry for a matching logical name. */
    for (uint8_t i = 0U; i < g_dyn_act_count; i++)
    {
        if (0 == strcmp(g_dyn_act_registry[i].p_name, name_buf))
        {
            httpd_resp_set_type(p_req, "text/html");
            httpd_resp_send(p_req, (const char *)g_dyn_act_registry[i].p_data,
                (ssize_t)(g_dyn_act_registry[i].p_end - g_dyn_act_registry[i].p_data));
            return ESP_OK;
        }
    }

    ESP_LOGW(gp_tag, "dyn file not found: '%s'", name_buf);
    httpd_resp_send_err(p_req, HTTPD_404_NOT_FOUND, "Dynamic activity not found");
    return ESP_OK;
}

/*** end of file ***/
