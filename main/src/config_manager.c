/**
 * \file
 * \brief NVS-backed runtime configuration manager — full implementation.
 *
 * \date 2026-03-14
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "config_manager.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

//==================================================================================================
// Internal Constants/Macros/Datatypes
//==================================================================================================

/** NVS namespace used by this module. */
#define CONFIG_MNGR_NAMESPACE ("esport_cfg")

/* NVS key strings (spec §8). */
#define CONFIG_MNGR_KEY_WIFI_SSID         ("wifi_ssid")
#define CONFIG_MNGR_KEY_WIFI_PWD          ("wifi_pwd")
#define CONFIG_MNGR_KEY_AP_SSID           ("ap_ssid")
#define CONFIG_MNGR_KEY_AP_PWD            ("ap_pwd")
#define CONFIG_MNGR_KEY_SECONDS_PER_PULSE ("spp")
#define CONFIG_MNGR_KEY_AP_THRESH         ("ap_thresh")
#define CONFIG_MNGR_KEY_CM_PER_PULSE      ("cpp")
#define CONFIG_MNGR_KEY_IDLE_SESSION_S    ("idle_s")
#define CONFIG_MNGR_KEY_START_SESSION_S   ("start_s")
#define CONFIG_MNGR_KEY_DEBOUNCE_MS       ("debounce_ms")
#define CONFIG_MNGR_KEY_TZ                ("tz")
#define CONFIG_MNGR_KEY_AP_THR_KBPS       ("ap_thr_kbps")
#define CONFIG_MNGR_KEY_AP_IDLE_TIMEOUT_S ("ap_idle_tmo")
#define CONFIG_MNGR_KEY_MIN_SPEED_X10     ("min_spd_x10")
#define CONFIG_MNGR_KEY_REWARD_COUNTER_S  ("reward_ctr_s")

/* Factory defaults (spec §3). */
#define CONFIG_MNGR_DEF_WIFI_SSID         (CONFIG_ESPORT_WIFI_SSID)
#define CONFIG_MNGR_DEF_WIFI_PWD          (CONFIG_ESPORT_WIFI_PASSWORD)
#define CONFIG_MNGR_DEF_AP_SSID           (CONFIG_ESPORT_REWARD_AP_SSID)
#define CONFIG_MNGR_DEF_AP_PWD            (CONFIG_ESPORT_REWARD_AP_PASSWORD)
#define CONFIG_MNGR_DEF_SECONDS_PER_PULSE ((uint16_t)3U)
#define CONFIG_MNGR_DEF_AP_THRESH         ((uint32_t)300U)
#define CONFIG_MNGR_DEF_CM_PER_PULSE      ((uint32_t)300U)
#define CONFIG_MNGR_DEF_IDLE_SESSION_S    ((uint16_t)30U)
#define CONFIG_MNGR_DEF_START_SESSION_S   ((uint16_t)10U)
#define CONFIG_MNGR_DEF_DEBOUNCE_MS       ((uint16_t)10U)
#define CONFIG_MNGR_DEF_TZ                ("CET-1CEST,M3.5.0,M10.5.0/3")
#define CONFIG_MNGR_DEF_AP_THR_KBPS       ((uint16_t)CONFIG_ESPORT_AP_THRESHOLD_KBPS)
#define CONFIG_MNGR_DEF_AP_IDLE_TIMEOUT_S ((uint16_t)CONFIG_ESPORT_AP_IDLE_TIMEOUT_S)
#define CONFIG_MNGR_DEF_MIN_SPEED_X10     ((uint16_t)CONFIG_ESPORT_MIN_SPEED_KMH_X10)
#define CONFIG_MNGR_DEF_REWARD_COUNTER_S  ((uint32_t)0U)
#define CONFIG_MNGR_KEY_BUZZER_ENABLED    ("buzzer_en")
#define CONFIG_MNGR_DEF_BUZZER_ENABLED    ((uint8_t)1U)

