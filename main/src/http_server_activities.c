/**
 * \file
 * \brief HTTP server activities page handlers.
 *
 * Implements GET /activities/manage, POST /activities/manage, and
 * GET /activities.  All three routes are protected by HTTP Basic Auth
 * (same credentials as /config).
 *
 * \date 2026-04-17
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "http_server_activities.h"
#include "http_server_config.h"
#include "http_server_utils.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "activity_manager.h"
#include "device_registry.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** Heap-allocated HTML response buffer size (bytes). */
#define ACT_HTML_BUF_LEN (16384U)

/** POST body read buffer size (reuse HTTP_SRV_POST_BODY_MAX_LEN from utils). */
#define ACT_POST_BODY_MAX (HTTP_SRV_POST_BODY_MAX_LEN)

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t hms_str_to_s(const char * p_str, uint32_t * p_out);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t http_srv_activities_manage_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    char * p_buf = (char *)malloc(ACT_HTML_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    /* Check ?saved=1 */
    bool b_saved = false;
    char query_buf[32];
    if (ESP_OK == httpd_req_get_url_query_str(p_req, query_buf, sizeof(query_buf)))
    {
        char saved_val[4];
        if (ESP_OK == httpd_query_key_value(query_buf, "saved", saved_val, sizeof(saved_val)))
        {
            b_saved = (0 == strcmp(saved_val, "1"));
        }
    }

    size_t pos = 0U;

#define APPEND(fmt, ...) \
    pos += (size_t)snprintf(p_buf + pos, ACT_HTML_BUF_LEN - pos, fmt, ##__VA_ARGS__)

    APPEND("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Activity Manager</title>"
           "<style>body{font-family:sans-serif;margin:1em 2em;max-width:900px}"
           "h2,h3{color:#333}table{border-collapse:collapse;width:100%%}"
           "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left}"
           "th{background:#eee}input[type=text],input[type=number]"
           "{width:100%%;box-sizing:border-box}"
           ".banner{background:#d4edda;border:1px solid #c3e6cb;padding:8px;"
           "margin-bottom:1em;border-radius:4px}"
           ".sect{margin-top:1.5em;border-top:2px solid #ccc;padding-top:0.5em}"
           ".card{background:#f9f9f9;border:1px solid #ddd;border-radius:4px;"
           "padding:.6em .9em;margin:.6em 0}"
           "button,input[type=submit]{cursor:pointer;padding:4px 10px}"
           "a.btn{display:inline-block;padding:4px 12px;background:#4a90d9;color:#fff;"
           "border-radius:3px;text-decoration:none;margin-right:.5em}"
           "a.btn:hover{background:#3a7fc9}"
           "</style>"
           "<script>function hmsInput(el){"
           "var d=el.value.replace(/\\D/g,'').slice(0,6);"
           "if(d.length>4)el.value=d.slice(0,2)+':'+d.slice(2,4)+':'+d.slice(4);"
           "else if(d.length>2)el.value=d.slice(0,2)+':'+d.slice(2);"
           "else el.value=d;}</script>"
           "</head><body>");

    APPEND("<h2>Activity Manager</h2>");
    if (b_saved)
    {
        APPEND("<div class='banner'>Changes saved.</div>");
    }

    /* ---- Global activity pool section ---- */
    APPEND("<div class='card'><h3>Global Activity Pool</h3>");
    APPEND("<form method='POST' action='/activities/manage'>");
    APPEND("<table><tr><th>ID</th><th>Name</th><th>Credit (h:mm:ss)</th>"
           "<th>Time Limit (h:mm:ss)</th><th>Daily Limit</th><th>Actions</th></tr>");

    uint8_t pool_cnt = act_mngr_activity_count();
    for (uint8_t s = 0U; s < pool_cnt; s++)
    {
        act_mngr_entry_t entry;
        if (ESP_OK != act_mngr_activity_slot_get(s, &entry))
        {
            continue;
        }

        /* Format credit and time_limit as h:mm:ss. */
        uint32_t c_h  = entry.credit_s / 3600U;
        uint32_t c_m  = (entry.credit_s % 3600U) / 60U;
        uint32_t c_s  = entry.credit_s % 60U;
        uint32_t tl_h = entry.time_limit_s / 3600U;
        uint32_t tl_m = (entry.time_limit_s % 3600U) / 60U;
        uint32_t tl_s = entry.time_limit_s % 60U;

        char name_enc[64];
        http_srv_html_attr_encode(entry.name, name_enc, sizeof(name_enc));

        APPEND("<tr>");
        APPEND("<td>%lu</td>", (unsigned long)entry.id);
        APPEND("<td><input type='text' name='act_name_%lu' maxlength='40' value='%s'></td>",
            (unsigned long)entry.id, name_enc);
        APPEND("<td><input type='text' name='act_credit_%lu' maxlength='8'"
               " value='%lu:%02lu:%02lu' oninput='hmsInput(this)'></td>",
            (unsigned long)entry.id, (unsigned long)c_h, (unsigned long)c_m, (unsigned long)c_s);
        APPEND("<td><input type='text' name='act_limit_%lu' maxlength='8'"
               " value='%lu:%02lu:%02lu' oninput='hmsInput(this)'></td>",
            (unsigned long)entry.id, (unsigned long)tl_h, (unsigned long)tl_m, (unsigned long)tl_s);
        APPEND("<td><input type='number' name='act_daily_%lu' min='1' max='255' value='%u'></td>",
            (unsigned long)entry.id, (unsigned int)entry.daily_limit);
        APPEND("<td>"
               "<button type='submit' name='action' value='update_%lu'>Update</button> "
               "<button type='submit' name='action' value='delete_%lu'"
               " onclick=\"return confirm('Delete this activity?')\">Delete</button>"
               "</td>",
            (unsigned long)entry.id, (unsigned long)entry.id);
        APPEND("</tr>");
    }
    APPEND("</table>");

    /* Add-activity row — part of the same table for column alignment. */
    APPEND("<table><tr><th></th><th>Name</th><th>Credit (h:mm:ss)</th>"
           "<th>Time Limit (h:mm:ss)</th><th>Daily Limit</th><th></th></tr>");
    APPEND("<tr>");
    APPEND("<td></td>"); /* Empty ID cell. */
    APPEND("<td><input type='text' name='new_act_name' maxlength='40' placeholder='Name'></td>");
    APPEND("<td><input type='text' name='new_act_credit' maxlength='8'"
           " placeholder='0:00:00' oninput='hmsInput(this)'></td>");
    APPEND("<td><input type='text' name='new_act_limit' maxlength='8'"
           " placeholder='0:00:00' oninput='hmsInput(this)'></td>");
    APPEND("<td><input type='number' name='new_act_daily' min='1' max='255' value='1'></td>");
    APPEND("<td><button type='submit' name='action' value='add_activity'>Add</button></td>");
    APPEND("</tr></table></form></div>");

    /* ---- Activity assignments section ---- */
    APPEND("<div class='card'><h3>Activity Assignments</h3>");

    uint8_t dev_count = device_reg_count_get();
    for (uint8_t d = 0U; d < dev_count; d++)
    {
        device_reg_entry_t dev;
        if (ESP_OK != device_reg_entry_get(d, &dev))
        {
            continue;
        }

        char nick_enc[32];
        http_srv_html_attr_encode(dev.nickname, nick_enc, sizeof(nick_enc));

        APPEND("<h4>Device %u: %s</h4>", (unsigned int)d, nick_enc);
        APPEND("<form method='POST' action='/activities/manage'>");
        APPEND("<table><tr><th>Activity</th><th>Action</th></tr>");

        act_mngr_user_assigns_t assigns;
        (void)act_mngr_user_assigns_get(d, &assigns);

        for (uint8_t i = 0U; i < assigns.count; i++)
        {
            act_mngr_entry_t act;
            char             act_name_enc[64];
            if (ESP_OK == act_mngr_activity_get(assigns.act_ids[i], &act))
            {
                http_srv_html_attr_encode(act.name, act_name_enc, sizeof(act_name_enc));
            }
            else
            {
                (void)strncpy(act_name_enc, "(deleted)", sizeof(act_name_enc) - 1U);
                act_name_enc[sizeof(act_name_enc) - 1U] = '\0';
            }

            APPEND("<tr><td>%s (ID %lu)</td>"
                   "<td><button type='submit' name='action' value='unassign_%u_%lu'>"
                   "Unassign</button></td></tr>",
                act_name_enc, (unsigned long)assigns.act_ids[i], (unsigned int)d,
                (unsigned long)assigns.act_ids[i]);
        }

        /* Assign new activity select. */
        APPEND("<tr><td><select name='new_act_%u'>", (unsigned int)d);
        for (uint8_t s = 0U; s < pool_cnt; s++)
        {
            act_mngr_entry_t act;
            if (ESP_OK != act_mngr_activity_slot_get(s, &act))
            {
                continue;
            }
            if (!act_mngr_user_is_assigned(d, act.id))
            {
                char aname_enc[64];
                http_srv_html_attr_encode(act.name, aname_enc, sizeof(aname_enc));
                APPEND("<option value='%lu'>%s</option>", (unsigned long)act.id, aname_enc);
            }
        }
        APPEND("</select></td>");
        APPEND("<td><button type='submit' name='action' value='assign_%u'>"
               "Assign</button></td></tr>",
            (unsigned int)d);
        APPEND("</table></form>");
    }

    APPEND("</div>");
    APPEND("<div style='margin-top:1.5em'>"
           "<a class='btn' href='/activities'>Back to Activities</a>"
           "<a class='btn' href='/config'>Back to Config</a>"
           "</div>");
    APPEND("</body></html>");

#undef APPEND

    httpd_resp_set_type(p_req, "text/html");
    httpd_resp_send(p_req, p_buf, (ssize_t)pos);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_activities_manage_post_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    char * body = (char *)malloc(ACT_POST_BODY_MAX);
    if (NULL == body)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int recv_len = httpd_req_recv(p_req, body, ACT_POST_BODY_MAX - 1U);
    if (recv_len <= 0)
    {
        free(body);
        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[recv_len] = '\0';

    char action[64];
    if (ESP_OK != http_srv_form_field_get(body, "action", action, sizeof(action)))
    {
        free(body);
        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Missing action");
        return ESP_FAIL;
    }

    esp_err_t op_ret = ESP_OK;
    char      err_msg[128];
    err_msg[0] = '\0';

    if (0 == strcmp(action, "add_activity"))
    {
        char name[ACT_MNGR_NAME_MAX_LEN + 2U];
        char credit_str[16];
        char limit_str[16];
        char daily_str[8];

        (void)http_srv_form_field_get(body, "new_act_name", name, sizeof(name));
        (void)http_srv_form_field_get(body, "new_act_credit", credit_str, sizeof(credit_str));
        (void)http_srv_form_field_get(body, "new_act_limit", limit_str, sizeof(limit_str));
        (void)http_srv_form_field_get(body, "new_act_daily", daily_str, sizeof(daily_str));

        uint32_t credit_s     = 0U;
        uint32_t time_limit_s = 0U;
        (void)hms_str_to_s(credit_str, &credit_s);
        (void)hms_str_to_s(limit_str, &time_limit_s);

        char *  p_end      = NULL;
        long    daily_long = strtol(daily_str, &p_end, 10);
        uint8_t daily_lim  = ((p_end != daily_str) && (daily_long >= 1L) && (daily_long <= 255L)) ?
                                 (uint8_t)daily_long :
                                 0U;

        uint32_t new_id = 0U;
        op_ret          = act_mngr_activity_add(name, credit_s, time_limit_s, daily_lim, &new_id);
        if (ESP_ERR_NO_MEM == op_ret)
        {
            (void)strncpy(err_msg, "Pool full - max 30 activities", sizeof(err_msg) - 1U);
        }
        else if (ESP_ERR_INVALID_ARG == op_ret)
        {
            (void)strncpy(err_msg, "Invalid activity fields (name 1-40 chars, daily limit 1-255)",
                sizeof(err_msg) - 1U);
        }
    }
    else if (0 == strncmp(action, "update_", 7U))
    {
        char *   p_end  = NULL;
        uint32_t act_id = (uint32_t)strtoul(action + 7U, &p_end, 10);

        char name[ACT_MNGR_NAME_MAX_LEN + 2U];
        char credit_str[16];
        char limit_str[16];
        char daily_str[8];

        /* Field names contain the activity ID. */
        char fn_name[32];
        char fn_credit[32];
        char fn_limit[32];
        char fn_daily[32];
        (void)snprintf(fn_name, sizeof(fn_name), "act_name_%lu", (unsigned long)act_id);
        (void)snprintf(fn_credit, sizeof(fn_credit), "act_credit_%lu", (unsigned long)act_id);
        (void)snprintf(fn_limit, sizeof(fn_limit), "act_limit_%lu", (unsigned long)act_id);
        (void)snprintf(fn_daily, sizeof(fn_daily), "act_daily_%lu", (unsigned long)act_id);

        (void)http_srv_form_field_get(body, fn_name, name, sizeof(name));
        (void)http_srv_form_field_get(body, fn_credit, credit_str, sizeof(credit_str));
        (void)http_srv_form_field_get(body, fn_limit, limit_str, sizeof(limit_str));
        (void)http_srv_form_field_get(body, fn_daily, daily_str, sizeof(daily_str));

        uint32_t credit_s     = 0U;
        uint32_t time_limit_s = 0U;
        (void)hms_str_to_s(credit_str, &credit_s);
        (void)hms_str_to_s(limit_str, &time_limit_s);

        char *  p_end2     = NULL;
        long    daily_long = strtol(daily_str, &p_end2, 10);
        uint8_t daily_lim  = ((p_end2 != daily_str) && (daily_long >= 1L) && (daily_long <= 255L)) ?
                                 (uint8_t)daily_long :
                                 0U;

        op_ret = act_mngr_activity_update(act_id, name, credit_s, time_limit_s, daily_lim);
        if (ESP_ERR_NOT_FOUND == op_ret)
        {
            (void)strncpy(err_msg, "Activity not found", sizeof(err_msg) - 1U);
        }
        else if (ESP_ERR_INVALID_ARG == op_ret)
        {
            (void)strncpy(err_msg, "Invalid activity fields", sizeof(err_msg) - 1U);
        }
    }
    else if (0 == strncmp(action, "delete_", 7U))
    {
        char *   p_end  = NULL;
        uint32_t act_id = (uint32_t)strtoul(action + 7U, &p_end, 10);
        op_ret          = act_mngr_activity_remove(act_id);
        if (ESP_ERR_NOT_FOUND == op_ret)
        {
            /* Idempotent - treat as success. */
            op_ret = ESP_OK;
        }
    }
    else if (0 == strncmp(action, "assign_", 7U))
    {
        /* action = "assign_N" where N = dev_idx */
        char *  p_end    = NULL;
        long    dev_long = strtol(action + 7U, &p_end, 10);
        uint8_t dev_idx  = (uint8_t)dev_long;

        char sel_key[24];
        (void)snprintf(sel_key, sizeof(sel_key), "new_act_%u", (unsigned int)dev_idx);
        char act_id_str[16];
        (void)http_srv_form_field_get(body, sel_key, act_id_str, sizeof(act_id_str));
        char *   p_end2 = NULL;
        uint32_t act_id = (uint32_t)strtoul(act_id_str, &p_end2, 10);

        op_ret = act_mngr_user_assign(dev_idx, act_id);
        if (ESP_ERR_NO_MEM == op_ret)
        {
            (void)strncpy(err_msg, "Max 20 activities per user", sizeof(err_msg) - 1U);
        }
        else if (ESP_ERR_NOT_FOUND == op_ret)
        {
            (void)strncpy(err_msg, "Activity not found", sizeof(err_msg) - 1U);
        }
        else if (ESP_ERR_INVALID_STATE == op_ret)
        {
            /* Already assigned - treat as success. */
            op_ret = ESP_OK;
        }
    }
    else if (0 == strncmp(action, "unassign_", 9U))
    {
        /* action = "unassign_N_M" where N=dev_idx, M=act_id */
        char *  p_end    = NULL;
        long    dev_long = strtol(action + 9U, &p_end, 10);
        uint8_t dev_idx  = (uint8_t)dev_long;

        uint32_t act_id = 0U;
        if ((NULL != p_end) && ('_' == *p_end))
        {
            char * p_end2 = NULL;
            act_id        = (uint32_t)strtoul(p_end + 1, &p_end2, 10);
        }
        (void)act_mngr_user_unassign(dev_idx, act_id);
        op_ret = ESP_OK;
    }
    else
    {
        free(body);
        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, "Unknown action");
        return ESP_FAIL;
    }

    free(body);

    if (ESP_OK != op_ret)
    {
        if ('\0' == err_msg[0])
        {
            (void)strncpy(err_msg, esp_err_to_name(op_ret), sizeof(err_msg) - 1U);
        }
        err_msg[sizeof(err_msg) - 1U] = '\0';
        httpd_resp_send_err(p_req, HTTPD_400_BAD_REQUEST, err_msg);
        return ESP_FAIL;
    }

    httpd_resp_set_status(p_req, "303 See Other");
    httpd_resp_set_hdr(p_req, "Location", "/activities/manage?saved=1");
    httpd_resp_send(p_req, NULL, 0);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

esp_err_t http_srv_activities_get_handler(httpd_req_t * p_req)
{
    if (!http_srv_cfg_auth_check(p_req))
    {
        return ESP_OK;
    }

    char * p_buf = (char *)malloc(ACT_HTML_BUF_LEN);
    if (NULL == p_buf)
    {
        httpd_resp_send_err(p_req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t pos = 0U;

#define APPEND(fmt, ...) \
    pos += (size_t)snprintf(p_buf + pos, ACT_HTML_BUF_LEN - pos, fmt, ##__VA_ARGS__)

    APPEND("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Activities</title>"
           "<style>body{font-family:sans-serif;margin:1em 2em;max-width:800px}"
           "h2,h3{color:#333}table{border-collapse:collapse;width:100%%}"
           "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left}"
           "th{background:#eee}button{cursor:pointer;padding:4px 10px}"
           "button:disabled{opacity:0.4;cursor:default}"
           ".total{font-size:1.1em;font-weight:bold;margin:0.5em 0}"
           ".card{background:#f9f9f9;border:1px solid #ddd;border-radius:4px;"
           "padding:.6em .9em;margin:.6em 0}"
           "a.btn{display:inline-block;padding:4px 12px;background:#4a90d9;color:#fff;"
           "border-radius:3px;text-decoration:none;margin-right:.5em}"
           "a.btn:hover{background:#3a7fc9}"
           ".log-sep{background:#f0f0f0;font-size:.85em;color:#666;"
           "text-align:center;padding:2px 4px}"
           "</style></head><body>");

    APPEND("<h2>Activities</h2>");

    /* User combobox. */
    APPEND("<p><label>User: </label><select id='user-select' onchange='loadUser(this.value)'>");
    uint8_t dev_count = device_reg_count_get();
    for (uint8_t d = 0U; d < dev_count; d++)
    {
        device_reg_entry_t dev;
        if (ESP_OK != device_reg_entry_get(d, &dev))
        {
            continue;
        }
        char nick_enc[32];
        http_srv_html_attr_encode(dev.nickname, nick_enc, sizeof(nick_enc));
        APPEND("<option value='%u'>%s</option>", (unsigned int)d, nick_enc);
    }
    APPEND("</select></p>");

    APPEND("<div class='total' id='total-credits'>Total credits: --</div>");
    APPEND("<div class='card' id='activity-list'><p><em>Loading...</em></p></div>");
    APPEND("<div class='card'><h3>Credit Log</h3>"
           "<div id='credit-log'><p><em>Loading...</em></p></div></div>");

    /* JavaScript. */
    APPEND(
        "<script>\n"
        "function fmtHMS(s){"
        "var h=Math.floor(s/3600);"
        "var m=Math.floor((s%%3600)/60);"
        "var sec=s%%60;"
        "return h+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');}\n"

        "function creditActivity(devIdx,actId,creditsS,btn){"
        "if(!confirm('Credit '+fmtHMS(creditsS)+' internet time?'))return;"
        "btn.disabled=true;"
        "fetch('/api/activities/credit',{"
        "method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({device_idx:devIdx,act_id:actId,credits_s:creditsS,"
        "completion_time_s:0})})"
        ".then(function(r){"
        "if(r.status===429){alert('Daily limit reached');btn.disabled=false;return null;}"
        "if(!r.ok){r.text().then(function(t){alert('Error: '+t)});btn.disabled=false;return null;}"
        "return r.json();})"
        ".then(function(d){"
        "if(!d)return;"
        "document.getElementById('total-credits').textContent="
        "'Total credits: '+fmtHMS(d.new_counter_s);"
        "loadUser(devIdx);"
        "});}\n"

        "function loadUser(devIdx){"
        "devIdx=parseInt(devIdx,10);"
        "fetch('/api/activities?device_idx='+devIdx)"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
        "var div=document.getElementById('activity-list');"
        "if(!d.activities||d.activities.length===0){"
        "div.innerHTML='<p><em>No activities assigned.</em></p>';return;}"
        "var html='<table><tr><th>Activity</th><th>Credit</th>"
        "<th>Done today</th><th>Limit</th><th>Action</th></tr>';"
        "d.activities.forEach(function(a){"
        "var dis=!a.available?' disabled':'';"
        "html+='<tr><td>'+a.name+'</td><td>'+a.credit_hms+'</td>'"
        "+'<td>'+a.done_today+'</td><td>'+a.daily_limit+'</td>'"
        "+'<td><button'+dis+' "
        "onclick=\\\"creditActivity('+devIdx+','+a.id+','+a.credit_s+',this)\\\">'"
        "+'Credit '+a.credit_hms+'</button></td></tr>';});"
        "html+='</table>';"
        "div.innerHTML=html;});"
        "fetch('/api/activities/log?device_idx='+devIdx)"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
        "var div=document.getElementById('credit-log');"
        "if(!d.log||d.log.length===0){"
        "div.innerHTML='<p><em>No credits yet.</em></p>';return;}"
        "var now=new Date();"
        "var today=now.getFullYear()+'-'+String(now.getMonth()+1).padStart(2,'0')"
        "+'-'+String(now.getDate()).padStart(2,'0');"
        "var html='<table><tr><th>Time</th><th>Activity</th><th>Credits</th></tr>';"
        "var seenOlder=false;"
        "d.log.forEach(function(e){"
        "var eDate=e.timestamp_local?e.timestamp_local.substring(0,10):'';"
        "if(!seenOlder&&eDate!==today){"
        "seenOlder=true;"
        "html+='<tr><td colspan=3 class=log-sep>&#x2014; Earlier &#x2014;</td></tr>';}"
        "html+='<tr><td>'+e.timestamp_local+'</td><td>'+e.act_name+"
        "'</td><td>'+e.credits_hms+'</td></tr>';});"
        "html+='</table>';"
        "div.innerHTML=html;});"
        "fetch('/api/status')"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
        "if(d.devices&&d.devices[devIdx]){"
        "document.getElementById('total-credits').textContent="
        "'Total credits: '+d.devices[devIdx].counter_hms;}});}\n"

        "loadUser(0);\n"
        "</script>\n");

    APPEND("<div style='margin-top:1.5em'>"
           "<a class='btn' href='/activities/manage'>Manage Activities</a>"
           "<a class='btn' href='/config'>Config</a>"
           "</div>");
    APPEND("</body></html>");

#undef APPEND

    httpd_resp_set_type(p_req, "text/html");
    httpd_resp_send(p_req, p_buf, (ssize_t)pos);
    free(p_buf);
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * \brief Parse a h:mm:ss or h:m:s time string into seconds.
 *
 * Expects exactly two ':' separators.  Minutes and seconds must be 0–59.
 * Hours are unconstrained.
 *
 * \param[in]  p_str  NUL-terminated time string.
 * \param[out] p_out  Receives the number of seconds.
 *
 * \return \c ESP_OK on success, \c ESP_ERR_INVALID_ARG on parse failure.
 */
static esp_err_t hms_str_to_s(const char * p_str, uint32_t * p_out)
{
    if ((NULL == p_str) || (NULL == p_out))
    {
        return ESP_ERR_INVALID_ARG;
    }

    char   buf[24];
    size_t slen = strlen(p_str);
    if (slen == 0U || slen >= sizeof(buf))
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }
    (void)strncpy(buf, p_str, sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';

    char * p_first_colon = strchr(buf, ':');
    if (NULL == p_first_colon)
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }
    *p_first_colon = '\0';

    char * p_second_colon = strchr(p_first_colon + 1, ':');
    if (NULL == p_second_colon)
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }
    *p_second_colon = '\0';

    char * p_end = NULL;
    long   h     = strtol(buf, &p_end, 10);
    if (p_end == buf)
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }

    long m = strtol(p_first_colon + 1, &p_end, 10);
    if ((p_end == (p_first_colon + 1)) || (m < 0L) || (m > 59L))
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }

    long s = strtol(p_second_colon + 1, &p_end, 10);
    if ((p_end == (p_second_colon + 1)) || (s < 0L) || (s > 59L))
    {
        *p_out = 0U;
        return ESP_ERR_INVALID_ARG;
    }

    *p_out = (uint32_t)h * 3600U + (uint32_t)m * 60U + (uint32_t)s;
    return ESP_OK;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
