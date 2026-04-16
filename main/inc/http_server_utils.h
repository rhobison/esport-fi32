/**
 * \file
 * \brief HTTP server shared utility helpers - constants and helper function declarations.
 *
 * Internal header shared by all http_server_*.c translation units.
 * Do NOT include this from public application code.
 *
 * \date 2026-03-15
 */

#ifndef HTTP_SERVER_UTILS_H
#define HTTP_SERVER_UTILS_H

//==================================================================================================
// Includes
//==================================================================================================

#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "session_log.h"

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Macros / Constants
//==================================================================================================

/** Maximum number of bytes read from a POST /config request body. */
#define HTTP_SRV_POST_BODY_MAX_LEN (2048U)

/** Heap buffer size for JSON and CSV response bodies. */
#define HTTP_SRV_JSON_BUF_LEN (8192U)

/** Number of calendar days in the daily-aggregates window. */
#define HTTP_SRV_DAILY_WINDOW_DAYS (31U)

/**
 * Buffer size for HTML attribute value encoding used in the config GET handler.
 * Longest string field is 64 chars; worst-case HTML encoding (every char
 * becomes &quot; = 6 bytes) gives 384 bytes.  400 provides a safe margin.
 * Only ONE such buffer is kept on the stack at a time (re-used per field).
 */
#define HTTP_SRV_ATTR_ENC_LEN (400U)

/** Per-entry intermediate buffer for JSON/CSV row formatting. */
#define HTTP_SRV_ENTRY_BUF_LEN (256U)

/** Seconds per day, used for daily-window date arithmetic. */
#define HTTP_SRV_SECS_PER_DAY (86400L)

/** Maximum encoded byte length of a single URL-encoded form field value. */
#define HTTP_SRV_FORM_VALUE_ENC_MAX_LEN (512U)

/** Heap buffer size for the HTML status dashboard response. */
#define HTTP_SRV_HTML_BUF_LEN (16384U)

/** Maximum session entries fetched for the history table. */
#define HTTP_SRV_HIST_MAX (20U)

/** Maximum session entries fetched for graph aggregation. */
#define HTTP_SRV_GRAPH_MAX (SESSION_LOG_MAX_ENTRIES)

//==================================================================================================
// Public Function Declarations
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
void http_srv_url_decode(const char * p_src, char * p_dst, size_t dst_len);

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
    size_t out_len);

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
void http_srv_html_attr_encode(const char * p_src, char * p_dst, size_t dst_len);

/**
 * \brief Decode a standard RFC 4648 Base64 string into \p p_out.
 *
 * The output is NOT null-terminated; the caller must null-terminate using the
 * returned length.
 *
 * \param[in]  p_in    NUL-terminated Base64-encoded input string.
 * \param[out] p_out   Output buffer.
 * \param[in]  out_len Size of \p p_out in bytes (must be >= decoded length + 1).
 *
 * \return Number of decoded bytes, or -1 on invalid input or buffer overflow.
 */
int http_srv_base64_decode(const char * p_in, char * p_out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_UTILS_H

/*** end of file ***/