/* String size limits (spec §5.1). */
#define CONFIG_MNGR_MAX_SSID_LEN (32U) /* max 32 chars + NUL */
#define CONFIG_MNGR_MAX_PWD_LEN  (64U) /* max 64 chars + NUL */
#define CONFIG_MNGR_MAX_TZ_LEN   (63U) /* max 63 chars + NUL */

//==================================================================================================
// Variables/Data
//==================================================================================================

/** Module log tag. */
static const char * gp_tag = "config_manager";

//==================================================================================================
// Internal Function Prototypes
//==================================================================================================

static esp_err_t config_mngr_default_str_write(nvs_handle_t handle, const char * p_key,
    const char * p_default);
static esp_err_t config_mngr_default_u16_write(nvs_handle_t handle, const char * p_key,
    uint16_t defval);
static esp_err_t config_mngr_default_u32_write(nvs_handle_t handle, const char * p_key,
    uint32_t defval);
static void      config_mngr_str_get(const char * p_key, char * p_buf, size_t len,
         const char * p_default);
static uint16_t  config_mngr_u16_get(const char * p_key, uint16_t defval);
static uint32_t  config_mngr_u32_get(const char * p_key, uint32_t defval);
static esp_err_t config_mngr_str_set(const char * p_key, const char * p_val, size_t min_len,
    size_t max_len);
static esp_err_t config_mngr_u16_set(const char * p_key, uint16_t val, uint16_t min_val,
    uint16_t max_val);
static esp_err_t config_mngr_u32_set(const char * p_key, uint32_t val, uint32_t min_val,
    uint32_t max_val);

//==================================================================================================
// Public Functions
//==================================================================================================

esp_err_t config_mngr_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if ((ESP_ERR_NVS_NO_FREE_PAGES == ret) || (ESP_ERR_NVS_NEW_VERSION_FOUND == ret))
    {
        ESP_LOGW(gp_tag, "NVS corrupt (0x%x), erasing and reinitialising", ret);
        ret = nvs_flash_erase();
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "nvs_flash_erase failed: 0x%x", ret);
            return ret;
        }
        ret = nvs_flash_init();
    }

    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_flash_init failed: 0x%x", ret);
        return ret;
    }

    nvs_handle_t handle;
    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "nvs_open failed: 0x%x", ret);
        return ret;
    }

    /* Write factory defaults for any absent key. */
    ret |=
        config_mngr_default_str_write(handle, CONFIG_MNGR_KEY_WIFI_SSID, CONFIG_MNGR_DEF_WIFI_SSID);
    ret |=
        config_mngr_default_str_write(handle, CONFIG_MNGR_KEY_WIFI_PWD, CONFIG_MNGR_DEF_WIFI_PWD);
    ret |= config_mngr_default_str_write(handle, CONFIG_MNGR_KEY_AP_SSID, CONFIG_MNGR_DEF_AP_SSID);
    ret |= config_mngr_default_str_write(handle, CONFIG_MNGR_KEY_AP_PWD, CONFIG_MNGR_DEF_AP_PWD);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_SECONDS_PER_PULSE,
        CONFIG_MNGR_DEF_SECONDS_PER_PULSE);
    ret |=
        config_mngr_default_u32_write(handle, CONFIG_MNGR_KEY_AP_THRESH, CONFIG_MNGR_DEF_AP_THRESH);
    ret |= config_mngr_default_u32_write(handle, CONFIG_MNGR_KEY_CM_PER_PULSE,
        CONFIG_MNGR_DEF_CM_PER_PULSE);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_IDLE_SESSION_S,
        CONFIG_MNGR_DEF_IDLE_SESSION_S);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_START_SESSION_S,
        CONFIG_MNGR_DEF_START_SESSION_S);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_DEBOUNCE_MS,
        CONFIG_MNGR_DEF_DEBOUNCE_MS);
    ret |= config_mngr_default_str_write(handle, CONFIG_MNGR_KEY_TZ, CONFIG_MNGR_DEF_TZ);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_AP_THR_KBPS,
        CONFIG_MNGR_DEF_AP_THR_KBPS);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_AP_IDLE_TIMEOUT_S,
        CONFIG_MNGR_DEF_AP_IDLE_TIMEOUT_S);
    ret |= config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_MIN_SPEED_X10,
        CONFIG_MNGR_DEF_MIN_SPEED_X10);
    ret |= config_mngr_default_u32_write(handle, CONFIG_MNGR_KEY_REWARD_COUNTER_S,
        CONFIG_MNGR_DEF_REWARD_COUNTER_S);

    /* buzzer_enabled (uint8: 0=false, 1=true) */
    {
        uint8_t   bz_val = 0U;
        esp_err_t bz_ret = nvs_get_u8(handle, CONFIG_MNGR_KEY_BUZZER_ENABLED, &bz_val);
        if (ESP_ERR_NVS_NOT_FOUND == bz_ret)
        {
            ret |=
                nvs_set_u8(handle, CONFIG_MNGR_KEY_BUZZER_ENABLED, CONFIG_MNGR_DEF_BUZZER_ENABLED);
        }
    }

    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "nvs_commit failed: 0x%x", ret);
        }
    }

    nvs_close(handle);
    ESP_LOGI(gp_tag, "initialised");
    return ret;
}

