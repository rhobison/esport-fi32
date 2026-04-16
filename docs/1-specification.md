# esport-fi32 Firmware Specification

**Version:** 2.2
**Date:** 2026-04-06
**Target:** ESP32-C6 (ESP-IDF v5.x)

---

## Table of Contents

- [esport-fi32 Firmware Specification](#esport-fi32-firmware-specification)
  - [Table of Contents](#table-of-contents)
  - [1. Overview](#1-overview)
  - [2. Hardware](#2-hardware)
  - [3. Configuration Parameters](#3-configuration-parameters)
  - [4. System Architecture](#4-system-architecture)
    - [Component/File Layout](#componentfile-layout)
  - [5. Module Specifications](#5-module-specifications)
    - [5.1 NVS Configuration Manager](#51-nvs-configuration-manager)
    - [5.2 WiFi Manager](#52-wifi-manager)
    - [5.3 SNTP / Time Manager](#53-sntp--time-manager)
    - [5.4 Pulse Input Module](#54-pulse-input-module)
    - [5.5 Time Counter \& Reward AP State Machine](#55-time-counter--reward-ap-state-machine)
    - [5.6 Session Tracker](#56-session-tracker)
    - [5.7 NVS Session Log](#57-nvs-session-log)
    - [5.8 HTTP Server](#58-http-server)
    - [5.9 Device Registry](#59-device-registry)
  - [6. Web Interface](#6-web-interface)
    - [6.1 Status Dashboard — `GET /`](#61-status-dashboard--get-)
    - [6.2 Configuration Page — `GET /config`](#62-configuration-page--get-config)
    - [6.3 JSON Status API — `GET /api/status`](#63-json-status-api--get-apistatus)
    - [6.4 JSON Sessions API — `GET /api/sessions`](#64-json-sessions-api--get-apisessions)
    - [6.5 Sessions Export API — `GET /api/sessions/export`](#65-sessions-export-api--get-apisessionsexport)
    - [6.6 Daily Aggregates API (Graphs) — `GET /api/sessions/daily`](#66-daily-aggregates-api-graphs--get-apisessionsdaily)
    - [6.7 Firmware Update — `GET /ota` and `POST /ota`](#67-firmware-update--get-ota-and-post-ota)
  - [7. System Behaviour Sequences](#7-system-behaviour-sequences)
    - [7.1 Boot Sequence](#71-boot-sequence)
    - [7.2 STA Connection Flow](#72-sta-connection-flow)
    - [7.3 Pulse \& Counter Flow](#73-pulse--counter-flow)
    - [7.4 Counter Decrement \& AP Shutdown](#74-counter-decrement--ap-shutdown)
    - [7.5 Session Lifecycle](#75-session-lifecycle)
    - [7.6 Config Change via Web](#76-config-change-via-web)
  - [8. NVS Layout](#8-nvs-layout)
    - [Partition](#partition)
    - [Namespace: `esport_cfg`](#namespace-esport_cfg)
    - [Namespace: `esport_log`](#namespace-esport_log)
    - [Namespace: `esport_dev`](#namespace-esport_dev)
    - [Namespace: `esport_ota`](#namespace-esport_ota)
  - [9. Event Bus](#9-event-bus)
  - [10. Factory Defaults \& NVS Recovery](#10-factory-defaults--nvs-recovery)
  - [11. Coding Conventions](#11-coding-conventions)

---

## 1. Overview

**esport-fi32** is an ESP32-C6 firmware that incentivises children's physical exercise by gating internet access behind time credits earned on a sports/exercise bike.

Key behaviour:

- The device permanently operates in **AP+STA** (simultaneous Access Point + Station) Wi-Fi mode with NAT so that devices connected to the reward Soft AP can reach the internet through the home network.
- The **reward Soft AP is always active from boot**, so children's devices remain connected at all times. Internet access for each registered device is gated individually: frames from a device are forwarded to the internet only while that device's per-device counter is greater than zero and the device is enabled.
- A GPIO interrupt counts mechanical pulses from the bike sensor. Each pulse adds `seconds_per_pulse` seconds to a **time counter** while a session is in progress.
- Once the exercise session has been active for `internet_gate_threshold_s` seconds (EARNING state), the accumulated pulse credits start counting down in real time. Pulses still add to the current rider's counter while EARNING is active.
- When the current rider's counter reaches 0, internet access is revoked for that device until more credits are earned.
- The **Device Registry** (up to 4 entries) stores one record per child's Wi-Fi device: MAC address, nickname, internet-time counter, enabled flag, and a throughput-gated sliding-window lock identical to the former global gate.
- A **current rider** selector on the config page binds one device slot to the exercise bike: earned credits go to that slot.
- Exercise sessions are detected and logged to NVS (non-volatile storage) as a ring buffer.
- Date/time is synchronised via SNTP at boot; a POSIX timezone string converts stored UTC timestamps to local time for display.
- A **configuration web portal** is always reachable via the home network (station IP) when connected, or via the reward AP (`192.168.5.1`) at all times.
- A **status dashboard** shows live state (per-device counters, AP status, session info, connected clients, NTP status) and session history.
- The firmware uses **semantic versioning** (`MAJOR.MINOR.PATCH`, e.g. `2.0.0`). The version string is set via `PROJECT_VER` in `CMakeLists.txt` and embedded in the firmware image via ESP-IDF's `esp_app_desc_t`. It is displayed in the status dashboard title as `ESPort-fi32 vX.Y.Z -- Status Dashboard`.

---

## 2. Hardware

| Item                   | Details                                                                 |
| ---------------------- | ----------------------------------------------------------------------- |
| MCU                    | ESP32-C6                                                                |
| Bike sensor input GPIO | **GPIO 10** (configurable at build time via `CONFIG_ESPORT_PULSE_GPIO`) |
| GPIO internal pull     | Pull-up (sensor contact closes to GND)                                  |
| GPIO active edge       | **Falling edge** (sensor closes → logic low pulse)                      |
| Buzzer output GPIO     | **GPIO 11** (configurable at build time via `CONFIG_ESPORT_BUZZER_GPIO`). Active buzzer, HIGH = on, LOW = off. |
| BOOT button GPIO       | **GPIO 9** (configurable at build time via `CONFIG_ESPORT_BOOT_BUTTON_GPIO`). Internal pull-up; active LOW. Used for physical password reset. |

> The GPIO number and active edge can be changed via Kconfig without changing source code.

---

## 3. Configuration Parameters

All parameters are stored at runtime in NVS and survive reboots. They are initialised from the factory defaults below if the NVS key is absent or the NVS partition is corrupt.

| Parameter                               | NVS Key        | Type   | Default         | Min | Max         | Description                                                                                                                                                                                                              |
| --------------------------------------- | -------------- | ------ | --------------- | --- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `wifi_ssid`                             | `wifi_ssid`    | string | `""`            | —   | 32 chars    | Home network SSID                                                                                                                                                                                                        |
| `wifi_password`                         | `wifi_pwd`     | string | `""`            | —   | 64 chars    | Home network WPA2 password                                                                                                                                                                                               |
| `soft_ap_ssid`                          | `ap_ssid`      | string | `"esport-fi32"` | —   | 32 chars    | Reward Soft AP SSID                                                                                                                                                                                                      |
| `soft_ap_password`                      | `ap_pwd`       | string | `"esport-fi32"` | —   | 64 chars    | Reward Soft AP WPA2 password                                                                                                                                                                                             |
| `seconds_per_pulse`                     | `spp`          | uint16 | `3`             | 1   | 60          | Seconds added to counter per valid pulse                                                                                                                                                                                 |
| `internet_gate_threshold_s`             | `inet_gate_s`    | uint32 | `300`           | 0   | (unlimited) | Session duration (s) before earned credits start counting down and internet access becomes available                                                                                                                                                                        |
| `centimeters_per_pulse`                 | `cpp`          | uint32 | `300`            | 1   | (unlimited) | Wheel travel per pulse (cm), used for speed                                                                                                                                                                              |
| `idle_session_interval_s`               | `idle_s`       | uint16 | `30`            | 5   | 600         | Gap (s) with no pulses that closes a session                                                                                                                                                                             |
| `start_session_interval_s`              | `start_s`      | uint16 | `10`            | 1   | 300         | Continuous pedalling (s) required to open a session                                                                                                                                                                      |
| `pulse_debounce_time_ms`                | `debounce_ms`  | uint16 | `10`            | 1   | 5000        | Minimum time (ms) between two accepted pulses                                                                                                                                                                            |
| `timezone`                              | `tz`           | string | `"UTC0"`        | —   | 63 chars    | POSIX TZ string (e.g. `"CET-1CEST,M3.5.0,M10.5.0/3"`)                                                                                                                                                                    |
| `soft_ap_dec_time_above_threshold_kbps` | `ap_thr_kbps`  | uint16 | `1`             | 0   | 65535       | Combined RX+TX throughput (kbps) below which the countdown is considered idle.                                                                                                                                           |
| `soft_ap_idle_throughput_timeout_s`     | `ap_idle_tmo`  | uint16 | `30`            | 0   | 65535       | Number of consecutive seconds that throughput must remain below the threshold before the countdown pauses.                                                                                                               |
| `min_speed_to_increment_time_kmh_x10`   | `min_spd_x10`  | uint16 | `30`            | 0   | 65535       | Minimum instantaneous speed in km/h × 10 required for a pulse to earn time credits.  Set to `0` to disable the gate.                                                                                                     |
| `reward_counter_s`                      | `reward_ctr_s` | uint32 | `0`             | 0   | (unlimited) | **Legacy / migration only.** Read once at boot by `time_ctr_init()` to seed the current rider's device-registry counter when that slot is still zero. No longer written by the firmware after Feature 4. |
| `buzzer_enabled`                        | `buzzer_en`    | uint8  | `1` (true)      | 0   | 1           | Enable/disable all buzzer audio feedback.  When `0` (false), all `buzzer_*` calls are no-ops and the GPIO stays LOW. |
| `low_speed_buzzer_threshold_s`          | `bz_spd_thr_s` | uint16 | `3`             | 0   | 65535       | Number of consecutive seconds the speed must remain below the minimum before the speed-low buzzer beep begins.  Set to `0` for immediate feedback on the first below-threshold tick. |
| `config_password`                       | `cfg_pwd`      | string | `"esport-fi32"` | —   | 63 chars    | Password for HTTP Basic Auth on all `/config` endpoints.  Username is always `admin`.  Change via `POST /config/pwd`. |

---

## 4. System Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                          esport-fi32                              │
│                                                                   │
│  ┌─────────────┐   pulses  ┌──────────────────────────────────┐   │
│  │ Pulse Input │──────────▶│   Time Counter State Machine     │   │
│  │   Module    │           │  (credits + per-device gating)   │   │
│  └─────────────┘           └──────────────┬───────────────────┘   │
│                                           │ session events        │
│  ┌─────────────┐           ┌──────────────▼───────────────────┐   │
│  │ NVS Config  │◀─────────▶│        Session Tracker           │   │
│  │  Manager    │           └──────────────┬───────────────────┘   │
│  └─────────────┘                          │                       │
│                                           ▼                       │
│  ┌─────────────┐           ┌──────────────────────────────────┐   │
│  │  SNTP/Time  │           │       NVS Session Log            │   │
│  │  Manager    │           │       (ring buffer)              │   │
│  └─────────────┘           └──────────────────────────────────┘   │
│                                                                   │
│  ┌─────────────────────────┐   ┌──────────────────────────────┐   │
│  │     Device Registry     │   │        WiFi Manager          │   │
│  │  (per-device counter,   │◀─▶│  STA + Always-On SoftAP(s)   │   │
│  │   MAC filter, NVS)      │   │  NAT + per-MAC frame gate    │   │
│  └─────────────────────────┘   └──────────────────────────────┘   │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │                       HTTP Server                            │ │
│  │  GET /            status dashboard (HTML)                    │ │
│  │  GET /config      configuration form (HTML)                  │ │
│  │  POST /config     save & apply configuration                 │ │
│  │  POST /config/reset  reset configuration to factory defaults │ │
│  │  GET /api/status  live state (JSON)                          │ │
│  │  GET /api/sessions session history (JSON)                    │ │
│  │  GET /api/sessions/export download CSV/JSON reports          │ │
│  │  GET /api/sessions/daily graph-ready daily aggregates        │ │
│  └──────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────┘
```

### Component/File Layout

```
firmware/
  CMakeLists.txt
  Kconfig.projbuild          <- build-time defaults
  idf_component.yml
  inc/
    config_manager.h
    device_registry.h
    wifi_manager.h
    time_manager.h
    pulse_input.h
    time_counter.h
    session_tracker.h
    session_log.h
    http_server.h
    http_server_config.h
    http_server_ota.h
    http_server_utils.h
    buzzer.h
    button_reset.h
    event_ids.h
  src/
    main.c                   <- app_main, module init sequencing
    config_manager.c
    device_registry.c
    wifi_manager.c
    time_manager.c
    pulse_input.c
    time_counter.c
    session_tracker.c
    session_log.c
    http_server.c
    http_server_config.c
    http_server_api.c
    http_server_dashboard.c
    http_server_export.c
    http_server_ota.c
    http_server_utils.c
    buzzer.c
    button_reset.c
    ota_manager.c
```

---

## 5. Module Specifications

### 5.1 NVS Configuration Manager

**File:** `config_manager.c` / `config_manager.h`

**Responsibilities:**
- Open the NVS partition at startup. If the partition is corrupt (`ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`), erase the partition and reinitialise it (factory reset of settings only).
- Expose typed getters and setters for every parameter listed in §3.
- Apply factory defaults for any missing key.
- Commit writes to NVS immediately (synchronous).

**NVS Namespace:** `esport_cfg`

**API (C):**

```c
esp_err_t config_mngr_init(void);

/* Getters */
void     config_mngr_wifi_ssid_get(char *buf, size_t len);
void     config_mngr_wifi_password_get(char *buf, size_t len);
void     config_mngr_soft_ap_ssid_get(char *buf, size_t len);
void     config_mngr_soft_ap_password_get(char *buf, size_t len);
uint16_t config_mngr_seconds_per_pulse_get(void);
uint32_t config_mngr_internet_gate_threshold_s_get(void);
uint32_t config_mngr_centimeters_per_pulse_get(void);
uint16_t config_mngr_idle_session_interval_s_get(void);
uint16_t config_mngr_start_session_interval_s_get(void);
uint16_t config_mngr_pulse_debounce_time_ms_get(void);
void     config_mngr_timezone_get(char *buf, size_t len);

/* Setters (validate range, return ESP_ERR_INVALID_ARG on out-of-range) */
esp_err_t config_mngr_wifi_ssid_set(const char *val);
esp_err_t config_mngr_wifi_password_set(const char *val);
esp_err_t config_mngr_soft_ap_ssid_set(const char *val);
esp_err_t config_mngr_soft_ap_password_set(const char *val);
esp_err_t config_mngr_seconds_per_pulse_set(uint16_t val);
esp_err_t config_mngr_internet_gate_threshold_s_set(uint32_t val);
esp_err_t config_mngr_centimeters_per_pulse_set(uint32_t val);
esp_err_t config_mngr_idle_session_interval_s_set(uint16_t val);
esp_err_t config_mngr_start_session_interval_s_set(uint16_t val);
esp_err_t config_mngr_pulse_debounce_time_ms_set(uint16_t val);
esp_err_t config_mngr_timezone_set(const char *val);
uint16_t  config_mngr_soft_ap_dec_threshold_kbps_get(void);
esp_err_t config_mngr_soft_ap_dec_threshold_kbps_set(uint16_t val);
uint16_t  config_mngr_soft_ap_idle_throughput_timeout_s_get(void);
esp_err_t config_mngr_soft_ap_idle_throughput_timeout_s_set(uint16_t val);
uint16_t  config_mngr_min_speed_to_increment_time_kmh_x10_get(void);
esp_err_t config_mngr_min_speed_to_increment_time_kmh_x10_set(uint16_t val);
uint32_t  config_mngr_reward_counter_s_get(void);
esp_err_t config_mngr_reward_counter_s_set(uint32_t val);

/* Config page password (Feature 6) */
esp_err_t config_mngr_cfg_password_get(char *p_buf, size_t len);
esp_err_t config_mngr_cfg_password_set(const char *p_password);
bool      config_mngr_cfg_credentials_check(const char *p_password);
```

**Validation rules (setters reject values outside this range with `ESP_ERR_INVALID_ARG`):**

| Parameter                   | Min     | Max        |
| --------------------------- | ------- | ---------- |
| `seconds_per_pulse`         | 1       | 60         |
| `internet_gate_threshold_s` | 0       | UINT32_MAX |
| `centimeters_per_pulse`     | 1       | UINT32_MAX |
| `idle_session_interval_s`   | 5       | 600        |
| `start_session_interval_s`  | 1       | 300        |
| `pulse_debounce_time_ms`    | 10      | 5000       |
| any SSID                    | 1 char  | 32 chars   |
| any password                | 0 chars | 64 chars   |
| `timezone`                  | 1 char  | 63 chars   |

---

### 5.2 WiFi Manager

**File:** `wifi_manager.c` / `wifi_manager.h`

**Responsibilities:**
- Initialise the WiFi subsystem in **AP+STA mode** from the first call.
- Manage two logical interfaces:
  1. **STA** - connects to the home network (`wifi_ssid` / `wifi_password`).
  2. **Reward SoftAP** - **always active from boot**; never torn down. `wifi_mngr_reward_ap_set(false)` is a no-op (logs and returns `ESP_OK` without changing state).
- Enable **IP_NAPT** on the AP netif so devices connected to the reward SoftAP can route traffic through the STA interface.
- Install **lwIP netif input and linkoutput hooks** on the reward AP netif to implement per-device internet access control:
  - **Input hook** (`wifi_mngr_ap_input_hook`): For each received Ethernet frame, call `device_reg_mac_rx_bytes_add()` to count RX bytes for the source MAC. Then apply the internet-access filter: two destination classes are **always** forwarded regardless of device registration: (1) subnet-local destinations (`192.168.5.0/24`) so unregistered devices can reach the gateway/dashboard; (2) limited broadcast (`255.255.255.255`) so DHCP Discover/Request frames are never dropped — without this, unregistered devices cannot obtain an IP address. All other IPv4 destinations (i.e., internet-bound traffic) are tested with `device_reg_mac_internet_allowed()`. If the source MAC is not allowed (device unregistered, disabled, or counter == 0), call `pbuf_free()` and return `ERR_OK` to silently discard the frame. Allowed frames are forwarded to the original input function.
  - **Output hook** (`wifi_mngr_ap_linkoutput_hook`): Call `device_reg_mac_tx_bytes_add()` to count TX bytes for the destination MAC, then forward to the original linkoutput function.
- Post ESP events on the application event loop to notify other modules of connectivity changes.

**Reward SoftAP:**
- SSID / password: from `config_manager`.
- Channel: follows the STA channel after STA connects; default channel 6 before STA connects.
- Max connected stations: 4.
- IP subnet: `192.168.5.0/24`, gateway `192.168.5.1`.
- **Always active from boot**; `wifi_mngr_reward_ap_set(false)` does nothing.
- Internet access for each client is controlled per-MAC by the Device Registry MAC filter in the input hook; being connected to the AP does not itself grant internet access.
- NAT is applied once at init and never removed.

**STA reconnection:**
- Retry indefinitely at 10-second intervals (not a configurable parameter; hardcoded).
- On each `WIFI_EVENT_STA_DISCONNECTED` event, schedule a reconnect attempt in 10 seconds.
- On `ESPORT_EVENT_CONFIG_CHANGED`, compare the newly saved `wifi_ssid` and `wifi_password` against the credentials currently loaded in the WiFi driver. If either changed:
  - If the STA is not connected, cancel any pending reconnect timer and call `esp_wifi_connect()` immediately with the new credentials.
  - If the STA is already connected, set an internal `immediate` flag and call `esp_wifi_disconnect()`. The subsequent `WIFI_EVENT_STA_DISCONNECTED` handler detects the flag, clears it, and reconnects immediately (no 10-second delay). Config changes that do not affect `wifi_ssid` or `wifi_password` (e.g. session timings, device nicknames) do not trigger any reconnect.
- After reconnection, post `ESPORT_EVENT_STA_CONNECTED` on the app event loop.

**API:**

```c
esp_err_t wifi_mngr_init(void);

/* Called by time_counter module */
esp_err_t wifi_mngr_reward_ap_set(bool enable);

/* Status queries */
bool     wifi_mngr_sta_is_connected(void);
bool     wifi_mngr_reward_ap_is_active(void);
uint8_t  wifi_mngr_reward_ap_client_count(void);
void     wifi_mngr_sta_ip_get(char *buf, size_t len);          /* dotted-decimal or "" */
void     wifi_mngr_reward_ap_ip_get(char *buf, size_t len);    /* dotted-decimal or "" when inactive */
uint32_t wifi_mngr_reward_ap_throughput_kbps(void);            /* combined RX+TX kbps over last 1-s interval; 0 when AP inactive or 0 clients */
```

---

### 5.3 SNTP / Time Manager

**File:** `time_manager.c` / `time_manager.h`

**Responsibilities:**
- Start SNTP synchronisation using `pool.ntp.org` (and `time.cloudflare.com` as secondary) once the STA interface has an IP.
- Apply the POSIX timezone string from `config_manager` via `setenv("TZ", tz, 1)` + `tzset()`.
- Re-apply timezone whenever the config is updated.
- Maintain a `time_synced` flag: set to `true` on the first successful synchronisation.
- If synchronisation fails or has never completed, timestamps are generated using `esp_timer_get_time()` as an offset from an epoch of `2000-01-01T00:00:00Z` (clearly distinct from valid UTC). The application must tag unsynced timestamps (see §8).

**API:**

```c
esp_err_t  time_mngr_init(void);
bool       time_mngr_is_synced(void);
time_t     time_mngr_utc_get(void);          /* seconds since Unix epoch */
void       time_mngr_timezone_apply(void);   /* call after timezone config change */
```

---

### 5.4 Pulse Input Module

**File:** `pulse_input.c` / `pulse_input.h`

**Responsibilities:**
- Configure `CONFIG_ESPORT_PULSE_GPIO` as input with internal pull-up.
- Install a GPIO interrupt on the **falling edge**.
- Implement software debounce: ignore any edge that arrives less than `pulse_debounce_time_ms` milliseconds after the previous accepted edge. Use `esp_timer_get_time()` for sub-millisecond resolution.
- For each accepted pulse, post an `ESPORT_EVENT_PULSE` event on the app event loop with no payload. The ISR inline payload is limited to 4 bytes; an `int64_t` µs timestamp does not fit. Handlers call `esp_timer_get_time()` directly — the sub-ms handler latency is negligible for all second-resolution consumers.
- Expose `pulse_in_total_count_get()` for diagnostic use.
- Compute and expose `pulse_in_speed_kmh_x10_get()` as the single source of truth for instantaneous speed, so that all consumers use a consistent formula and data source (the ISR-maintained inter-pulse interval).

**Notes:**
- The GPIO ISR must be minimal (set a flag / use `esp_event_isr_post()`). All business logic is handled in event callbacks outside the ISR.
- Debounce state is maintained in a static variable updated inside the ISR using `IRAM_ATTR`.

**Kconfig:**

```
CONFIG_ESPORT_PULSE_GPIO          int     default 6                  range 0 30
```

These are **build-time** constants set via `idf.py menuconfig`. They are not stored in NVS and cannot be changed at runtime.

**API:**

```c
esp_err_t pulse_in_init(void);
uint32_t  pulse_in_total_count_get(void);
uint32_t  pulse_in_last_interval_ms_get(void);   /* UINT32_MAX if fewer than 2 pulses accepted */
uint32_t  pulse_in_speed_kmh_x10_get(void);      /* cpp_cm * 360 / last_interval_ms; 0 when indeterminate */
```

---

### 5.5 Time Counter & Reward AP State Machine

**File:** `time_counter.c` / `time_counter.h`

**Responsibilities:**
- Maintain the **time counter** per device slot via the Device Registry (`device_reg_entry_counter_set/get`).
- Listen for `ESPORT_EVENT_PULSE` events: add `seconds_per_pulse` credits directly to the current rider's device-registry counter (NVS-persisted) in both SESSION and EARNING states. Pulses in IDLE state are ignored. Qualifying pulses are credited on `SESSION_OPENED` — see below.
- Listen for `ESPORT_EVENT_SESSION_OPENED` (payload: `uint32_t` qualifying pulse count): credit qualifying pulses directly to the device-registry counter. If `counter > 0` **and** the per-device internet gate lock is `false`, bypass the gate (IDLE->EARNING, post `ESPORT_EVENT_EARNING_STARTED`). Otherwise engage the gate lock, go IDLE->SESSION and start a one-shot `internet_gate_threshold_s` timer.
- Listen for `ESPORT_EVENT_SESSION_CLOSED`: if in SESSION state, cancel the timer, go to IDLE — gate lock remains set (internet blocked) but all credits are already in the device-registry counter (persisted). If in EARNING state, clear the gate lock, go to IDLE and post `ESPORT_EVENT_EARNING_STOPPED`.
- Run a **1-second periodic tick timer** that is started permanently in `time_ctr_init()` (never stopped). Each tick calls `device_reg_tick()` which handles per-device counter decrement, throughput gating, NVS save, and posts `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED`. After `device_reg_tick()`, maintain a `g_speed_low_ticks` counter: increment it when the speed-low condition holds (`g_state` is SESSION or EARNING, `min_speed > 0`, `0 < current_speed < min_speed`); reset it to zero otherwise. Call `buzzer_speed_low_update(true)` only when the speed-low condition holds **and** `g_speed_low_ticks >= low_speed_buzzer_threshold_s`; call `buzzer_speed_low_update(false)` otherwise. Then post `ESPORT_EVENT_COUNTER_CHANGED`.
- Post `ESPORT_EVENT_EARNING_STARTED` when the threshold timer fires (SESSION -> EARNING transition).
- Post `ESPORT_EVENT_EARNING_STOPPED` when the EARNING state exits to IDLE (session closed while earning).
- **Legacy migration:** `time_ctr_init()` reads `config_mngr_reward_counter_s_get()` and, if the returned value is non-zero and the current rider's counter is still zero, seeds the rider's counter with that value.
- **Runtime counter override:** `time_ctr_counter_set(val)` delegates to `device_reg_entry_counter_set(current_rider, val)`. Returns `ESP_ERR_INVALID_STATE` when no rider is selected (`DEVICE_REG_NO_RIDER`).

**State machine:**

```
        +------------------------------------------------------+
        |                   IDLE state                         |
        |  current rider counter may be non-zero (paused)      |
        |                                                      |
        |  On SESSION_OPENED: check counter & gate lock ->     |
        |    bypass if (counter>0 && lock=false)               |
        +------------------+-----------------------------------+
                           |  ESPORT_EVENT_SESSION_OPENED
                           v
        +------------------------------------------------------+
        |                SESSION state                         |
        |  Gate lock = true for this rider                     |
        |                                                      |
        |  On pulse:   rider counter += spp (NVS-persisted)    |
        |  time_ctr_get() = device_reg counter                 |
        |  On SESSION_CLOSED: cancel timer, -> IDLE            |
        |    (lock stays true; credits already in NVS)         |
        +------------------+-----------------------------------+
                           |  threshold timer fires
                           |  (internet_gate_threshold_s elapsed)
                           v
        +------------------------------------------------------+
        |                EARNING state                         |
        |  Gate lock = false; internet open per-device         |
        |                                                      |
        |  On pulse:  rider counter += spp (NVS-persisted)     |
        |  On tick:   device_reg_tick() decrements all enabled |
        |             unlocked devices with internet access    |
        +------------------+-----------------------------------+
                           |  SESSION_CLOSED
                           v
                    back to IDLE state
```

**Thread safety:** All counter access is via the Device Registry spinlock (`g_dev_mux`). The tick callback runs in the ESP timer task and is safe to call from any context.

**Speed-gated pulse crediting:** When an `ESPORT_EVENT_PULSE` is received, the instantaneous speed is obtained from `pulse_in_speed_kmh_x10_get()` -- the single source of truth owned by the Pulse Input module:

```
speed_x10 = pulse_in_speed_kmh_x10_get()   /* 0 when fewer than 2 pulses accepted */
min_spd   = config_mngr_min_speed_to_increment_time_kmh_x10_get()

if min_spd > 0 and speed_x10 < min_spd:
    skip credit addition (pulse still counted for session tracking)
```

- `min_spd = 0` disables the gate entirely (all pulses earn credits, original behaviour).
- The first pulse of a session (`UINT32_MAX` interval) never earns credits when `min_spd > 0`.
- `ESPORT_EVENT_COUNTER_CHANGED` is still posted for every accepted pulse.

**API:**

```c
esp_err_t time_ctr_init(void);
uint32_t  time_ctr_get(void);                        /* current rider's device_reg counter + g_session_credits (SESSION state only); 0 if no rider */
uint32_t  time_ctr_current_speed_x10_get(void);      /* most recent instantaneous speed in km/h x 10; 0 when idle */
esp_err_t time_ctr_counter_set(uint32_t val);        /* delegates to device_reg; ESP_ERR_INVALID_STATE if no rider */
```

---

### 5.6 Session Tracker

**File:** `session_tracker.c` / `session_tracker.h`

**Responsibilities:**
- Listen for `ESPORT_EVENT_PULSE` events.
- Detect and track exercise sessions using two phases:

**Phase 1 — Qualification window:**
- On the first pulse after idle, record `potential_start_time` and `potential_start_pulse_count`.
- If pulses continue for `start_session_interval_s` seconds without a gap > `idle_session_interval_s`, the session is **confirmed** (opened). The session's `start_time` = `potential_start_time`.
- If a gap > `idle_session_interval_s` occurs during qualification, reset and wait for the next first pulse.

**Phase 2 — Active session:**
- Track total `pulse_count` and `last_pulse_time`.
- Run an idle-detection timer (FreeRTOS timer, period = `idle_session_interval_s` seconds). Reset the timer on each pulse.
- When the idle timer fires (no pulse for `idle_session_interval_s` seconds), the session is closed:
  - `end_time` = `last_pulse_time` (the time of the last received pulse, NOT current time).
  - `duration_s` = `end_time - start_time`.
  - `avg_speed_kmh` = `(pulse_count * centimeters_per_pulse) / (duration_s * 100.0) * 3.6` (converts cm/s to km/h).
  - Post `ESPORT_EVENT_SESSION_CLOSED` with a `session_trk_record_t` payload.
  - Reset state to idle.

**Handling qualification pulses in session stats:** Pulses during the qualification window also count toward the confirmed session (pulse_count includes them all from potential_start).

**Handling qualification pulses in time credits:** When the session is confirmed (QUALIFYING→ACTIVE), `session_tracker` posts `ESPORT_EVENT_SESSION_OPENED` with a `uint32_t` payload containing the qualifying pulse count. `time_counter` seeds `g_session_credits = qualifying_pulses * seconds_per_pulse` so that earned credits include the entire qualification effort. If the session fails qualification (idle gap before confirmation), the credits are discarded — no event is posted.

**Re-read config on each session start** (re-read `start_session_interval_s`, `idle_session_interval_s`, `centimeters_per_pulse` from config_manager so changes apply to the next session without requiring a reboot).

**Buzzer feedback** (calls into the Buzzer Module, §5.10):
- ST_IDLE → ST_QUALIFYING: `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFYING)` (250 ms beep).
- ST_QUALIFYING → ST_ACTIVE: `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFIED)` (500 ms beep).
- ST_ACTIVE → ST_IDLE (idle timeout): `buzzer_pattern_play(BUZZER_PATTERN_SESSION_CLOSED)` (3 × 100 ms beeps).
- ST_QUALIFYING → ST_IDLE (qualifying gap): no buzzer call.

**API:**

```c
esp_err_t session_trk_init(void);

typedef struct {
    int64_t  start_time_utc;       /* Unix timestamp, 0 if unsynced */
    bool     time_synced;          /* true if system clock was synced at session start */
    uint32_t duration_s;
    uint32_t pulse_count;
    uint16_t avg_speed_kmh_x10;    /* km/h * 10 to avoid float, e.g. 123 = 12.3 km/h */
    uint32_t internet_earned_s;    /* pulse_count * seconds_per_pulse (cached at session start) */
} session_trk_record_t;
```

---

### 5.7 NVS Session Log

**File:** `session_log.c` / `session_log.h`

**Responsibilities:**
- Listen for `ESPORT_EVENT_SESSION_CLOSED` events and persist the `session_trk_record_t` payload to NVS.
- Implement a **ring buffer** backed by NVS:
  - Maximum entries: `SESSION_LOG_MAX_ENTRIES` = 50 (compile-time constant).
  - Keys: `slog_head` (uint16, write index), `slog_count` (uint16), and `slog_N` (blob, where N is 0–49).
  - On write: store at index `head`, advance `head = (head + 1) % SESSION_LOG_MAX_ENTRIES`, increment count (cap at max).
  - Old entries are overwritten when the buffer is full.
- Expose a read function that returns sessions in reverse chronological order (newest first).
- On NVS corruption of log namespace, erase namespace and start fresh.

**NVS Namespace:** `esport_log`

**API:**

```c
esp_err_t session_log_init(void);
esp_err_t session_log_write(const session_trk_record_t *rec);
uint16_t  session_log_count(void);

/* Returns up to `max_count` sessions, newest first. Returns actual count written. */
uint16_t  session_log_read(session_trk_record_t *out, uint16_t max_count);
```

---

### 5.8 HTTP Server

**File:** `http_server.c` / `http_server.h`

**Responsibilities:**
- Start an `esp_http_server` instance on port 80.
- Register the routes listed in §6 (including OTA routes from `http_server_ota.c/h`).
- The server runs regardless of which network interface is active; it is reachable on all active IPs.
- Parse and validate POST body (URL-encoded form data) for the config endpoint. Reject malformed or out-of-range values with HTTP 400 and a human-readable error message.
- After a successful config save, post `ESPORT_EVENT_CONFIG_CHANGED`. The WiFi Manager handles any STA reconnect automatically if `wifi_ssid` or `wifi_password` changed.
- Provide session export endpoints (CSV and JSON) with `Content-Disposition: attachment` so browsers download report files.
- Provide a daily-aggregate JSON endpoint for chart rendering in the Web UI.
- Provide OTA firmware update endpoints (§6.7) delegated to `http_server_ota.c/h`.

**File:** `http_server_ota.c` / `http_server_ota.h`

**Responsibilities:**
- Implement the four OTA URI handlers: `GET /ota`, `POST /ota`, `GET /ota/pwd`,
  `POST /ota/pwd`.
- Enforce HTTP Basic Auth on all four routes (username `admin`, password stored in NVS
  namespace `esport_ota`).

**API:**

```c
esp_err_t http_srv_init(void);
```

---

### 5.9 Device Registry

**File:** `device_registry.c` / `device_registry.h`

**Responsibilities:**
- Store up to `DEVICE_REG_MAX_ENTRIES` (4) device records in NVS, each containing: 6-byte MAC address, 15-char nickname, 32-bit internet-time counter (seconds), enabled flag, and a per-device throughput sliding-window state.
- Track a **current rider** index (`uint8_t`; `DEVICE_REG_NO_RIDER = 0xFF` when unset) persisted in NVS.
- Expose `device_reg_mac_internet_allowed(mac)` called from the WiFi Manager input hook to gate IPv4 forwarding per source MAC. Returns `true` if the MAC matches an enabled entry with `counter_s > 0` that is not paused by the throughput gate.
- Accept per-MAC byte counts from the WiFi Manager (`device_reg_mac_rx_bytes_add`, `device_reg_mac_tx_bytes_add`) and aggregate them into a per-second throughput figure updated on each `device_reg_tick()` call.
- Implement `device_reg_tick()` called once per second from `time_ctr_tick_cb()`. For each device slot:
  - Drain and reset the RX/TX byte accumulators (they accumulate even from background AP netif traffic such as DHCP or ARP probes, so they must be cleared every tick regardless of connection status).
  - Check whether the device's MAC appears in the current AP station list (connection check).
  - **Not connected:** set `throughput_kbps = 0`, reset `below_ticks` to 0, mark as paused, and skip to the next entry. This prevents background AP netif traffic from producing phantom throughput readings for devices that are not associated with the reward AP.
  - **Connected:** compute `throughput_kbps` from the drained byte delta, then apply the sliding-window traffic gate:
    ```
    if throughput > dec_threshold:
        below_ticks[i] = 0; paused[i] = false
    else:
        below_ticks[i]++
        if timeout == 0 or below_ticks[i] >= timeout:
            paused[i] = true
    if not paused[i] and enabled and counter_s > 0:
        counter_s--
    ```
  - Trigger a NVS save when `counter_s` reaches 0 or every `DEVICE_REG_SAVE_INTERVAL_S` (60) seconds.
  - Post `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` after each tick.
- `device_reg_entry_counter_set()` updates the counter **in RAM only**; it does not write to NVS. Persistence is deferred to the `device_reg_tick()` save policy above to avoid FLASH wear.
- Persist entries as NVS blobs (`dev_0` ... `dev_3`), count as `dev_count` uint8, and rider index as `dev_rider` uint8 in namespace `esport_dev`.

**Data model:**

```c
#define DEVICE_REG_MAX_ENTRIES      (4U)
#define DEVICE_REG_MAC_LEN          (6U)
#define DEVICE_REG_NICKNAME_MAX_LEN (15U)
#define DEVICE_REG_NO_RIDER         (0xFFU)
#define DEVICE_REG_SAVE_INTERVAL_S  (60U)

typedef struct device_reg_entry_tag
{
    uint8_t  mac[DEVICE_REG_MAC_LEN];
    char     nickname[DEVICE_REG_NICKNAME_MAX_LEN + 1U];
    uint32_t counter_s;
    bool     b_enabled;
} device_reg_entry_t;
```

**NVS Namespace:** `esport_dev`

**Thread safety:** All read/write access to the entry array, counters, and rider index is protected by a `portMUX_TYPE` spinlock (`g_dev_mux = portMUX_INITIALIZER_UNLOCKED`). `device_reg_mac_rx_bytes_add` and `device_reg_mac_tx_bytes_add` update `volatile` byte accumulators atomically (single 32-bit writes on RISC-V are naturally atomic, but the spinlock is held for the array scan to avoid TOCTOU during add/remove).

**API:**

```c
esp_err_t device_reg_init(void);

/* Entry management */
esp_err_t device_reg_entry_add(const uint8_t *p_mac, const char *p_nickname);
esp_err_t device_reg_entry_remove(uint8_t idx);
uint8_t   device_reg_entry_count(void);
esp_err_t device_reg_entry_get(uint8_t idx, device_reg_entry_t *p_out);
esp_err_t device_reg_entry_nickname_set(uint8_t idx, const char *p_nickname);
esp_err_t device_reg_entry_enabled_set(uint8_t idx, bool b_enabled);
esp_err_t device_reg_entry_counter_set(uint8_t idx, uint32_t val);
uint32_t  device_reg_entry_counter_get(uint8_t idx);

/* Current rider */
uint8_t   device_reg_current_rider_get(void);
esp_err_t device_reg_current_rider_set(uint8_t idx);   /* DEVICE_REG_NO_RIDER to clear */

/* WiFi Manager hooks */
bool      device_reg_mac_internet_allowed(const uint8_t *p_mac);
void      device_reg_mac_rx_bytes_add(const uint8_t *p_mac, uint32_t bytes);
void      device_reg_mac_tx_bytes_add(const uint8_t *p_mac, uint32_t bytes);

/* Per-second tick (called from time_counter tick callback) */
void      device_reg_tick(void);

/* Runtime status (for HTTP API) */
bool      device_reg_entry_is_paused(uint8_t idx);
uint32_t  device_reg_entry_throughput_kbps(uint8_t idx);
bool      device_reg_entry_is_connected(uint8_t idx);
```

---

### 5.10 Buzzer Module

**File:** `buzzer.c` / `buzzer.h`

**Responsibilities:**
- GPIO output control for an active buzzer (`CONFIG_ESPORT_BUZZER_GPIO`, default GPIO 11, active HIGH).
- Non-blocking pattern playback via an `esp_timer` running at a 50 ms period (`BUZZER_UNIT_MS`).
- Four predefined beep patterns:

| Pattern ID                          | Trigger                              | Sequence                                          |
| ----------------------------------- | ------------------------------------ | ------------------------------------------------- |
| `BUZZER_PATTERN_SESSION_QUALIFYING` | ST_IDLE → ST_QUALIFYING              | 5 units ON (250 ms)                               |
| `BUZZER_PATTERN_SESSION_QUALIFIED`  | ST_QUALIFYING → ST_ACTIVE            | 10 units ON (500 ms)                              |
| `BUZZER_PATTERN_SESSION_CLOSED`     | ST_ACTIVE → ST_IDLE (idle timeout)   | 2 ON, 1 OFF, 2 ON, 1 OFF, 2 ON (3 beeps)        |
| `BUZZER_PATTERN_SPEED_LOW`          | Per tick: SESSION or EARNING, speed below min for ≥ `low_speed_buzzer_threshold_s` consecutive ticks   | 2 units ON (100 ms)                               |
| `BUZZER_PATTERN_PASSWORD_RESET`     | BOOT button held for reset duration  | 50 units ON (2.5 s continuous beep)               |

- **Interruption rule:** a new `buzzer_pattern_play()` call immediately interrupts the current pattern and starts the new one.  `buzzer_speed_low_update(false)` only stops a `SPEED_LOW` pattern; it does not interrupt other patterns.
- Runtime enable/disable via `config_mngr_buzzer_enabled_get()`.  When disabled, all API calls are no-ops and the GPIO stays LOW.

**Thread safety:** all playback state is protected by a `portMUX_TYPE` spinlock.  The timer callback is O(1), performs only GPIO writes, and is safe in the `esp_timer` task.  `buzzer_pattern_play()` and `buzzer_speed_low_update()` are safe to call from any task (app event loop, FreeRTOS timer daemon, `esp_timer` callback).

**API:**

```c
esp_err_t buzzer_init(void);
void      buzzer_pattern_play(buzzer_pattern_id_t pattern);
void      buzzer_stop(void);
void      buzzer_speed_low_update(bool b_active);
```

---

### 5.11 Button Reset Module

**File:** `button_reset.c` / `button_reset.h`

**Responsibilities:**
- Monitor the BOOT button (`CONFIG_ESPORT_BOOT_BUTTON_GPIO`, default GPIO 9, active LOW, internal pull-up) via a 100 ms periodic `esp_timer`.
- When the button is held continuously for `CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S` seconds (default 5 s), reset both the config page password and the OTA password to their factory defaults (`"esport-fi32"`).
- Play `BUZZER_PATTERN_PASSWORD_RESET` (2.5 s continuous beep) to confirm the reset.
- Prevent re-triggering: after a reset fires, the reset flag is cleared only when the button is released (GPIO reads HIGH).
- Both Kconfig symbols (`CONFIG_ESPORT_BOOT_BUTTON_GPIO` and `CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S`) are user-configurable via `menuconfig`.

**API:**

```c
esp_err_t btn_rst_init(void);
```

Must be called after `buzzer_init()`, `config_mngr_init()`, and `ota_mngr_init()`.

---

## 6. Web Interface

### 6.1 Status Dashboard — `GET /`

Serves a self-contained HTML page (generated as chunked C string literals). All live fields are updated in-place every 2 seconds by a JavaScript `fetch('/api/status')` polling loop (`setInterval`, 2 000 ms), which fires once immediately on page load. There is no `<meta http-equiv="refresh">` full-page reload; the session history table and SVG graphs are static until the user manually refreshes the page.

**Page title:** `ESPort-fi32 vX.Y.Z -- Status Dashboard`, where `vX.Y.Z` is read at runtime from `esp_app_get_description()->version` (set by `PROJECT_VER` in `CMakeLists.txt`).

**Displayed information:**

| Section          | Fields                                                                                                                                                                                                                                                                        |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| System           | Current local time, NTP sync status, uptime                                                                                                                                                                                                                                   |
| Wi-Fi            | STA status, home SSID, station IP, reward AP status, reward AP SSID, reward AP IP (dashboard access URL from the reward AP network), connected clients count, reward AP traffic (kbps)                                                                      |
| Current Session  | Current rider's counter (s + h:mm:ss), threshold, status (idle / qualifying / active), current speed (km/h, one decimal place), pulse-crediting status (Crediting / Gated (speed too low)), session duration                                                 |
| Session History  | Table of last 20 sessions: start (local time), duration (h:mm:ss), avg speed (km/h), pulse count, internet earned (h:mm:ss) |
| Session Graphs   | Bar charts with day-of-month on X axis: average speed and total session duration per day                                                                                                                                                                                      |

Dashboard requirements for reports:

- Include a **Devices** table updated every 2 seconds by the JS polling loop, showing one row per registered device: nickname (starred if current rider), counter (h:mm:ss), enabled status, connected status, throughput (kbps) + pause indicator, internet access active status.
- Include an **Export Reports** panel with two actions:
  - Download CSV
  - Download JSON
- Include a **Session Graphs** panel that renders two bar charts from `GET /api/sessions/daily`:
  - Daily average speed (km/h)
  - Daily total duration (minutes)
- Graph rendering must use native browser features only (inline SVG or Canvas). No external JS/CSS libraries.

### 6.2 Configuration Page — `GET /config`

Serves a form pre-populated with current config values.

**Fields:**

| Label                    | Input type | Parameter                   |
| ------------------------ | ---------- | --------------------------- |
| Home Wi-Fi SSID          | text       | `wifi_ssid`                 |
| Home Wi-Fi Password      | password   | `wifi_password`             |
| Reward AP SSID           | text       | `soft_ap_ssid`              |
| Reward AP Password       | password   | `soft_ap_password`          |
| Seconds per Pulse        | number     | `seconds_per_pulse`         |
| Internet Gate Threshold (s) | number     | `internet_gate_threshold_s` |
| Centimeters per Pulse    | number     | `centimeters_per_pulse`     |
| Idle Session Timeout (s) | number     | `idle_session_interval_s`   |
| Session Start Window (s) | number     | `start_session_interval_s`  |
| Pulse Debounce (ms)      | number     | `pulse_debounce_time_ms`    |
| Timezone (POSIX TZ)      | text       | `timezone`                  |
| Buzzer feedback          | checkbox   | `buzzer_enabled`            |

All `/config` endpoints are protected by HTTP Basic Auth (username `admin`, password stored
in NVS key `cfg_pwd`).  A "Change Config Password" link navigates to `GET /config/pwd`.

**Device Management section** (rendered after the base parameters):

| Element                   | Description                                                                                       |
| ------------------------- | ------------------------------------------------------------------------------------------------- |
| Registered Devices table  | One row per device: nickname input, MAC (read-only), counter h:mm:ss input (with `oninput` auto-format: strips non-digits, limits to 6 digits, inserts colons automatically as user types), enabled checkbox, current-rider radio, Remove button |
| No Rider radio            | Clears the current rider assignment (`DEVICE_REG_NO_RIDER`)                                        |
| Add Device sub-form       | MAC address text input (`AA:BB:CC:DD:EE:FF` or `AABBCCDDEEFF`), nickname text input, Add button  |

POST handling for device management fields:
- `current_rider`: sets current rider index or `DEVICE_REG_NO_RIDER` if value is `"255"`.
- `dev_N_remove`: removes device at index N.
- `dev_N_nickname`: updates nickname for device N.
- `dev_N_enabled`: checkbox; absence means `false`.
- `dev_N_counter_hms`: h:mm:ss counter value; parsed to seconds and applied via `device_reg_entry_counter_set()`.
- `action=add_device` + `new_dev_mac` + `new_dev_nickname`: adds a new device entry.

On submit: `POST /config` with `application/x-www-form-urlencoded` body.
On success: redirect to `/config?saved=1` with a success banner.
On error: redirect to `/config` with HTTP 400 error message.

**Reset to Defaults button:**

Below the save form a separate `<form method="POST" action="/config/reset">` renders a
"Reset to Factory Defaults" button styled in red.  Clicking the button triggers a
browser `confirm()` dialogue ("Reset ALL settings to factory defaults?\nThis cannot be
undone.") before submitting.  The POST body is empty; all logic is server-side.

### 6.2.1 Reset Configuration — `POST /config/reset`

Resets every configuration parameter to its factory default.

**Behaviour:**
1. Erase the entire `esport_cfg` NVS namespace with `nvs_erase_all()`.
2. Write all factory-default values and commit.
3. Call `time_mngr_timezone_apply()` to apply the default timezone immediately.
4. Post `ESPORT_EVENT_CONFIG_CHANGED` so all modules reload their cached values.
5. Redirect to `GET /config?reset=1`.

On redirect, `GET /config` renders a teal confirmation banner: "✓ Configuration reset to factory defaults."

**Firmware Update link:**

Below the reset form, a horizontal divider separates the admin actions. A "Firmware Update"
button-styled link navigates to `/ota` (§6.7). This is the primary entry point for OTA
updates.

On NVS failure: HTTP 500 Internal Server Error.

### 6.3 JSON Status API — `GET /api/status`

Returns JSON:

```json
{
  "fw_version": "2.0.0",
  "time_utc": 1741910400,
  "time_local": "2026-03-14T10:00:00",
  "time_synced": true,
  "uptime_s": 3600,
  "sta_connected": true,
  "sta_ssid": "HomeNetwork",
  "sta_ip": "192.168.1.42",
  "reward_ap_active": true,
  "reward_ap_ssid": "esport-fi32",
  "reward_ap_ip": "192.168.5.1",
  "reward_ap_clients": 2,
  "counter_s": 147,
  "inet_gate_threshold_s": 300,
  "session_state": "active",
  "session_start_utc": 1741905000,
  "session_duration_s": 5400,
  "session_pulse_count": 1800,
  "live_speed_kmh_x10": 123,
  "reward_ap_throughput_kbps": 42,
  "current_speed_kmh_x10": 0,
  "current_rider_idx": 0,
  "devices": [
    {
      "idx": 0,
      "nickname": "Alice",
      "mac": "AA:BB:CC:DD:EE:FF",
      "counter_s": 147,
      "counter_hms": "0:02:27",
      "enabled": true,
      "internet_active": true,
      "is_current_rider": true,
      "connected": true,
      "throughput_kbps": 42,
      "paused": false
    }
  ]
}
```

### 6.4 JSON Sessions API — `GET /api/sessions`

Returns JSON array (max 50 entries, newest first):

```json
[
  {
    "start_utc": 1741905000,
    "start_local": "2026-03-14T08:30:00",
    "time_synced": true,
    "duration_s": 5400,
    "pulse_count": 1800,
    "avg_speed_kmh_x10": 123,
    "internet_earned_s": 5400
  }
]
```

### 6.5 Sessions Export API — `GET /api/sessions/export`

Exports session reports as downloadable files.

**Query parameter:**

- `format=csv` or `format=json`
- Default when omitted: `csv`

**Response behavior:**

- `format=csv`
  - Content-Type: `text/csv`
  - Content-Disposition: `attachment; filename="esport-fi32-sessions-YYYYMMDD.csv"`
- `format=json`
  - Content-Type: `application/json`
  - Content-Disposition: `attachment; filename="esport-fi32-sessions-YYYYMMDD.json"`
- Invalid `format` value:
  - HTTP 400 with error body: `{"error":"invalid format"}`

**CSV columns (header row):**

`start_utc,start_local,time_synced,duration_s,pulse_count,avg_speed_kmh,distance_m,internet_earned_s`

Where:

- `avg_speed_kmh` is decimal (`avg_speed_kmh_x10 / 10.0`)
- `distance_m = (pulse_count * centimeters_per_pulse) / 100.0`
- `internet_earned_s` is the pre-computed internet time earned this session

### 6.6 Daily Aggregates API (Graphs) — `GET /api/sessions/daily`

Returns graph-ready aggregates grouped by local calendar day for the most recent 31 days.

**Response JSON:**

```json
{
  "days": [
    {
      "day": "2026-03-01",
      "day_of_month": 1,
      "sessions": 2,
      "avg_speed_kmh_x10": 118,
      "total_duration_s": 4200
    }
  ]
}
```

Aggregation rules:

- Group by `start_local` date (timezone-aware local date).
- `avg_speed_kmh_x10` is the arithmetic mean of session average speeds for that day (0 if no sessions).
- `total_duration_s` is the sum of session durations for that day.
- Days with no sessions are included with `sessions = 0`, `avg_speed_kmh_x10 = 0`, `total_duration_s = 0` so the chart has a stable X axis.

---

### 6.7 Firmware Update — `GET /ota`, `POST /ota`

Protected by HTTP Basic Auth on all four OTA routes.

| Route           | Auth | Description                                         |
| --------------- | ---- | --------------------------------------------------- |
| `GET /ota`      | Yes  | HTML upload page showing running firmware version   |
| `POST /ota`     | Yes  | Receive `.bin`, flash inactive slot, reboot         |
| `GET /ota/pwd`  | Yes  | OTA password change form                           |
| `POST /ota/pwd` | Yes  | Save new OTA password to NVS                       |

**Credentials:**
- Username: `"admin"` (hardcoded constant `OTA_MNGR_HTTP_USERNAME`).
- Password: configurable via `POST /ota/pwd`, stored in NVS namespace `esport_ota` key
  `ota_pwd`. Default: `"esport-fi32"`.

### 6.8 Config Password Change — `GET /config/pwd`, `POST /config/pwd`

Protected by HTTP Basic Auth (same credentials as `/config`).

| Route              | Auth | Description                                              |
| ------------------ | ---- | -------------------------------------------------------- |
| `GET /config/pwd`  | Yes  | HTML form: current password, new password, confirm       |
| `POST /config/pwd` | Yes  | Validate + save new config page password; redirect on success |

`POST /config/pwd` validates the current password via `config_mngr_cfg_credentials_check()`,
checks new/confirm match and length (1-63 chars), then calls `config_mngr_cfg_password_set()`.
Returns HTTP 400 on any validation failure; redirects to `GET /config/pwd?saved=1` on success.

**`GET /ota`** returns an HTML page showing the running firmware version and a file-input
form for uploading a new `.bin` image. Browser-side JavaScript uploads the file via
`XMLHttpRequest` with a progress bar.

**`POST /ota`** receives an `application/octet-stream` body (the `.bin` file), writes it
to the inactive OTA slot incrementally using `esp_ota_ops`, verifies the image header, sets
the new boot partition, and reboots. The browser receives HTTP 200 `"OK"` just before the
reboot.

On reboot, the bootloader loads the new image. `ota_mngr_init()` marks it valid at boot
step 2c. If the device crashes before reaching `ota_mngr_init()`, the bootloader
automatically rolls back to the previous slot.

**`GET /ota/pwd`** returns an HTML form with three password fields: current, new, and
confirm. Accepts `?saved=1` query parameter to display a success banner.

**`POST /ota/pwd`** validates the current password, checks new/confirm match, and persists
the new password to NVS namespace `esport_ota`.

---

## 7. System Behaviour Sequences

### 7.1 Boot Sequence

```
1. nvs_flash_init()
2. config_mngr_init()         <- load config, apply factory defaults
2a. device_reg_init()         <- load device registry from NVS (esport_dev namespace)
2b. buzzer_init()             <- configure buzzer GPIO; create pattern timer
2c. btn_rst_init()            <- configure BOOT button GPIO; start 100 ms poll timer
2d. ota_mngr_init()           <- mark firmware valid (cancel rollback); open esport_ota namespace
3. esp_event_loop_create_default()
4. wifi_mngr_init()           <- start AP+STA; reward AP always-on from init
   a. if wifi_ssid is empty: skip STA connection (reward AP still starts)
   b. otherwise: attempt STA connection; STA keeps retrying every 10 s
      in the background until it gets an IP
5. http_srv_init()            <- start web server (reachable via reward AP at 192.168.5.1)
6. time_mngr_init()           <- register callback: sync SNTP on STA_GOT_IP
7. pulse_in_init()
8. time_ctr_init()            <- start permanent tick timer; legacy migration from esport_cfg
9. session_trk_init()
10. session_log_init()
```

### 7.2 STA Connection Flow

```
app_main  →  wifi_manager attempts to connect to wifi_ssid
          →  STA_DISCONNECTED (first failed attempt or any later drop)
          →  wifi_manager schedules reconnect in 10 s (loops indefinitely)

          →  STA connects → STA_GOT_IP event
          →  time_manager starts SNTP sync
          →  (reward AP running independently; always-on)

          →  STA disconnects again → STA_DISCONNECTED event
          →  wifi_manager schedules reconnect in 10 s
```

### 7.3 Pulse & Counter Flow

```
Bike sensor -> falling edge on GPIO 10
            -> ISR: check debounce (esp_timer_get_time)
            -> if valid: post ESPORT_EVENT_PULSE

ESPORT_EVENT_PULSE
  -> time_counter: if state == SESSION or EARNING: rider counter += spp (NVS-persisted)
  -> session_tracker: update pulse count, last_pulse_time, manage timers

ESPORT_EVENT_SESSION_OPENED (posted by session_tracker on QUALIFYING->ACTIVE)
  -> time_counter: credit qualifying_pulses * spp to rider counter
                   if rider counter > 0 AND gate lock == false:
                     IDLE->EARNING (gate bypass)
                     post ESPORT_EVENT_EARNING_STARTED
                   else:
                     set gate lock = true for this rider
                     IDLE->SESSION
                     start one-shot threshold timer
                    (internet_gate_threshold_s seconds)

threshold timer fires
  -> time_counter: SESSION->EARNING
                   clear gate lock for this rider
                   post ESPORT_EVENT_EARNING_STARTED

SESSION_CLOSED while in SESSION state:
  -> time_counter: SESSION->IDLE
                   cancel threshold timer
                   gate lock stays true (cleared on boot or next gate completion)
```

### 7.4 Counter Decrement & Internet Gate

```
1-second periodic tick (permanent; runs in all states)
  -> device_reg_tick(): for each connected+enabled device with counter_s > 0
       AND gate lock == false:
       compute throughput; apply sliding-window gate; decrement if not paused
  -> post ESPORT_EVENT_COUNTER_CHANGED

On EARNING->IDLE (SESSION_CLOSED while in EARNING state):
  -> clear gate lock for this rider
  -> post ESPORT_EVENT_EARNING_STOPPED
  -> state = IDLE
```

### 7.5 Session Lifecycle

```
First pulse after idle
  → potential_start_time = now, potential_pulse_count = 1
  → start qualification timer (start_session_interval_s)

Subsequent pulses within idle_session_interval_s
  → reset idle detection timer
  → increment pulse count

Qualification timer fires (start_session_interval_s elapsed without long gap)
  → session CONFIRMED (opened)
  → session.start_time = potential_start_time

Idle detection timer fires (idle_session_interval_s without pulse)
  → if session is open:
      session.end_time = last_pulse_time
      session.duration_s = end_time - start_time
      compute avg_speed_kmh_x10
      post ESPORT_EVENT_SESSION_CLOSED
  → reset all state

ESPORT_EVENT_SESSION_CLOSED
  → session_log_write(record)
```

### 7.6 Config Change via Web

```
POST /config
  -> http_server validates all fields
  -> calls config_mngr_*_set() for each field
  -> if wifi_ssid or wifi_password changed:
      schedule wifi_manager reconnect after 1 s
  -> if timezone changed:
      time_mngr_timezone_apply()
  -> device management fields (add/remove/nickname/enabled/counter/rider) applied
      immediately via device_registry API calls
  -> redirect to GET /config with success message

POST /config/reset
  -> config_mngr_reset_to_defaults()
      nvs_erase_all() on esport_cfg namespace
      write all factory defaults and commit
  -> time_mngr_timezone_apply()  (apply default TZ immediately)
  -> post ESPORT_EVENT_CONFIG_CHANGED
  -> redirect to GET /config?reset=1
```

---

## 8. NVS Layout

### Partition

Custom NVS partition (`nvs`, 0x9000, **80 KB**) defined in `partitions.csv`.
`otadata` and `phy_init` are packed immediately before the 64 KB-aligned `ota_0` boundary
so that no flash byte goes unused across the full 4 MB device.

### Namespace: `esport_cfg`

| Key            | Type   | Content                               |
| -------------- | ------ | ------------------------------------- |
| `wifi_ssid`    | string | Home SSID                             |
| `wifi_pwd`     | string | Home password                         |
| `ap_ssid`      | string | Reward AP SSID                        |
| `ap_pwd`       | string | Reward AP password                    |
| `spp`          | uint16 | seconds_per_pulse                     |
| `inet_gate_s`  | uint32 | internet_gate_threshold_s             |
| `cpp`          | uint32 | centimeters_per_pulse                 |
| `idle_s`       | uint16 | idle_session_interval_s               |
| `start_s`      | uint16 | start_session_interval_s              |
| `debounce_ms`  | uint16 | pulse_debounce_time_ms                |
| `tz`           | string | POSIX TZ string                       |
| `ap_thr_kbps`  | uint16 | soft_ap_dec_time_above_threshold_kbps |
| `ap_idle_tmo`  | uint16 | soft_ap_idle_throughput_timeout_s     |
| `min_spd_x10`  | uint16 | min_speed_to_increment_time_kmh_x10   |
| `reward_ctr_s` | uint32 | reward_counter_s (persisted counter)  |
| `buzzer_en`    | uint8  | buzzer_enabled (0 = false, 1 = true)  |
| `bz_spd_thr_s` | uint16 | low_speed_buzzer_threshold_s          |
| `cfg_pwd`      | string | config page Basic Auth password (max 63 chars, default `"esport-fi32"`) |

### Namespace: `esport_log`

| Key                  | Type   | Content                        |
| -------------------- | ------ | ------------------------------ |
| `slog_head`          | uint16 | Next write index (0–49)        |
| `slog_count`         | uint16 | Number of valid entries (0–50) |
| `slog_0` … `slog_49` | blob   | `session_trk_record_t` binary  |

`session_trk_record_t` binary layout (20 bytes):

| Field               | Offset | Type       | Notes                                   |
| ------------------- | ------ | ---------- | --------------------------------------- |
| `start_time_utc`    | 0      | int64_t    | Unix timestamp; 0 = boot-epoch fallback |
| `time_synced`       | 8      | uint8_t    | 1 if synced                             |
| `_pad`              | 9      | uint8_t[1] | reserved                                |
| `duration_s`        | 10     | uint16_t   | session duration in seconds (max ~18 h) |
| `pulse_count`       | 12     | uint16_t   | total pulses (max 65535)                |
| `avg_speed_kmh_x10` | 14     | uint16_t   | km/h x 10 (e.g. 123 = 12.3 km/h)        |
| `internet_earned_s` | 16     | uint32_t   | pulse_count * seconds_per_pulse         |

> Total: 20 bytes x 50 entries = 1000 bytes plus ~50 bytes for metadata keys.

---

### Namespace: `esport_dev`

| Key              | Type   | Content                                                     |
| ---------------- | ------ | ----------------------------------------------------------- |
| `dev_count`      | uint8  | Number of registered devices (0-4)                          |
| `dev_rider`      | uint8  | Current rider index; `0xFF` = no rider                      |
| `dev_0`...`dev_3` | blob   | `device_reg_entry_t` binary (MAC + nickname + counter + enabled) |

---

### Namespace: `esport_ota`

| Key       | Type   | Content                                              |
| --------- | ------ | ---------------------------------------------------- |
| `ota_pwd` | string | OTA Basic Auth password (max 63 chars, default `"esport-fi32"`) |

---

## 9. Event Bus

All inter-module communication uses the default ESP event loop (`esp_event_loop_create_default`).

**Event base:** `ESPORT_EVENT_BASE`

| Event ID                        | Payload type           | Posted by            | Consumed by                       |
| ------------------------------- | ---------------------- | -------------------- | --------------------------------- |
| `ESPORT_EVENT_PULSE`            | none (NULL)            | `pulse_input`        | `time_counter`, `session_tracker` |
| `ESPORT_EVENT_COUNTER_CHANGED`  | `uint32_t` (counter_s) | `time_counter`       | `http_server` (status cache)      |
| `ESPORT_EVENT_EARNING_STARTED`  | —                      | `time_counter`       | (logging, status)                 |
| `ESPORT_EVENT_EARNING_STOPPED`  | —                      | `time_counter`       | (logging, status)                 |
| `ESPORT_EVENT_SESSION_OPENED`   | `uint32_t` (qualifying pulse count) | `session_tracker`    | `time_counter`                    |
| `ESPORT_EVENT_SESSION_CLOSED`   | `session_trk_record_t` | `session_tracker`    | `time_counter`, `session_log`     |
| `ESPORT_EVENT_STA_CONNECTED`    | —                      | `wifi_manager`       | `time_manager` (start SNTP)       |
| `ESPORT_EVENT_STA_DISCONNECTED` | —                      | `wifi_manager`       | (logging, status)                 |
| `ESPORT_EVENT_CONFIG_CHANGED`   | none (NULL)            | `http_server_config` | `pulse_input`, `time_counter`     |
| `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` | none (NULL) | `device_registry`    | `http_server` (status refresh)    |

`ESPORT_EVENT_CONFIG_CHANGED` is posted once at the end of a successful `POST /config` or `POST /config/reset` request.  Modules that cache NVS-backed config values subscribe to this event and re-read only the values they own, so configuration changes take effect immediately without a reboot.

---

## 10. Factory Defaults & NVS Recovery

**Defaults applied by `config_mngr_init`** when a key is absent or corrupt (see §3 table).

**NVS corruption handling:**
- `config_manager`: if `nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, call `nvs_flash_erase()` then `nvs_flash_init()` again. All config defaults are applied.
- `session_log`: if the log namespace cannot be opened or its metadata is inconsistent (`head > MAX` or `count > MAX`), erase the log namespace and reinitialise.

**No hardware factory-reset button.** Recovery is via the config web portal, accessible via the reward AP at `192.168.5.1` or via the home network STA IP.

**Physical password reset:** Holding the BOOT button (GPIO 9, configurable) for 5 seconds (configurable) resets both the config page and OTA passwords to `"esport-fi32"` and plays a 2.5 s buzzer confirmation beep.

---

## 11. Coding Conventions

- Language: **C11** (no C++).
- RTOS: **FreeRTOS** via ESP-IDF.
- Coding standard: **BARR-C:2018 (BARR-2018) is mandatory** for all source and header files.
- **No Unicode characters** in source files, header files, or commit messages. Use only 7-bit ASCII. Doxygen comments and log strings must be plain ASCII (use e.g. `->` instead of `→`, `>=` instead of `≥`, `(R)` instead of `®`).
- **Variable naming prefixes (BARR-C:2018):**
  - Variable names must be **entirely lowercase** (underscore-separated words). Only macros and constants defined with `#define` or `enum` use uppercase.
  - Pointer variables (including parameters) must start with `p_` (e.g. `char * p_buf`).
  - Boolean variables (including parameters) must start with `b_` (e.g. `bool b_enable`).
  - File-scope (`static`) and global variables must start with `g_`. Combined prefixes apply: global pointer → `gp_`, global boolean → `gb_` (e.g. `static const char * gp_tag`).
- **Yoda notation**: for `==` and `!=` comparisons, always place the constant (literal, macro, or enum value) on the **left-hand side** (e.g. `ESP_OK == ret`, `NULL != p_buf`).
- **Internal (private) functions** must be declared `static`. Every `static` function must have its prototype listed in the `// Internal Function Prototypes` section of the same `.c` file (as a bare declaration, without a Doxygen comment); the Doxygen comment belongs on the function **definition**.
- All modules expose an `_init()` function that must be called from `app_main` in the order specified in §7.1.
- No module calls another module's internals directly; all cross-module communication is via the event bus or explicit API calls.
- All `esp_err_t` return values must be checked; use `ESP_ERROR_CHECK()` for fatal initialisation failures and `ESP_LOGE` + graceful degradation for runtime errors.
- String inputs from HTTP POST bodies must be length-checked and null-terminated before being passed to `config_mngr_*_set()`.
- ISR functions must be declared `IRAM_ATTR` and kept minimal.
- Log tags: one `static const char * gp_tag` per `.c` file, initialised to the module name string (e.g. `"pulse_input"`).
- All timestamps stored and compared as UTC `time_t`; conversion to local time for display only.
- All public APIs, structs, enums, and macros must be documented with **Doxygen** comments.
- Use **Doxygen tags with `\\` prefix** (for example `\\file`, `\\brief`, `\\param`, `\\return`, `\\note`). Do not use `@` tags.
- Each `.h` and `.c` file must begin with a Doxygen file header (`\\file`, `\\brief`, `\\date`). The `\\file` tag takes no argument; Doxygen derives the filename automatically.
- Each public function must include at minimum: `\\brief`, one `\\param` per argument, and `\\return` when non-void.
- Each `\\param` must include direction as `\\param[in]`, `\\param[out]`, or `\\param[in,out]`.
- Doxygen blocks must include one blank line between the last `\\param...` line and `\\return` or `\\retval`.
- Every `typedef struct` and `typedef enum` must include a **tag name**: `typedef struct my_struct_tag { … } my_struct_t;` and `typedef enum my_enum_tag { … } my_enum_t;`.
- Every `enum` enumerator must have an **explicit integer value**: `MY_ENUM_FOO = 0`, `MY_ENUM_BAR = 1`, etc. Do not rely on implicit sequential assignment.
- Internal `static` functions should include at least a `\\brief` when the logic is non-trivial.
- **Single-line Doxygen comments** (`/** ... */` on one line) must **not** use `\\brief`.
- **Multi-line Doxygen comments**: `\\brief` must appear on the **second line** (immediately after the opening `/**` line). One blank `*` line must always follow the `\\brief` line before any additional content.
- The closing `*/` of a Doxygen block must always appear **alone on its own line**.
- All `.h` files must follow the **`hhtemplate`** structure and all `.c` files must follow the **`cctemplate`** structure defined in `.vscode/esport-fi32.code-snippets`. This mandates: `//===` section separators, `extern "C"` with the brace on the next line, `#endif // GUARD` comment style, and a `/*** end of file ***/` marker at the bottom of every file.
- In source files, the `//--------------------------------------------------------------------------------------------------` function separator (from `cctemplate`) must appear **after every function definition**. The closing `}` of every function is followed by a blank line, then the `//--------------------------------------------------------------------------------------------------` separator line, then a blank line, before the next function or section divider.
- In Doxygen comments, reference **project-defined** types, functions, macros, and enum values using the `#` prefix (e.g. `#session_trk_record_t`, `#ESPORT_EVENT_PULSE`, `#config_mngr_init()`). This enables Doxygen to generate hyperlinks automatically. Use `\c` for external identifiers (ESP-IDF, C standard library, POSIX) and for plain code tokens that are not project symbols.

Required Doxygen file-header format:

```c
/**
 * \file
 * \brief <brief_description>
 *
 * <long_description>
 *
 * \date YYYY-MM-DD
 */
```

Required Doxygen function format:

```c
/**
 * \brief <brief_description>
 *
 * <long_description>
 *
 * \param[in] param1 <param_description>
 * \param[in,out] param2 <param_description>
 *
 * \return <return_description>
 */
```
