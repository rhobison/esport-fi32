# esport-fi32 Development Plan

**Version:** 1.0
**Date:** 2026-03-14
**Executor:** AI coding agents
**Spec reference:** `docs/1-specification.md`

---

## Overview

This plan breaks the firmware into **10 phases** that can be executed sequentially by AI coding agents.
Each phase is self-contained: it has clear inputs, explicit acceptance criteria, and defined output files.
Agents must not skip phases or merge phases; each phase must pass its acceptance criteria before the next begins.

### Quick Reference

| Phase | Name                           | Key output files                  |
| ----- | ------------------------------ | --------------------------------- |
| 0     | Project Skeleton               | CMakeLists, headers, Kconfig      |
| 1     | NVS Config Manager             | `config_manager.c/h`              |
| 2     | WiFi Manager                   | `wifi_manager.c/h`                |
| 3     | SNTP / Time Manager            | `time_manager.c/h`                |
| 4     | Pulse Input Module             | `pulse_input.c/h`                 |
| 5     | Time Counter State Machine     | `time_counter.c/h`                |
| 6     | Session Tracker                | `session_tracker.c/h`             |
| 7     | NVS Session Log                | `session_log.c/h`                 |
| 8     | HTTP Server — Config & API     | `http_server.c/h` (config + JSON) |
| 9     | HTTP Server — Status Dashboard | `http_server.c/h` (HTML UI)       |
| 10    | Integration & Verification     | `main.c` final wiring             |

---

## Phase 0 — Project Skeleton

### Goal

Establish the complete file structure, all header files, the event bus definitions, the Kconfig, and the top-level `CMakeLists.txt`. No implementation logic yet — only types, constants, function declarations, and stubs that compile cleanly.

### Inputs

- `docs/1-specification.md` (§4, §5, §9, §11)
- `firmware/CMakeLists.txt` (existing, to be updated)
- `firmware/Kconfig.projbuild` (existing, to be replaced)

### Tasks

1. **Replace `firmware/Kconfig.projbuild`** with a new `menu "esport-fi32 Configuration"` containing:
   - `CONFIG_ESPORT_PULSE_GPIO` int, default 6, range 0 30.
   - `CONFIG_ESPORT_CONFIG_AP_SSID` string, default `"esport-fi32_config"`, max 32 characters.
   - `CONFIG_ESPORT_CONFIG_AP_PASSWORD` string, default `"esport-fi32_config"`, max 64 characters.
   - Remove all pre-existing example entries.

2. **Update `firmware/CMakeLists.txt`** so that `SRCS` lists all `.c` files under `firmware/src/` and `INCLUDE_DIRS` includes `firmware/inc/`.

3. **Create `firmware/inc/event_ids.h`**:
   - Declare `ESPORT_EVENT_BASE` using `ESP_EVENT_DECLARE_BASE`.
   - Declare the enum `esport_event_id_t` with all event IDs from spec §9.

4. **Create all header files** (`config_manager.h`, `wifi_manager.h`, `time_manager.h`, `pulse_input.h`, `time_counter.h`, `session_tracker.h`, `session_log.h`, `http_server.h`) with:
   - All typedefs and structs from the spec (especially `session_record_t`). Boolean struct members must use the `b_` prefix.
   - All function declarations from the spec. Pointer parameters must use `p_` prefix; boolean parameters must use `b_` prefix.
   - Doxygen documentation for all public declarations using `\\` tags (not `@`). Reference project-defined symbols with `#`; use `\\c` only for external identifiers.
   - Use `\\param[in]`, `\\param[out]`, or `\\param[in,out]` for all parameters.
   - Keep one blank line between the last `\\param...` line and `\\return` or `\\retval`.
   - Single-line Doxygen comments (`/** ... */`) must not use `\\brief`.
   - Multi-line Doxygen comments: `\\brief` on the second line, one blank `*` line after `\\brief`, `*/` alone on its own line.
   - File structure must follow the `hhtemplate` from `.vscode/esport-fi32.code-snippets`: `//===` section separators, `extern "C"` brace on its own line, `#endif // GUARD`, `/*** end of file ***/` footer.

