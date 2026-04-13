/**
 * \file
 * \brief HTTP OTA handler implementation.
 *
 * Provides four URI handlers that implement a Basic-Auth-protected firmware
 * upload page (GET /ota), firmware upload endpoint (POST /ota), and OTA
 * password management (GET /ota/pwd, POST /ota/pwd).
 *
 * Basic Auth credentials: username = "admin" (hardcoded), password = stored
 * in NVS namespace \c esport_ota via #ota_mngr_credentials_check().
 *
 * \date 2026-04-06
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_ota.h"
#include "http_server_utils.h"
#include "ota_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Receive buffer size for POST /ota body chunks (bytes). */
#define OTA_MNGR_RECV_BUF_SIZE (1024U)

/** Maximum length including NUL for the Authorization header value. */
#define HTTP_SRV_OTA_AUTH_HDR_MAX (256U)

/** Minimum length of "Basic " prefix in the Authorization header. */
#define HTTP_SRV_OTA_BASIC_PREFIX_LEN (6U)

/** Maximum length of the decoded "user:password" string. */
#define HTTP_SRV_OTA_DECODED_MAX (192U)

/** Maximum length of a single URL-decoded form field. */
#define HTTP_SRV_OTA_FIELD_MAX (OTA_MNGR_PASSWORD_MAX_LEN + 1U)

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_server_ota";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static bool http_srv_ota_auth_check(httpd_req_t * p_req);
static int  http_srv_ota_base64_decode(const char * p_in, char * p_out, size_t out_len);

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Handle GET /ota -- serve the firmware upload page.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_ota_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_ota_auth_check(p_req))
    {
        return ESP_OK;
    }

    char version[32];
    ota_mngr_running_version_get(version, sizeof(version));

    (void)httpd_resp_set_type(p_req, "text/html");

    (void)httpd_resp_sendstr_chunk(p_req,
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESPort-fi32 -- Firmware Update</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:600px;margin:2em auto;padding:0 1em;}"
        "h1{font-size:1.3em;}"
        "h2{font-size:1.1em;margin-top:1.5em;}"
        ".card{border:1px solid #ccc;border-radius:6px;padding:1em;margin:1em 0;}"
        "input[type=file]{display:block;margin:0.5em 0;}"
        "button,.btn{background:#2a6db5;color:#fff;border:none;padding:0.5em 1.2em;"
        "border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;}"
        "button:hover,.btn:hover{background:#1e5490;}"
        "progress{width:100%;margin:0.5em 0;}"
        "#upload-status{margin-top:0.5em;font-weight:bold;}"
        "nav a{margin-right:1em;}"
        "</style>"
        "</head><body>"
        "<h1>ESPort-fi32 -- Firmware Update</h1>");

    /* Running firmware section */
    static char buf[128];
    (void)snprintf(buf, sizeof(buf),
        "<div class=\"card\"><h2>Running Firmware</h2>"
        "<p>Version: <strong>%s</strong></p></div>",
        version);
    (void)httpd_resp_sendstr_chunk(p_req, buf);

    /* Upload section */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<div class=\"card\"><h2>Upload New Firmware</h2>"
        "<progress id=\"upload-progress\" value=\"0\" max=\"100\" "
        "style=\"display:none;\"></progress>"
        "<input type=\"file\" id=\"fw-file\" accept=\".bin\">"
        "<button id=\"flash-btn\">Flash Firmware</button>"
        "<div id=\"upload-status\"></div>"
        "</div>");

    /* OTA password section */
    (void)httpd_resp_sendstr_chunk(p_req, "<div class=\"card\"><h2>OTA Password</h2>"
                                          "<p><a href=\"/ota/pwd\">Change OTA password</a></p>"
                                          "</div>");

    /* Navigation */
    (void)httpd_resp_sendstr_chunk(p_req, "<nav><a class=\"btn\" href=\"/\">Dashboard</a></nav>");

    /* Inline upload script */
    (void)httpd_resp_sendstr_chunk(p_req,
        "<script>"
        "document.getElementById('flash-btn').addEventListener('click',function(){"
        "var f=document.getElementById('fw-file').files[0];"
        "if(!f){document.getElementById('upload-status').textContent="
        "'Please select a .bin file.';return;}"
        "var xhr=new XMLHttpRequest();"
        "xhr.open('POST','/ota',true);"
        "xhr.setRequestHeader('Content-Type','application/octet-stream');"
        "xhr.upload.onprogress=function(e){"
        "if(e.lengthComputable){"
        "var p=document.getElementById('upload-progress');"
        "p.style.display='block';"
        "p.value=e.loaded;p.max=e.total;}"
        "};"
        "xhr.onload=function(){"
        "document.getElementById('upload-status').textContent="
        "(xhr.status===200)?'Update successful. Rebooting...':"
        "'Error: '+xhr.responseText;"
        "};"
        "xhr.onerror=function(){"
        "document.getElementById('upload-status').textContent="
        "'Connection lost (device is rebooting).';"
        "};"
        "xhr.send(f);"
        "});"
        "</script>"
        "</body></html>");

    (void)httpd_resp_sendstr_chunk(p_req, NULL);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handle POST /ota -- receive and flash a firmware binary.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK on success (device reboots), \c ESP_FAIL on error.
 */
