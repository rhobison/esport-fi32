# esport-fi32 Firmware Specification

**Version:** 1.0
**Date:** 2026-03-14
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
  - [6. Web Interface](#6-web-interface)
    - [6.1 Status Dashboard — `GET /`](#61-status-dashboard--get-)
    - [6.2 Configuration Page — `GET /config`](#62-configuration-page--get-config)
    - [6.3 JSON Status API — `GET /api/status`](#63-json-status-api--get-apistatus)
    - [6.4 JSON Sessions API — `GET /api/sessions`](#64-json-sessions-api--get-apisessions)
    - [6.5 Sessions Export API — `GET /api/sessions/export`](#65-sessions-export-api--get-apisessionsexport)
    - [6.6 Daily Aggregates API (Graphs) — `GET /api/sessions/daily`](#66-daily-aggregates-api-graphs--get-apisessionsdaily)
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
  - [9. Event Bus](#9-event-bus)
  - [10. Factory Defaults \& NVS Recovery](#10-factory-defaults--nvs-recovery)
  - [11. Coding Conventions](#11-coding-conventions)

---

## 1. Overview

**esport-fi32** is an ESP32-C6 firmware that incentivises children's physical exercise by gating internet access behind time credits earned on a sports/exercise bike.

Key behaviour:

- The device permanently operates in **AP+STA** (simultaneous Access Point + Station) Wi-Fi mode with NAT so that devices connected to the reward Soft AP can reach the internet through the home network.
- A GPIO interrupt counts mechanical pulses from the bike sensor. Each pulse adds `seconds_per_pulse` seconds to a **time counter**.
- Once the time counter reaches `soft_ap_start_threshold_s`, a **reward Soft AP** is created and the counter starts counting down in real time. Pulses still add to the counter while the AP is active.
- When the counter reaches 0 the reward Soft AP is disabled.
- Exercise sessions are detected and logged to NVS (non-volatile storage) as a ring buffer.
- Date/time is synchronised via SNTP at boot; a POSIX timezone string converts stored UTC timestamps to local time for display.
- A **configuration web portal** is always reachable: via the home network (station IP) when connected, or via a dedicated fallback config AP (`esport-fi32_config`) when STA connection is unavailable.
- A **status dashboard** shows live state (counter, AP status, session info, connected clients, NTP status) and session history.

---

## 2. Hardware

| Item                   | Details                                                                |
| ---------------------- | ---------------------------------------------------------------------- |
| MCU                    | ESP32-C6                                                               |
| Bike sensor input GPIO | **GPIO 6** (configurable at build time via `CONFIG_ESPORT_PULSE_GPIO`) |
| GPIO internal pull     | Pull-up (sensor contact closes to GND)                                 |
| GPIO active edge       | **Falling edge** (sensor closes → logic low pulse)                     |

> The GPIO number and active edge can be changed via Kconfig without changing source code.

---

## 3. Configuration Parameters

All parameters are stored at runtime in NVS and survive reboots. They are initialised from the factory defaults below if the NVS key is absent or the NVS partition is corrupt.

| Parameter                   | NVS Key       | Type   | Default         | Min | Max         | Description                                           |
| --------------------------- | ------------- | ------ | --------------- | --- | ----------- | ----------------------------------------------------- |
| `wifi_ssid`                 | `wifi_ssid`   | string | `""`            | —   | 32 chars    | Home network SSID                                     |
| `wifi_password`             | `wifi_pwd`    | string | `""`            | —   | 64 chars    | Home network WPA2 password                            |
| `soft_ap_ssid`              | `ap_ssid`     | string | `"esport-fi32"` | —   | 32 chars    | Reward Soft AP SSID                                   |
| `soft_ap_password`          | `ap_pwd`      | string | `"esport-fi32"` | —   | 64 chars    | Reward Soft AP WPA2 password                          |
| `seconds_per_pulse`         | `spp`         | uint16 | `3`             | 1   | 60          | Seconds added to counter per valid pulse              |
| `soft_ap_start_threshold_s` | `ap_thresh`   | uint32 | `300`           | 0   | (unlimited) | Counter value (s) required to enable reward AP        |
| `centimeters_per_pulse`     | `cpp`         | uint32 | `25`            | 1   | (unlimited) | Wheel travel per pulse (cm), used for speed           |
| `idle_session_interval_s`   | `idle_s`      | uint16 | `30`            | 5   | 600         | Gap (s) with no pulses that closes a session          |
| `start_session_interval_s`  | `start_s`     | uint16 | `10`            | 1   | 300         | Continuous pedalling (s) required to open a session   |
| `pulse_debounce_time_ms`    | `debounce_ms` | uint16 | `200`           | 10  | 5000        | Minimum time (ms) between two accepted pulses         |
| `timezone`                  | `tz`          | string | `"UTC0"`        | —   | 63 chars    | POSIX TZ string (e.g. `"CET-1CEST,M3.5.0,M10.5.0/3"`) |

---

## 4. System Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                          esport-fi32                              │
│                                                                   │
│  ┌─────────────┐   pulses  ┌──────────────────────────────────┐   │
│  │ Pulse Input │──────────▶│   Time Counter State Machine     │   │
│  │   Module    │           │  (credits + reward AP control)   │   │
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
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │                      WiFi Manager                            │ │
│  │   STA (home network)  +  Reward SoftAP  +  Config SoftAP     │ │
│  │                       (NAT enabled)                          │ │
│  └──────────────────────────────────────────────────────────────┘ │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │                       HTTP Server                            │ │
│  │  GET /            status dashboard (HTML)                    │ │
│  │  GET /config      configuration form (HTML)                  │ │
│  │  POST /config     save & apply configuration                 │ │
│  │  GET /api/status  live state (JSON)                          │ │
│  │  GET /api/sessions session history (JSON)                    │ │
│  │  GET /api/sessions/export download CSV/JSON reports           │ │
│  │  GET /api/sessions/daily graph-ready daily aggregates         │ │
│  └──────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────┘
```

### Component/File Layout

```
firmware/
  CMakeLists.txt
  Kconfig.projbuild          ← build-time defaults
  idf_component.yml
  inc/
    config_manager.h
    wifi_manager.h
    time_manager.h
    pulse_input.h
    time_counter.h
    session_tracker.h
    session_log.h
    http_server.h
    event_ids.h
  src/
    main.c                   ← app_main, module init sequencing
    config_manager.c
    wifi_manager.c
    time_manager.c
    pulse_input.c
    time_counter.c
    session_tracker.c
    session_log.c
    http_server.c
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
esp_err_t config_manager_init(void);

/* Getters */
void     config_get_wifi_ssid(char *buf, size_t len);
void     config_get_wifi_password(char *buf, size_t len);
void     config_get_soft_ap_ssid(char *buf, size_t len);
void     config_get_soft_ap_password(char *buf, size_t len);
uint16_t config_get_seconds_per_pulse(void);
uint32_t config_get_soft_ap_start_threshold_s(void);
uint32_t config_get_centimeters_per_pulse(void);
uint16_t config_get_idle_session_interval_s(void);
uint16_t config_get_start_session_interval_s(void);
uint16_t config_get_pulse_debounce_time_ms(void);
void     config_get_timezone(char *buf, size_t len);

/* Setters (validate range, return ESP_ERR_INVALID_ARG on out-of-range) */
esp_err_t config_set_wifi_ssid(const char *val);
esp_err_t config_set_wifi_password(const char *val);
esp_err_t config_set_soft_ap_ssid(const char *val);
esp_err_t config_set_soft_ap_password(const char *val);
esp_err_t config_set_seconds_per_pulse(uint16_t val);
esp_err_t config_set_soft_ap_start_threshold_s(uint32_t val);
esp_err_t config_set_centimeters_per_pulse(uint32_t val);
esp_err_t config_set_idle_session_interval_s(uint16_t val);
esp_err_t config_set_start_session_interval_s(uint16_t val);
esp_err_t config_set_pulse_debounce_time_ms(uint16_t val);
esp_err_t config_set_timezone(const char *val);
```

**Validation rules (setters reject values outside this range with `ESP_ERR_INVALID_ARG`):**

| Parameter                   | Min     | Max        |
| --------------------------- | ------- | ---------- |
| `seconds_per_pulse`         | 1       | 60         |
| `soft_ap_start_threshold_s` | 0       | UINT32_MAX |
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
- Manage three logical interfaces:
  1. **STA** – connects to the home network (`wifi_ssid` / `wifi_password`).
  2. **Reward SoftAP** – enabled/disabled by the Time Counter module.
  3. **Config SoftAP** – enabled only when STA is not connected.
- Enable **IP_NAPT** on the AP netif so devices connected to either softAP can route traffic through the STA interface.
- Post ESP events on the application event loop to notify other modules of connectivity changes.

**Config SoftAP (fallback):**
- SSID: `CONFIG_ESPORT_CONFIG_AP_SSID` (Kconfig build-time string, default `"esport-fi32_config"`)
- Password: `CONFIG_ESPORT_CONFIG_AP_PASSWORD` (Kconfig build-time string, default `"esport-fi32_config"`)
- IP: `192.168.4.1`
- Enabled: **immediately on the first `WIFI_EVENT_STA_DISCONNECTED` event** (including the initial failed connection attempt at boot). There is no retry counter threshold — the portal is available without delay so the user can correct credentials at any time.
- STA reconnection continues in the background every 10 seconds while the config AP is active.
- Disabled: as soon as STA obtains an IP (`IP_EVENT_STA_GOT_IP`).

**Reward SoftAP:**
- SSID / password: from `config_manager`.
- Channel: follows the STA channel after STA connects; default channel 6 before STA connects.
- Max connected stations: 4.
- IP subnet: `192.168.5.0/24`, gateway `192.168.5.1`.
- Enabled/disabled only via `wifi_manager_set_reward_ap(bool enable)`.
- NAT must be (re-)applied if NAPT was reset when the AP was toggled.

**STA reconnection:**
- Retry indefinitely at 10-second intervals (not a configurable parameter; hardcoded).
- On each `WIFI_EVENT_STA_DISCONNECTED` event, schedule a reconnect attempt.
- After reconnection, post `ESPORT_EVENT_STA_CONNECTED` on the app event loop.

**API:**

```c
esp_err_t wifi_manager_init(void);

/* Called by time_counter module */
esp_err_t wifi_manager_set_reward_ap(bool enable);

/* Status queries */
bool     wifi_manager_is_sta_connected(void);
bool     wifi_manager_is_reward_ap_active(void);
uint8_t  wifi_manager_reward_ap_client_count(void);
void     wifi_manager_get_sta_ip(char *buf, size_t len);   /* dotted-decimal or "" */
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
esp_err_t  time_manager_init(void);
bool       time_manager_is_synced(void);
time_t     time_manager_get_utc(void);          /* seconds since Unix epoch */
void       time_manager_apply_timezone(void);   /* call after timezone config change */
```

---

### 5.4 Pulse Input Module

**File:** `pulse_input.c` / `pulse_input.h`

**Responsibilities:**
- Configure `CONFIG_ESPORT_PULSE_GPIO` as input with internal pull-up.
- Install a GPIO interrupt on the **falling edge**.
- Implement software debounce: ignore any edge that arrives less than `pulse_debounce_time_ms` milliseconds after the previous accepted edge. Use `esp_timer_get_time()` for sub-millisecond resolution.
- For each accepted pulse, post an `ESPORT_EVENT_PULSE` event on the app event loop (payload: `int64_t timestamp_us` of the pulse).
- Expose `pulse_input_get_total_count()` for diagnostic use.

**Notes:**
- The GPIO ISR must be minimal (set a flag / use `esp_event_isr_post()`). All business logic is handled in event callbacks outside the ISR.
- Debounce state is maintained in a static variable updated inside the ISR using `IRAM_ATTR`.

**Kconfig:**

```
CONFIG_ESPORT_PULSE_GPIO          int     default 6                  range 0 30
CONFIG_ESPORT_CONFIG_AP_SSID      string  default "esport-fi32_config"  max 32 chars
CONFIG_ESPORT_CONFIG_AP_PASSWORD  string  default "esport-fi32_config"  max 64 chars
```

These are **build-time** constants set via `idf.py menuconfig`. They are not stored in NVS and cannot be changed at runtime. The config AP credentials are intentionally build-time only so the portal remains accessible even after a full NVS erase.

**API:**

```c
esp_err_t pulse_input_init(void);
uint32_t  pulse_input_get_total_count(void);
```

---

### 5.5 Time Counter & Reward AP State Machine

**File:** `time_counter.c` / `time_counter.h`

**Responsibilities:**
- Maintain the **time counter** (integer, unit: seconds, minimum 0).
- Listen for `ESPORT_EVENT_PULSE` events and add `seconds_per_pulse` to the counter for each.
- Run a 1-second periodic timer (`esp_timer_create`) that decrements the counter by 1 when the reward AP is active. The counter never goes below 0.
- Manage reward AP state:
  - **Enable reward AP** when counter crosses `soft_ap_start_threshold_s` from below (i.e. the counter just became ≥ threshold for the first time since it was last at 0-and-AP-off). Call `wifi_manager_set_reward_ap(true)`.
  - **Disable reward AP** when counter reaches 0 while AP is active. Call `wifi_manager_set_reward_ap(false)`.
  - Once the reward AP is enabled, it stays enabled until the counter reaches 0 — even if the counter temporarily drops below `soft_ap_start_threshold_s` due to the realtime decrement.
- Post `ESPORT_EVENT_COUNTER_CHANGED` (payload: `uint32_t counter_s`) after every change (pulse or tick).
- Post `ESPORT_EVENT_REWARD_AP_ON` and `ESPORT_EVENT_REWARD_AP_OFF` when AP transitions occur.

**State machine:**

```
        ┌──────────────────────────────────────────────────────┐
        │                   IDLE state                         │
        │  counter < threshold                                 │
        │  reward AP: OFF                                      │
        │                                                      │
        │  On pulse:  counter += seconds_per_pulse             │
        │  On tick:   (no decrement, AP is off)                │
        └──────────────────┬───────────────────────────────────┘
                           │  counter >= threshold
                           ▼
        ┌──────────────────────────────────────────────────────┐
        │                  ACTIVE state                        │
        │  reward AP: ON                                       │
        │                                                      │
        │  On pulse:  counter += seconds_per_pulse             │
        │  On tick:   counter -= 1  (min 0)                    │
        └──────────────────┬───────────────────────────────────┘
                           │  counter == 0
                           ▼
                    back to IDLE state
```

**Thread safety:** The counter variable is accessed from the FreeRTOS timer callback and from ESP event loop callbacks. Protect it with a `portMUX_TYPE` spinlock or a FreeRTOS mutex.

**API:**

```c
esp_err_t time_counter_init(void);
uint32_t  time_counter_get(void);
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
  - Post `ESPORT_EVENT_SESSION_CLOSED` with a `session_record_t` payload.
  - Reset state to idle.

**Handling qualification pulses in session stats:** Pulses during the qualification window also count toward the confirmed session (pulse_count includes them all from potential_start).

**Re-read config on each session start** (re-read `start_session_interval_s`, `idle_session_interval_s`, `centimeters_per_pulse` from config_manager so changes apply to the next session without requiring a reboot).

**API:**

```c
esp_err_t session_tracker_init(void);

typedef struct {
    int64_t  start_time_utc;   /* Unix timestamp, 0 if unsynced */
    bool     time_synced;      /* true if system clock was synced at session start */
    uint32_t duration_s;
    uint32_t pulse_count;
    uint16_t avg_speed_kmh_x10; /* km/h * 10 to avoid float, e.g. 123 = 12.3 km/h */
} session_record_t;
```

---

### 5.7 NVS Session Log

**File:** `session_log.c` / `session_log.h`

**Responsibilities:**
- Listen for `ESPORT_EVENT_SESSION_CLOSED` events and persist the `session_record_t` payload to NVS.
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
esp_err_t session_log_write(const session_record_t *rec);
uint16_t  session_log_count(void);

/* Returns up to `max_count` sessions, newest first. Returns actual count written. */
uint16_t  session_log_read(session_record_t *out, uint16_t max_count);
```

---

### 5.8 HTTP Server

**File:** `http_server.c` / `http_server.h`

**Responsibilities:**
- Start an `esp_http_server` instance on port 80.
- Register the routes listed in §6.
- The server runs regardless of which network interface is active; it is reachable on all active IPs.
- Parse and validate POST body (URL-encoded form data) for the config endpoint. Reject malformed or out-of-range values with HTTP 400 and a human-readable error message.
- After a successful config save that changes `wifi_ssid` or `wifi_password`, schedule a WiFi reconnect after a 1-second delay (to allow the HTTP response to be delivered first).
- Provide session export endpoints (CSV and JSON) with `Content-Disposition: attachment` so browsers download report files.
- Provide a daily-aggregate JSON endpoint for chart rendering in the Web UI.

**API:**

```c
esp_err_t http_server_init(void);
```

---

## 6. Web Interface

### 6.1 Status Dashboard — `GET /`

Serves a self-contained HTML page (embedded as a C string literal or embedded file via `EMBED_FILES`). The page auto-refreshes every 5 seconds using `<meta http-equiv="refresh" content="5">`.

**Displayed information:**

| Section          | Fields                                                                                                  |
| ---------------- | ------------------------------------------------------------------------------------------------------- |
| System           | Current local time, NTP sync status, uptime                                                             |
| Wi-Fi            | STA status, home SSID, station IP, config AP status, reward AP status                                   |
| Reward AP        | SSID, active/inactive, connected clients count                                                          |
| Exercise Counter | Current counter value (seconds + human-readable h:mm:ss), threshold, AP enabled                         |
| Current Session  | Status (idle / qualifying / active), qualification progress, live speed (km/h, rolling 5-pulse average) |
| Session History  | Table of last 20 sessions: start (local time), duration (h:mm:ss), avg speed (km/h), pulse count        |
| Session Graphs   | Bar charts with day-of-month on X axis: average speed and total session duration per day                |

Dashboard requirements for reports:

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
| Counter Threshold (s)    | number     | `soft_ap_start_threshold_s` |
| Centimeters per Pulse    | number     | `centimeters_per_pulse`     |
| Idle Session Timeout (s) | number     | `idle_session_interval_s`   |
| Session Start Window (s) | number     | `start_session_interval_s`  |
| Pulse Debounce (ms)      | number     | `pulse_debounce_time_ms`    |
| Timezone (POSIX TZ)      | text       | `timezone`                  |

On submit: `POST /config` with `application/x-www-form-urlencoded` body.
On success: redirect to `/config` with a success banner.
On error: re-render form with error message.

### 6.3 JSON Status API — `GET /api/status`

Returns JSON:

```json
{
  "time_utc": 1741910400,
  "time_local": "2026-03-14T10:00:00",
  "time_synced": true,
  "uptime_s": 3600,
  "sta_connected": true,
  "sta_ssid": "HomeNetwork",
  "sta_ip": "192.168.1.42",
  "config_ap_active": false,
  "reward_ap_active": true,
  "reward_ap_ssid": "esport-fi32",
  "reward_ap_clients": 2,
  "counter_s": 147,
  "threshold_s": 300,
  "session_state": "active",
  "session_start_utc": 1741905000,
  "session_duration_s": 5400,
  "session_pulse_count": 1800,
  "live_speed_kmh_x10": 123
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
    "avg_speed_kmh_x10": 123
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

`start_utc,start_local,time_synced,duration_s,pulse_count,avg_speed_kmh,distance_m`

Where:

- `avg_speed_kmh` is decimal (`avg_speed_kmh_x10 / 10.0`)
- `distance_m = (pulse_count * centimeters_per_pulse) / 100.0`

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

## 7. System Behaviour Sequences

### 7.1 Boot Sequence

```
1. nvs_flash_init()
2. config_manager_init()         ← load config, apply factory defaults
3. esp_event_loop_create_default()
4. wifi_manager_init()           ← start AP+STA, attempt STA connect
   a. if wifi_ssid is empty: skip STA, enable config AP immediately
   b. otherwise: attempt STA connection; config AP is enabled immediately on
      the first WIFI_EVENT_STA_DISCONNECTED (no retry count needed); STA
      keeps retrying every 10 s in the background until it gets an IP
5. http_server_init()            ← start web server (reachable immediately via config AP)
6. time_manager_init()           ← register callback: sync SNTP on STA_GOT_IP
7. pulse_input_init()
8. time_counter_init()
9. session_tracker_init()
10. session_log_init()
```

### 7.2 STA Connection Flow

```
app_main  →  wifi_manager attempts to connect to wifi_ssid
          →  STA_DISCONNECTED (first failed attempt or any later drop)
          →  wifi_manager enables config AP immediately (no retry threshold)
          →  wifi_manager schedules reconnect in 10 s (loops indefinitely)

          →  STA connects → STA_GOT_IP event
          →  time_manager starts SNTP sync
          →  wifi_manager disables config AP
          →  (reward AP managed independently by time_counter)

          →  STA disconnects again → STA_DISCONNECTED event
          →  wifi_manager re-enables config AP immediately
          →  wifi_manager schedules reconnect in 10 s
```

### 7.3 Pulse & Counter Flow

```
Bike sensor → falling edge on GPIO 6
            → ISR: check debounce (esp_timer_get_time)
            → if valid: post ESPORT_EVENT_PULSE

ESPORT_EVENT_PULSE
  → time_counter: counter += seconds_per_pulse
      if counter >= threshold && state == IDLE:
          state = ACTIVE
          wifi_manager_set_reward_ap(true)
          post ESPORT_EVENT_REWARD_AP_ON
  → session_tracker: update pulse count, last_pulse_time, manage timers
```

### 7.4 Counter Decrement & AP Shutdown

```
1-second periodic timer (only runs in ACTIVE state)
  → counter -= 1
  → post ESPORT_EVENT_COUNTER_CHANGED
  if counter == 0:
      state = IDLE
      wifi_manager_set_reward_ap(false)
      post ESPORT_EVENT_REWARD_AP_OFF
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
  → http_server validates all fields
  → calls config_set_*() for each field
  → if wifi_ssid or wifi_password changed:
      schedule wifi_manager reconnect after 1 s
  → if timezone changed:
      time_manager_apply_timezone()
  → redirect to GET /config with success message
```

---

## 8. NVS Layout

### Partition

Use the default NVS partition (`nvs`, 0x9000, 0x6000 from `sdkconfig`). No custom partition table required unless session log storage needs to be expanded.

### Namespace: `esport_cfg`

| Key           | Type   | Content                   |
| ------------- | ------ | ------------------------- |
| `wifi_ssid`   | string | Home SSID                 |
| `wifi_pwd`    | string | Home password             |
| `ap_ssid`     | string | Reward AP SSID            |
| `ap_pwd`      | string | Reward AP password        |
| `spp`         | uint16 | seconds_per_pulse         |
| `ap_thresh`   | uint32 | soft_ap_start_threshold_s |
| `cpp`         | uint32 | centimeters_per_pulse     |
| `idle_s`      | uint16 | idle_session_interval_s   |
| `start_s`     | uint16 | start_session_interval_s  |
| `debounce_ms` | uint16 | pulse_debounce_time_ms    |
| `tz`          | string | POSIX TZ string           |

### Namespace: `esport_log`

| Key                  | Type   | Content                        |
| -------------------- | ------ | ------------------------------ |
| `slog_head`          | uint16 | Next write index (0–49)        |
| `slog_count`         | uint16 | Number of valid entries (0–50) |
| `slog_0` … `slog_49` | blob   | `session_record_t` binary      |

`session_record_t` binary layout (16 bytes):

| Field               | Offset | Type       | Notes                                   |
| ------------------- | ------ | ---------- | --------------------------------------- |
| `start_time_utc`    | 0      | int64_t    | Unix timestamp; 0 = boot-epoch fallback |
| `time_synced`       | 8      | uint8_t    | 1 if synced                             |
| `_pad`              | 9      | uint8_t[1] | reserved                                |
| `duration_s`        | 10     | uint16_t   | session duration in seconds (max ~18 h) |
| `pulse_count`       | 12     | uint16_t   | total pulses (max 65535)                |
| `avg_speed_kmh_x10` | 14     | uint16_t   | km/h × 10 (e.g. 123 = 12.3 km/h)        |

> Total: 16 bytes × 50 entries = 800 bytes plus ~50 bytes for metadata keys.

---

## 9. Event Bus

All inter-module communication uses the default ESP event loop (`esp_event_loop_create_default`).

**Event base:** `ESPORT_EVENT_BASE`

| Event ID                        | Payload type             | Posted by         | Consumed by                       |
| ------------------------------- | ------------------------ | ----------------- | --------------------------------- |
| `ESPORT_EVENT_PULSE`            | `int64_t` (timestamp_us) | `pulse_input`     | `time_counter`, `session_tracker` |
| `ESPORT_EVENT_COUNTER_CHANGED`  | `uint32_t` (counter_s)   | `time_counter`    | `http_server` (status cache)      |
| `ESPORT_EVENT_REWARD_AP_ON`     | —                        | `time_counter`    | (logging, status)                 |
| `ESPORT_EVENT_REWARD_AP_OFF`    | —                        | `time_counter`    | (logging, status)                 |
| `ESPORT_EVENT_SESSION_CLOSED`   | `session_record_t`       | `session_tracker` | `session_log`                     |
| `ESPORT_EVENT_STA_CONNECTED`    | —                        | `wifi_manager`    | `time_manager` (start SNTP)       |
| `ESPORT_EVENT_STA_DISCONNECTED` | —                        | `wifi_manager`    | (logging, status)                 |

---

## 10. Factory Defaults & NVS Recovery

**Defaults applied by `config_manager_init`** when a key is absent or corrupt (see §3 table).

**NVS corruption handling:**
- `config_manager`: if `nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, call `nvs_flash_erase()` then `nvs_flash_init()` again. All config defaults are applied.
- `session_log`: if the log namespace cannot be opened or its metadata is inconsistent (`head > MAX` or `count > MAX`), erase the log namespace and reinitialise.

**No hardware factory-reset button.** Recovery is via the config web portal (accessible from the config AP).

---

## 11. Coding Conventions

- Language: **C11** (no C++).
- RTOS: **FreeRTOS** via ESP-IDF.
- Coding standard: **BARR-C:2018 (BARR-2018) is mandatory** for all source and header files.
- **Variable naming prefixes (BARR-C:2018):**
  - Variable names must be **entirely lowercase** (underscore-separated words). Only macros and constants defined with `#define` or `enum` use uppercase.
  - Pointer variables (including parameters) must start with `p_` (e.g. `char * p_buf`).
  - Boolean variables (including parameters) must start with `b_` (e.g. `bool b_enable`).
  - File-scope (`static`) and global variables must start with `g_`. Combined prefixes apply: global pointer → `gp_`, global boolean → `gb_` (e.g. `static const char * gp_tag`).
- **Yoda notation**: for `==` and `!=` comparisons, always place the constant (literal, macro, or enum value) on the **left-hand side** (e.g. `ESP_OK == ret`, `NULL != p_buf`).
- **Internal (private) functions** must be declared `static`. Every `static` function must have its prototype listed in the `// Internal Function Prototypes` section of the same `.c` file before it is defined.
- All modules expose an `_init()` function that must be called from `app_main` in the order specified in §7.1.
- No module calls another module's internals directly; all cross-module communication is via the event bus or explicit API calls.
- All `esp_err_t` return values must be checked; use `ESP_ERROR_CHECK()` for fatal initialisation failures and `ESP_LOGE` + graceful degradation for runtime errors.
- String inputs from HTTP POST bodies must be length-checked and null-terminated before being passed to `config_set_*()`.
- ISR functions must be declared `IRAM_ATTR` and kept minimal.
- Log tags: one `static const char * gp_tag` per `.c` file, initialised to the module name string (e.g. `"pulse_input"`).
- All timestamps stored and compared as UTC `time_t`; conversion to local time for display only.
- All public APIs, structs, enums, and macros must be documented with **Doxygen** comments.
- Use **Doxygen tags with `\\` prefix** (for example `\\file`, `\\brief`, `\\param`, `\\return`, `\\note`). Do not use `@` tags.
- Each `.h` and `.c` file must begin with a Doxygen file header (`\\file`, `\\brief`, `\\date`). The `\\file` tag takes no argument; Doxygen derives the filename automatically.
- Each public function must include at minimum: `\\brief`, one `\\param` per argument, and `\\return` when non-void.
- Each `\\param` must include direction as `\\param[in]`, `\\param[out]`, or `\\param[in,out]`.
- Doxygen blocks must include one blank line between the last `\\param...` line and `\\return` or `\\retval`.
- Internal `static` functions should include at least a `\\brief` when the logic is non-trivial.
- **Single-line Doxygen comments** (`/** ... */` on one line) must **not** use `\\brief`.
- **Multi-line Doxygen comments**: `\\brief` must appear on the **second line** (immediately after the opening `/**` line). One blank `*` line must always follow the `\\brief` line before any additional content.
- The closing `*/` of a Doxygen block must always appear **alone on its own line**.
- All `.h` files must follow the **`hhtemplate`** structure and all `.c` files must follow the **`cctemplate`** structure defined in `.vscode/esport-fi32.code-snippets`. This mandates: `//===` section separators, `extern "C"` with the brace on the next line, `#endif // GUARD` comment style, and a `/*** end of file ***/` marker at the bottom of every file.
- In source files, the `//---` function separator (from `cctemplate`) must appear **after every function definition**. The closing `}` of every function is followed by a blank line, then the `//---` separator line, then a blank line, before the next function or section divider.
- In Doxygen comments, reference **project-defined** types, functions, macros, and enum values using the `#` prefix (e.g. `#session_record_t`, `#ESPORT_EVENT_PULSE`, `#config_manager_init()`). This enables Doxygen to generate hyperlinks automatically. Use `\c` for external identifiers (ESP-IDF, C standard library, POSIX) and for plain code tokens that are not project symbols.

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