5. **Create stub `.c` files** for every module that implement all declared functions as stubs returning `ESP_OK` (or `0`/`false` as appropriate). Include `gp_tag` log constant (`static const char * gp_tag`).
   - Add Doxygen file headers in each `.c` file using `\\file`, `\\brief`, and `\\date` (no filename argument on `\\file`).
   - Single-line `gp_tag` comment must not use `\\brief`.
   - File structure must follow the `cctemplate` from `.vscode/esport-fi32.code-snippets`: `//===` section separators, `/*** end of file ***/` footer.
   - The `//---` function separator must follow every function definition's closing `}`.
   - Apply BARR-C:2018 variable naming: `p_` for pointer parameters, `b_` for boolean parameters, `gp_` for the file-scope `TAG` pointer.
   - Apply Yoda notation for all `==` and `!=` comparisons (constant on the left).
   - Any internal helper functions must be `static` with prototypes in the `Internal Function Prototypes` section.
6. **Replace `firmware/src/main.c`** with a skeleton `app_main` that calls each `module_init()` in the order from spec §7.1, all guarded by `ESP_ERROR_CHECK`.

7. **Ensure `idf_component.yml`** lists all required ESP-IDF components:
   - `esp_wifi`, `esp_event`, `nvs_flash`, `esp_netif`, `esp_timer`, `driver`, `lwip`, `esp_http_server`, `esp_sntp`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings (treat `-Werror` as enforced).
- [ ] All headers included in `main.c` without missing symbol errors.
- [ ] Kconfig menu renders correctly in `idf.py menuconfig`.
- [ ] Public APIs in headers have Doxygen comments using `\\` tags only.
- [ ] Doxygen comments use parameter direction markers (`[in]`, `[out]`, `[in,out]`) and keep a blank line before `\\return`/`\\retval`.
- [ ] Single-line Doxygen comments do not use `\\brief`.
- [ ] Multi-line Doxygen `\\brief` is on the second line; one blank `*` line follows; `*/` is alone on its own line.
- [ ] All `.h` files match the `hhtemplate` structure; all `.c` files match the `cctemplate` structure from `.vscode/esport-fi32.code-snippets`.
- [ ] Every function definition in `.c` files is followed by a `//---` separator line.
- [ ] BARR-C:2018 variable naming applied: all variable names lowercase; pointer params `p_`, boolean params `b_`, file-scope pointer `gp_tag`.
- [ ] All `==` and `!=` comparisons use Yoda notation (constant on the left).
- [ ] Any internal functions are `static` with prototypes in the `Internal Function Prototypes` section.
- [ ] Project-defined symbols in Doxygen comments use `#` prefix; external symbols use `\\c`.

---

## Phase 1 — NVS Configuration Manager

### Goal

Implement `config_manager.c` fully: NVS initialisation, factory defaults, typed getters, validated setters, and corruption recovery.

### Inputs

- `docs/1-specification.md` §5.1, §3, §10
- `firmware/inc/config_manager.h` (Phase 0 output)

### Tasks

1. Implement `config_manager_init()`:
   - Call `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, erase and reinit.
   - Open namespace `esport_cfg` with `NVS_READWRITE`.
   - For each parameter in the spec §3 table, read the key;  if `ESP_ERR_NVS_NOT_FOUND`, write the factory default.

2. Implement all getters: open namespace read-only, read key, fall back to default on error, close handle.

3. Implement all setters: validate range (return `ESP_ERR_INVALID_ARG` on failure), open namespace `NVS_READWRITE`, write, commit, close.

4. Use the NVS keys exactly as specified in spec §8 (e.g. `"wifi_ssid"`, `"spp"`, `"ap_thresh"`).

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Calling `config_manager_init()` twice in sequence does not corrupt state.
- [ ] `config_set_seconds_per_pulse(0)` returns `ESP_ERR_INVALID_ARG`.
- [ ] `config_set_seconds_per_pulse(61)` returns `ESP_ERR_INVALID_ARG`.
- [ ] `config_set_seconds_per_pulse(5)` followed by `config_get_seconds_per_pulse()` returns `5` after a simulated reboot (reinit).
- [ ] All boundary values from spec §5.1 validation table are correctly accepted/rejected.

---

## Phase 2 — WiFi Manager

### Goal

Implement `wifi_manager.c` fully: AP+STA initialisation, config AP lifecycle (auto-enable on STA failure, disable on connection), reward AP enable/disable, NAPT, STA reconnection.

### Inputs

- `docs/1-specification.md` §5.2, §7.2
- `firmware/inc/wifi_manager.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1 output)