esp_err_t http_srv_ota_post_handler(httpd_req_t * p_req)
{
    if (!http_srv_ota_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Determine image size from Content-Length header if available. */
    char   cl_buf[32];
    size_t image_size = 0U;
    if (0U < httpd_req_get_hdr_value_len(p_req, "Content-Length"))
    {
        if (ESP_OK == httpd_req_get_hdr_value_str(p_req, "Content-Length", cl_buf, sizeof(cl_buf)))
        {
            image_size = (size_t)strtoul(cl_buf, NULL, 10);
        }
    }

    esp_err_t ret = ota_mngr_begin(image_size);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "ota_mngr_begin failed: %s", esp_err_to_name(ret));
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, esp_err_to_name(ret));
        return ESP_FAIL;
    }

    static char buf[OTA_MNGR_RECV_BUF_SIZE];
    int         received;

    for (;;)
    {
        received = httpd_req_recv(p_req, buf, OTA_MNGR_RECV_BUF_SIZE);

        if (0 == received)
        {
            /* All data received. */
            break;
        }

        if (received < 0)
        {
            ESP_LOGE(gp_tag, "httpd_req_recv error: %d", received);
            (void)ota_mngr_abort();
            (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
            (void)httpd_resp_sendstr(p_req, "Receive error");
            return ESP_FAIL;
        }

        ret = ota_mngr_write(buf, (size_t)received);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "ota_mngr_write failed: %s", esp_err_to_name(ret));
            (void)ota_mngr_abort();
            (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
            (void)httpd_resp_sendstr(p_req, esp_err_to_name(ret));
            return ESP_FAIL;
        }
    }

    ret = ota_mngr_end();
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "ota_mngr_end failed: %s", esp_err_to_name(ret));
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, esp_err_to_name(ret));
        return ESP_FAIL;
    }

    (void)httpd_resp_sendstr(p_req, "OK");

    /* Activate reboots the device; on failure it returns. */
    ota_mngr_activate();

    /* Only reached if activation failed. */
    (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
    (void)httpd_resp_sendstr(p_req, "Activation failed");
    return ESP_FAIL;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handle GET /ota/pwd -- serve the OTA password change form.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_ota_pwd_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_ota_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Check for ?saved=1 query string. */
    char qs[16]  = { 0 };
    bool b_saved = false;
    if (0U < httpd_req_get_url_query_len(p_req))
    {
        (void)httpd_req_get_url_query_str(p_req, qs, sizeof(qs));
        char val[4] = { 0 };
        if (ESP_OK == httpd_query_key_value(qs, "saved", val, sizeof(val)))
        {
            b_saved = (0 == strcmp(val, "1"));
        }
    }

    char max_len_str[8];
    (void)snprintf(max_len_str, sizeof(max_len_str), "%u", (unsigned)OTA_MNGR_PASSWORD_MAX_LEN);

    (void)httpd_resp_set_type(p_req, "text/html");

    (void)httpd_resp_sendstr_chunk(p_req,
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESPort-fi32 -- OTA Password</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:500px;margin:2em auto;padding:0 1em;}"
        "h1{font-size:1.3em;}h2{font-size:1.1em;margin-top:1.5em;}"
        ".card{border:1px solid #ccc;border-radius:6px;padding:1em;margin:1em 0;}"
        "label{display:block;margin:0.4em 0 0.1em;}"
        "input[type=password]{width:100%;box-sizing:border-box;padding:0.4em;}"
        "button,.btn{background:#2a6db5;color:#fff;border:none;padding:0.5em 1.2em;"
        "border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block;"
        "margin-top:0.8em;}"
        "button:hover,.btn:hover{background:#1e5490;}"
        ".ok{color:green;font-weight:bold;}"
        "nav a{margin-right:1em;}"
        "</style></head><body>"
        "<h1>OTA Password</h1>");

    if (b_saved)
    {
        (void)httpd_resp_sendstr_chunk(p_req, "<p class=\"ok\">Password saved successfully.</p>");
    }

    static char form_buf[512];
    (void)snprintf(form_buf, sizeof(form_buf),
        "<div class=\"card\"><h2>Change OTA Password</h2>"
        "<form method=\"POST\" action=\"/ota/pwd\">"
        "<label>Current password</label>"
        "<input type=\"password\" name=\"current_pwd\" required>"
        "<label>New password (max %s chars)</label>"
        "<input type=\"password\" name=\"new_pwd\" maxlength=\"%s\" required>"
        "<label>Confirm new password</label>"
        "<input type=\"password\" name=\"confirm_pwd\" maxlength=\"%s\" required>"
        "<button type=\"submit\">Save</button>"
        "</form></div>",
        max_len_str, max_len_str, max_len_str);
    (void)httpd_resp_sendstr_chunk(p_req, form_buf);

    (void)httpd_resp_sendstr_chunk(p_req,
        "<nav><a class=\"btn\" href=\"/ota\">Back to Firmware Update</a></nav>"
        "</body></html>");

    (void)httpd_resp_sendstr_chunk(p_req, NULL);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Handle POST /ota/pwd -- save a new OTA password.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c ESP_OK always.
 */