//--------------------------------------------------------------------------------------------------

void config_mngr_wifi_ssid_get(char * p_buf, size_t len)
{
    config_mngr_str_get(CONFIG_MNGR_KEY_WIFI_SSID, p_buf, len, CONFIG_MNGR_DEF_WIFI_SSID);
}

//--------------------------------------------------------------------------------------------------

void config_mngr_wifi_password_get(char * p_buf, size_t len)
{
    config_mngr_str_get(CONFIG_MNGR_KEY_WIFI_PWD, p_buf, len, CONFIG_MNGR_DEF_WIFI_PWD);
}

//--------------------------------------------------------------------------------------------------

void config_mngr_soft_ap_ssid_get(char * p_buf, size_t len)
{
    config_mngr_str_get(CONFIG_MNGR_KEY_AP_SSID, p_buf, len, CONFIG_MNGR_DEF_AP_SSID);
}

//--------------------------------------------------------------------------------------------------

void config_mngr_soft_ap_password_get(char * p_buf, size_t len)
{
    config_mngr_str_get(CONFIG_MNGR_KEY_AP_PWD, p_buf, len, CONFIG_MNGR_DEF_AP_PWD);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_seconds_per_pulse_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_SECONDS_PER_PULSE,
        CONFIG_MNGR_DEF_SECONDS_PER_PULSE);
}

//--------------------------------------------------------------------------------------------------

uint32_t config_mngr_soft_ap_start_threshold_s_get(void)
{
    return config_mngr_u32_get(CONFIG_MNGR_KEY_AP_THRESH, CONFIG_MNGR_DEF_AP_THRESH);
}

//--------------------------------------------------------------------------------------------------

uint32_t config_mngr_centimeters_per_pulse_get(void)
{
    return config_mngr_u32_get(CONFIG_MNGR_KEY_CM_PER_PULSE, CONFIG_MNGR_DEF_CM_PER_PULSE);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_idle_session_interval_s_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_IDLE_SESSION_S, CONFIG_MNGR_DEF_IDLE_SESSION_S);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_start_session_interval_s_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_START_SESSION_S, CONFIG_MNGR_DEF_START_SESSION_S);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_pulse_debounce_time_ms_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_DEBOUNCE_MS, CONFIG_MNGR_DEF_DEBOUNCE_MS);
}

//--------------------------------------------------------------------------------------------------