### Tasks

1. Implement `wifi_manager_init()`:
   - Init TCP/IP stack: `esp_netif_init()`, `esp_netif_create_default_wifi_ap()`, `esp_netif_create_default_wifi_sta()`.
   - Init WiFi with `WIFI_MODE_APSTA`.
   - Register event handlers for `WIFI_EVENT` and `IP_EVENT`.
   - Attempt STA connection using `config_get_wifi_ssid/password()`. If SSID is empty, skip STA and enable config AP immediately.

2. Implement config AP logic:
   - SSID `CONFIG_ESPORT_CONFIG_AP_SSID`, password `CONFIG_ESPORT_CONFIG_AP_PASSWORD`, channel 1, max 4 clients.
   - Enable on `WIFI_EVENT_STA_DISCONNECTED` (after exhausting retry without IP).
   - Disable on `IP_EVENT_STA_GOT_IP`.
   - Post `ESPORT_EVENT_STA_CONNECTED` / `ESPORT_EVENT_STA_DISCONNECTED` on app event loop.

3. Implement STA reconnection: retry every 10 seconds indefinitely using an `esp_timer`.

4. Implement `wifi_manager_set_reward_ap(bool enable)`:
   - When `enable == true`: configure AP with `config_get_soft_ap_ssid/password()`, set subnet `192.168.5.0/24`, start AP.
   - When `enable == false`: stop AP interface.
   - Enable NAPT (`ip_napt_enable`) on AP netif pointing to STA netif after AP start.
   - Guard against double-enable: if already in desired state, return `ESP_OK` immediately.

5. Implement `wifi_manager_is_sta_connected()`, `wifi_manager_is_reward_ap_active()`, `wifi_manager_reward_ap_client_count()`, `wifi_manager_get_sta_ip()`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] On boot with valid `wifi_ssid`: STA connects, config AP is NOT enabled.
- [ ] On boot with invalid/empty `wifi_ssid`: config AP `esport-fi32_config` is active.
- [ ] After STA disconnection, config AP is re-enabled within 1 reconnect cycle.
- [ ] `wifi_manager_set_reward_ap(true)` brings up the reward AP with correct SSID.
- [ ] `wifi_manager_set_reward_ap(false)` brings down the reward AP.
- [ ] NAPT is enabled; a device on the reward AP can ping through to the internet when STA is connected.
- [ ] Calling `wifi_manager_set_reward_ap(true)` twice does not crash or duplicate AP.

---

## Phase 3 — SNTP / Time Manager

### Goal

Implement `time_manager.c`: SNTP sync at STA connection, timezone application, and uptime-based fallback.

### Inputs

- `docs/1-specification.md` §5.3, §7.2
- `firmware/inc/time_manager.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1), `wifi_manager` (Phase 2)

### Tasks

1. Implement `time_manager_init()`:
   - Register a handler for `ESPORT_EVENT_STA_CONNECTED` on the app event loop.
   - On that event, call `esp_sntp_setoperatingmode(SNTP_OPMODE_POLL)`, `esp_sntp_setservername(0, "pool.ntp.org")`, `esp_sntp_setservername(1, "time.cloudflare.com")`, `esp_sntp_init()`.
   - Register SNTP sync notification callback to set `time_synced = true`.
   - Call `time_manager_apply_timezone()` immediately to apply the stored TZ string.

2. Implement `time_manager_apply_timezone()`:
   - Read `config_get_timezone()`, call `setenv("TZ", tz, 1)` and `tzset()`.

3. Implement `time_manager_is_synced()` and `time_manager_get_utc()`:
   - `get_utc()`: return `time(NULL)`. If not synced, this returns a value derived from uptime + base epoch `946684800` (2000-01-01T00:00:00Z).

4. If `wifi_ssid` is empty (no STA) or STA never connects, the fallback epoch ensures the device still functions with monotonic timestamps.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] After STA connects with internet access, `time_manager_is_synced()` returns `true` within 30 s.
- [ ] `time_manager_get_utc()` returns a plausible Unix timestamp (> 1700000000) after sync.
- [ ] `time_manager_apply_timezone()` after setting `timezone` to `"CET-1CEST,M3.5.0,M10.5.0/3"` causes `localtime()` to return a CET-offset time.
- [ ] When STA never connects, `time_manager_get_utc()` returns a non-zero monotonically increasing value.

---

## Phase 4 — Pulse Input Module

### Goal

Implement `pulse_input.c`: GPIO interrupt, software debounce, and event posting.

### Inputs

- `docs/1-specification.md` §5.4
- `firmware/inc/pulse_input.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1)