esp_err_t http_srv_ota_pwd_post_handler(httpd_req_t * p_req)
{
    if (!http_srv_ota_auth_check(p_req))
    {
        return ESP_OK;
    }

    /* Read POST body. */
    char * p_body   = NULL;
    int    body_len = (int)p_req->content_len;

    if ((body_len <= 0) || (body_len >= (int)HTTP_SRV_HTML_BUF_LEN))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Invalid body length");
        return ESP_OK;
    }

    p_body = malloc((size_t)body_len + 1U);
    if (NULL == p_body)
    {
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, "Out of memory");
        return ESP_OK;
    }

    int received = httpd_req_recv(p_req, p_body, (size_t)body_len);
    if (received <= 0)
    {
        free(p_body);
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Body read error");
        return ESP_OK;
    }
    p_body[received] = '\0';

    /* Parse fields. */
    static char current_pwd[HTTP_SRV_OTA_FIELD_MAX];
    static char new_pwd[HTTP_SRV_OTA_FIELD_MAX];
    static char confirm_pwd[HTTP_SRV_OTA_FIELD_MAX];
    static char enc_val[HTTP_SRV_FORM_VALUE_ENC_MAX_LEN + 1U];

    current_pwd[0] = '\0';
    new_pwd[0]     = '\0';
    confirm_pwd[0] = '\0';

    if (ESP_OK == httpd_query_key_value(p_body, "current_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, current_pwd, sizeof(current_pwd));
    }
    if (ESP_OK == httpd_query_key_value(p_body, "new_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, new_pwd, sizeof(new_pwd));
    }
    if (ESP_OK == httpd_query_key_value(p_body, "confirm_pwd", enc_val, sizeof(enc_val)))
    {
        http_srv_url_decode(enc_val, confirm_pwd, sizeof(confirm_pwd));
    }

    free(p_body);

    /* Validate current password. */
    if (!ota_mngr_credentials_check(current_pwd))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Current password incorrect");
        return ESP_OK;
    }

    /* Validate new password length. */
    size_t new_len = strlen(new_pwd);
    if ((0U == new_len) || (new_len > OTA_MNGR_PASSWORD_MAX_LEN))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Password too short or too long");
        return ESP_OK;
    }

    /* Validate passwords match. */
    if (0 != strcmp(new_pwd, confirm_pwd))
    {
        (void)httpd_resp_set_status(p_req, "400 Bad Request");
        (void)httpd_resp_sendstr(p_req, "Passwords do not match");
        return ESP_OK;
    }

    esp_err_t ret = ota_mngr_password_set(new_pwd);
    if (ESP_OK != ret)
    {
        (void)httpd_resp_set_status(p_req, "500 Internal Server Error");
        (void)httpd_resp_sendstr(p_req, esp_err_to_name(ret));
        return ESP_OK;
    }

    /* Redirect to GET /ota/pwd?saved=1. */
    (void)httpd_resp_set_status(p_req, "303 See Other");
    (void)httpd_resp_set_hdr(p_req, "Location", "/ota/pwd?saved=1");
    (void)httpd_resp_sendstr(p_req, "");
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Verify the HTTP Basic Auth header on \p p_req.
 *
 * Sends a 401 response (with \c WWW-Authenticate header) and returns \c false
 * if credentials are absent, malformed, or incorrect.
 *
 * \param[in] p_req  HTTP request handle.
 *
 * \return \c true if authentication passed, \c false otherwise.
 */
