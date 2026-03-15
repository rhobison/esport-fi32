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
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "http_srv_utils";

//==================================================================================================
// Public Functions
//==================================================================================================

/**
 * \brief Decode a URL-encoded string into \p p_dst.
 *
 * Converts '+' to space and '%XX' hex-escape sequences to their byte values.
 * The output is always NUL-terminated and never written past \p dst_len bytes
 * (including the terminator).
 *
 * \param[in]  p_src   NUL-terminated URL-encoded source string.
 * \param[out] p_dst   Destination buffer.
 * \param[in]  dst_len Total size of \p p_dst in bytes including the NUL terminator.
 */
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

/*** end of file ***/