### Tasks

1. Implement `pulse_input_init()`:
   - Configure `CONFIG_ESPORT_PULSE_GPIO` as input with `GPIO_PULLUP_ENABLE`, `GPIO_INTR_NEGEDGE`.
   - Install GPIO ISR service (`gpio_install_isr_service(0)`) and add ISR handler.
   - Read `config_get_pulse_debounce_time_ms()` and store as a static `uint64_t debounce_us`.

2. Implement ISR (`IRAM_ATTR`):
   - Read `esp_timer_get_time()` for current timestamp.
   - If `(now - last_accepted_us) < debounce_us`, return immediately (discard).
   - Otherwise, update `last_accepted_us`, increment `total_count`, and post `ESPORT_EVENT_PULSE` with the timestamp payload via `esp_event_isr_post`.

3. Implement `pulse_input_get_total_count()` returning the static counter.

### Notes

- `esp_event_isr_post` is permitted from ISR context as of ESP-IDF v5.x. If this fails (queue full), silently discard — do not log from ISR.
- `last_accepted_us` and `total_count` must be declared `static volatile` and modified inside the ISR only.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Simulating pulses faster than debounce interval on GPIO produces correctly filtered `ESPORT_EVENT_PULSE` events (only 1 event per debounce window).
- [ ] `pulse_input_get_total_count()` increments only for accepted pulses.
- [ ] No crash or watchdog trigger under continuous rapid pulse injection.

---

## Phase 5 — Time Counter & Reward AP State Machine

### Goal

Implement `time_counter.c`: credit accumulation, real-time decrement, and reward AP enable/disable transitions.

### Inputs

- `docs/1-specification.md` §5.5, §7.3, §7.4
- `firmware/inc/time_counter.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1), `wifi_manager` (Phase 2)

### Tasks

1. Declare a static `uint32_t s_counter` protected by a `portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED` spinlock.

2. Declare `typedef enum { TC_STATE_IDLE, TC_STATE_ACTIVE } tc_state_t` for the state machine.

3. Implement `time_counter_init()`:
   - Register handler for `ESPORT_EVENT_PULSE` on app event loop.
   - Create a 1-second periodic `esp_timer` handle (`s_tick_timer`); do NOT start it yet.

4. In the pulse event handler:
   - Lock spinlock, add `config_get_seconds_per_pulse()` to `s_counter`, unlock.
   - If `s_state == TC_STATE_IDLE && s_counter >= config_get_soft_ap_start_threshold_s()`:
     - Set `s_state = TC_STATE_ACTIVE`.
     - Start `s_tick_timer`.
     - Call `wifi_manager_set_reward_ap(true)`.
     - Post `ESPORT_EVENT_REWARD_AP_ON`.
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with current counter value.

5. In the 1-second tick callback:
   - Lock spinlock, decrement `s_counter` (floor 0), unlock.
   - Post `ESPORT_EVENT_COUNTER_CHANGED`.
   - If `s_counter == 0`:
     - Stop `s_tick_timer`.
     - Set `s_state = TC_STATE_IDLE`.
     - Call `wifi_manager_set_reward_ap(false)`.
     - Post `ESPORT_EVENT_REWARD_AP_OFF`.

6. Implement `time_counter_get()`: lock spinlock, read, unlock, return.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Counter starts at 0; reward AP is off.
- [ ] After N pulses where `N * seconds_per_pulse >= threshold`, `wifi_manager_is_reward_ap_active()` returns `true`.
- [ ] Counter decrements by 1 every second when AP is active.
- [ ] After AP activates, counter temporarily dropping below threshold (due to decrement) does NOT disable AP.
- [ ] After counter reaches 0, AP is disabled and state returns to IDLE.
- [ ] Additional pulses after AP off correctly re-enable AP when threshold is crossed again.
- [ ] No race conditions: rapid pulses interleaved with tick do not produce negative counter.

---

## Phase 6 — Session Tracker

### Goal

Implement `session_tracker.c`: two-phase session detection (qualification + active), idle detection, session record creation.

### Inputs

- `docs/1-specification.md` §5.6, §7.5
- `firmware/inc/session_tracker.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1), `time_manager` (Phase 3)

