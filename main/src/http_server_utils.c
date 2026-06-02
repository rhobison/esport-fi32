/**
 * \file
 * \brief HTTP server shared utility helpers.
 *
 * Implements URL decoding, form-field extraction, and HTML attribute
 * encoding helpers shared across all http_server_*.c translation units.
 *
 * \date 2026-03-15
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_utils.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_utils";

/** Shared response scratch buffer reused across requests instead of large
 *  per-request heap allocations.  Reusing a single static buffer avoids the
 *  long-run internal-heap fragmentation that repeated multi-kB malloc/free
 *  cycles (every dashboard/status poll) would otherwise cause, which can
 *  eventually starve the Wi-Fi driver of dynamic buffers. */
static char g_scratch[HTTP_SRV_SCRATCH_LEN];

/** Mutex serialising access to #g_scratch. */
static SemaphoreHandle_t g_scratch_mux = NULL;

/** Static storage backing #g_scratch_mux. */
static StaticSemaphore_t g_scratch_mux_buf;

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t http_srv_utils_init(void)
{
    if (NULL == g_scratch_mux)
    {
        g_scratch_mux = xSemaphoreCreateMutexStatic(&g_scratch_mux_buf);
        if (NULL == g_scratch_mux)
        {
            ESP_LOGE(gp_tag, "scratch mutex create failed");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

char * http_srv_scratch_take(uint32_t timeout_ms)
{
    if (NULL == g_scratch_mux)
    {
        return NULL;
    }
    if (pdTRUE != xSemaphoreTake(g_scratch_mux, pdMS_TO_TICKS(timeout_ms)))
    {
        ESP_LOGW(gp_tag, "scratch buffer busy");
        return NULL;
    }
    return g_scratch;
}

//--------------------------------------------------------------------------------------------------

void http_srv_scratch_give(void)
{
    if (NULL != g_scratch_mux)
    {
        (void)xSemaphoreGive(g_scratch_mux);
    }
}

//--------------------------------------------------------------------------------------------------

void http_srv_url_decode(const char * p_src, char * p_dst, size_t dst_len)
{
    if ((NULL == p_src) || (NULL == p_dst) || (0U == dst_len))
    {
        return;
    }

    size_t pos = 0U;

    while (('\0' != *p_src) && (pos < (dst_len - 1U)))
    {
        if ('+' == *p_src)
        {
            p_dst[pos] = ' ';
            pos++;
            p_src++;
        }
        else if (('%' == *p_src) && (0 != isxdigit((unsigned char)p_src[1])) &&
                 (0 != isxdigit((unsigned char)p_src[2])))
        {
            char hex[3] = { p_src[1], p_src[2], '\0' };
            p_dst[pos]  = (char)strtol(hex, NULL, 16);
            pos++;
            p_src += 3;
        }
        else
        {
            p_dst[pos] = *p_src;
            pos++;
            p_src++;
        }
    }

    p_dst[pos] = '\0';
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief Look up a URL-encoded form field by key and decode its value.
 *
 * Searches \p p_body for "key=value" (fields separated by '&'), URL-decodes
 * the value into \p p_out (bounded by \p out_len), and NUL-terminates the
 * result.
 *
 * \param[in]  p_body   NUL-terminated URL-encoded form body string.
 * \param[in]  p_key    NUL-terminated field name to search for.
 * \param[out] p_out    Destination buffer for the decoded value.
 * \param[in]  out_len  Size of \p p_out in bytes including the NUL terminator.
 *
 * \return \c ESP_OK when the key was found and decoded,
 *         \c ESP_ERR_NOT_FOUND when the key is absent.
 */
esp_err_t http_srv_form_field_get(const char * p_body, const char * p_key, char * p_out,
    size_t out_len)
{
    if ((NULL == p_body) || (NULL == p_key) || (NULL == p_out) || (0U == out_len))
    {
        ESP_LOGW(gp_tag, "http_srv_form_field_get: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    size_t       key_len = strlen(p_key);
    const char * p_pos   = p_body;

    while (NULL != p_pos)
    {
        if ((0 == strncmp(p_pos, p_key, key_len)) && ('=' == p_pos[key_len]))
        {
            const char * p_val_start = p_pos + key_len + 1U;
            const char * p_amp       = strchr(p_val_start, '&');
            size_t raw_len = (NULL == p_amp) ? strlen(p_val_start) : (size_t)(p_amp - p_val_start);

            /* Cap the raw length to protect the shared static decode buffer. */
            static char raw[HTTP_SRV_FORM_VALUE_ENC_MAX_LEN];
            if (raw_len > sizeof(raw) - 1U)
            {
                raw_len = sizeof(raw) - 1U;
            }
            memcpy(raw, p_val_start, raw_len);
            raw[raw_len] = '\0';

            http_srv_url_decode(raw, p_out, out_len);
            return ESP_OK;
        }

        p_pos = strchr(p_pos, '&');
        if (NULL != p_pos)
        {
            p_pos++; /* skip the '&' separator */
        }
    }

    p_out[0] = '\0';
    return ESP_ERR_NOT_FOUND;
}

//--------------------------------------------------------------------------------------------------

/**
 * \brief HTML-encode a string value for safe insertion into a double-quoted attribute.
 *
 * Replaces \c & with \c &amp;, \c " with \c &quot;, \c < with \c &lt;,
 * and \c > with \c &gt;.  The output is NUL-terminated and never written
 * past \p dst_len bytes.
 *
 * \param[in]  p_src   NUL-terminated plain text source string.
 * \param[out] p_dst   Destination buffer.
 * \param[in]  dst_len Total size of \p p_dst in bytes including the NUL terminator.
 */
void http_srv_html_attr_encode(const char * p_src, char * p_dst, size_t dst_len)
{
    if ((NULL == p_src) || (NULL == p_dst) || (0U == dst_len))
    {
        return;
    }

    size_t pos = 0U;

    while (('\0' != *p_src) && (pos < (dst_len - 1U)))
    {
        if ('&' == *p_src)
        {
            if ((pos + 5U) < dst_len)
            {
                memcpy(p_dst + pos, "&amp;", 5U);
                pos += 5U;
            }
            else
            {
                break;
            }
        }
        else if ('"' == *p_src)
        {
            if ((pos + 6U) < dst_len)
            {
                memcpy(p_dst + pos, "&quot;", 6U);
                pos += 6U;
            }
            else
            {
                break;
            }
        }
        else if ('<' == *p_src)
        {
            if ((pos + 4U) < dst_len)
            {
                memcpy(p_dst + pos, "&lt;", 4U);
                pos += 4U;
            }
            else
            {
                break;
            }
        }
        else if ('>' == *p_src)
        {
            if ((pos + 4U) < dst_len)
            {
                memcpy(p_dst + pos, "&gt;", 4U);
                pos += 4U;
            }
            else
            {
                break;
            }
        }
        else
        {
            p_dst[pos] = *p_src;
            pos++;
        }
        p_src++;
    }

    p_dst[pos] = '\0';
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
int http_srv_base64_decode(const char * p_in, char * p_out, size_t out_len)
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