static bool http_srv_ota_auth_check(httpd_req_t * p_req)
{
    size_t hdr_len = httpd_req_get_hdr_value_len(p_req, "Authorization");

    if (0U == hdr_len)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    static char hdr_buf[HTTP_SRV_OTA_AUTH_HDR_MAX];
    if (ESP_OK != httpd_req_get_hdr_value_str(p_req, "Authorization", hdr_buf, sizeof(hdr_buf)))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    /* Verify "Basic " prefix. */
    if (0 != strncmp(hdr_buf, "Basic ", HTTP_SRV_OTA_BASIC_PREFIX_LEN))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    /* Base64-decode the credential portion. */
    static char decoded[HTTP_SRV_OTA_DECODED_MAX + 1U];
    int dec_len = http_srv_ota_base64_decode(hdr_buf + HTTP_SRV_OTA_BASIC_PREFIX_LEN, decoded,
        sizeof(decoded));

    if (dec_len < 0)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }
    decoded[dec_len] = '\0';

    /* Split on first ':'. */
    char * p_colon = strchr(decoded, ':');
    if (NULL == p_colon)
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }
    *p_colon                = '\0';
    const char * p_user     = decoded;
    const char * p_password = p_colon + 1;

    /* Check username and password. */
    if ((0 != strcmp(p_user, OTA_MNGR_HTTP_USERNAME)) || !ota_mngr_credentials_check(p_password))
    {
        (void)httpd_resp_set_status(p_req, "401 Unauthorized");
        (void)httpd_resp_set_hdr(p_req, "WWW-Authenticate", "Basic realm=\"esport-fi32 OTA\"");
        (void)httpd_resp_sendstr(p_req, "Unauthorized");
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Decode a standard RFC 4648 Base64 string into \p p_out.
 *
 * Uses a look-up table for the Base64 alphabet.  The output is NOT
 * null-terminated by this function; the caller must null-terminate using the
 * returned length.
 *
 * \param[in]  p_in    NUL-terminated Base64-encoded input string.
 * \param[out] p_out   Output buffer.
 * \param[in]  out_len Size of \p p_out in bytes (must be >= decoded length + 1).
 *
 * \return Number of decoded bytes, or -1 on invalid input or buffer overflow.
 */
static int http_srv_ota_base64_decode(const char * p_in, char * p_out, size_t out_len)
{
    /* Look-up table: maps ASCII value to 6-bit group; 0xFF = invalid. */
    static const uint8_t sc_lut[256] = {
        /* 0x00-0x2B */ 0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        /* '+' = 62 */ 62U,
        /* 0x2C-0x2E */ 0xFF,
        0xFF,
        0xFF,
        /* '/' = 63 */ 63U,
        /* '0'-'9' */ 52U,
        53U,
        54U,
        55U,
        56U,
        57U,
        58U,
        59U,
        60U,
        61U,
        /* 0x3A-0x40 */ 0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        /* 'A'-'Z' */ 0U,
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
        7U,
        8U,
        9U,
        10U,
        11U,
        12U,
        13U,
        14U,
        15U,
        16U,
        17U,
        18U,
        19U,
        20U,
        21U,
        22U,
        23U,
        24U,
        25U,
        /* 0x5B-0x60 */ 0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        /* 'a'-'z' */ 26U,
        27U,
        28U,
        29U,
        30U,
        31U,
        32U,
        33U,
        34U,
        35U,
        36U,
        37U,
        38U,
        39U,
        40U,
        41U,
        42U,
        43U,
        44U,
        45U,
        46U,
        47U,
        48U,
        49U,
        50U,
        51U,
        /* 0x7B-0xFF */ 0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };

    size_t out_idx = 0U;
    size_t in_idx  = 0U;
    size_t in_len  = strlen(p_in);

    while (in_idx < in_len)
    {
        /* Skip whitespace / newlines that may trail the encoded string. */
        if (('\r' == p_in[in_idx]) || ('\n' == p_in[in_idx]) || (' ' == p_in[in_idx]))
        {
            in_idx++;
            continue;
        }

        /* Collect up to 4 Base64 characters. */
        uint8_t c[4];
        int     valid = 0;

        for (int i = 0; i < 4; i++)
        {
            if ((in_idx + (size_t)i) >= in_len)
            {
                c[i] = 0U;
            }
            else if ('=' == p_in[in_idx + (size_t)i])
            {
                c[i] = 0U; /* padding */
            }
            else
            {
                uint8_t v = sc_lut[(uint8_t)p_in[in_idx + (size_t)i]];
                if (0xFFU == v)
                {
                    return -1; /* invalid character */
                }
                c[i]  = v;
                valid = i + 1;
            }
        }

        /* Emit decoded bytes. */
        if (valid >= 2)
        {
            if (out_idx >= (out_len - 1U))
            {
                return -1; /* overflow */
            }
            p_out[out_idx++] = (char)(((c[0] << 2) & 0xFC) | ((c[1] >> 4) & 0x03));
        }
        if (valid >= 3)
        {
            if (out_idx >= (out_len - 1U))
            {
                return -1;
            }
            p_out[out_idx++] = (char)(((c[1] << 4) & 0xF0) | ((c[2] >> 2) & 0x0F));
        }
        if (valid >= 4)
        {
            if (out_idx >= (out_len - 1U))
            {
                return -1;
            }
            p_out[out_idx++] = (char)(((c[2] << 6) & 0xC0) | (c[3] & 0x3F));
        }

        in_idx += 4U;
    }

    return (int)out_idx;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