### Tasks

1. Define `typedef enum { ST_IDLE, ST_QUALIFYING, ST_ACTIVE } st_state_t`.

2. Static state variables:
   ```c
   static st_state_t    s_state;
   static int64_t       s_potential_start_us;
   static int64_t       s_last_pulse_us;
   static uint32_t      s_pulse_count;
   static TimerHandle_t s_qualify_timer;   /* FreeRTOS sw timer */
   static TimerHandle_t s_idle_timer;      /* FreeRTOS sw timer */
   static bool          s_session_confirmed;
   static int64_t       s_session_start_utc;
   ```

3. Implement `session_tracker_init()`:
   - Register handler for `ESPORT_EVENT_PULSE`.
   - Create (but do not start) `s_qualify_timer` and `s_idle_timer` using `xTimerCreate`.

4. Pulse handler logic:
   - Update `s_last_pulse_us = event_timestamp`.
   - Increment `s_pulse_count`.
   - **ST_IDLE:** `s_state = ST_QUALIFYING`; record `s_potential_start_us`; reset `s_pulse_count = 1`; start `s_qualify_timer` (`start_session_interval_s` ms); start `s_idle_timer` (`idle_session_interval_s` ms).
   - **ST_QUALIFYING:** reset `s_idle_timer`; (qualify timer still running).
   - **ST_ACTIVE:** reset `s_idle_timer`.

5. `s_qualify_timer` callback (fires after `start_session_interval_s`):
   - `s_state = ST_ACTIVE`.
   - `s_session_start_utc = time_manager_get_utc() - (esp_timer_get_time() - s_potential_start_us) / 1000000`.
   (Back-calculate start UTC from elapsed time since `s_potential_start_us`.)

6. `s_idle_timer` callback (fires after `idle_session_interval_s`):
   - If `s_state == ST_ACTIVE`:
     - Compute stats (see spec §5.6).
     - Post `ESPORT_EVENT_SESSION_CLOSED` with a heap-allocated `session_record_t` (copy as event data, not pointer).
   - Reset: stop both timers, `s_state = ST_IDLE`, zero all state.
   - If `s_state == ST_QUALIFYING`: gap during qualification → reset to IDLE.

7. Speed calculation:
   ```c
   uint64_t total_cm = (uint64_t)s_pulse_count * config_get_centimeters_per_pulse();
   uint32_t dur_s    = (uint32_t)(... duration ...);
   /* avg_speed_kmh_x10 = (total_cm / 100.0) / dur_s * 3.6 * 10
                        = total_cm * 36 / (dur_s * 1000) */
   uint16_t speed = (dur_s > 0) ? (uint16_t)(total_cm * 36 / ((uint64_t)dur_s * 1000)) : 0;
   ```

8. Duration must be capped at `UINT16_MAX` (65535 s ≈ 18 h 12 min) to fit in `uint16_t`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Pulses for less than `start_session_interval_s` do NOT create a session.
- [ ] Pulses for more than `start_session_interval_s` without a long gap open a session.
- [ ] A gap > `idle_session_interval_s` during the qualification window resets to IDLE.
- [ ] `session.start_time` equals the UTC time of the first qualifying pulse (not the confirmation time).
- [ ] `session.duration_s` equals `last_pulse_time - first_pulse_time` (not including idle gap).
- [ ] `avg_speed_kmh_x10` is calculated correctly (regression test with known pulse count, interval, CPP).
- [ ] `ESPORT_EVENT_SESSION_CLOSED` is posted exactly once per session.

---

## Phase 7 — NVS Session Log

### Goal

Implement `session_log.c`: ring buffer backed by NVS, write on session close, read for display.

### Inputs

- `docs/1-specification.md` §5.7, §8
- `firmware/inc/session_log.h`, `firmware/inc/event_ids.h`
- `session_tracker.h` (Phase 6 output — `session_record_t` type)
- `config_manager` (Phase 1 — NVS initialisation already done)