void config_mngr_timezone_get(char * p_buf, size_t len)
{
    config_mngr_str_get(CONFIG_MNGR_KEY_TZ, p_buf, len, CONFIG_MNGR_DEF_TZ);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_wifi_ssid_set(const char * p_val)
{
    return config_mngr_str_set(CONFIG_MNGR_KEY_WIFI_SSID, p_val, 1U, CONFIG_MNGR_MAX_SSID_LEN);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_wifi_password_set(const char * p_val)
{
    return config_mngr_str_set(CONFIG_MNGR_KEY_WIFI_PWD, p_val, 0U, CONFIG_MNGR_MAX_PWD_LEN);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_soft_ap_ssid_set(const char * p_val)
{
    return config_mngr_str_set(CONFIG_MNGR_KEY_AP_SSID, p_val, 1U, CONFIG_MNGR_MAX_SSID_LEN);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_soft_ap_password_set(const char * p_val)
{
    return config_mngr_str_set(CONFIG_MNGR_KEY_AP_PWD, p_val, 0U, CONFIG_MNGR_MAX_PWD_LEN);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_seconds_per_pulse_set(uint16_t val)
{
    return config_mngr_u16_set(CONFIG_MNGR_KEY_SECONDS_PER_PULSE, val, 1U, 60U);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_soft_ap_start_threshold_s_set(uint32_t val)
{
    return config_mngr_u32_set(CONFIG_MNGR_KEY_AP_THRESH, val, 0U, UINT32_MAX);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_centimeters_per_pulse_set(uint32_t val)
{
    return config_mngr_u32_set(CONFIG_MNGR_KEY_CM_PER_PULSE, val, 1U, UINT32_MAX);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_idle_session_interval_s_set(uint16_t val)
{
    return config_mngr_u16_set(CONFIG_MNGR_KEY_IDLE_SESSION_S, val, 5U, 600U);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_start_session_interval_s_set(uint16_t val)
{
    return config_mngr_u16_set(CONFIG_MNGR_KEY_START_SESSION_S, val, 1U, 300U);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_pulse_debounce_time_ms_set(uint16_t val)
{
    return config_mngr_u16_set(CONFIG_MNGR_KEY_DEBOUNCE_MS, val, 1U, 5000U);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_timezone_set(const char * p_val)
{
    return config_mngr_str_set(CONFIG_MNGR_KEY_TZ, p_val, 1U, CONFIG_MNGR_MAX_TZ_LEN);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_soft_ap_dec_threshold_kbps_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_AP_THR_KBPS, CONFIG_MNGR_DEF_AP_THR_KBPS);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_soft_ap_dec_threshold_kbps_set(uint16_t val)
{
    /* Full uint16_t range 0–65535 is valid per spec §3. */
    return config_mngr_u16_set(CONFIG_MNGR_KEY_AP_THR_KBPS, val, 0U, UINT16_MAX);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_soft_ap_idle_throughput_timeout_s_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_AP_IDLE_TIMEOUT_S,
        CONFIG_MNGR_DEF_AP_IDLE_TIMEOUT_S);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_soft_ap_idle_throughput_timeout_s_set(uint16_t val)
{
    /* Full uint16_t range 0–65535 is valid per spec §3. */
    return config_mngr_u16_set(CONFIG_MNGR_KEY_AP_IDLE_TIMEOUT_S, val, 0U, UINT16_MAX);
}

//--------------------------------------------------------------------------------------------------

uint16_t config_mngr_min_speed_to_increment_time_kmh_x10_get(void)
{
    return config_mngr_u16_get(CONFIG_MNGR_KEY_MIN_SPEED_X10, CONFIG_MNGR_DEF_MIN_SPEED_X10);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_min_speed_to_increment_time_kmh_x10_set(uint16_t val)
{
    /* Full uint16_t range 0–65535 is valid per spec §3. */
    return config_mngr_u16_set(CONFIG_MNGR_KEY_MIN_SPEED_X10, val, 0U, UINT16_MAX);
}

//--------------------------------------------------------------------------------------------------

uint32_t config_mngr_reward_counter_s_get(void)
{
    return config_mngr_u32_get(CONFIG_MNGR_KEY_REWARD_COUNTER_S, CONFIG_MNGR_DEF_REWARD_COUNTER_S);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_reward_counter_s_set(uint32_t val)
{
    /* Full uint32_t range 0–UINT32_MAX is valid per spec §3. */
    return config_mngr_u32_set(CONFIG_MNGR_KEY_REWARD_COUNTER_S, val, 0U, UINT32_MAX);
}

//--------------------------------------------------------------------------------------------------

bool config_mngr_buzzer_enabled_get(void)
{
    nvs_handle_t handle;
    uint8_t      val = CONFIG_MNGR_DEF_BUZZER_ENABLED;

    esp_err_t ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_OK == ret)
    {
        (void)nvs_get_u8(handle, CONFIG_MNGR_KEY_BUZZER_ENABLED, &val);
        nvs_close(handle);
    }

    return (val != 0U);
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_buzzer_enabled_set(bool b_enabled)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "config_mngr_buzzer_enabled_set open failed: 0x%x", ret);
        return ret;
    }

    ret = nvs_set_u8(handle, CONFIG_MNGR_KEY_BUZZER_ENABLED, (uint8_t)(b_enabled ? 1U : 0U));
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_buzzer_enabled_set commit failed: 0x%x", ret);
        }
    }
    else
    {
        ESP_LOGE(gp_tag, "nvs_set_u8(%s) failed: 0x%x", CONFIG_MNGR_KEY_BUZZER_ENABLED, ret);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

esp_err_t config_mngr_reset_to_defaults(void)
{
    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "reset_to_defaults: nvs_open failed: 0x%x", ret);
        return ret;
    }

    /* Erase all keys in the namespace before writing fresh defaults. */
    ret = nvs_erase_all(handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "reset_to_defaults: nvs_erase_all failed: 0x%x", ret);
        nvs_close(handle);
        return ret;
    }

    /* Write every factory default unconditionally. */
    ret |= nvs_set_str(handle, CONFIG_MNGR_KEY_WIFI_SSID, CONFIG_MNGR_DEF_WIFI_SSID);
    ret |= nvs_set_str(handle, CONFIG_MNGR_KEY_WIFI_PWD, CONFIG_MNGR_DEF_WIFI_PWD);
    ret |= nvs_set_str(handle, CONFIG_MNGR_KEY_AP_SSID, CONFIG_MNGR_DEF_AP_SSID);
    ret |= nvs_set_str(handle, CONFIG_MNGR_KEY_AP_PWD, CONFIG_MNGR_DEF_AP_PWD);
    ret |=
        nvs_set_u16(handle, CONFIG_MNGR_KEY_SECONDS_PER_PULSE, CONFIG_MNGR_DEF_SECONDS_PER_PULSE);
    ret |= nvs_set_u32(handle, CONFIG_MNGR_KEY_AP_THRESH, CONFIG_MNGR_DEF_AP_THRESH);
    ret |= nvs_set_u32(handle, CONFIG_MNGR_KEY_CM_PER_PULSE, CONFIG_MNGR_DEF_CM_PER_PULSE);
    ret |= nvs_set_u16(handle, CONFIG_MNGR_KEY_IDLE_SESSION_S, CONFIG_MNGR_DEF_IDLE_SESSION_S);
    ret |= nvs_set_u16(handle, CONFIG_MNGR_KEY_START_SESSION_S, CONFIG_MNGR_DEF_START_SESSION_S);
    ret |= nvs_set_u16(handle, CONFIG_MNGR_KEY_DEBOUNCE_MS, CONFIG_MNGR_DEF_DEBOUNCE_MS);
    ret |= nvs_set_str(handle, CONFIG_MNGR_KEY_TZ, CONFIG_MNGR_DEF_TZ);
    ret |= nvs_set_u16(handle, CONFIG_MNGR_KEY_AP_THR_KBPS, CONFIG_MNGR_DEF_AP_THR_KBPS);
    ret |=
        nvs_set_u16(handle, CONFIG_MNGR_KEY_AP_IDLE_TIMEOUT_S, CONFIG_MNGR_DEF_AP_IDLE_TIMEOUT_S);
    ret |= nvs_set_u16(handle, CONFIG_MNGR_KEY_MIN_SPEED_X10, CONFIG_MNGR_DEF_MIN_SPEED_X10);
    ret |= nvs_set_u32(handle, CONFIG_MNGR_KEY_REWARD_COUNTER_S, CONFIG_MNGR_DEF_REWARD_COUNTER_S);
    ret |= nvs_set_u8(handle, CONFIG_MNGR_KEY_BUZZER_ENABLED, CONFIG_MNGR_DEF_BUZZER_ENABLED);

    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "reset_to_defaults: nvs_commit failed: 0x%x", ret);
        }
    }

    nvs_close(handle);

    if (ESP_OK == ret)
    {
        ESP_LOGI(gp_tag, "all settings reset to factory defaults");
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

//==================================================================================================
// Private Functions
//==================================================================================================

/**
 * Write \p p_default as a string to \p p_key only if the key is absent.
 *
 * \param[in] handle    Open NVS handle (NVS_READWRITE).
 * \param[in] p_key     NVS key name.
 * \param[in] p_default Default string value.
 *
 * \return \c ESP_OK on success, or an NVS error code.
 */
static esp_err_t config_mngr_default_str_write(nvs_handle_t handle, const char * p_key,
    const char * p_default)
{
    size_t    required = 0U;
    esp_err_t ret      = nvs_get_str(handle, p_key, NULL, &required);

    if (ESP_ERR_NVS_NOT_FOUND == ret)
    {
        ret = nvs_set_str(handle, p_key, p_default);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_default_str_write(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ret = ESP_OK; /* key already exists */
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * Write \p defval as a uint16 to \p p_key only if the key is absent.
 *
 * \param[in] handle    Open NVS handle (NVS_READWRITE).
 * \param[in] p_key     NVS key name.
 * \param[in] defval    Default uint16 value.
 *
 * \return \c ESP_OK on success, or an NVS error code.
 */
static esp_err_t config_mngr_default_u16_write(nvs_handle_t handle, const char * p_key,
    uint16_t defval)
{
    uint16_t  tmp = 0U;
    esp_err_t ret = nvs_get_u16(handle, p_key, &tmp);

    if (ESP_ERR_NVS_NOT_FOUND == ret)
    {
        ret = nvs_set_u16(handle, p_key, defval);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_default_u16_write(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ret = ESP_OK; /* key already exists */
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * Write \p defval as a uint32 to \p p_key only if the key is absent.
 *
 * \param[in] handle    Open NVS handle (NVS_READWRITE).
 * \param[in] p_key     NVS key name.
 * \param[in] defval    Default uint32 value.
 *
 * \return \c ESP_OK on success, or an NVS error code.
 */
static esp_err_t config_mngr_default_u32_write(nvs_handle_t handle, const char * p_key,
    uint32_t defval)
{
    uint32_t  tmp = 0U;
    esp_err_t ret = nvs_get_u32(handle, p_key, &tmp);

    if (ESP_ERR_NVS_NOT_FOUND == ret)
    {
        ret = nvs_set_u32(handle, p_key, defval);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_default_u32_write(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ret = ESP_OK; /* key already exists */
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * Read a string value from \p p_key; fall back to \p p_default on any error.
 *
 * \param[in]  p_key      NVS key name.
 * \param[out] p_buf      Destination buffer.
 * \param[in]  len        Size of \p p_buf in bytes.
 * \param[in]  p_default  Fallback string if the key cannot be read.
 */
static void config_mngr_str_get(const char * p_key, char * p_buf, size_t len,
    const char * p_default)
{
    if ((NULL == p_buf) || (0U == len))
    {
        return;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READONLY, &handle);

    if (ESP_OK == ret)
    {
        size_t out_len = len;
        ret            = nvs_get_str(handle, p_key, p_buf, &out_len);
        nvs_close(handle);
    }

    if (ESP_OK != ret)
    {
        strncpy(p_buf, p_default, len - 1U);
        p_buf[len - 1U] = '\0';
    }
}

//--------------------------------------------------------------------------------------------------

/**
 * Read a uint16 value from \p p_key; fall back to \p defval on any error.
 *
 * \param[in] p_key   NVS key name.
 * \param[in] defval  Fallback value.
 *
 * \return The stored value, or \p defval on error.
 */
static uint16_t config_mngr_u16_get(const char * p_key, uint16_t defval)
{
    nvs_handle_t handle;
    uint16_t     val = defval;

    esp_err_t ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_OK == ret)
    {
        (void)nvs_get_u16(handle, p_key, &val);
        nvs_close(handle);
    }

    return val;
}

//--------------------------------------------------------------------------------------------------

/**
 * Read a uint32 value from \p p_key; fall back to \p defval on any error.
 *
 * \param[in] p_key   NVS key name.
 * \param[in] defval  Fallback value.
 *
 * \return The stored value, or \p defval on error.
 */
static uint32_t config_mngr_u32_get(const char * p_key, uint32_t defval)
{
    nvs_handle_t handle;
    uint32_t     val = defval;

    esp_err_t ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_OK == ret)
    {
        (void)nvs_get_u32(handle, p_key, &val);
        nvs_close(handle);
    }

    return val;
}

//--------------------------------------------------------------------------------------------------

/**
 * Validate and persist a string configuration value.
 *
 * \param[in] p_key   NVS key name.
 * \param[in] p_val   New value (NUL-terminated).
 * \param[in] min_len Minimum allowed string length (bytes, not counting NUL).
 * \param[in] max_len Maximum allowed string length (bytes, not counting NUL).
 *
 * \return \c ESP_OK, \c ESP_ERR_INVALID_ARG, or an NVS error code.
 */
static esp_err_t config_mngr_str_set(const char * p_key, const char * p_val, size_t min_len,
    size_t max_len)
{
    if (NULL == p_val)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t vlen = strlen(p_val);
    if ((vlen < min_len) || (vlen > max_len))
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "config_mngr_str_set open failed: 0x%x", ret);
        return ret;
    }

    ret = nvs_set_str(handle, p_key, p_val);
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_str_set commit(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ESP_LOGE(gp_tag, "nvs_set_str(%s) failed: 0x%x", p_key, ret);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * Validate and persist a uint16 configuration value.
 *
 * \param[in] p_key   NVS key name.
 * \param[in] val     New value.
 * \param[in] min_val Minimum allowed value (inclusive).
 * \param[in] max_val Maximum allowed value (inclusive).
 *
 * \return \c ESP_OK, \c ESP_ERR_INVALID_ARG, or an NVS error code.
 */
static esp_err_t config_mngr_u16_set(const char * p_key, uint16_t val, uint16_t min_val,
    uint16_t max_val)
{
    if ((val < min_val) || (val > max_val))
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "config_mngr_u16_set open failed: 0x%x", ret);
        return ret;
    }

    ret = nvs_set_u16(handle, p_key, val);
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_u16_set commit(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ESP_LOGE(gp_tag, "nvs_set_u16(%s) failed: 0x%x", p_key, ret);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/**
 * Validate and persist a uint32 configuration value.
 *
 * \param[in] p_key   NVS key name.
 * \param[in] val     New value.
 * \param[in] min_val Minimum allowed value (inclusive).
 * \param[in] max_val Maximum allowed value (inclusive).
 *
 * \return \c ESP_OK, \c ESP_ERR_INVALID_ARG, or an NVS error code.
 */
static esp_err_t config_mngr_u32_set(const char * p_key, uint32_t val, uint32_t min_val,
    uint32_t max_val)
{
    if ((val < min_val) || (val > max_val))
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t    ret = nvs_open(CONFIG_MNGR_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != ret)
    {
        ESP_LOGE(gp_tag, "config_mngr_u32_set open failed: 0x%x", ret);
        return ret;
    }

    ret = nvs_set_u32(handle, p_key, val);
    if (ESP_OK == ret)
    {
        ret = nvs_commit(handle);
        if (ESP_OK != ret)
        {
            ESP_LOGE(gp_tag, "config_mngr_u32_set commit(%s) failed: 0x%x", p_key, ret);
        }
    }
    else
    {
        ESP_LOGE(gp_tag, "nvs_set_u32(%s) failed: 0x%x", p_key, ret);
    }

    nvs_close(handle);
    return ret;
}

//--------------------------------------------------------------------------------------------------

/*** end of file ***/