### Tasks

1. Define `SESSION_LOG_MAX_ENTRIES 50` in `session_log.c`.

2. Implement `session_log_init()`:
   - Open namespace `esport_log` NVS_READWRITE.
   - Read `slog_head` and `slog_count`; validate ranges. If invalid, reset both to 0 and erase namespace.
   - Register handler for `ESPORT_EVENT_SESSION_CLOSED` on app event loop.

3. Implement `session_log_write(const session_record_t *rec)`:
   - Write blob at key `slog_N` (where N = current `slog_head`).
   - Advance `slog_head = (slog_head + 1) % MAX`.
   - Increment `slog_count` (cap at MAX).
   - Write updated `slog_head` and `slog_count` to NVS and commit.

4. Implement `session_log_count()`: return cached `slog_count`.

5. Implement `session_log_read(out, max_count)`:
   - Read sessions starting from `(head - 1) % MAX` going backward, up to `min(slog_count, max_count)`.
   - Fill `out[]` array in reverse-chronological order.
   - Return actual count copied.

6. Key naming: use `snprintf(key, 16, "slog_%u", idx)` to generate keys.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Writing 60 sessions and reading back returns the 50 most recent in correct order.
- [ ] After a simulated reboot (`session_log_init()` again), data persists.
- [ ] Corrupt metadata resets log gracefully (no crash, no garbage data).
- [ ] `session_log_count()` accurately reflects the ring buffer fill level.

---

## Phase 8 — HTTP Server — Configuration & JSON API

### Goal

Implement `http_server.c` with config form and JSON API endpoints. No HTML dashboard yet.

### Inputs

- `docs/1-specification.md` §5.8, §6.2, §6.3, §6.4, §6.5, §6.6
- All previously completed modules.

### Tasks

1. Implement `http_server_init()`: start `esp_http_server` on port 80, register all URI handlers.

2. **`GET /config`** handler:
   - Read all config values.
   - Respond with a minimal but functional HTML form (inline C string) pre-populated with current values.
   - All fields from spec §6.2 must be present.

3. **`POST /config`** handler:
   - Read request body (URL-encoded, up to 2 KB).
   - Parse each field using a simple key=value parser (no third-party library; implement as a local helper).
   - Validate and call the appropriate `config_set_*()` for each field.
   - Accumulate any validation errors.
   - On success: call `time_manager_apply_timezone()` if `timezone` changed; schedule WiFi reconnect if wifi credentials changed; redirect to `GET /config` with query `?saved=1`.
   - On error: respond HTTP 400 with error details.

4. **`GET /api/status`** handler:
   - Gather live state from all modules.
   - Respond with JSON matching spec §6.3.
   - Content-Type: `application/json`.

5. **`GET /api/sessions`** handler:
   - Call `session_log_read()` for up to 50 entries.
   - Respond with JSON array matching spec §6.4.

6. **`GET /api/sessions/export`** handler:
   - Parse `format` query parameter (`csv`/`json`, default `csv`).
   - For `csv`: respond with `text/csv` and attachment filename `esport-fi32-sessions-YYYYMMDD.csv`.
   - For `json`: respond with `application/json` and attachment filename `esport-fi32-sessions-YYYYMMDD.json`.
   - For invalid format: return HTTP 400 with JSON error body.

7. **`GET /api/sessions/daily`** handler:
   - Build 31-day local-date aggregates from session log entries.
   - Include days with zero sessions (zero-filled bins).
   - Respond with JSON matching spec §6.6.

8. URL-encoded body parser: implement a `static esp_err_t parse_form_field(const char *body, const char *key, char *out, size_t out_len)` helper that finds `key=value` pairs. URL-decode `+` as space and `%XX` as hex bytes.

9. **Security**: reject any string field longer than the spec maximum in the parser before passing to setters, to prevent buffer overflows.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /config` returns HTTP 200 with a form containing all 11 fields.
- [ ] `POST /config` with valid data saves values to NVS (verified by subsequent GET).
- [ ] `POST /config` with `seconds_per_pulse=0` returns HTTP 400.
- [ ] `GET /api/status` returns valid JSON with all required keys.
- [ ] `GET /api/sessions` returns valid JSON array.
- [ ] `GET /api/sessions/export?format=csv` returns downloadable CSV with correct header columns.
- [ ] `GET /api/sessions/export?format=json` returns downloadable JSON.
- [ ] `GET /api/sessions/export?format=xml` returns HTTP 400.
- [ ] `GET /api/sessions/daily` returns valid JSON with 31 day bins (including zero-filled days).
- [ ] URL-encoded values with `%20` and `+` are decoded correctly.
- [ ] Oversized string fields are rejected (no buffer overrun).

---

## Phase 9 — HTTP Server — Status Dashboard

### Goal

Add the HTML status dashboard (`GET /`) to `http_server.c`.

### Inputs

- `docs/1-specification.md` §6.1
- `http_server.c` (Phase 8 output)
- All other modules.

### Tasks

1. **`GET /`** handler:
   - Gather live state from all modules (same data as `/api/status` plus session history).
   - Generate an HTML page as a C string using `snprintf` into a heap-allocated buffer.
   - Include `<meta http-equiv="refresh" content="5">` for auto-refresh.
   - Sections and fields as specified in §6.1:
     - **System**: current local time (formatted), NTP sync status, uptime.
     - **Wi-Fi**: STA status + SSID + IP, config AP status, reward AP status.
     - **Reward AP**: SSID, active/inactive, connected client count.
     - **Exercise Counter**: counter (seconds + `h:mm:ss`), threshold, AP indicator.
     - **Current Session**: state label (Idle / Qualifying / Active), live speed.
     - **Session History**: table of last 20 sessions (start local time, duration `h:mm:ss`, avg speed, pulse count).
       - **Session Graphs**: two bar charts (daily avg speed and daily total duration) using data from `/api/sessions/daily`.
       - **Export Reports**: CSV and JSON download controls linked to `/api/sessions/export`.
   - Mark any session where `time_synced == false` with `(*)` in the start time column.
   - The page must be self-contained (no external CSS/JS resources; use inline `<style>`).
    - Keep the HTML compact; no large framework; functional and readable in a mobile browser.
    - Render charts using inline SVG or Canvas only (no external charting libraries).

2. Allocate the response buffer from heap; free after `httpd_resp_send`. Ensure allocation failure returns HTTP 500.

3. Add a "Go to Configuration" link pointing to `/config`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /` returns HTTP 200 with valid HTML.
- [ ] Page renders correctly in a browser (test via `curl` for structure validation).
- [ ] All data sections from spec §6.1 are present.
- [ ] Auto-refresh meta tag is present.
- [ ] Session history table shows up to 20 rows.
- [ ] Unsynced sessions are marked with `(*)`.
- [ ] Session graph panel renders both required bar charts with day-of-month X axis.
- [ ] Export buttons trigger downloadable CSV/JSON files.

---

## Phase 10 — Integration & Verification

### Goal

Wire all modules together in `main.c`, add final integration, and verify end-to-end behaviour against all specification requirements.

### Inputs

- All Phase 0–9 outputs.
- `docs/1-specification.md` (full document).

### Tasks

1. **Finalise `firmware/src/main.c`**:
   - Implement the boot sequence exactly as specified in §7.1.
   - Register the `ESPORT_EVENT_BASE` on the default event loop before any module init.

2. **Register cross-module event handlers** in `main.c` (avoid circular dependencies between modules):
   - `ESPORT_EVENT_STA_CONNECTED` → `time_manager` (already handled internally, verify).
   - `ESPORT_EVENT_COUNTER_CHANGED` → log to `ESP_LOGD` for debugging.
   - `ESPORT_EVENT_REWARD_AP_ON` / `ESPORT_EVENT_REWARD_AP_OFF` → `ESP_LOGI`.

3. **Define the event base** in one `.c` file: `ESP_EVENT_DEFINE_BASE(ESPORT_EVENT_BASE)`.

4. **Add startup log banner** via `ESP_LOGI`:
   ```
   esport-fi32 starting. Build: <date/time>
   Config: threshold=%u spp=%u cpp=%u
   ```

5. **Verify idf_component.yml** includes all needed components; run `idf.py build` and address any linker errors.

6. **End-to-end checklist** (document test results in `docs/TEST_RESULTS.md`):

   | #   | Test                                                                            | Pass/Fail |
   | --- | ------------------------------------------------------------------------------- | --------- |
   | 1   | Boot with no wifi_ssid: config AP `esport-fi32_config` appears                  |           |
   | 2   | Connect to config AP, open `192.168.4.1/config`, submit valid wifi credentials  |           |
   | 3   | Device reboots/reconnects as STA; config AP disappears                          |           |
   | 4   | Simulate N pulses exceeding threshold; reward AP appears                        |           |
   | 5   | Counter decrements in real time; AP disappears at 0                             |           |
   | 6   | More pulses during countdown extend the time                                    |           |
   | 7   | Device connected to reward AP can reach the internet (ping test)                |           |
   | 8   | Sustained pedalling > `start_session_interval_s` creates a session log entry    |           |
   | 9   | Short pedalling < `start_session_interval_s` creates NO session log entry       |           |
   | 10  | After 50+ sessions, ring buffer discards oldest, keeps 50 newest                |           |
   | 11  | `/api/status` JSON has all required keys                                        |           |
   | 12  | `/api/sessions` returns correct session data                                    |           |
   | 13  | Status dashboard renders all sections; auto-refreshes                           |           |
   | 14  | Config page: change timezone & verify local time display change                 |           |
   | 15  | Power cycle: counter = 0 (not persisted), config persists, session log persists |           |
   | 16  | STA disconnect mid-run: config AP reappears; reconnects; config AP disappears   |           |
   | 17  | NTP sync: after connecting to internet, timestamps are real UTC                 |           |
   | 18  | Debounce: rapid GPIO pulses filtered to one per debounce window                 |           |
   | 19  | `/api/sessions/export?format=csv` downloads CSV with expected columns           |           |
   | 20  | `/api/sessions/export?format=json` downloads JSON report                        |           |
   | 21  | `/api/sessions/daily` drives dashboard charts with correct daily bins           |           |
   | 22  | Dashboard shows two bar charts (avg speed, duration) and export controls        |           |

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors.
- [ ] All 22 end-to-end tests pass.
- [ ] No assertion failures or watchdog triggers during 30-minute continuous operation.
- [ ] `TEST_RESULTS.md` is created and filled in.

---

## Dependency Graph

```
Phase 0 (Skeleton)
   └── Phase 1 (Config Manager)
          ├── Phase 2 (WiFi Manager)
          │      └── Phase 3 (SNTP/Time)
          │             └── Phase 4 (Pulse Input)
          │                    ├── Phase 5 (Time Counter)
          │                    └── Phase 6 (Session Tracker)
          │                               └── Phase 7 (Session Log)
          └─────────────────────────────────────────────────────────┐
                                                                    │
Phase 8 (HTTP Config+API) ── requires phases 1-7 complete           │
Phase 9 (HTTP Dashboard)  ── requires phase 8 complete              │
Phase 10 (Integration)    ── requires phases 0-9 complete ──────────┘
```

## Notes for AI Agents

- Each phase has its own acceptance criteria. **Do not proceed to the next phase until all acceptance criteria are met.**
- When in doubt about a behaviour not covered by a criterion, refer to `docs/1-specification.md` and implement accordingly.
- Do not add features not described in the specification without flagging them.
- Follow **BARR-C:2018 (BARR-2018)** coding rules for naming, formatting, function size, and defensive coding practices.
- Use **Doxygen for all code documentation**. Use only `\\` Doxygen tags (for example `\\brief`, `\\param`, `\\return`, `\\note`); do not use `@` tags.
- Doxygen parameter tags must include direction: `\\param[in]`, `\\param[out]`, or `\\param[in,out]`.
- Always leave one blank line between the last `\\param...` line and the `\\return` or `\\retval` line.
- Use this required function-comment template:

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
- Use `ESP_LOGI` for normal operational events, `ESP_LOGW` for recoverable anomalies, `ESP_LOGE` for errors.
- Prefer stack allocation; use heap only when size is unknown at compile time (e.g. HTTP response body). Always check `malloc` return value.
- When allocating the HTTP response buffer, `4096` bytes is sufficient for the JSON endpoints; use `16384` bytes for the HTML dashboard.
- The entire codebase must compile cleanly under ESP-IDF v5.x with `-Werror`.
