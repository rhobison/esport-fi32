# esport-fi32 Development Plan

**Version:** 1.0
**Date:** 2026-03-14
**Executor:** AI coding agents
**Spec reference:** `docs/1-specification.md`

---

## Overview

This plan breaks the firmware into **10 top-level phases** (with sub-phases 9A–9F for the HTTP server file refactoring) that can be executed sequentially by AI coding agents.
Each phase is self-contained: it has clear inputs, explicit acceptance criteria, and defined output files.
Agents must not skip phases or merge phases; each phase must pass its acceptance criteria before the next begins.

### Quick Reference

| Phase | Name                               | Key output files                                     |
| ----- | ---------------------------------- | ---------------------------------------------------- |
| 0     | Project Skeleton                   | CMakeLists, headers, Kconfig                         |
| 1     | NVS Config Manager                 | `config_manager.c/h`                                 |
| 2     | WiFi Manager                       | `wifi_manager.c/h`                                   |
| 3     | SNTP / Time Manager                | `time_manager.c/h`                                   |
| 4     | Pulse Input Module                 | `pulse_input.c/h`                                    |
| 5     | Time Counter State Machine         | `time_counter.c/h`                                   |
| 6     | Session Tracker                    | `session_tracker.c/h`                                |
| 7     | NVS Session Log                    | `session_log.c/h`                                    |
| 8     | HTTP Server — Config & API         | `http_server.c/h` (config + JSON)                    |
| 9     | HTTP Server — Status Dashboard     | `http_server.c/h` (HTML UI)                          |
| 9A    | HTTP Server Refactor: Utils        | `http_server_utils.c/h`                              |
| 9B    | HTTP Server Refactor: Config       | `http_server_config.c`, `http_server_config.h`       |
| 9C    | HTTP Server Refactor: API          | `http_server_api.c/h`                                |
| 9D    | HTTP Server Refactor: Export       | `http_server_export.c`, `http_server_export.h`       |
| 9E    | HTTP Server Refactor: Dashboard    | `http_server_dashboard.c`, `http_server_dashboard.h` |
| 9F    | HTTP Server Refactor: Core & Build | `http_server.c` (trimmed), `CMakeLists.txt`          |
| 10    | Integration & Verification         | `main.c` final wiring                                |

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
   - ~~`CONFIG_ESPORT_CONFIG_AP_SSID`~~ / ~~`CONFIG_ESPORT_CONFIG_AP_PASSWORD`~~ — removed (Feature 4: Config AP removed).
   - Remove all pre-existing example entries.

2. **Update `firmware/CMakeLists.txt`** so that `SRCS` lists all `.c` files under `firmware/src/` and `INCLUDE_DIRS` includes `firmware/inc/`.

3. **Create `firmware/inc/event_ids.h`**:
   - Declare `ESPORT_EVENT_BASE` using `ESP_EVENT_DECLARE_BASE`.
   - Declare the enum `esport_event_id_t` with all event IDs from spec §9.

4. **Create all header files** (`config_manager.h`, `wifi_manager.h`, `time_manager.h`, `pulse_input.h`, `time_counter.h`, `session_tracker.h`, `session_log.h`, `http_server.h`) with:
   - All typedefs and structs from the spec (especially `session_trk_record_t`). Boolean struct members must use the `b_` prefix.
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
   - The `//--------------------------------------------------------------------------------------------------` function separator must follow every function definition's closing `}`.
   - Apply BARR-C:2018 variable naming: `p_` for pointer parameters, `b_` for boolean parameters, `gp_` for the file-scope `TAG` pointer.
   - Apply Yoda notation for all `==` and `!=` comparisons (constant on the left).
   - Enclose every `#define` replacement value in parentheses: `#define FOO (123)`, `#define BAR ("text")`.
   - Prefix every symbol (functions, types, macros, enums — public **and** internal) with the module's designated prefix from the **Module Prefix Table** (see end of document). Lower-case prefix for functions/types; upper-case prefix for macros/enums.
   - Function names must follow the pattern `{module_prefix}_{subject}_{action}`: the **action verb goes last**. Examples: `config_mngr_wifi_ssid_get`, `config_mngr_seconds_per_pulse_set`, `wifi_mngr_sta_is_connected`, `time_mngr_timezone_apply`. Exception: `*_init` functions have no subject and keep the form `{module_prefix}_init`.
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
- [ ] Every function definition in `.c` files is followed by a `//--------------------------------------------------------------------------------------------------` separator line.
- [ ] BARR-C:2018 variable naming applied: all variable names lowercase; pointer params `p_`, boolean params `b_`, file-scope pointer `gp_tag`.
- [ ] All `==` and `!=` comparisons use Yoda notation (constant on the left).
- [ ] Every `#define` replacement value is enclosed in parentheses.
- [ ] All symbols (public and internal) carry the module prefix from the **Module Prefix Table**.
- [ ] All function names follow the `{module_prefix}_{subject}_{action}` pattern (action verb last); `*_init` functions are the only exception.
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

1. Implement `config_mngr_init()`:
   - Call `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, erase and reinit.
   - Open namespace `esport_cfg` with `NVS_READWRITE`.
   - For each parameter in the spec §3 table, read the key;  if `ESP_ERR_NVS_NOT_FOUND`, write the factory default.

2. Implement all getters: open namespace read-only, read key, fall back to default on error, close handle.

3. Implement all setters: validate range (return `ESP_ERR_INVALID_ARG` on failure), open namespace `NVS_READWRITE`, write, commit, close.

4. Use the NVS keys exactly as specified in spec §8 (e.g. `"wifi_ssid"`, `"spp"`, `"ap_thresh"`).

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Calling `config_mngr_init()` twice in sequence does not corrupt state.
- [ ] `config_mngr_seconds_per_pulse_set(0)` returns `ESP_ERR_INVALID_ARG`.
- [ ] `config_mngr_seconds_per_pulse_set(61)` returns `ESP_ERR_INVALID_ARG`.
- [ ] `config_mngr_seconds_per_pulse_set(5)` followed by `config_mngr_seconds_per_pulse_get()` returns `5` after a simulated reboot (reinit).
- [ ] All boundary values from spec §5.1 validation table are correctly accepted/rejected.

---

## Phase 2 — WiFi Manager

### Goal

Implement `wifi_manager.c` fully: AP+STA initialisation, always-on reward AP (no config AP), NAPT, STA reconnection.

### Inputs

- `docs/1-specification.md` §5.2, §7.2
- `firmware/inc/wifi_manager.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1 output)

### Tasks

1. Implement `wifi_mngr_init()`:
   - Init TCP/IP stack: `esp_netif_init()`, `esp_netif_create_default_wifi_ap()`, `esp_netif_create_default_wifi_sta()`.
   - Init WiFi with `WIFI_MODE_APSTA`.
   - Register event handlers for `WIFI_EVENT` and `IP_EVENT`.
   - Attempt STA connection using `config_mngr_wifi_ssid_get/password()`. If SSID is empty, skip STA connection; reward AP still starts.

2. ~~Implement config AP logic~~ — removed (Feature 4: reward AP is always-on).

3. Implement STA reconnection: retry every 10 seconds indefinitely using an `esp_timer`.

4. Implement `wifi_mngr_reward_ap_set(bool enable)`:
   - When `enable == true`: configure AP with `config_mngr_soft_ap_ssid_get/password()`, set subnet `192.168.5.0/24`, start AP.
   - When `enable == false`: stop AP interface.
   - Enable NAPT (`ip_napt_enable`) on AP netif pointing to STA netif after AP start.
   - Guard against double-enable: if already in desired state, return `ESP_OK` immediately.

5. Implement `wifi_mngr_sta_is_connected()`, `wifi_mngr_reward_ap_is_active()`, `wifi_mngr_reward_ap_client_count()`, `wifi_mngr_sta_ip_get()`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] On boot with valid `wifi_ssid`: STA connects; reward AP is always active.
- [ ] On boot with empty `wifi_ssid`: only reward AP is active (no STA attempt).
- [ ] After STA disconnection, reconnect is retried every 10 s; reward AP stays active.
- [ ] `wifi_mngr_reward_ap_set(true)` brings up the reward AP with correct SSID.
- [ ] `wifi_mngr_reward_ap_set(false)` brings down the reward AP.
- [ ] NAPT is enabled; a device on the reward AP can ping through to the internet when STA is connected.
- [ ] Calling `wifi_mngr_reward_ap_set(true)` twice does not crash or duplicate AP.

---

## Phase 3 — SNTP / Time Manager

### Goal

Implement `time_manager.c`: SNTP sync at STA connection, timezone application, and uptime-based fallback.

### Inputs

- `docs/1-specification.md` §5.3, §7.2
- `firmware/inc/time_manager.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1), `wifi_manager` (Phase 2)

### Tasks

1. Implement `time_mngr_init()`:
   - Register a handler for `ESPORT_EVENT_STA_CONNECTED` on the app event loop.
   - On that event, call `esp_sntp_setoperatingmode(SNTP_OPMODE_POLL)`, `esp_sntp_setservername(0, "pool.ntp.org")`, `esp_sntp_setservername(1, "time.cloudflare.com")`, `esp_sntp_init()`.
   - Register SNTP sync notification callback to set `time_synced = true`.
   - Call `time_mngr_timezone_apply()` immediately to apply the stored TZ string.

2. Implement `time_mngr_timezone_apply()`:
   - Read `config_mngr_timezone_get()`, call `setenv("TZ", tz, 1)` and `tzset()`.

3. Implement `time_mngr_is_synced()` and `time_mngr_utc_get()`:
   - `get_utc()`: return `time(NULL)`. If not synced, this returns a value derived from uptime + base epoch `946684800` (2000-01-01T00:00:00Z).

4. If `wifi_ssid` is empty (no STA) or STA never connects, the fallback epoch ensures the device still functions with monotonic timestamps.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] After STA connects with internet access, `time_mngr_is_synced()` returns `true` within 30 s.
- [ ] `time_mngr_utc_get()` returns a plausible Unix timestamp (> 1700000000) after sync.
- [ ] `time_mngr_timezone_apply()` after setting `timezone` to `"CET-1CEST,M3.5.0,M10.5.0/3"` causes `localtime()` to return a CET-offset time.
- [ ] When STA never connects, `time_mngr_utc_get()` returns a non-zero monotonically increasing value.

---

## Phase 4 — Pulse Input Module

### Goal

Implement `pulse_input.c`: GPIO interrupt, software debounce, and event posting.

### Inputs

- `docs/1-specification.md` §5.4
- `firmware/inc/pulse_input.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1)

### Tasks

1. Implement `pulse_in_init()`:
   - Configure `CONFIG_ESPORT_PULSE_GPIO` as input with `GPIO_PULLUP_ENABLE`, `GPIO_INTR_NEGEDGE`.
   - Install GPIO ISR service (`gpio_install_isr_service(0)`) and add ISR handler.
   - Read `config_mngr_pulse_debounce_time_ms_get()` and store as a static `uint64_t debounce_us`.

2. Implement ISR (`IRAM_ATTR`):
   - Read `esp_timer_get_time()` for current timestamp.
   - If `(now - last_accepted_us) < debounce_us`, return immediately (discard).
   - Otherwise, update `last_accepted_us`, increment `total_count`, and post `ESPORT_EVENT_PULSE` with the timestamp payload via `esp_event_isr_post`.

3. Implement `pulse_in_total_count_get()` returning the static counter.

### Notes

- `esp_event_isr_post` is permitted from ISR context as of ESP-IDF v5.x. If this fails (queue full), silently discard — do not log from ISR.
- `last_accepted_us` and `total_count` must be declared `static volatile` and modified inside the ISR only.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Simulating pulses faster than debounce interval on GPIO produces correctly filtered `ESPORT_EVENT_PULSE` events (only 1 event per debounce window).
- [ ] `pulse_in_total_count_get()` increments only for accepted pulses.
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

3. Implement `time_ctr_init()`:
   - Register handler for `ESPORT_EVENT_PULSE` on app event loop.
   - Create a 1-second periodic `esp_timer` handle (`s_tick_timer`); do NOT start it yet.

4. In the pulse event handler:
   - Lock spinlock, add `config_mngr_seconds_per_pulse_get()` to `s_counter`, unlock.
   - If `s_state == TC_STATE_IDLE && s_counter >= config_mngr_soft_ap_start_threshold_s_get()`:
     - Set `s_state = TC_STATE_ACTIVE`.
     - Start `s_tick_timer`.
     - Call `wifi_mngr_reward_ap_set(true)`.
     - Post `ESPORT_EVENT_REWARD_AP_ON`.
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with current counter value.

5. In the 1-second tick callback:
   - Lock spinlock, decrement `s_counter` (floor 0), unlock.
   - Post `ESPORT_EVENT_COUNTER_CHANGED`.
   - If `s_counter == 0`:
     - Stop `s_tick_timer`.
     - Set `s_state = TC_STATE_IDLE`.
     - Call `wifi_mngr_reward_ap_set(false)`.
     - Post `ESPORT_EVENT_REWARD_AP_OFF`.

6. Implement `time_ctr_get()`: lock spinlock, read, unlock, return.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Counter starts at 0; reward AP is off.
- [ ] After N pulses where `N * seconds_per_pulse >= threshold`, `wifi_mngr_reward_ap_is_active()` returns `true`.
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

3. Implement `session_trk_init()`:
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
   - `s_session_start_utc = time_mngr_utc_get() - (esp_timer_get_time() - s_potential_start_us) / 1000000`.
   (Back-calculate start UTC from elapsed time since `s_potential_start_us`.)

6. `s_idle_timer` callback (fires after `idle_session_interval_s`):
   - If `s_state == ST_ACTIVE`:
     - Compute stats (see spec §5.6).
     - Post `ESPORT_EVENT_SESSION_CLOSED` with a heap-allocated `session_trk_record_t` (copy as event data, not pointer).
   - Reset: stop both timers, `s_state = ST_IDLE`, zero all state.
   - If `s_state == ST_QUALIFYING`: gap during qualification → reset to IDLE.

7. Speed calculation:
   ```c
   uint64_t total_cm = (uint64_t)s_pulse_count * config_mngr_centimeters_per_pulse_get();
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
- `session_tracker.h` (Phase 6 output — `session_trk_record_t` type)
- `config_manager` (Phase 1 — NVS initialisation already done)

### Tasks

1. Define `SESSION_LOG_MAX_ENTRIES 50` in `session_log.c`.

2. Implement `session_log_init()`:
   - Open namespace `esport_log` NVS_READWRITE.
   - Read `slog_head` and `slog_count`; validate ranges. If invalid, reset both to 0 and erase namespace.
   - Register handler for `ESPORT_EVENT_SESSION_CLOSED` on app event loop.

3. Implement `session_log_write(const session_trk_record_t *rec)`:
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

1. Implement `http_srv_init()`: start `esp_http_server` on port 80, register all URI handlers.

2. **`GET /config`** handler:
   - Read all config values.
   - Respond with a minimal but functional HTML form (inline C string) pre-populated with current values.
   - All fields from spec §6.2 must be present.

3. **`POST /config`** handler:
   - Read request body (URL-encoded, up to 2 KB).
   - Parse each field using a simple key=value parser (no third-party library; implement as a local helper).
   - Validate and call the appropriate `config_set_*()` for each field.
   - Accumulate any validation errors.
   - On success: call `time_mngr_timezone_apply()` if `timezone` changed; post `ESPORT_EVENT_CONFIG_CHANGED` (the WiFi Manager handles reconnect automatically if wifi credentials changed); redirect to `GET /config` with query `?saved=1`.
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
   - Render all live fields inside named `<span id="...">` elements with initial values pre-populated by C; no `<meta http-equiv="refresh">`.
   - Include an inline `<script>` block with a `setInterval(refresh, 2000)` loop (plus an immediate `refresh()` call on load) that fetches `/api/status` and updates every live span in-place.
   - Sections and fields as specified in §6.1:
     - **System**: current local time (formatted), NTP sync status, uptime.
     - **Wi-Fi**: STA status + SSID + IP, reward AP status.
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
- [ ] Live fields (System, Wi-Fi, Exercise Counter, Current Session) update every 2 s via JS fetch without a full page reload.
- [ ] No `<meta http-equiv="refresh">` tag is present.
- [ ] Session history table shows up to 20 rows.
- [ ] Unsynced sessions are marked with `(*)`.
- [ ] Session graph panel renders both required bar charts with day-of-month X axis.
- [ ] Export buttons trigger downloadable CSV/JSON files.

---

## Phase 9A — HTTP Server Refactor: Shared Utilities

### Goal

Extract the three internal helper functions and all shared buffer-size constants from `http_server.c` into a dedicated translation unit. All subsequent refactoring phases depend on this file and must not begin until it compiles cleanly.

### Inputs

- `main/src/http_server.c` (Phase 9 output — the monolithic 1718-line file)
- `main/inc/http_server.h`

### Tasks

1. **Create `main/inc/http_server_utils.h`** (internal header — not part of the public API):
   - Move the following `#define` constants here from `http_server.c`: `HTTP_SRV_POST_BODY_MAX_LEN`, `HTTP_SRV_JSON_BUF_LEN`, `HTTP_SRV_DAILY_WINDOW_DAYS`, `HTTP_SRV_ATTR_ENC_LEN`, `HTTP_SRV_ENTRY_BUF_LEN`, `HTTP_SRV_SECS_PER_DAY`, `HTTP_SRV_FORM_VALUE_ENC_MAX_LEN`, `HTTP_SRV_HTML_BUF_LEN`, `HTTP_SRV_HIST_MAX`, `HTTP_SRV_GRAPH_MAX`.
   - Do **not** include `HTTP_SRV_SVG_*` constants — those will be defined locally in `http_server_dashboard.c` only.
   - Declare the three helpers as non-`static` (they must be callable from other translation units):
     ```c
     void      http_srv_url_decode(const char *p_src, char *p_dst, size_t dst_len);
     esp_err_t http_srv_form_field_get(const char *p_body, const char *p_key,
                                       char *p_out, size_t out_len);
     void      http_srv_html_attr_encode(const char *p_src, char *p_dst, size_t dst_len);
     ```
   - Use `hhtemplate` structure; guard with `HTTP_SERVER_UTILS_H`.

2. **Create `main/src/http_server_utils.c`**:
   - Move the three helper implementations verbatim from `http_server.c`; remove `static` from each definition.
   - Follow `cctemplate` structure: Doxygen file header (`\file`, `\brief`, `\date`), `gp_tag`, `//===` section separators, `//--------------------------------------------------------------------------------------------------` after every function `}`, `/*** end of file ***/` footer.
   - Include `http_server_utils.h` plus required system headers (`<ctype.h>`, `<stdlib.h>`, `<string.h>`, `esp_err.h`).

3. **Update `main/src/http_server.c`**:
   - Delete the three helper implementations and their forward declarations from the Internal Function Prototypes section.
   - Delete the moved `#define` constants.
   - Add `#include "http_server_utils.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_utils.c"` to the `SRCS` list.

### Notes

> **Stack budget — applies to all phases 9A–9F.**
> No function in any `http_server_*.c` file, nor any function it calls transitively within the same task, may allocate more than **512 bytes in total on the stack** at any one point in the call chain. All buffers larger than 512 bytes must be heap-allocated with `malloc` (and the return value checked). This constraint exists because HTTP handlers run in the `httpd` worker task, which has a limited stack. The only exception is small, bounded scratch variables (e.g. a `char num[16]` or `char hex[3]`) — these are fine.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors.
- [ ] All shared `HTTP_SRV_*` constants are defined exactly once, in `http_server_utils.h`.
- [ ] `http_srv_url_decode()`, `http_srv_form_field_get()`, and `http_srv_html_attr_encode()` are no longer defined in `http_server.c`.
- [ ] All existing endpoints continue to respond correctly.
- [ ] No individual function allocates more than 512 bytes of local variables on the stack.

---

## Phase 9B — HTTP Server Refactor: Configuration Handlers

### Goal

Move the `GET /config` and `POST /config` handlers out of `http_server.c` into a dedicated translation unit.

### Inputs

- `main/src/http_server.c` (Phase 9A output)
- `main/inc/http_server_utils.h`

### Tasks

1. **Create `main/inc/http_server_config.h`** (internal header):
   - Declare the two handlers as non-`static`:
     ```c
     esp_err_t http_srv_config_get_handler(httpd_req_t *p_req);
     esp_err_t http_srv_config_post_handler(httpd_req_t *p_req);
     ```
   - Include `esp_http_server.h`; guard with `HTTP_SERVER_CONFIG_H`; use `hhtemplate` structure.

2. **Create `main/src/http_server_config.c`**:
   - Move both handler implementations verbatim from `http_server.c`; remove `static` from each definition.
   - Include `http_server_config.h`, `http_server_utils.h`, and all required module headers (`config_manager.h`, `time_manager.h`, `esp_http_server.h`, etc.).
   - Follow `cctemplate` structure with `gp_tag`.

3. **Update `main/src/http_server.c`**:
   - Remove both handler implementations and their forward declarations.
   - Add `#include "http_server_config.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_config.c"` to `SRCS`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /config` returns HTTP 200 with all 11 form fields pre-populated.
- [ ] `POST /config` with valid data saves to NVS and redirects to `/config?saved=1`.
- [ ] `POST /config` with `seconds_per_pulse=0` returns HTTP 400.

---

## Phase 9C — HTTP Server Refactor: JSON API Handlers

### Goal

Move `GET /api/status`, `GET /api/sessions`, and `GET /api/sessions/daily` to a dedicated file. Extract the duplicated 31-day binning logic into a single shared helper `http_srv_daily_bins_build()` so the dashboard (Phase 9E) can call it instead of copying the code.

### Inputs

- `main/src/http_server.c` (Phase 9B output)
- `main/inc/http_server_utils.h`

### Tasks

1. **Create `main/inc/http_server_api.h`** (internal header):
   - Move `http_srv_daily_bin_t` typedef here from the Internal Constants section of `http_server.c`.
   - Declare the shared bin-builder helper:
     ```c
     void http_srv_daily_bins_build(http_srv_daily_bin_t       *p_bins,
                                    const session_trk_record_t *p_sessions,
                                    uint16_t                    count);
     ```
     This function fills the caller-supplied `p_bins` array (`HTTP_SRV_DAILY_WINDOW_DAYS` entries) with the 31-day window ending today (local time) and accumulates `p_sessions` into the matching bins.
   - Declare the three API handlers (non-`static`).
   - Guard with `HTTP_SERVER_API_H`; use `hhtemplate` structure.

2. **Create `main/src/http_server_api.c`**:
   - Implement `http_srv_daily_bins_build()` by extracting and consolidating the identical bin-building loops currently duplicated in `http_srv_root_get_handler()` and `http_srv_api_sessions_daily_handler()`.
   - Move all three handler implementations verbatim; update `http_srv_api_sessions_daily_handler()` to call `http_srv_daily_bins_build()` instead of the inline loop.
   - Remove `static` from all four function definitions.
   - Follow `cctemplate` structure with `gp_tag`.

3. **Update `main/src/http_server.c`**:
   - Remove the three handler implementations, the `http_srv_daily_bin_t` typedef, and their forward declarations.
   - Add `#include "http_server_api.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_api.c"` to `SRCS`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /api/status` returns valid JSON with all keys from spec §6.3.
- [ ] `GET /api/sessions` returns valid JSON array.
- [ ] `GET /api/sessions/daily` returns valid JSON with 31 day bins including zero-filled days.
- [ ] The 31-day binning logic exists in exactly one place: `http_srv_daily_bins_build()` in `http_server_api.c`.

---

## Phase 9D — HTTP Server Refactor: Export Handlers

### Goal

Move `GET /api/sessions/export` and its two internal send helpers to a dedicated translation unit.

### Inputs

- `main/src/http_server.c` (Phase 9C output)
- `main/inc/http_server_utils.h`

### Tasks

1. **Create `main/inc/http_server_export.h`** (internal header):
   - Declare only the route handler (the two `_send` helpers remain `static` inside the `.c` file):
     ```c
     esp_err_t http_srv_api_sessions_export_handler(httpd_req_t *p_req);
     ```
   - Guard with `HTTP_SERVER_EXPORT_H`; use `hhtemplate` structure.

2. **Create `main/src/http_server_export.c`**:
   - Move `http_srv_export_csv_send()`, `http_srv_export_json_send()`, and `http_srv_api_sessions_export_handler()` verbatim from `http_server.c`.
   - `http_srv_export_csv_send()` and `http_srv_export_json_send()` keep `static`; add their forward declarations to the Internal Function Prototypes section.
   - `http_srv_api_sessions_export_handler()` removes `static`.
   - Include `http_server_export.h`, `http_server_utils.h`, and required module headers.
   - Follow `cctemplate` structure with `gp_tag`.

3. **Update `main/src/http_server.c`**: remove the three function implementations and their forward declarations; add `#include "http_server_export.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_export.c"` to `SRCS`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /api/sessions/export?format=csv` returns downloadable CSV with the correct header row.
- [ ] `GET /api/sessions/export?format=json` returns downloadable JSON with attachment header.
- [ ] `GET /api/sessions/export?format=xml` returns HTTP 400 with JSON error body.

---

## Phase 9E — HTTP Server Refactor: Status Dashboard

### Goal

Move the `GET /` status dashboard handler to a dedicated file. Eliminate code duplication by calling `http_srv_daily_bins_build()` from Phase 9C instead of re-implementing the bin loop.

### Inputs

- `main/src/http_server.c` (Phase 9D output)
- `main/inc/http_server_api.h` (for `http_srv_daily_bin_t` and `http_srv_daily_bins_build()`)
- `main/inc/http_server_utils.h`

### Tasks

1. **Create `main/inc/http_server_dashboard.h`** (internal header):
   - Declare:
     ```c
     esp_err_t http_srv_root_get_handler(httpd_req_t *p_req);
     ```
   - Guard with `HTTP_SERVER_DASHBOARD_H`; use `hhtemplate` structure.

2. **Create `main/src/http_server_dashboard.c`**:
   - Move `http_srv_root_get_handler()` verbatim from `http_server.c`.
   - Replace the inline 31-day bin population loop with a call to `http_srv_daily_bins_build(p_bins, p_graph, graph_count)`.
   - Define `HTTP_SRV_SVG_*` constants locally in this file (they are only needed here).
   - Remove `static` from `http_srv_root_get_handler()`.
   - Include `http_server_dashboard.h`, `http_server_api.h`, `http_server_utils.h`, and all required module headers.
   - Follow `cctemplate` structure with `gp_tag`.

3. **Update `main/src/http_server.c`**:
   - Remove `http_srv_root_get_handler()` implementation, the `HTTP_SRV_SVG_*` constant definitions, and the handler's forward declaration.
   - Add `#include "http_server_dashboard.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_dashboard.c"` to `SRCS`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /` returns HTTP 200 with valid HTML containing all sections from spec §6.1.
- [ ] Both SVG bar charts (daily avg speed and daily total duration) render correctly with day-of-month X-axis labels.
- [ ] No `HTTP_SRV_SVG_*` constants remain in `http_server.c`.
- [ ] `http_server_dashboard.c` calls `http_srv_daily_bins_build()` — it does not contain a copy of the bin-building loop.

---

## Phase 9F — HTTP Server Refactor: Core Cleanup & Build Verification

### Goal

Confirm `http_server.c` is now an init-only file, verify `CMakeLists.txt` lists all new sources, and close out the refactoring with a clean build.

### Inputs

- All Phase 9A–9E outputs.

### Tasks

1. **Audit `main/src/http_server.c`**. After all prior phases it must contain only:
   - Doxygen file header and `#include` directives (including the five internal headers: `http_server_utils.h`, `http_server_config.h`, `http_server_api.h`, `http_server_export.h`, `http_server_dashboard.h`).
   - `gp_tag` and `gp_server_handle` static variables.
   - `http_srv_init()` implementation (server start + URI registration for all 7 routes).
   - No handler function bodies, no helper functions, no `#define` constants other than those used solely inside `http_srv_init()`.

2. **Confirm `main/inc/http_server.h`** (the public API header) is unchanged — it must still declare only `http_srv_init()`.

3. **Confirm `main/CMakeLists.txt`** `SRCS` contains all six files:
   ```
   "src/http_server.c"
   "src/http_server_utils.c"
   "src/http_server_config.c"
   "src/http_server_api.c"
   "src/http_server_export.c"
   "src/http_server_dashboard.c"
   ```

4. Run `idf.py build` and fix any remaining compilation or linker errors.

5. Verify final file sizes — no single `.c` file in the `http_server` group should exceed 450 lines.

### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings (`-Werror` enforced).
- [ ] `http_server.c` is ≤ 100 lines.
- [ ] No `HTTP_SRV_*` size or window constant is defined in more than one file.
- [ ] No handler function body exists in `http_server.c`.
- [ ] All six new `.c` files follow `cctemplate` structure; all five new internal `.h` files follow `hhtemplate` structure.
- [ ] All endpoints (`/`, `/config`, `POST /config`, `/api/status`, `/api/sessions`, `/api/sessions/export`, `/api/sessions/daily`) respond correctly.

---

## Phase 10 — Integration & Verification

### Goal

Wire all modules together in `main.c`, add final integration, and verify end-to-end behaviour against all specification requirements.

### Inputs

- All Phase 0–9F outputs.
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

   | #   | Test                                                                                    | Pass/Fail |
   | --- | --------------------------------------------------------------------------------------- | --------- |
   | 1   | Boot with no wifi_ssid: reward AP `esport-fi32` is active at 192.168.5.1          |           |
   | 2   | Connect to reward AP, open `192.168.5.1/config`, submit valid wifi credentials     |           |
   | 3   | Device connects as STA; reward AP remains active                                   |           |
   | 4   | Simulate N pulses exceeding threshold; reward AP appears                                |           |
   | 5   | Counter decrements in real time; AP disappears at 0                                     |           |
   | 6   | More pulses during countdown extend the time                                            |           |
   | 7   | Device connected to reward AP can reach the internet (ping test)                        |           |
   | 8   | Sustained pedalling > `start_session_interval_s` creates a session log entry            |           |
   | 9   | Short pedalling < `start_session_interval_s` creates NO session log entry               |           |
   | 10  | After 50+ sessions, ring buffer discards oldest, keeps 50 newest                        |           |
   | 11  | `/api/status` JSON has all required keys                                                |           |
   | 12  | `/api/sessions` returns correct session data                                            |           |
   | 13  | Status dashboard renders all sections; live fields update every 2 s without page reload |           |
   | 14  | Config page: change timezone & verify local time display change                         |           |
   | 15  | Power cycle: counter = 0 (not persisted), config persists, session log persists         |           |
   | 16  | STA disconnect mid-run: reward AP stays active; STA reconnects after 10 s          |           |
   | 17  | NTP sync: after connecting to internet, timestamps are real UTC                         |           |
   | 18  | Debounce: rapid GPIO pulses filtered to one per debounce window                         |           |
   | 19  | `/api/sessions/export?format=csv` downloads CSV with expected columns                   |           |
   | 20  | `/api/sessions/export?format=json` downloads JSON report                                |           |
   | 21  | `/api/sessions/daily` drives dashboard charts with correct daily bins                   |           |
   | 22  | Dashboard shows two bar charts (avg speed, duration) and export controls                |           |

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
Phase 8  (HTTP Config+API)       ── requires phases 1-7 complete         │
Phase 9  (HTTP Dashboard)        ── requires phase 8 complete            │
Phase 9A (Refactor: Utils)       ── requires phase 9 complete            │
Phase 9B (Refactor: Config)      ── requires phase 9A complete           │
Phase 9C (Refactor: API)         ── requires phase 9A complete           │
Phase 9D (Refactor: Export)      ── requires phase 9A complete           │
Phase 9E (Refactor: Dashboard)   ── requires phases 9A + 9C complete     │
Phase 9F (Refactor: Core+Build)  ── requires phases 9B–9E complete      │
Phase 10 (Integration)           ── requires phases 0–9F complete ─────┘
```

## Module Prefix Table

Every symbol (functions, types, `#define` macros, `enum` values) **must** start with the module's designated prefix.  This applies to **both public and internal** symbols.  Use lower-case prefixes for functions/types and upper-case for macros/enum values.

| Source file             | Function / type prefix | Macro / enum prefix |
| ----------------------- | ---------------------- | ------------------- |
| `config_manager`        | `config_mngr_`         | `CONFIG_MNGR_`      |
| `wifi_manager`          | `wifi_mngr_`           | `WIFI_MNGR_`        |
| `time_manager`          | `time_mngr_`           | `TIME_MNGR_`        |
| `pulse_input`           | `pulse_in_`            | `PULSE_IN_`         |
| `time_counter`          | `time_ctr_`            | `TIME_CTR_`         |
| `session_tracker`       | `session_trk_`         | `SESSION_TRK_`      |
| `session_log`           | `session_log_`         | `SESSION_LOG_`      |
| `http_server`           | `http_srv_`            | `HTTP_SRV_`         |
| `http_server_utils`     | `http_srv_`            | `HTTP_SRV_`         |
| `http_server_config`    | `http_srv_`            | `HTTP_SRV_`         |
| `http_server_api`       | `http_srv_`            | `HTTP_SRV_`         |
| `http_server_export`    | `http_srv_`            | `HTTP_SRV_`         |
| `http_server_dashboard` | `http_srv_`            | `HTTP_SRV_`         |
| `device_registry`       | `device_reg_`          | `DEVICE_REG_`       |

> `gp_tag` is a universal file-scope variable name and does **not** carry a module prefix (it follows the BARR-C:2018 pointer variable naming rule instead).

---

## Notes for AI Agents

- Each phase has its own acceptance criteria. **Do not proceed to the next phase until all acceptance criteria are met.**
- When in doubt about a behaviour not covered by a criterion, refer to `docs/1-specification.md` and implement accordingly.
- Do not add features not described in the specification without flagging them.
- Follow **BARR-C:2018 (BARR-2018)** coding rules for naming, formatting, function size, and defensive coding practices.
- Enclose every `#define` replacement value in parentheses: `#define FOO (123)`, `#define MY_STR ("text")`.
- Every `typedef struct` and `typedef enum` must include a **tag name**: `typedef struct my_struct_tag { … } my_struct_t;` and `typedef enum my_enum_tag { … } my_enum_t;`.
- Every `enum` enumerator must have an **explicit integer value**: `MY_ENUM_FOO = 0`, `MY_ENUM_BAR = 1`, etc. Do not rely on implicit sequential assignment.
- Every symbol in a module (public and internal: functions, types, macros, enums) **must** be prefixed with the module's designated prefix from the **Module Prefix Table** above.
- Function names must follow the pattern `{module_prefix}_{subject}_{action}` — the **action verb goes last**. Examples: `config_mngr_wifi_ssid_get`, `config_mngr_seconds_per_pulse_set`, `wifi_mngr_sta_is_connected`, `time_mngr_timezone_apply`. The only exception is `*_init` (no subject).
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
- **Stack budget:** no function (nor any transitive callee within the same task) may allocate more than **512 bytes in total on the stack** at any single point in the call chain. Buffers larger than 512 bytes must be heap-allocated. Small scratch variables (e.g. `char num[16]`, `char hex[3]`) are exempt.
- When allocating the HTTP response buffer, `4096` bytes is sufficient for the JSON endpoints; use `16384` bytes for the HTML dashboard.
- The entire codebase must compile cleanly under ESP-IDF v5.x with `-Werror`.

---

## Improvement Features

This section tracks incremental improvements beyond the base specification.  Each feature is numbered and broken into sub-phases following the same conventions as the base plan.  Features are independent of each other unless explicitly noted.

### Quick Reference — Improvement Features

| Phase | Feature | Name                                           | Key output files                                                       |
| ----- | ------- | ---------------------------------------------- | ---------------------------------------------------------------------- |
| 1.1   | 1       | Spec update — traffic-gated countdown          | `docs/1-specification.md`                                              |
| 1.2   | 1       | Config Manager — two new params                | `config_manager.c/h`, `Kconfig.projbuild`                              |
| 1.3   | 1       | WiFi Manager — throughput query                | `wifi_manager.c/h`, `sdkconfig.defaults`                               |
| 1.4   | 1       | Time Counter — sliding-window gated decrement  | `time_counter.c/h`                                                     |
| 1.5   | 1       | Web UI & API — pause indicator and config      | `http_server_config.c`, `http_server_api.c`, `http_server_dashboard.c` |
| 2.1   | 2       | Spec update — speed-gated increment            | `docs/1-specification.md`                                              |
| 2.2   | 2       | Pulse Input — last interval query              | `pulse_input.c/h`                                                      |
| 2.3   | 2       | Config Manager — speed threshold param         | `config_manager.c/h`, `Kconfig.projbuild`                              |
| 2.4   | 2       | Time Counter — speed-gated pulse crediting     | `time_counter.c/h`                                                     |
| 2.5   | 2       | Web UI & API — speed threshold config & status | `http_server_config.c`, `http_server_api.c`, `http_server_dashboard.c` |
| 3.1   | 3       | Spec update — counter persistence              | `docs/1-specification.md`                                              |
| 3.2   | 3       | Config Manager — reward counter param          | `config_manager.c/h`                                                   |
| 3.3   | 3       | Time Counter — NVS persistence & counter set   | `time_counter.c/h`                                                     |
| 3.4   | 3       | Web UI — reward counter config field           | `http_server_config.c`                                                 |
| 4.1   | 4       | Device Registry Module                         | `device_registry.c/h`, `event_ids.h`, `CMakeLists.txt`                 |
| 4.2   | 4       | WiFi Manager: Always-On AP & MAC Filter        | `wifi_manager.c`                                                       |
| 4.3   | 4       | Time Counter: Per-Device Earning & Tick        | `time_counter.c/h`                                                     |
| 4.4   | 4       | HTTP Server: Config Device Management          | `http_server_config.c`                                                 |
| 4.5   | 4       | HTTP Server: API & Dashboard Per-Device Status | `http_server_api.c`, `http_server_dashboard.c`                         |
| 4.6   | 4       | Spec Update                                    | `docs/1-specification.md`                                              |
| 5.1   | 5       | Buzzer Core Module                             | `buzzer.c/h`, `Kconfig.projbuild`, `CMakeLists.txt`                    |
| 5.2   | 5       | Config Manager — buzzer enable param           | `config_manager.c/h`                                                   |
| 5.3   | 5       | Session Tracker — buzzer integration           | `session_tracker.c`                                                    |
| 5.4   | 5       | Time Counter — speed-low beep integration      | `time_counter.c`                                                       |
| 5.5   | 5       | HTTP Server — buzzer enable config field       | `http_server_config.c`                                                 |
| 5.6   | 5       | Spec Update                                    | `docs/1-specification.md`                                              |

---

## Feature 1 — Traffic-Gated Countdown Decrement

### Overview

The reward Soft AP time counter currently decrements unconditionally every second while the AP is active.  This feature gates the decrement on real-time Wi-Fi traffic: if throughput on the reward AP has been below a configurable threshold for a configurable number of seconds, the countdown pauses.  The countdown resumes immediately the moment throughput exceeds the threshold again.

This prevents the timer from draining while no one is actually using the internet connection (e.g. no client connected, or a client device is idle).

### New Configuration Parameters

| Parameter                               | Type       | NVS key         | Default | Valid range |
| --------------------------------------- | ---------- | --------------- | ------- | ----------- |
| `soft_ap_dec_time_above_threshold_kbps` | `uint16_t` | `"ap_thr_kbps"` | 1       | 0–65535     |
| `soft_ap_idle_throughput_timeout_s`     | `uint16_t` | `"ap_idle_tmo"` | 30      | 0–65535     |

### Pause/Resume Logic — Sliding Window (evaluated once per 1-second tick)

```
throughput = wifi_mngr_reward_ap_throughput_kbps()  // combined RX + TX, kbps
threshold  = config_mngr_soft_ap_dec_threshold_kbps_get()
timeout    = config_mngr_soft_ap_idle_throughput_timeout_s_get()

if throughput > threshold:
    g_below_ticks = 0        // reset grace-period streak
    g_paused      = false    // resume decrement if it was paused
else:
    g_below_ticks++
    if timeout == 0 or g_below_ticks >= timeout:
        g_paused = true

if not g_paused:             // decrement while above threshold OR within grace period
    decrement counter by 1
```

- `timeout = 0` means pause immediately on the first below-threshold tick.
- While throughput is below threshold but `g_below_ticks < timeout` (the grace period), the
  counter continues to decrement.
- If throughput rises above the threshold during the grace period, `g_below_ticks` resets to 0.
- No clients connected → 0 kbps → handled identically to low-traffic.
- `g_below_ticks` is reset to `0` whenever the state machine exits `TIME_CTR_STATE_AP_ACTIVE`.

---

### Phase 1.1 — Spec Update

**Goal:** Update `docs/1-specification.md` to describe both new configuration parameters, the throughput-query function, the updated tick behaviour, and the new API/dashboard fields.  All later phases implement what this Spec describes.

**Inputs**
- `docs/0-draft-input.md` §Improvements item 1
- `docs/1-specification.md` (current)

**Tasks**

1. **§3 Configuration Parameters table** — add two rows:
   - `soft_ap_dec_time_above_threshold_kbps` — `uint16_t`, NVS key `"ap_thr_kbps"`, default `1`, range `0–65535`, description: "Combined RX+TX throughput (kbps) below which the countdown is considered idle."
   - `soft_ap_idle_throughput_timeout_s` — `uint16_t`, NVS key `"ap_idle_tmo"`, default `30`, range `0–65535`, description: "Number of consecutive seconds that throughput must remain below the threshold before the countdown pauses."

2. **§5.1 NVS Configuration Manager** — add getter/setter entries for both parameters to the public API table.

3. **§5.2 WiFi Manager** — add `wifi_mngr_reward_ap_throughput_kbps()` → `uint32_t` to the public API table, with description: "Returns the combined RX+TX throughput on the reward AP in kbps over the last 1-second interval.  Returns `0` when the reward AP is inactive."

4. **§5.5 Time Counter & Reward AP State Machine** — replace the unconditional decrement description with the sliding-window logic documented in the Feature 1 overview above.  Add `time_ctr_is_paused()` → `bool` to the public API.

5. **§6.1 Status Dashboard** — add a pause indicator: a status line labelled "Countdown" showing either "Decrementing" or "⏸ Paused (low traffic)".

6. **§6.3 JSON Status API (`GET /api/status`)** — add `"countdown_paused"` (`bool`, always present) to the documented JSON schema.

7. **§8 NVS Layout — namespace `esport_cfg`** — add `"ap_thr_kbps"` (`uint16_t`) and `"ap_idle_tmo"` (`uint16_t`) to the key table.

**Acceptance Criteria**

- [ ] Both parameters appear in §3 with correct types, NVS keys, defaults, and ranges.
- [ ] `wifi_mngr_reward_ap_throughput_kbps()` is described in §5.2.
- [ ] §5.5 tick description matches the sliding-window pseudo-code above.
- [ ] `time_ctr_is_paused()` is listed in §5.5 public API.
- [ ] `"countdown_paused"` is in the §6.3 JSON schema.
- [ ] Both NVS keys appear in §8.

---

### Phase 1.2 — Config Manager

**Goal:** Add getters and setters for `soft_ap_dec_time_above_threshold_kbps` and `soft_ap_idle_throughput_timeout_s` to the configuration manager.

**Inputs**
- `docs/1-specification.md` §3, §5.1 (Phase 1.1 output)
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)
- `main/Kconfig.projbuild` (existing)

**Tasks**

1. **`main/Kconfig.projbuild`** — add inside the existing `menu "esport-fi32 Configuration"`:
   ```
   config ESPORT_AP_THRESHOLD_KBPS
       int "Reward AP idle throughput threshold (kbps)"
       range 0 65535
       default 1
   config ESPORT_AP_IDLE_TIMEOUT_S
       int "Reward AP idle throughput timeout (seconds)"
       range 0 65535
       default 30
   ```

2. **`main/inc/config_manager.h`** — declare:
   ```c
   uint16_t  config_mngr_soft_ap_dec_threshold_kbps_get(void);
   esp_err_t config_mngr_soft_ap_dec_threshold_kbps_set(uint16_t val);
   uint16_t  config_mngr_soft_ap_idle_throughput_timeout_s_get(void);
   esp_err_t config_mngr_soft_ap_idle_throughput_timeout_s_set(uint16_t val);
   ```
   Follow the existing Doxygen comment style (multi-line, `\brief`, parameter directions, blank line before `\return`).

3. **`main/src/config_manager.c`** — implement all four functions:
   - Getters: open namespace read-only, read NVS key (fall back to Kconfig default on error), close handle.
   - Setters: full `uint16_t` range `0–65535` is valid; return `ESP_ERR_INVALID_ARG` only if a value exceeds the datatype range (cannot happen for `uint16_t` — effectively always `ESP_OK` for range); open `NVS_READWRITE`, write, commit, close.
   - In `config_mngr_init()`: read `"ap_thr_kbps"` and `"ap_idle_tmo"`; if `ESP_ERR_NVS_NOT_FOUND`, write Kconfig defaults.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_soft_ap_dec_threshold_kbps_set(0)` returns `ESP_OK`.
- [ ] `config_mngr_soft_ap_dec_threshold_kbps_set(65535)` returns `ESP_OK`.
- [ ] Value survives `config_mngr_init()` reinit (simulated reboot).
- [ ] Factory default `1` applied when NVS key absent.
- [ ] `config_mngr_soft_ap_idle_throughput_timeout_s_set(0)` returns `ESP_OK` (disables timeout grace period).
- [ ] Factory default `30` applied when NVS key absent.

---

### Phase 1.3 — WiFi Manager: Throughput Query

**Goal:** Add `wifi_mngr_reward_ap_throughput_kbps()` to the WiFi manager, measuring combined RX+TX traffic on the reward AP interface over each 1-second window.

**Inputs**
- `docs/1-specification.md` §5.2 (Phase 1.1 output)
- `main/inc/wifi_manager.h`, `main/src/wifi_manager.c` (existing)
- `sdkconfig.defaults` (existing)

**Tasks**

1. **`sdkconfig.defaults`** — append `CONFIG_LWIP_STATS=y`.  This enables `esp_netif_get_stats()` on the AP netif.

2. **`main/inc/wifi_manager.h`** — declare:
   ```c
   uint32_t wifi_mngr_reward_ap_throughput_kbps(void);
   ```
   Doxygen: "Returns the combined RX+TX throughput on the reward AP in kbps measured over the previous 1-second call interval.  Returns `0` when the reward AP is inactive or on the first call after activation."

3. **`main/src/wifi_manager.c`** — implement `wifi_mngr_reward_ap_throughput_kbps()`:
   - Add two `static uint64_t` file-scope variables `g_prev_rx_bytes` and `g_prev_tx_bytes` (initialised `0`).
   - Call `esp_netif_get_stats(gp_netif_ap, &stats)`.  On any error or AP inactive (`!gb_reward_ap_active`), return `0`.
   - Compute `delta = (stats.rx_bytes - g_prev_rx_bytes) + (stats.tx_bytes - g_prev_tx_bytes)`.
   - Update `g_prev_rx_bytes` and `g_prev_tx_bytes`.
   - Return `(uint32_t)(delta * 8U / 1000U)`.
   - Inside `wifi_mngr_reward_ap_set(false)`: reset `g_prev_rx_bytes = 0` and `g_prev_tx_bytes = 0`.
   - Function is designed to be called exactly once per second from the time counter tick callback.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Returns `0` when reward AP is inactive.
- [ ] Returns `0` when no clients are connected (0 bytes transferred).
- [ ] Returns a non-zero value during active data transfer on the reward AP.
- [ ] After `wifi_mngr_reward_ap_set(false)`, the next call after re-enabling returns `0` (prev bytes reset).

---

### Phase 1.4 — Time Counter: Sliding-Window Gated Decrement

**Goal:** Update the time counter tick callback to use a sliding-window traffic gate.  Add `time_ctr_is_paused()` to the public API.

**Inputs**
- `docs/1-specification.md` §5.5 (Phase 1.1 output)
- `main/inc/time_counter.h`, `main/src/time_counter.c` (existing)
- `config_manager` (Phase 1.2 output), `wifi_manager` (Phase 1.3 output)

**Tasks**

1. **`main/inc/time_counter.h`** — declare:
   ```c
   bool time_ctr_is_paused(void);
   ```
   Doxygen: "Returns `true` if the countdown is currently paused due to below-threshold throughput on the reward AP."

2. **`main/src/time_counter.c`** — add two file-scope variables protected by `g_spinlock`:
   ```c
   static volatile bool    g_paused      = false;
   static          uint16_t g_below_ticks = 0U;
   ```

3. Replace the body of `time_ctr_tick_cb()` with the sliding-window logic:
   - **Outside spinlock**: call `wifi_mngr_reward_ap_throughput_kbps()` → `throughput`; call `config_mngr_soft_ap_dec_threshold_kbps_get()` → `threshold`; call `config_mngr_soft_ap_idle_throughput_timeout_s_get()` → `timeout`.
   - **Inside spinlock**:
     - If `throughput > threshold`: set `g_below_ticks = 0`, set `g_paused = false`, decrement `g_counter_s` (floored at 0), check `reached_zero`.
     - Else: increment `g_below_ticks`; if `g_below_ticks >= timeout` (or `timeout == 0`): set `g_paused = true`.  Do **not** decrement.  Set `reached_zero = false`.
   - Remainder of the function (posting `ESPORT_EVENT_COUNTER_CHANGED` and handling `reached_zero`) is unchanged.

4. Reset `g_below_ticks = 0` and `g_paused = false` in the `TIME_CTR_STATE_AP_ACTIVE` exit path (i.e. when the state transitions back to `TIME_CTR_STATE_IDLE`, whether by counter reaching zero or session-closed handling).

5. Implement `time_ctr_is_paused()`:
   ```c
   bool time_ctr_is_paused(void)
   {
       portENTER_CRITICAL(&g_spinlock);
       bool paused = g_paused;
       portEXIT_CRITICAL(&g_spinlock);
       return paused;
   }
   ```

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] With `timeout = 5` and `threshold = 10`: counter does not decrement for the first 5 below-threshold ticks but decrements on tick 6 if throughput exceeds threshold in between (window resets).
- [ ] With `timeout = 0`: counter pauses on the very next below-threshold tick.
- [ ] `time_ctr_is_paused()` reflects the current state correctly under concurrent access.
- [ ] `g_below_ticks` is `0` after the reward AP is deactivated and re-activated.
- [ ] No race condition: rapid pulses interleaved with tick still produce a non-negative counter.
- [ ] `ESPORT_EVENT_COUNTER_CHANGED` is still posted every tick regardless of pause state.

---

### Phase 1.5 — Web UI & API: Pause Indicator and Config Fields

**Goal:** Expose the new configuration parameters via the config web page, add `"countdown_paused"` to `/api/status`, and show a live pause indicator on the status dashboard.

**Inputs**
- `docs/1-specification.md` §6.1, §6.2, §6.3 (Phase 1.1 output)
- `main/src/http_server_config.c`, `main/src/http_server_api.c`, `main/src/http_server_dashboard.c` (existing)
- `config_manager` (Phase 1.2 output), `time_counter` (Phase 1.4 output)

**Tasks**

1. **`main/src/http_server_config.c`**:
   - In the config form HTML, add two `<input type="number">` fields:
     - `soft_ap_dec_time_above_threshold_kbps` — label "Reward AP idle throughput threshold (kbps)", min `0`, max `65535`.
     - `soft_ap_idle_throughput_timeout_s` — label "Idle throughput timeout (s)", min `0`, max `65535`.
   - In the GET handler: populate both fields from `config_mngr_soft_ap_dec_threshold_kbps_get()` and `config_mngr_soft_ap_idle_throughput_timeout_s_get()`.
   - In the POST handler: parse both fields with `strtoul`; validate `0–65535`; call the corresponding setters; return HTTP 400 with an error message on out-of-range values.

2. **`main/src/http_server_api.c`**:
   - In the `/api/status` JSON response, append `"countdown_paused": <true|false>` using `time_ctr_is_paused()`.  The field must always be present, including when the reward AP is inactive (value will be `false`).
   - Append `"reward_ap_throughput_kbps": <value>` using `wifi_mngr_reward_ap_throughput_kbps()`.  The field must always be present; `0` when the reward AP is inactive.

3. **`main/src/http_server_dashboard.c`**:
   - In the Exercise Counter section of the dashboard HTML, add a "Traffic" line displaying the live reward AP throughput in kbps, e.g. `<span id="ap-throughput">0</span> kbps` (visible at all times).
   - In the countdown section, add:
     ```html
     <span id="pause-indicator" style="display:none;">⏸ Paused (low traffic)</span>
     ```
   - In the dashboard's JavaScript auto-refresh handler (polling `/api/status`), update the throughput display:
     ```js
     var t = document.getElementById('ap-throughput');
     if (t) t.textContent = data.reward_ap_throughput_kbps;
     ```
   - Also update the pause/decrement indicator visibility using `data.countdown_paused`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Config page renders both new fields with correct current values.
- [ ] Submitting values `0` and `65535` for both fields saves and reflects correctly on reload.
- [ ] Submitting a value of `65536` or a non-numeric string returns HTTP 400.
- [ ] `/api/status` JSON contains `"countdown_paused"` key in all states.
- [ ] `/api/status` JSON contains `"reward_ap_throughput_kbps"` key in all states (0 when AP inactive).
- [ ] Dashboard Exercise Counter section shows current reward AP throughput in kbps.
- [ ] Dashboard throughput display updates on each JS fetch cycle.
- [ ] Dashboard pause indicator is hidden when `countdown_paused` is `false`.
- [ ] Dashboard pause indicator shows "⏸ Paused (low traffic)" when `countdown_paused` is `true`.
- [ ] Devices table has no Rider column; the Nickname column shows &#9733; suffix for the current rider.

---

## Feature 2 — Speed-Gated Pulse Increment

### Overview

Currently every accepted pulse adds `seconds_per_pulse` credits to the time counter regardless of how fast the rider is pedalling.  This feature gates the increment: credits are only added when the rider's instantaneous speed is at or above a configurable minimum (`min_speed_to_increment_time_kmh_x10`, expressed in km/h × 10 for integer precision).  Below that speed, pulses are still counted for session tracking purposes but do **not** add time credits.

This prevents the child from earning internet time by barely touching the pedals.

### New Configuration Parameter

| Parameter                             | Type       | NVS key         | Default | Valid range |
| ------------------------------------- | ---------- | --------------- | ------- | ----------- |
| `min_speed_to_increment_time_kmh_x10` | `uint16_t` | `"min_spd_x10"` | 30      | 0–65535     |

A value of `30` represents 3.0 km/h.  A value of `0` disables the gate entirely (all pulses earn credits, preserving the original behaviour).

### Speed Calculation

Instantaneous speed is derived from the most recent inter-pulse interval measured in the pulse input ISR:

```
speed_kmh_x10 = (centimeters_per_pulse * 360) / last_pulse_interval_ms
```

- `last_pulse_interval_ms` is the elapsed time in milliseconds between the two most recent accepted pulses, computed from `esp_timer_get_time()` timestamps already captured in the ISR.
- If no prior pulse timestamp is available (first pulse of a session), speed is considered `0` and no credits are awarded for that pulse.

---

### Phase 2.1 — Spec Update

**Goal:** Update `docs/1-specification.md` to document the new configuration parameter, the speed calculation method, and the updated pulse credit behaviour.

**Inputs**
- `docs/0-draft-input.md` §Improvements item 2
- `docs/1-specification.md` (current)

**Tasks**

1. **§3 Configuration Parameters table** — add one row:
   - `min_speed_to_increment_time_kmh_x10` — `uint16_t`, NVS key `"min_spd_x10"`, default `30`, range `0–65535`, description: "Minimum instantaneous speed in km/h × 10 required for a pulse to earn time credits.  Set to `0` to disable the gate."

2. **§5.1 NVS Configuration Manager** — add getter/setter entries for the new parameter to the public API table.

3. **§5.4 Pulse Input Module** — document `pulse_in_last_interval_ms_get()` → `uint32_t`, which returns the elapsed time in milliseconds between the two most recent accepted pulses, or `UINT32_MAX` if fewer than two pulses have been accepted since boot (speed indeterminate).

4. **§5.5 Time Counter & Reward AP State Machine** — update the pulse event handler description: before adding credits, compute instantaneous speed using `pulse_in_last_interval_ms_get()` and `config_mngr_centimeters_per_pulse_get()`; skip the credit if speed is below `min_speed_to_increment_time_kmh_x10` and the threshold is non-zero.  Add `time_ctr_current_speed_x10_get()` → `uint32_t` to the public API.

5. **§6.1 Status Dashboard** — document a "Current speed" display line showing the live speed in km/h × 10.

6. **§6.3 JSON Status API (`GET /api/status`)** — add `"current_speed_kmh_x10"` (`uint32_t`, always present, `0` when idle) to the documented JSON schema.

7. **§8 NVS Layout — namespace `esport_cfg`** — add `"min_spd_x10"` (`uint16_t`) to the key table.

**Acceptance Criteria**

- [ ] New parameter appears in §3 with correct type, NVS key, default, and range.
- [ ] `pulse_in_last_interval_ms_get()` is described in §5.4.
- [ ] §5.5 pulse handler description includes the speed gate logic.
- [ ] `time_ctr_current_speed_x10_get()` is listed in §5.5 public API.
- [ ] `"current_speed_kmh_x10"` is in the §6.3 JSON schema.
- [ ] `"min_spd_x10"` appears in §8.

---

### Phase 2.2 — Pulse Input: Last Interval Query

**Goal:** Expose the most recent inter-pulse interval from the pulse input module so the time counter can compute instantaneous speed without duplicating ISR state.

**Inputs**
- `docs/1-specification.md` §5.4 (Phase 2.1 output)
- `main/inc/pulse_input.h`, `main/src/pulse_input.c` (existing)

**Tasks**

1. **`main/src/pulse_input.c`** — add a `static volatile uint32_t g_last_interval_ms = UINT32_MAX` file-scope variable.  In the ISR, after the debounce check passes, capture the previous `last_accepted_us` before updating it, then compute:
   ```c
   uint64_t interval_us = now_us - prev_accepted_us;
   g_last_interval_ms = (interval_us > (uint64_t)UINT32_MAX * 1000ULL)
                        ? UINT32_MAX
                        : (uint32_t)(interval_us / 1000U);
   ```
   The very first accepted pulse (no valid prior timestamp) leaves `g_last_interval_ms = UINT32_MAX`.

2. **`main/inc/pulse_input.h`** — declare:
   ```c
   uint32_t pulse_in_last_interval_ms_get(void);
   ```
   Doxygen: "Returns the elapsed time in milliseconds between the two most recent accepted pulses.  Returns `UINT32_MAX` if fewer than two pulses have been accepted (speed indeterminate).  Safe to call from any task context (volatile read)."

3. **`main/src/pulse_input.c`** — implement `pulse_in_last_interval_ms_get()` as a simple volatile read of `g_last_interval_ms`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Returns `UINT32_MAX` before two pulses have been received.
- [ ] After two pulses separated by a known interval, returns the correct millisecond value (±debounce tolerance).
- [ ] Rapid pulses do not cause wrap-around or negative intervals.

---

### Phase 2.3 — Config Manager: Speed Threshold Parameter

**Goal:** Add a getter and setter for `min_speed_to_increment_time_kmh_x10` to the configuration manager.

**Inputs**
- `docs/1-specification.md` §3, §5.1 (Phase 2.1 output)
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)
- `main/Kconfig.projbuild` (existing)

**Tasks**

1. **`main/Kconfig.projbuild`** — add inside the existing `menu "esport-fi32 Configuration"`:
   ```
   config ESPORT_MIN_SPEED_KMH_X10
       int "Minimum speed to earn time credits (km/h × 10)"
       range 0 65535
       default 30
   ```

2. **`main/inc/config_manager.h`** — declare:
   ```c
   uint16_t  config_mngr_min_speed_to_increment_time_kmh_x10_get(void);
   esp_err_t config_mngr_min_speed_to_increment_time_kmh_x10_set(uint16_t val);
   ```
   Follow the existing Doxygen comment style.

3. **`main/src/config_manager.c`** — implement getter (NVS key `"min_spd_x10"`, fallback to `CONFIG_ESPORT_MIN_SPEED_KMH_X10`) and setter (full `0–65535` range is valid).  Handle the key in `config_mngr_init()` with factory default.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_min_speed_to_increment_time_kmh_x10_set(0)` returns `ESP_OK` (gate disabled).
- [ ] `config_mngr_min_speed_to_increment_time_kmh_x10_set(65535)` returns `ESP_OK`.
- [ ] Value survives `config_mngr_init()` reinit.
- [ ] Factory default `30` applied when NVS key absent.

---

### Phase 2.4 — Time Counter: Speed-Gated Pulse Crediting

**Goal:** Update the pulse event handler in `time_counter.c` to skip credit addition when the rider's instantaneous speed is below `min_speed_to_increment_time_kmh_x10`.

**Inputs**
- `docs/1-specification.md` §5.5 (Phase 2.1 output)
- `main/inc/time_counter.h`, `main/src/time_counter.c` (existing)
- `pulse_input` (Phase 2.2 output), `config_manager` (Phase 2.3 output)

**Tasks**

1. **`main/src/time_counter.c`** — add a file-scope variable protected by `g_spinlock`:
   ```c
   static volatile uint32_t g_current_speed_x10 = 0U;
   ```

2. In `time_ctr_pulse_handler()`, before the credit block, compute instantaneous speed **outside** the spinlock:
   - `interval_ms = pulse_in_last_interval_ms_get()`
   - `cpp = config_mngr_centimeters_per_pulse_get()`
   - `min_spd = config_mngr_min_speed_to_increment_time_kmh_x10_get()`
   - If `interval_ms == UINT32_MAX` or `interval_ms == 0`: `speed_x10 = 0`; else `speed_x10 = (uint32_t)cpp * 36U / interval_ms`

3. **Inside spinlock**: update `g_current_speed_x10 = speed_x10`.  Then apply the gate: if `min_spd > 0` and `speed_x10 < min_spd`, set `b_credit = false` (skip the `g_counter_s` increment).  The remainder of the function (posting `ESPORT_EVENT_COUNTER_CHANGED`) is unchanged.

4. Reset `g_current_speed_x10 = 0` when the state machine returns to `TIME_CTR_STATE_IDLE`.

5. **`main/inc/time_counter.h`** — declare:
   ```c
   uint32_t time_ctr_current_speed_x10_get(void);
   ```
   Doxygen: "Returns the most recently computed instantaneous speed in km/h × 10.  Returns `0` when no speed data is available.  Thread-safe."

6. **`main/src/time_counter.c`** — implement `time_ctr_current_speed_x10_get()` as a spinlock-guarded read of `g_current_speed_x10`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] With `min_spd = 30` (3.0 km/h): pulses producing speed < 3.0 km/h do **not** add credits; pulses at ≥ 3.0 km/h **do** add credits.
- [ ] With `min_spd = 0`: all pulses add credits regardless of speed (original behaviour preserved).
- [ ] First pulse of a session (interval = `UINT32_MAX`) does not add credits when `min_spd > 0`.
- [ ] `time_ctr_current_speed_x10_get()` returns `0` in IDLE state.
- [ ] `ESPORT_EVENT_COUNTER_CHANGED` is posted for every pulse (including gated-out ones), carrying the current counter value.
- [ ] No race conditions under rapid pulse injection interleaved with the tick timer.

---

### Phase 2.5 — Web UI & API: Speed Threshold Config & Live Speed

**Goal:** Expose `min_speed_to_increment_time_kmh_x10` via the config page, add `"current_speed_kmh_x10"` to `/api/status`, and show live speed on the status dashboard.

**Inputs**
- `docs/1-specification.md` §6.1, §6.2, §6.3 (Phase 2.1 output)
- `main/src/http_server_config.c`, `main/src/http_server_api.c`, `main/src/http_server_dashboard.c` (existing)
- `config_manager` (Phase 2.3 output), `time_counter` (Phase 2.4 output)

**Tasks**

1. **`main/src/http_server_config.c`**:
   - Add a `<input type="number">` field labelled "Minimum speed to earn credits (km/h × 10)", min `0`, max `65535`.
   - GET handler: populate from `config_mngr_min_speed_to_increment_time_kmh_x10_get()`.
   - POST handler: parse with `strtoul`; validate `0–65535`; call setter; return HTTP 400 on out-of-range or non-numeric input.

2. **`main/src/http_server_api.c`**:
   - In the `/api/status` JSON response, append `"current_speed_kmh_x10": <value>` using `time_ctr_current_speed_x10_get()`.  Always present; `0` when idle.
   - Append `"speed_gate_active": <bool>` — `true` when `min_speed_to_increment_time_kmh_x10 > 0` and `current_speed_kmh_x10 < min_speed_to_increment_time_kmh_x10`, otherwise `false`.  Always present.

3. **`main/src/http_server_dashboard.c`**:
   - Show current speed as decimal km/h (e.g. `<span id="current-speed">0.0</span> km/h`), dividing the `×10` internal value by 10.  The label must read "km/h", not "km/h × 10".
   - Add a "Pulse crediting" status line in the Exercise Counter section with two mutually exclusive spans driven by `speed_gate_active`:
     ```html
     <span id="speed-gate-indicator" style="display:none;">⊘ Gated (speed too low)</span>
     <span id="speed-credit-indicator" style="display:inline;">Crediting</span>
     ```
   - In the dashboard's JavaScript auto-refresh handler, format speed as one decimal place and update both spans:
     ```js
     var cs = document.getElementById('current-speed');
     if (cs) cs.innerHTML = '<b>' + (d.current_speed_kmh_x10 / 10).toFixed(1) + '</b>';
     var sg = document.getElementById('speed-gate-indicator');
     var sc = document.getElementById('speed-credit-indicator');
     if (sg && sc) {
         sg.style.display = d.speed_gate_active ? 'inline' : 'none';
         sc.style.display = d.speed_gate_active ? 'none'   : 'inline';
     }
     ```

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Config page renders the new field with the correct current value.
- [ ] Submitting `0` and `65535` saves and reflects correctly on reload.
- [ ] Submitting `65536` or a non-numeric string returns HTTP 400.
- [ ] `/api/status` JSON contains `"current_speed_kmh_x10"` in all states.
- [ ] `/api/status` JSON contains `"speed_gate_active"` in all states; `false` when gate is disabled (`min_spd = 0`).
- [ ] Dashboard speed display shows decimal km/h (e.g. "12.3 km/h"), not a raw `×10` integer.
- [ ] Dashboard speed display updates on each JS fetch cycle.
- [ ] Dashboard "Pulse crediting" line shows "⊘ Gated (speed too low)" when `speed_gate_active` is `true`.
- [ ] Dashboard "Pulse crediting" line shows "Crediting" when `speed_gate_active` is `false`.
- [ ] Both "Pulse crediting" spans update on each JS fetch cycle.

---

## Feature 3 — Reward Counter Persistence & Editability

### Overview

Currently the reward time counter is not persisted: a power cycle discards all earned internet time.  This feature saves the counter to NVS regularly and restores it on boot, so earned time is never lost.  It also exposes the counter as an editable field on the configuration page, so a parent can manually grant or adjust internet time without pedalling.

Key behaviours:
- Counter restored from NVS on boot; reward AP enabled immediately if the restored value is non-zero.
- Counter saved to NVS every 60 seconds (hardcoded constant `TIME_CTR_SAVE_INTERVAL_S = 60`).
- Counter saved immediately when it reaches 0 (before AP is disabled).
- Manual edits via the config page take effect immediately at runtime (via `time_ctr_counter_set()`).
- The config page field uses `hh:mm:ss` format for human-readable input.

### New Configuration Parameter

| Parameter          | Type     | NVS key        | Default | Valid range  |
| ------------------ | -------- | -------------- | ------- | ------------ |
| `reward_counter_s` | `uint32` | `reward_ctr_s` | `0`     | 0–UINT32_MAX |

---

### Phase 3.1 — Spec Update

**Goal:** Update `docs/1-specification.md` to document the new parameter, the NVS persistence behaviour, the `time_ctr_counter_set()` API, the `hh:mm:ss` config field format, and all affected sections.

**Inputs**
- `docs/0-draft-input.md` §Improvements item 3
- `docs/1-specification.md` (current)

**Tasks**

1. **§3 Configuration Parameters table** — add one row:
   - `reward_counter_s` — `uint32`, NVS key `"reward_ctr_s"`, default `0`, range `0–(unlimited)`, description: "Reward internet time counter (seconds remaining). Persisted to NVS every 60 s and immediately on AP disable; restored on boot. Setting a non-zero value via the config page enables the reward AP immediately."

2. **§5.1 NVS Configuration Manager** — add getter/setter to the public API:
   ```c
   uint32_t  config_mngr_reward_counter_s_get(void);
   esp_err_t config_mngr_reward_counter_s_set(uint32_t val);
   ```

3. **§5.5 Time Counter** — add:
   - Two new responsibility bullets: **NVS persistence** (boot restore + periodic save + save-on-zero) and **Runtime counter override** (`time_ctr_counter_set()`).
   - A **Boot restore** note before the **Thread safety** note.
   - `time_ctr_counter_set(uint32_t val)` → `esp_err_t` to the public API.

4. **§6.2 Configuration Page** — add "Reward Counter" (`hh:mm:ss` text field) to the fields table; add a note explaining the format and immediate-effect behaviour.

5. **§7.1 Boot Sequence** — annotate step 8 (`time_ctr_init`) to mention counter restore.

6. **§7.6 Config Change via Web** — add a `reward_counter_s` branch that calls `time_ctr_counter_set()`.

7. **§8 NVS Layout — namespace `esport_cfg`** — add `"reward_ctr_s"` (`uint32`) row.

**Acceptance Criteria**

- [ ] `reward_counter_s` appears in §3 with correct type, NVS key, default, and range.
- [ ] Getter/setter appear in §5.1 API.
- [ ] §5.5 lists the two new responsibility bullets.
- [ ] `time_ctr_counter_set()` is in the §5.5 public API.
- [ ] §6.2 fields table contains "Reward Counter" with `hh:mm:ss` format note.
- [ ] §7.1 step 8 mentions counter restore.
- [ ] §7.6 includes `reward_counter_s` handling.
- [ ] `"reward_ctr_s"` appears in §8.

---

### Phase 3.2 — Config Manager: Reward Counter Parameter

**Goal:** Add getter and setter for `reward_counter_s` to the configuration manager.  No Kconfig entry is needed — the default is hardcoded `0` because there is no useful build-time starting value.

**Inputs**
- `docs/1-specification.md` §3, §5.1 (Phase 3.1 output)
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)

**Tasks**

1. **`main/src/config_manager.c`** — add:
   - `#define CONFIG_MNGR_KEY_REWARD_COUNTER_S  ("reward_ctr_s")`
   - `#define CONFIG_MNGR_DEF_REWARD_COUNTER_S  ((uint32_t)0U)`
   - In `config_mngr_init()`: call `config_mngr_default_u32_write(handle, CONFIG_MNGR_KEY_REWARD_COUNTER_S, CONFIG_MNGR_DEF_REWARD_COUNTER_S)`.

2. **`main/inc/config_manager.h`** — declare (with Doxygen, following existing style):
   ```c
   uint32_t  config_mngr_reward_counter_s_get(void);
   esp_err_t config_mngr_reward_counter_s_set(uint32_t val);
   ```

3. **`main/src/config_manager.c`** — implement:
   - `config_mngr_reward_counter_s_get()`: `config_mngr_u32_get(CONFIG_MNGR_KEY_REWARD_COUNTER_S, CONFIG_MNGR_DEF_REWARD_COUNTER_S)`.
   - `config_mngr_reward_counter_s_set(val)`: `config_mngr_u32_set(CONFIG_MNGR_KEY_REWARD_COUNTER_S, val, 0U, UINT32_MAX)`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_reward_counter_s_set(0)` returns `ESP_OK`.
- [ ] `config_mngr_reward_counter_s_set(UINT32_MAX)` returns `ESP_OK`.
- [ ] Value survives `config_mngr_init()` reinit (simulated reboot).
- [ ] Factory default `0` applied when NVS key absent.

---

### Phase 3.3 — Time Counter: NVS Persistence & Counter Set

**Goal:** Implement counter restore on boot, periodic NVS save (60 s), save-on-zero, and `time_ctr_counter_set()`.

**Inputs**
- `docs/1-specification.md` §5.5 (Phase 3.1 output)
- `main/inc/time_counter.h`, `main/src/time_counter.c` (existing)
- `config_manager` (Phase 3.2 output)

**Tasks**

1. **`main/src/time_counter.c`** — add:
   ```c
   #define TIME_CTR_SAVE_INTERVAL_S (60U)
   ```
   And a file-scope `static esp_timer_handle_t g_save_timer`.

2. In `time_ctr_init()`:
   - Read `config_mngr_reward_counter_s_get()` → `restored_val`.
   - If `restored_val > 0`:
     - **Inside spinlock**: set `g_counter_s = restored_val`, set state to `TIME_CTR_STATE_AP_ACTIVE`.
     - **Outside spinlock**: call `wifi_mngr_reward_ap_set(true)`, post `ESPORT_EVENT_REWARD_AP_ON`, start the decrement tick timer.
   - Create and start the periodic save timer (`TIME_CTR_SAVE_INTERVAL_S * 1 000 000 µs`, periodic) with callback `time_ctr_save_cb`.

3. **`time_ctr_save_cb()`** — static callback: calls `config_mngr_reward_counter_s_set(time_ctr_get())`.

4. In the zero-reached-handling code inside the tick callback: call `config_mngr_reward_counter_s_set(0U)` **before** calling `wifi_mngr_reward_ap_set(false)`.

5. **`time_ctr_counter_set(uint32_t val)`** (new public function):
   - **Inside spinlock**: set `g_counter_s = val`; capture `old_state = g_state`.
   - **Outside spinlock**: call `config_mngr_reward_counter_s_set(val)`.
   - If `val > 0` and `old_state == TIME_CTR_STATE_IDLE`: transition to `AP_ACTIVE` (call `wifi_mngr_reward_ap_set(true)`, post `ESPORT_EVENT_REWARD_AP_ON`, start tick timer, reset pause state). Post `ESPORT_EVENT_COUNTER_CHANGED`.
   - If `val == 0`: post `ESPORT_EVENT_COUNTER_CHANGED`; the running tick timer handles AP shutdown on the next tick (≤ 1 s).
   - Return `ESP_OK`.

6. **`main/inc/time_counter.h`** — declare (with Doxygen):
   ```c
   esp_err_t time_ctr_counter_set(uint32_t val);
   ```

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] On simulated reboot after setting a non-zero counter value: counter is restored and reward AP is enabled.
- [ ] Counter value is persisted to NVS within 60 s of any change.
- [ ] When counter reaches 0: NVS reads back `0` after the tick.
- [ ] `time_ctr_counter_set(300)` while IDLE enables the reward AP within one event loop cycle.
- [ ] `time_ctr_counter_set(300)` while AP_ACTIVE updates the live counter immediately.
- [ ] `time_ctr_counter_set(0)` while AP_ACTIVE results in AP being disabled within 1 s.
- [ ] `ESPORT_EVENT_COUNTER_CHANGED` is posted by `time_ctr_counter_set()`.
- [ ] No race conditions under concurrent pulse events and timer callbacks.

---

### Phase 3.4 — Web UI: Reward Counter Config Field

**Goal:** Add the `reward_counter_s` field (in `hh:mm:ss` format) to the configuration web page and wire the POST handler to call `time_ctr_counter_set()` for immediate effect.

**Inputs**
- `docs/1-specification.md` §6.2 (Phase 3.1 output)
- `main/src/http_server_config.c` (existing)
- `time_counter` (Phase 3.3 output), `config_manager` (Phase 3.2 output)

**Tasks**

1. **GET handler** — add one field to the config form HTML:
   - Label: "Reward Counter (hh:mm:ss)"
   - `<input type="text" name="reward_counter_s" placeholder="0:00:00" value="%s">`
   - Populate by formatting `config_mngr_reward_counter_s_get()` as `hh:mm:ss`:
     ```c
     uint32_t secs = config_mngr_reward_counter_s_get();
     snprintf(ctr_buf, sizeof(ctr_buf), "%u:%02u:%02u",
              secs / 3600U, (secs % 3600U) / 60U, secs % 60U);
     ```

2. **POST handler** — parse `reward_counter_s`:
   - Read raw string from form body.
   - Split on `:` expecting exactly two `:` separators (three tokens: hours, minutes, seconds).
   - Convert each token with `strtoul`; compute `total_s = h*3600 + m*60 + s`.
   - On parse failure (non-numeric, wrong number of separators): return HTTP 400 with error message.
   - Call `time_ctr_counter_set(total_s)` — this applies immediately and saves to NVS. Do **not** call `config_mngr_reward_counter_s_set()` separately.

3. Confirm that `ESPORT_EVENT_CONFIG_CHANGED` is posted after the successful save (this is already done by the existing POST handler; verify `reward_counter_s` is handled in the same commit path).

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Config page renders the "Reward Counter" field pre-populated with the current value in `hh:mm:ss` format.
- [ ] Submitting `1:30:00` sets the counter to 5400 s and enables the reward AP if it was inactive.
- [ ] Submitting `0:00:00` sets the counter to 0 s; AP shuts down within 1 s if active.
- [ ] Submitting an empty string, `abc`, or a value with too few/many `:` returns HTTP 400.
- [ ] After submit, the page reloads and shows the updated value in `hh:mm:ss`.
- [ ] The live counter on the dashboard (`counter_s` in `/api/status`) reflects the new value within 2 s.
- [ ] NVS persists the value (verified by simulated reboot reading `config_mngr_reward_counter_s_get()`).

---

## Feature 4 — Per-Device Internet Access Control

### Overview

Currently the reward Soft AP is torn down when the global time counter reaches zero, cutting internet access for every connected device simultaneously. This feature replaces that blunt mechanism with per-device internet gating: the Soft AP remains visible from boot at all times, and each registered device has its own credit counter. Only registered devices with remaining credits and an enabled flag can route traffic to the internet; unregistered or expired devices stay associated to the AP and can reach the gateway (status dashboard) but cannot reach the internet.

A device registry of up to four entries stores: MAC address, a human-readable nickname (up to 15 characters), a per-device internet time counter (`counter_s`), and an enabled/disabled toggle. A "current rider" selection wires the bike sensor to one specific device: credits earned during a confirmed exercise session are added to that device's counter. All other aspects of the time counter state machine (session qualification, threshold, traffic gate, speed gate) are unchanged.

Per-device counter decrement runs independently of the credit-earning state: every registered device that is currently connected, has `b_enabled == true`, and has `counter_s > 0` loses one second per tick, regardless of whether anyone is pedalling.

Key behaviours:

- Reward Soft AP is **always on** from boot. `wifi_mngr_reward_ap_set(false)` becomes a no-op.
- Internet access is enforced via an **IP-layer filter** in the existing `wifi_mngr_ap_input_hook`: IPv4 packets destined for addresses outside `192.168.5.0/24` are silently dropped if the source MAC is not internet-allowed. ARP and local traffic are always passed through.
- A **device registry** (`device_registry.c/h`) manages all per-device state with NVS persistence.
- The **current rider** is selected on the `/config` page; bike credits go to that device's counter.
- The `/config` page provides full device management: add, remove, rename, set counter (`hh:mm:ss`), toggle enabled, select current rider.
- The global `g_counter_s` and its NVS persistence (Feature 3) are removed from `time_counter.c`; per-device NVS persistence is handled entirely by `device_registry.c`.
- The 1-second tick timer in `time_counter.c` is started at init and **runs permanently**. The tick callback calls `device_reg_tick()` **unconditionally** — the traffic gate is applied inside `device_reg_tick()` independently per device.
- The global traffic gate (`g_paused`, `g_below_ticks`, `time_ctr_is_paused()`) from Feature 1 is **removed** from `time_counter.c`. The same sliding-window logic is re-applied per device inside `device_reg_tick()`, using the same global configuration parameters (`soft_ap_dec_time_above_threshold_kbps`, `soft_ap_idle_throughput_timeout_s`).
- Per-device byte counters (RX and TX) are maintained in `device_registry.c` via `device_reg_mac_rx_bytes_add()` / `device_reg_mac_tx_bytes_add()`, called from the existing `wifi_mngr_ap_input_hook` and `wifi_mngr_ap_linkoutput_hook`. Each device's throughput is computed and its pause state updated once per second inside `device_reg_tick()`.
- The top-level `"countdown_paused"` field is **removed** from `GET /api/status`. Per-device pause and throughput are exposed as `"paused"` and `"throughput_kbps"` inside each element of the `"devices"` array.

---

### Phase 4.1 — Device Registry Module

#### Goal

Create `device_registry.c` / `device_registry.h` — the central store for registered devices, their nicknames, per-device internet counters, enabled flags, and the current rider selection. All data is NVS-backed. Add the new `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` event to `event_ids.h`. Register the module in `CMakeLists.txt`.

#### Inputs

- `docs/1-specification.md` (current — §5 module spec conventions, §8 NVS conventions, §11 coding conventions)
- `main/inc/event_ids.h` (existing)
- `main/CMakeLists.txt` (existing)

#### Data Model

```c
#define DEVICE_REG_MAX_ENTRIES    (4U)
#define DEVICE_REG_NICKNAME_MAX_LEN (15U)
#define DEVICE_REG_NO_RIDER       (0xFFU)
#define DEVICE_REG_SAVE_INTERVAL_S (60U)

typedef struct device_reg_entry_tag {
    uint8_t  mac[6];
    char     nickname[DEVICE_REG_NICKNAME_MAX_LEN + 1U]; /* null-terminated */
    uint32_t counter_s;
    bool     b_enabled;
} device_reg_entry_t;
```

**NVS Namespace:** `esport_dev`

| NVS Key    | Type   | Description                                        |
| ---------- | ------ | -------------------------------------------------- |
| `dev_count` | uint8 | Number of registered entries (0–4); `0` on first boot |
| `dev_0` … `dev_3` | blob | One `device_reg_entry_t` per slot             |
| `dev_rider` | uint8 | Current rider index, or `DEVICE_REG_NO_RIDER` (0xFF) |

**Per-device traffic state (RAM only — not persisted to NVS):**

The following per-device arrays live in `device_registry.c` as file-scope variables, zeroed on `device_reg_init()`. They are never written to NVS because throughput is a live metric that resets each second.

| File-scope array        | Type                   | Description                                                  |
| ----------------------- | ---------------------- | ------------------------------------------------------------ |
| `g_rx_bytes[4]`         | `volatile uint32_t`    | Cumulative RX bytes per device since last tick               |
| `g_tx_bytes[4]`         | `volatile uint32_t`    | Cumulative TX bytes per device since last tick               |
| `g_throughput_kbps[4]`  | `uint32_t`             | Last computed RX+TX kbps per device (updated in tick)        |
| `g_below_ticks[4]`      | `uint16_t`             | Consecutive below-threshold ticks per device                 |
| `g_dev_paused[4]`       | `bool`                 | Current traffic-gate pause state per device                  |

All five arrays are indexed by device slot (0–3), matching the `g_entries[]` index.

#### Public API

```c
esp_err_t device_reg_init(void);
uint8_t   device_reg_count_get(void);
esp_err_t device_reg_entry_add(const uint8_t *p_mac, const char *p_nickname);
esp_err_t device_reg_entry_remove(uint8_t idx);
esp_err_t device_reg_entry_get(uint8_t idx, device_reg_entry_t *p_out);
esp_err_t device_reg_entry_nickname_set(uint8_t idx, const char *p_nickname);
esp_err_t device_reg_entry_enabled_set(uint8_t idx, bool b_enabled);
esp_err_t device_reg_entry_counter_set(uint8_t idx, uint32_t counter_s);
uint32_t  device_reg_entry_counter_get(uint8_t idx);       /* 0 when idx out of range */
int8_t    device_reg_mac_find(const uint8_t *p_mac);       /* -1 if not found */
bool      device_reg_mac_internet_allowed(const uint8_t *p_mac); /* registered + b_enabled + counter_s > 0 */
uint8_t   device_reg_current_rider_get(void);              /* DEVICE_REG_NO_RIDER when none selected */
esp_err_t device_reg_current_rider_set(uint8_t idx);              /* DEVICE_REG_NO_RIDER clears selection */
esp_err_t device_reg_tick(void);                                  /* call once per second from time_counter tick */
void      device_reg_mac_rx_bytes_add(const uint8_t *p_mac, uint32_t bytes); /* called from lwIP input hook */
void      device_reg_mac_tx_bytes_add(const uint8_t *p_mac, uint32_t bytes); /* called from lwIP linkoutput hook */
uint32_t  device_reg_entry_throughput_kbps_get(uint8_t idx);      /* last 1-s kbps for device; 0 when unknown idx */
bool      device_reg_entry_is_paused(uint8_t idx);                /* true when traffic gate is holding decrement */
```

#### Tasks

1. **`main/inc/event_ids.h`** — add `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` to `esport_event_id_t` with the next sequential explicit integer value. Post this event (no payload) whenever any entry is added, removed, or modified.

2. **Create `main/inc/device_registry.h`**:
   - Define `DEVICE_REG_MAX_ENTRIES`, `DEVICE_REG_NICKNAME_MAX_LEN`, `DEVICE_REG_NO_RIDER`, `DEVICE_REG_SAVE_INTERVAL_S` as `#define` constants (each replacement value in parentheses).
   - Define `device_reg_entry_t` with the tag name `device_reg_entry_tag`.
   - Declare all public API functions with Doxygen (`\\` tags, `\\param[in/out]`, blank line before `\\return`).
   - Use `hhtemplate` structure; guard with `DEVICE_REGISTRY_H`.

3. **Create `main/src/device_registry.c`**:
   - Declare `static const char *gp_tag = "device_reg"`.
   - Declare `static device_reg_entry_t g_entries[DEVICE_REG_MAX_ENTRIES]`.
   - Declare `static uint8_t g_count = 0U` and `static uint8_t g_rider = DEVICE_REG_NO_RIDER`.
   - Declare `static portMUX_TYPE g_dev_mux = portMUX_INITIALIZER_UNLOCKED` for spinlock.
   - Declare `static uint16_t g_tick_count = 0U` (periodic save counter).
   - Declare per-device traffic state arrays (all `[DEVICE_REG_MAX_ENTRIES]`, zeroed in `device_reg_init()`):
     - `static volatile uint32_t g_rx_bytes[DEVICE_REG_MAX_ENTRIES]` — cumulative RX bytes since last tick.
     - `static volatile uint32_t g_tx_bytes[DEVICE_REG_MAX_ENTRIES]` — cumulative TX bytes since last tick.
     - `static uint32_t g_throughput_kbps[DEVICE_REG_MAX_ENTRIES]` — last computed kbps per device.
     - `static uint16_t g_below_ticks[DEVICE_REG_MAX_ENTRIES]` — consecutive below-threshold ticks per device.
     - `static bool g_dev_paused[DEVICE_REG_MAX_ENTRIES]` — pause state per device.
   - Internal helpers (static, prototyped in Internal Function Prototypes section):
     - `device_reg_entry_save(uint8_t idx)` — opens `esport_dev` NVS_READWRITE, writes `dev_N` blob, commits, closes.
     - `device_reg_meta_save(void)` — saves `dev_count` and `dev_rider`.
     - `device_reg_counters_save_all(void)` — calls `device_reg_entry_save()` for every index `< g_count`.

4. **Implement `device_reg_init()`**:
   - Open namespace `esport_dev` NVS_READWRITE.
   - Read `dev_count`; if `ESP_ERR_NVS_NOT_FOUND` write `0`.
   - Validate `g_count <= DEVICE_REG_MAX_ENTRIES`; if invalid, reset to 0 and erase metadata.
   - For each index `< g_count`: read `dev_N` blob into `g_entries[N]`; on error log warning and set that entry to zeroed state.
   - Read `dev_rider`; validate `< g_count || == DEVICE_REG_NO_RIDER`; reset to `DEVICE_REG_NO_RIDER` on invalid value.
   - Close handle.

5. **Implement `device_reg_entry_add()`**:
   - Under spinlock: return `ESP_ERR_NO_MEM` if `g_count == DEVICE_REG_MAX_ENTRIES`.
   - Iterate existing entries; return `ESP_ERR_INVALID_STATE` if MAC already present.
   - Validate `p_nickname`: non-NULL, non-empty, `strlen <= DEVICE_REG_NICKNAME_MAX_LEN`; return `ESP_ERR_INVALID_ARG` on failure.
   - Copy MAC and nickname into `g_entries[g_count]`; set `counter_s = 0`, `b_enabled = true`; increment `g_count`.
   - Outside spinlock: call `device_reg_entry_save(new_idx)` and `device_reg_meta_save()`.
   - Post `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED`.

6. **Implement `device_reg_entry_remove()`**:
   - Return `ESP_ERR_INVALID_ARG` if `idx >= g_count`.
   - Under spinlock: shift entries `[idx+1 … g_count-1]` left by one (compact array). Decrement `g_count`. Adjust `g_rider`: if `g_rider == idx`, set to `DEVICE_REG_NO_RIDER`; if `g_rider > idx`, decrement by 1.
   - Outside spinlock: rewrite all blobs `dev_0` … `dev_{g_count-1}` (the shifted set) plus `dev_count` and `dev_rider` to NVS. Delete the now-stale last blob key (`"dev_N"` where N = old `g_count - 1`) using `nvs_erase_key()`.
   - Post `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED`.

7. **Implement `device_reg_tick()`**:

   This function implements the per-device sliding-window traffic gate and decrements each eligible counter. It must be called exactly once per second.

   a. **Read global gate parameters** (outside spinlock):
      - `threshold = config_mngr_soft_ap_dec_threshold_kbps_get()`
      - `timeout   = config_mngr_soft_ap_idle_throughput_timeout_s_get()`

   b. **Get connected stations**: call `esp_wifi_ap_get_sta_list(&sta_list)`.

   c. **Per-device gate and decrement** — for each index `i < g_count`:

      Under `g_dev_mux` spinlock:
      - Compute throughput: `delta = g_rx_bytes[i] + g_tx_bytes[i]`; set `g_throughput_kbps[i] = (uint32_t)(delta * 8U / 1000U)`; reset `g_rx_bytes[i] = 0; g_tx_bytes[i] = 0`.
      - Apply the sliding-window gate (identical logic to Feature 1 Overview pseudo-code, using `g_throughput_kbps[i]`, `threshold`, `timeout`, `g_below_ticks[i]`, `g_dev_paused[i]`).
      - If `!g_dev_paused[i]` AND `g_entries[i].b_enabled` AND `g_entries[i].counter_s > 0`:
        - Check if `g_entries[i].mac` matches any `sta_list.sta[j].mac`; if connected, decrement `g_entries[i].counter_s`, set `b_changed = true`, and if it just reached 0 set `b_zero[i] = true`.

      Exit spinlock.

   d. **Post-tick saves** (outside spinlock):
      - For any `b_zero[i]`: call `device_reg_entry_save(i)` immediately.
      - Post `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` if any `b_changed` was set.

   e. **Periodic full save**: increment `g_tick_count`; if `g_tick_count >= DEVICE_REG_SAVE_INTERVAL_S`, call `device_reg_counters_save_all()` and reset `g_tick_count = 0`.

8. **Implement all remaining getters/setters** following the pattern of existing config manager functions: spinlock for in-RAM state; NVS write in setters; validate `idx < g_count`; return `ESP_ERR_INVALID_ARG` on out-of-range index.

9. **`device_reg_mac_internet_allowed()`**: under spinlock, linear scan of `g_entries`; return `true` iff found AND `b_enabled == true` AND `counter_s > 0`. Must complete in O(4) — no NVS access.

10. **Implement `device_reg_mac_rx_bytes_add(const uint8_t *p_mac, uint32_t bytes)`**:
    - Under spinlock: linear scan `g_entries[0..g_count-1]`; if MAC matches entry `i`, add `bytes` to `g_rx_bytes[i]`. If not found, no-op. Must be safe to call from the lwIP driver task: O(4) scan, no NVS access, no heap allocation.

11. **Implement `device_reg_mac_tx_bytes_add(const uint8_t *p_mac, uint32_t bytes)`**:
    - Identical to `device_reg_mac_rx_bytes_add()` but increments `g_tx_bytes[i]`.

12. **Implement `device_reg_entry_throughput_kbps_get(uint8_t idx)`**:
    - Under spinlock: return `g_throughput_kbps[idx]` if `idx < g_count`; otherwise return `0`.

13. **Implement `device_reg_entry_is_paused(uint8_t idx)`**:
    - Under spinlock: return `g_dev_paused[idx]` if `idx < g_count`; otherwise return `false`.

14. **`main/CMakeLists.txt`** — add `"src/device_registry.c"` to the `SRCS` list.

15. **`main/src/main.c`** — call `device_reg_init()` after `config_mngr_init()` and before `wifi_mngr_init()` in the boot sequence, guarded by `ESP_ERROR_CHECK`.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `device_reg_entry_add()` with four different MACs returns `ESP_OK` each time; fifth call returns `ESP_ERR_NO_MEM`.
- [ ] Adding a duplicate MAC returns `ESP_ERR_INVALID_STATE`.
- [ ] Empty or oversized nickname returns `ESP_ERR_INVALID_ARG`.
- [ ] `device_reg_mac_internet_allowed()` returns `false` for an unknown MAC.
- [ ] `device_reg_mac_internet_allowed()` returns `false` for a known MAC with `counter_s == 0`.
- [ ] `device_reg_mac_internet_allowed()` returns `false` for a known MAC with `b_enabled == false`.
- [ ] `device_reg_mac_internet_allowed()` returns `true` for a known MAC with `b_enabled == true` and `counter_s > 0`.
- [ ] After `device_reg_init()` reinit (simulated reboot), all entries, counters, `g_rider`, and `g_count` are restored from NVS.
- [ ] `device_reg_entry_remove(0)` with two entries: entry at index 1 is now at index 0; `g_count == 1`.
- [ ] If removed index equals `g_rider`, `g_rider` is reset to `DEVICE_REG_NO_RIDER`.
- [ ] If removed index is less than `g_rider`, `g_rider` decrements by 1.
- [ ] `device_reg_tick()` decrements only entries that are enabled, have `counter_s > 0`, whose MAC appears in the AP station list, and whose per-device traffic gate is not paused.
- [ ] When `counter_s` reaches 0 during a tick, NVS holds `0` for that entry immediately after the tick.
- [ ] `ESPORT_EVENT_DEVICE_REGISTRY_CHANGED` is posted on every add, remove, and counter-reaches-zero event.
- [ ] With `threshold = 10 kbps` and `timeout = 5`: a device's counter does not decrement after 5 consecutive below-threshold ticks; resumes when its traffic exceeds the threshold.
- [ ] With `timeout = 0`: a device's counter pauses on the very next below-threshold tick.
- [ ] A second device's counter is unaffected while the first device is paused (independent per-device gate).
- [ ] `device_reg_mac_rx_bytes_add()` increments only the slot matching the supplied MAC; all other slots are unchanged.
- [ ] `device_reg_entry_throughput_kbps_get(i)` returns the kbps computed in the last tick for slot `i`.
- [ ] `device_reg_entry_is_paused(i)` reflects the current gate state for slot `i`.

---

### Phase 4.2 — WiFi Manager: Always-On Reward AP & MAC Filtering

#### Goal

Make the reward Soft AP start at boot and remain active permanently. Add per-device MAC filtering inside the existing `wifi_mngr_ap_input_hook` to enforce per-device internet access control at the IP layer.

#### Inputs

- `docs/1-specification.md` §5.2
- `main/src/wifi_manager.c`, `main/inc/wifi_manager.h` (existing)
- `device_registry` (Phase 4.1 output)

#### Tasks

1. **Always-on reward AP** — in `wifi_mngr_init()`, after all event handlers are registered and the STA connection attempt has been issued, call `wifi_mngr_reward_ap_set(true)`. This starts the reward AP from boot.

2. **Make `wifi_mngr_reward_ap_set(false)` a no-op** — in the `b_enable == false` branch, log `ESP_LOGD(gp_tag, "reward AP always-on: disable request ignored")` and return `ESP_OK`. Do not change any state, do not tear down the AP, do not disable NAPT. The `true` branch is completely unchanged (reconfigures SSID/password/subnet/NAPT on every call). This preserves compilation of all existing callers without any changes.

3. **Per-device byte counting** — add calls to the device registry byte-add functions in both hook functions, immediately **before** the existing global byte-count spinlock block:
   - In `wifi_mngr_ap_input_hook`: call `device_reg_mac_rx_bytes_add((const uint8_t *)p->payload + 6, (uint32_t)p->tot_len)`. The source MAC (bytes `[6..11]` of the Ethernet header) identifies the sending device.
   - In `wifi_mngr_ap_linkoutput_hook`: call `device_reg_mac_tx_bytes_add((const uint8_t *)p->payload + 0, (uint32_t)p->tot_len)`. The destination MAC (bytes `[0..5]`) identifies the receiving device.

   Both calls are placed before the global spinlock block (no ordering dependency). Both are O(4), spinlock-guarded inside `device_registry.c`, and safe to make from the WiFi driver task.

4. **MAC filter in `wifi_mngr_ap_input_hook`** — immediately after the per-device and global byte-count blocks and before the final `return gp_orig_ap_input(p, inp)` call, add the following logic:
   - Guard: `if (p->len >= 34U)` — ensures at least 14 bytes of Ethernet header plus 20 bytes of IPv4 header (up to destination address) are present in the first pbuf segment. On the ESP32 WiFi driver, AP client frames always arrive with the full Ethernet + IP header in the first segment; add a code comment documenting this assumption.
   - Extract `ethertype = (uint16_t)(((const uint8_t *)p->payload)[12] << 8) | ((const uint8_t *)p->payload)[13]`.
   - If `ethertype == 0x0800U` (IPv4):
     - Extract `dst_ip` from bytes `[30..33]` of `p->payload` as a big-endian `uint32_t`.
     - If `(dst_ip & 0xFFFFFF00U) != 0xC0A80500U` (destination is outside the `192.168.5.0/24` subnet, i.e. internet-bound):
       - Call `device_reg_mac_internet_allowed((const uint8_t *)p->payload + 6)`.
       - If not allowed: call `pbuf_free(p)` and `return ERR_OK` (silently drop the frame; do not call the original input function).
   - All other frames — ARP (EtherType `0x0806`), local IPv4 destinations, non-IPv4 — fall through unchanged to `gp_orig_ap_input(p, inp)`.
   - Add `#include "device_registry.h"` at the top of `wifi_manager.c`.

5. **No changes to `main/inc/wifi_manager.h`** — the public API is unchanged. The filter is an internal implementation detail.

#### Notes

> `device_reg_mac_internet_allowed()` is called in the lwIP input path (WiFi driver task context on ESP32-C6). It acquires a `portMUX_TYPE` spinlock and performs a linear scan of four entries. On a single-core ESP32-C6, `portENTER_CRITICAL` disables interrupts briefly; the O(4) scan with no NVS access completes in well under 1 µs. This is acceptable for the lwIP fast path.

> 802.1Q VLAN-tagged frames (EtherType `0x8100`) are not handled; they are passed through without filtering. VLAN tagging is not used on home networks or by the ESP32 AP.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Reward AP SSID (`esport-fi32` by default) is visible to Wi-Fi scanning devices immediately after boot, before any pedalling.
- [ ] `wifi_mngr_reward_ap_set(false)` returns `ESP_OK` and the reward AP remains active.
- [ ] An unregistered device can associate to the AP and receives a DHCP lease (`192.168.5.x`).
- [ ] The unregistered device can ping `192.168.5.1` (the gateway) but cannot ping an external address (e.g. `8.8.8.8`).
- [ ] A registered device with `b_enabled == true` and `counter_s > 0` can ping an external address.
- [ ] A registered device with `counter_s == 0` cannot ping an external address.
- [ ] A registered device with `b_enabled == false` cannot ping an external address.
- [ ] ARP and DHCP traffic is never blocked; unregistered devices always keep their IP lease.
- [ ] Toggling `b_enabled` from `false` to `true` (via `device_reg_entry_enabled_set`) takes effect on the next frame (no AP restart required).
- [ ] After a registered device sends traffic, `device_reg_entry_throughput_kbps_get()` returns a non-zero value on the following tick.
- [ ] Per-device byte counters reset to `0` each tick; `device_reg_entry_throughput_kbps_get()` returns `0` in the tick after the device goes idle.

---

### Phase 4.3 — Time Counter: Per-Device Credit Earning & Tick Integration

#### Goal

Replace the global counter with per-device credit logic. Pulse credits go to the current rider's device_reg counter. The 1-second tick timer runs permanently from `time_ctr_init()` and delegates device counter decrement entirely to `device_reg_tick()`. Remove all `wifi_mngr_reward_ap_set()` calls. Remove the global `g_counter_s` and its NVS persistence (Feature 3). Update `time_ctr_get()` and `time_ctr_counter_set()` to operate on the current rider's counter.

#### Inputs

- `main/inc/time_counter.h`, `main/src/time_counter.c` (existing, Features 1–3 output)
- `device_registry` (Phase 4.1 output)
- `wifi_manager` (Phase 4.2 output — `wifi_mngr_reward_ap_set(false)` is now a no-op)

#### Tasks

1. **Remove `g_counter_s`** (the global credit counter). Replace with a local session accumulator:
   ```c
   static volatile uint32_t g_session_credits = 0U;
   ```
   This accumulates pulse credits during `TIME_CTR_STATE_SESSION` (before the threshold is crossed). It is reset to `0` when the session closes before the threshold fires. It is flushed to the current rider's `device_reg` counter when the threshold fires (transition to `TIME_CTR_STATE_EARNING`).

2. **Rename `TIME_CTR_STATE_AP_ACTIVE` to `TIME_CTR_STATE_EARNING`** in the enum definition. Update all uses within `time_counter.c`. This is a purely internal rename (the enum is `static`).

3. **Always-running tick timer** — in `time_ctr_init()`, start the 1-second periodic tick timer immediately (do not wait for a state transition). Remove the `esp_timer_start` call from the threshold-fired handler. Remove the `esp_timer_stop` call from the counter-reached-zero handler and from any state transition back to `TIME_CTR_STATE_IDLE`. The timer now runs for the lifetime of the firmware.

4. **Remove the periodic NVS save timer** (`g_save_timer` from Feature 3) and its callback `time_ctr_save_cb`. The per-device NVS persistence is now owned by `device_registry.c`.

5. **Boot restore** — in `time_ctr_init()`, remove the restore of `config_mngr_reward_counter_s_get()`. Per-device counters are already restored by `device_reg_init()` (called before `time_ctr_init()` in `main.c`). **Migration:** if `config_mngr_reward_counter_s_get()` returns a value `> 0` and `device_reg_current_rider_get() != DEVICE_REG_NO_RIDER` and the current rider's `device_reg_entry_counter_get()` returns `0`, set the current rider's counter to the migrated value via `device_reg_entry_counter_set()`, then call `config_mngr_reward_counter_s_set(0U)` to clear the old key. Log the migration at `ESP_LOGI` level. This one-time migration prevents loss of earned time when upgrading from Feature 3.

6. **Pulse handler (`time_ctr_pulse_event_handler`)** — update credit logic:
   - Speed gate check (Feature 2): unchanged — `b_credit` flag is still computed before entering the spinlock.
   - In `TIME_CTR_STATE_SESSION` (under spinlock): if `b_credit`, increment `g_session_credits` by `config_mngr_seconds_per_pulse_get()`.
   - In `TIME_CTR_STATE_EARNING` (under spinlock): if `b_credit` and `device_reg_current_rider_get() != DEVICE_REG_NO_RIDER`, call `device_reg_entry_counter_set(rider_idx, device_reg_entry_counter_get(rider_idx) + spp)` outside the spinlock (after reading `rider_idx` under spinlock).
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with payload `time_ctr_get()` after every pulse (unchanged call site, updated return value — see task 9).

7. **Threshold timer callback** — on firing (transition `SESSION → EARNING`):
   - Remove `wifi_mngr_reward_ap_set(true)`.
   - If `g_session_credits > 0` and `device_reg_current_rider_get() != DEVICE_REG_NO_RIDER`: call `device_reg_entry_counter_set(rider_idx, device_reg_entry_counter_get(rider_idx) + g_session_credits)`.
   - Reset `g_session_credits = 0`.
   - Post `ESPORT_EVENT_REWARD_AP_ON` (keep for dashboard compatibility; semantics change to "earning started").
   - Do **not** start the tick timer here (it is already running permanently).

8. **Tick callback (`time_ctr_tick_cb`)** — simplify to unconditional delegation:
   - **Remove** the entire Feature 1 traffic gate block: delete the call to `wifi_mngr_reward_ap_throughput_kbps()` and all reads/writes of `g_paused` and `g_below_ticks`. The gate is now applied per device inside `device_reg_tick()`.
   - Remove the `g_paused` and `g_below_ticks` file-scope variables entirely from `time_counter.c`.
   - Remove `time_ctr_is_paused()` from `main/inc/time_counter.h` and its implementation from `main/src/time_counter.c`. Per-device pause state is exposed by `device_reg_entry_is_paused()` instead.
   - Remove the `"countdown_paused"` field from the `/api/status` JSON in `main/src/http_server_api.c` (the call to `time_ctr_is_paused()` that was added in Feature 1.5). The per-device `"paused"` fields in the `"devices"` array (Phase 4.5) serve this role.
   - Also remove the HTML `<span id="pause-indicator">` element and its associated JavaScript update from `main/src/http_server_dashboard.c` (added in Feature 1.5), as the global pause indicator has no backing data after `g_paused` is removed.
   - Replace the `if !g_paused: g_counter_s--` block with an unconditional call to `device_reg_tick()`.
   - Remove the `counter == 0` → AP-disable → IDLE transition block entirely. The state machine no longer transitions based on a counter reaching zero.
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with payload `time_ctr_get()` (unchanged call site).
   - Note: `wifi_mngr_reward_ap_throughput_kbps()` is **not** removed from `wifi_manager.c`; it is still called from `http_server_api.c` for the `"reward_ap_throughput_kbps"` total-AP diagnostic field.

9. **Session-closed handler** — transitions in all states:
   - `TIME_CTR_STATE_SESSION` + `ESPORT_EVENT_SESSION_CLOSED`: capture `g_session_credits`, reset it to `0`, cancel the threshold timer → `TIME_CTR_STATE_IDLE`, then flush the captured credits to the current rider's `device_reg` counter (same logic as the threshold callback). Credits are earned from session start; the threshold only gates when `ESPORT_EVENT_REWARD_AP_ON` is posted. Post `ESPORT_EVENT_COUNTER_CHANGED` with the updated counter value.
   - `TIME_CTR_STATE_EARNING` + `ESPORT_EVENT_SESSION_CLOSED`: transition to `TIME_CTR_STATE_IDLE`; post `ESPORT_EVENT_REWARD_AP_OFF`. (`g_paused` and `g_below_ticks` have been removed in task 8; no reset needed.)
   - Remove the old `TIME_CTR_STATE_AP_ACTIVE + SESSION_CLOSED → ignore` branch.

10. **`time_ctr_get()`** — return `device_reg_entry_counter_get(current_rider) + session_credits`, where `session_credits` is `g_session_credits` read under `g_spinlock` if `g_state == TIME_CTR_STATE_SESSION`, otherwise `0`. This ensures the dashboard displays live credit accumulation during the SESSION phase (before the threshold fires). Returns `0` if no rider is selected (`DEVICE_REG_NO_RIDER`).

11. **`time_ctr_counter_set(uint32_t val)`** (Feature 3 API) — update semantics: call `device_reg_entry_counter_set(rider_idx, val)` for the current rider. If no rider is selected, return `ESP_ERR_INVALID_STATE` and log `ESP_LOGW`. Remove all AP on/off logic that this function previously contained (it was calling `wifi_mngr_reward_ap_set()` — now redundant).

12. **Update Doxygen** for `time_ctr_get()` and `time_ctr_counter_set()` in `main/inc/time_counter.h` to reflect the new semantics.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] The 1-second tick fires continuously from `time_ctr_init()` regardless of session state.
- [ ] Pulses during `SESSION` state accumulate in `g_session_credits` and do **not** yet appear in the rider's `device_reg` counter.
- [ ] When the threshold timer fires, `g_session_credits` is flushed to the current rider's `device_reg` counter and the accumulator is reset to zero.
- [ ] Pulses in `EARNING` state add directly to the current rider's `device_reg` counter.
- [ ] `device_reg_tick()` is called unconditionally once per second from `time_ctr_tick_cb()`.
- [ ] Per-device counters only decrement when the per-device traffic gate is not paused (gating is handled inside `device_reg_tick()`).
- [ ] `time_ctr_is_paused()` no longer exists in `main/inc/time_counter.h`.
- [ ] `time_ctr_get()` returns the current rider's `device_reg` counter; returns `0` when no rider is selected.
- [ ] `time_ctr_get()` includes `g_session_credits` during SESSION state: the counter increments each pulse and is visible on the dashboard without waiting for the threshold to fire.
- [ ] Session closed in `SESSION` state: `g_session_credits` is reset to `0` and its value is flushed to the current rider's `device_reg` counter (credits are **not** discarded). `ESPORT_EVENT_REWARD_AP_ON` is **not** posted.
- [ ] Session closed in `EARNING` state: state returns to `IDLE`; device counters continue to decrement normally in subsequent ticks.
- [ ] No call to `wifi_mngr_reward_ap_set()` remains anywhere in `time_counter.c`.
- [ ] `time_ctr_counter_set()` returns `ESP_ERR_INVALID_STATE` when no rider is selected.
- [ ] Migration: a non-zero `config_mngr_reward_counter_s_get()` value is transferred to the current rider's counter on first boot; `config_mngr_reward_counter_s_get()` reads `0` thereafter.
- [ ] No race conditions under rapid pulse injection interleaved with tick (counter never negative).

---

### Phase 4.4 — HTTP Server: Config Page Device Management

#### Goal

Add a complete device registry management section to the `/config` page: list all registered devices with editable fields, an "Add Device" form, current rider selection, and a global reward counter field replacement. Remove the global "Reward Counter" field (Feature 3) from the config form, as per-device counter fields supersede it.

#### Inputs

- `main/src/http_server_config.c` (existing, Feature 3.4 output)
- `device_registry` (Phase 4.1 output)
- `time_counter` (Phase 4.3 output)

#### Tasks

1. **GET `/config` handler** — add a "Registered Devices" section after the existing config fields:

   a. Render a table with one row per registered device (`device_reg_count_get()` rows). Each row contains:
   - Nickname: `<input type="text" name="dev_N_nickname" maxlength="15" value="...">` (pre-populated).
   - MAC address: read-only text, formatted `XX:XX:XX:XX:XX:XX`, wrapped in `<span>`.
   - Counter: `<input type="text" name="dev_N_counter" placeholder="0:00:00" value="h:mm:ss">` — format the counter using `snprintf(buf, sizeof(buf), "%u:%02u:%02u", s/3600, (s%3600)/60, s%60)`.
   - Enabled: `<input type="checkbox" name="dev_N_enabled" value="1"` checked if `b_enabled`.
   - Current rider: `<input type="radio" name="current_rider" value="N"` checked if `device_reg_current_rider_get() == N`.
   - Remove button: `<button type="submit" name="dev_N_remove" value="1">Remove</button>` (inline form submit).

   b. Below the table, an "Add Device" sub-form with:
   - MAC: `<input type="text" name="new_dev_mac" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17">`.
   - Nickname: `<input type="text" name="new_dev_nickname" maxlength="15">`.
   - Submit: `<button type="submit" name="action" value="add_device">Add Device</button>`.

   c. A "No rider" radio option: `<input type="radio" name="current_rider" value="255">` (value `255` = `DEVICE_REG_NO_RIDER`), checked if no rider selected.

   d. **Remove** the "Reward Counter (hh:mm:ss)" field added by Feature 3.4 from the single-device global section. Per-device counters replace it.

2. **POST `/config` handler** — add parsing for device registry fields:

   a. Parse `current_rider`: `strtoul`; if empty or `== DEVICE_REG_NO_RIDER`, call `device_reg_current_rider_set(DEVICE_REG_NO_RIDER)`; else validate `< device_reg_count_get()` and call `device_reg_current_rider_set(idx)`.

   b. Check each index `N = 0` to `device_reg_count_get() - 1`:
   - Parse `dev_N_remove`: if value `"1"`, call `device_reg_entry_remove(N)`, skip remaining fields for this index (indices shift — break the per-device loop after removal and rely on the form redirect to re-render correct state).
   - Parse `dev_N_nickname`: if non-empty and different from current, call `device_reg_entry_nickname_set(N, nickname)`.
   - Parse `dev_N_enabled`: present in POST body = `true`; absent = `false`; call `device_reg_entry_enabled_set(N, b_enabled)`.
   - Parse `dev_N_counter` (`h:mm:ss` or `hh:mm:ss`): split on `:`, expect exactly two `:` separators; `strtoul` each token; validate minutes and seconds `0–59`, hours `≤ 1 193 046`; compute `total_s = h*3600 + m*60 + s`; call `device_reg_entry_counter_set(N, total_s)` and `time_ctr_counter_set(total_s)` only if `N == device_reg_current_rider_get()`. On parse failure, return HTTP 400.
   - The counter input HTML must include an `oninput` handler that auto-formats as `hh:mm:ss` while the user types: strip non-digits, limit to 6 digits, and insert colons automatically (e.g. typing `13000` produces `1:30:00`). Use `maxlength="8"` to cap the formatted output.

   c. Add Device: parse `new_dev_mac` and `new_dev_nickname` if `action == "add_device"`. Call static helper `parse_mac_address(p_str, p_mac_out)` (see below). Call `device_reg_entry_add(mac, nickname)`. Handle `ESP_ERR_NO_MEM` ("Registry full — max 4 devices") and `ESP_ERR_INVALID_STATE` ("Device already registered") as validation errors.

   d. Remove the parsing of the global `reward_counter_s` field (Feature 3 POST logic) since it is replaced by per-device fields.

3. **Static helper `parse_mac_address(const char *p_str, uint8_t *p_mac_out)`**:
   - Accepts both `"AA:BB:CC:DD:EE:FF"` (17 chars with colons) and `"AABBCCDDEEFF"` (12 hex chars without colons).
   - For colon format: split on `:`, parse each of 6 tokens with `strtoul(token, &end, 16)`; verify `end` advanced and value `<= 0xFF`.
   - For no-colon format: verify exactly 12 hex characters; parse pairs.
   - Return `ESP_ERR_INVALID_ARG` on any malformed input.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /config`: device table renders with correct nickname, counter (`h:mm:ss`), enabled state, and rider radio per device.
- [ ] `GET /config`: "No rider" radio option is rendered and pre-selected when no rider is set.
- [ ] `GET /config`: the global "Reward Counter" field from Feature 3 is no longer present.
- [ ] `POST /config`: adding a new device (valid MAC `AA:BB:CC:DD:EE:FF` + nickname) adds it and the table shows it on next `GET`.
- [ ] `POST /config`: adding a duplicate MAC returns HTTP 400.
- [ ] `POST /config`: adding a 5th device returns HTTP 400 with "Registry full" message.
- [ ] `POST /config`: invalid MAC format (e.g. `"ZZ:00:00:00:00:00"`) returns HTTP 400.
- [ ] `POST /config`: MAC without colons (`"AABBCCDDEEFF"`) is accepted.
- [ ] `POST /config`: setting `dev_0_counter` to `"0:30:00"` sets that device's counter to `1800`.
- [ ] `POST /config`: invalid counter format (`"abc"`, `"1:2"`) returns HTTP 400.
- [ ] `POST /config`: removing a device removes it and remaining devices shift indices correctly.
- [ ] `POST /config`: toggling enabled checkbox saves `b_enabled` correctly.
- [ ] `POST /config`: selecting a rider radio button calls `device_reg_current_rider_set()`.
- [ ] `POST /config`: selecting "No rider" calls `device_reg_current_rider_set(DEVICE_REG_NO_RIDER)`.
- [ ] All changes persist across simulated reboot.

---

### Phase 4.5 — HTTP Server: API & Dashboard Per-Device Status

#### Goal

Extend `GET /api/status` with a per-device `"devices"` array and add a "Devices" panel to the status dashboard that updates live every 2 seconds via the existing JS polling loop.

#### Inputs

- `main/src/http_server_api.c`, `main/src/http_server_dashboard.c` (existing)
- `device_registry` (Phase 4.1 output)

#### Tasks

1. **`main/src/http_server_api.c`** — in the `/api/status` JSON response:

   a. Append `"current_rider_idx": <value>` — `device_reg_current_rider_get()` (255 when none).

   **Remove** the top-level `"countdown_paused"` field added by Feature 1.5 (no longer backed by any data; superseded by per-device `"paused"` inside `"devices"`). The `"reward_ap_throughput_kbps"` total field is retained as a diagnostic aggregate.

   b. Append a `"devices": [...]` JSON array. For each registered device (`i = 0` to `device_reg_count_get() - 1`):
   - Call `device_reg_entry_get(i, &entry)`.
   - Check if the device is currently connected: call `esp_wifi_ap_get_sta_list(&sta_list)` once (before the array loop) and compare MACs.
   - Format each element as:
     ```json
     {
       "idx": 0,
       "nickname": "Alice",
       "mac": "AA:BB:CC:DD:EE:FF",
       "counter_s": 1800,
       "counter_hms": "0:30:00",
       "enabled": true,
       "internet_active": true,
       "is_current_rider": true,
       "connected": true,
       "throughput_kbps": 42,
       "paused": false
     }
     ```
   - `"counter_s"` and `"counter_hms"`: for the entry where `i == rider_idx`, use `time_ctr_get()` as the display counter instead of `entry.counter_s`. `time_ctr_get()` already adds `g_session_credits` when in SESSION state, so the devices table updates live as the rider pedals, not only after the session closes. For all other devices use `entry.counter_s`.
   - `"internet_active"`: `entry.b_enabled && display_counter_s > 0` (use the same `display_counter_s`).
   - `"is_current_rider"`: `device_reg_current_rider_get() == i`.
   - `"mac"`: format as `"%02X:%02X:%02X:%02X:%02X:%02X"`.
   - `"throughput_kbps"`: `device_reg_entry_throughput_kbps_get(i)`. Always present; `0` when device is not connected or idle.
   - `"paused"`: `device_reg_entry_is_paused(i)`. Always present; `false` when the traffic gate is not active for this device.
   - Heap-allocate the JSON buffer for the `/api/status` response with enough room for the expanded `"devices"` array (increase the existing buffer allocation to `8192` bytes if the current `4096` is insufficient).

2. **`main/src/http_server_dashboard.c`** — add a "Devices" section to the HTML, between the "Exercise Counter" section and the "Current Session" section:

   a. Static initial HTML (rendered at page load from C):
   ```html
   <h3>Devices</h3>
   <table id="devices-table">
     <thead>
       <tr><th>Nickname</th><th>Counter</th><th>Internet</th><th>Connected</th><th>Traffic</th><th>Rider</th></tr>
     </thead>
     <tbody id="devices-tbody"></tbody>
   </table>
   ```
   The `<tbody>` is populated entirely by JavaScript; the initial C render inserts a placeholder row `<tr><td colspan="6">Loading…</td></tr>`.

   b. In the dashboard JavaScript `refresh()` function (polling `/api/status`), add device table update logic:
   ```js
   var tbody = document.getElementById('devices-tbody');
   if (tbody && data.devices) {
       tbody.innerHTML = '';
       data.devices.forEach(function(d) {
           var pauseStr = d.paused ? ' &#9646;&#9646;' : '';
           var row = '<tr>' +
               '<td>' + d.nickname + (d.is_current_rider ? ' &#9733;' : '') + '</td>' +
               '<td>' + d.counter_hms + '</td>' +
               '<td>' + (d.internet_active ? '&#9989;' : '&#10060;') + '</td>' +
               '<td>' + (d.connected ? '&#9989;' : '&ndash;') + '</td>' +
               '<td>' + d.throughput_kbps + ' kbps' + pauseStr + '</td>' +
               '<td>' + (d.is_current_rider ? '&#9733;' : '') + '</td>' +
               '</tr>';
           tbody.innerHTML += row;
       });
       if (data.devices.length === 0) {
           tbody.innerHTML = '<tr><td colspan="6">No devices registered.</td></tr>';
       }
   }
   ```
   - A star ★ (`&#9733;`) marks the current rider in both the Nickname and Rider columns.
   - ✅ (`&#9989;`) and ❌ (`&#10060;`) indicate internet and connection status.
   - The Traffic column shows the last-tick throughput in kbps. A ⏸ pause symbol (`&#9646;&#9646;`) is appended when `paused == true` for that device.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /api/status` JSON contains `"current_rider_idx"` key in all states.
- [ ] `GET /api/status` JSON contains `"devices"` array; array is empty `[]` when no devices are registered.
- [ ] Each element in `"devices"` has all required keys: `idx`, `nickname`, `mac`, `counter_s`, `counter_hms`, `enabled`, `internet_active`, `is_current_rider`, `connected`.
- [ ] `"connected"` is `true` only for MACs currently associated to the AP.
- [ ] `"internet_active"` is `true` only when `enabled == true` and `counter_s > 0`.
- [ ] `"is_current_rider"` is `true` for at most one device; `false` for all when `current_rider_idx == 255`.
- [ ] `"counter_hms"` matches `counter_s` (e.g. `counter_s = 3661` → `"counter_hms": "1:01:01"`).
- [ ] For the current rider during an active session (SESSION state), `counter_s` in the devices array increments on each pulse and does not wait for the session to close or the threshold to fire.
- [ ] Dashboard "Devices" section is rendered in the HTML structure.
- [ ] Device table rows update every 2 seconds via the JS polling loop without a full page reload.
- [ ] Current rider is marked with ★ in the device table.
- [ ] Internet and connected status indicators update correctly as state changes.
- [ ] "No devices registered." row is shown when the registry is empty.
- [ ] Each device row shows the current traffic in kbps, updated every 2 s via the JS polling loop.
- [ ] The pause symbol (⏸) appears next to the kbps value when `paused == true` for that device.
- [ ] `GET /api/status` JSON no longer contains a top-level `"countdown_paused"` field.
- [ ] Each element in `"devices"` contains `"throughput_kbps"` and `"paused"` keys.

---

### Phase 4.6 — Spec Update: `docs/1-specification.md`

#### Goal

Update the specification to reflect all architectural changes introduced by Feature 4: always-on reward AP, per-device internet access control, the device registry module, changes to the time counter state machine, and all affected API, dashboard, and NVS sections.

#### Tasks

1. **§1 Overview** — replace "When the counter reaches 0 the reward Soft AP is disabled" with: "The reward Soft AP is always active from boot. Internet access is controlled per device: only registered devices with remaining credits and an enabled flag can route traffic; unregistered or expired devices can reach the configuration dashboard but not the internet."

2. **§4 System Architecture** — add `Device Registry` block to the architecture diagram between `NVS Config Manager` and `WiFi Manager`. Update the description under the WiFi Manager block to read "Reward SoftAP (always on, MAC-filtered)".

3. **§4 Component/File Layout** — add `device_registry.h` and `device_registry.c` to the `inc/` and `src/` listings.

4. **New §5.X — Device Registry** — insert a new module specification section (use the next sequential number after the current §5.8) covering:
   - Responsibilities: device list CRUD, NVS persistence, per-device counter storage, internet-allowed query, current rider selection, one-second tick with per-device sliding-window traffic gate, per-device throughput measurement.
   - Data model: `device_reg_entry_t` struct (NVS-persisted fields only), `DEVICE_REG_MAX_ENTRIES`, `DEVICE_REG_NICKNAME_MAX_LEN`, `DEVICE_REG_NO_RIDER`, `DEVICE_REG_SAVE_INTERVAL_S`. Per-device traffic state arrays (`g_rx_bytes`, `g_tx_bytes`, `g_throughput_kbps`, `g_below_ticks`, `g_dev_paused`) are RAM-only and not persisted.
   - NVS Namespace `esport_dev` with full key table.
   - Full public API listing (all functions declared in `device_registry.h`), including `device_reg_mac_rx_bytes_add()`, `device_reg_mac_tx_bytes_add()`, `device_reg_entry_throughput_kbps_get()`, `device_reg_entry_is_paused()`.
   - Per-device sliding-window gate: same threshold and timeout parameters as the former global gate; applied independently per device slot inside `device_reg_tick()`.
   - Thread safety note: spinlock `g_dev_mux` used for all in-RAM access; `device_reg_mac_internet_allowed()`, `device_reg_mac_rx_bytes_add()`, and `device_reg_mac_tx_bytes_add()` are safe to call from the lwIP input/output path.

5. **§5.2 WiFi Manager** — update the Reward SoftAP sub-section:
   - Remove "Enabled/disabled only via `wifi_mngr_reward_ap_set(bool enable)`" as the primary lifecycle description.
   - Replace with: "Always active from boot. `wifi_mngr_reward_ap_set(true)` is called during `wifi_mngr_init()`. `wifi_mngr_reward_ap_set(false)` is a no-op in Feature 4 and later."
   - Add a new sub-section "Per-Device MAC Filter" describing the IPv4 drop logic in `wifi_mngr_ap_input_hook`.

6. **§5.5 Time Counter & Reward AP State Machine** — update:
   - Rename state `AP_ACTIVE` to `EARNING` in the state diagram and description.
   - Update pulse handler description: credits in `EARNING` state go to the current rider's `device_reg` counter via `device_reg_entry_counter_set()`; credits in `SESSION` state accumulate in `g_session_credits` and are flushed to the rider's counter on threshold.
   - Update tick description: `device_reg_tick()` is called **unconditionally** once per second; the per-device traffic gate is applied inside `device_reg_tick()`.
   - Remove `time_ctr_is_paused()` from the public API listing (the function is removed in Feature 4; per-device pause is exposed via `device_reg_entry_is_paused()`).
   - Remove all mentions of `g_paused` and `g_below_ticks` from this module.
   - Remove all mentions of `wifi_mngr_reward_ap_set()` from this module's responsibilities.
   - Update `time_ctr_get()` docstring: returns the current rider's device_reg counter.
   - Update `time_ctr_counter_set()` docstring: sets the current rider's counter; returns `ESP_ERR_INVALID_STATE` when no rider selected.

7. **§6.1 Status Dashboard** — add a "Devices" section description listing the table columns: Nickname (with ★ for current rider), Counter (`h:mm:ss`), Internet (✅/❌), Connected (✅/–), Rider (★).

8. **§6.2 Configuration Page** — add a "Registered Devices" section describing the device table fields (nickname, MAC, counter, enabled checkbox, rider radio) and the "Add Device" sub-form. Remove the "Reward Counter" global field entry (superseded by per-device counters).

9. **§6.3 JSON Status API (`GET /api/status`)** — add `"current_rider_idx"` (`uint8`, `255` when none) and `"devices"` array (with full per-element schema including `"throughput_kbps"` and `"paused"`) to the documented JSON schema. Remove `"countdown_paused"` from the top-level schema (superseded by per-device `"paused"`).

10. **§8 NVS Layout** — add namespace `esport_dev` with key table (`dev_count`, `dev_0`–`dev_3`, `dev_rider`).

11. **Module Prefix Table** — add row: `device_registry` | `device_reg_` | `DEVICE_REG_`.

#### Acceptance Criteria

- [ ] §1 no longer describes the AP as being disabled when the counter reaches zero.
- [ ] §4 architecture diagram includes the Device Registry block.
- [ ] §4 file layout lists `device_registry.h` and `device_registry.c`.
- [ ] New §5.X fully documents the Device Registry module (data model, NVS, API, thread safety).
- [ ] §5.2 Reward SoftAP description reflects always-on behaviour and the MAC filter.
- [ ] §5.5 state diagram shows `EARNING` (not `AP_ACTIVE`) and no AP on/off calls.
- [ ] §5.5 no longer lists `time_ctr_is_paused()` in the public API.
- [ ] §6.1 documents the Devices table in the dashboard.
- [ ] §6.2 documents the device management section of the config page and no longer lists the global Reward Counter field.
- [ ] §6.3 JSON schema includes `"current_rider_idx"` and `"devices"` array with full element schema (including `"throughput_kbps"` and `"paused"`); `"countdown_paused"` is absent from the top-level schema.
- [ ] §8 includes the `esport_dev` NVS namespace.
- [ ] Module Prefix Table includes `device_registry`.

---

## Feature 5 — Buzzer Feedback

### Overview

An active buzzer connected to a configurable GPIO pin provides audio feedback for key session events and real-time speed warnings.  The buzzer is driven by a time-base of 50 ms per unit ("beep unit"); all timing constants are expressed as multiples of this unit and defined as `#define` macros.

The buzzer module is **passive**: other modules call its API directly to trigger patterns.  `session_tracker.c` calls `buzzer_pattern_play()` at state-transition points; `time_counter.c` calls `buzzer_speed_low_update()` from its 1-second tick callback.  No new event IDs are added to `event_ids.h`.

Audio feedback can be disabled at runtime via an NVS-backed boolean exposed on the web configuration page.  When disabled every buzzer API call is a no-op and the GPIO remains LOW.

### Hardware

| Item | Details |
| ---- | ------- |
| Buzzer type | Active (ON/OFF duty control) |
| Control GPIO | `CONFIG_ESPORT_BUZZER_GPIO` (Kconfig, default GPIO 11) |
| Active level | HIGH = on, LOW = off |

### New Configuration Parameter

| Parameter | Type | NVS key | Default | Valid range |
| --------- | ---- | ------- | ------- | ----------- |
| `buzzer_enabled` | `bool` (stored as `uint8`) | `"buzzer_en"` | `true` | true / false |

### Beep Patterns

One beep unit = `BUZZER_UNIT_MS` = 50 ms.

| Pattern ID constant | Trigger | Sequence |
| ------------------- | ------- | -------- |
| `BUZZER_PATTERN_SESSION_QUALIFYING` | ST_IDLE → ST_QUALIFYING | 5 units ON (`BUZZER_PATTERN_QUALIFYING_UNITS`) |
| `BUZZER_PATTERN_SESSION_QUALIFIED` | ST_QUALIFYING → ST_ACTIVE | 10 units ON (`BUZZER_PATTERN_QUALIFIED_UNITS`) |
| `BUZZER_PATTERN_SESSION_CLOSED` | ST_ACTIVE → ST_IDLE (idle timeout) | 2 ON, 1 OFF, 2 ON, 1 OFF, 2 ON (`BUZZER_PATTERN_CLOSED_BEEP_UNITS`, `BUZZER_PATTERN_CLOSED_GAP_UNITS`, `BUZZER_PATTERN_CLOSED_BEEP_COUNT`) |
| `BUZZER_PATTERN_SPEED_LOW` | Per tick: EARNING state, 0 < speed < min\_speed | 2 units ON (`BUZZER_PATTERN_SPEED_LOW_UNITS`) |

### Interruption Rule

A new call to `buzzer_pattern_play()` while a pattern is playing **immediately interrupts** the current pattern and starts the new one.  The sole exception is `buzzer_speed_low_update(false)`: it stops a `SPEED_LOW` pattern in progress but does **not** interrupt any other pattern (e.g. a `SESSION_CLOSED` pattern triggered at the same tick must not be cut short).

### Speed-Low Beep Firing Condition

Called from `time_ctr_tick_cb()` once per second.  `buzzer_speed_low_update(true)` is passed when **all** of the following hold; `buzzer_speed_low_update(false)` otherwise:

1. `g_state == TIME_CTR_STATE_SESSION` or `g_state == TIME_CTR_STATE_EARNING` (session open)
2. `config_mngr_min_speed_to_increment_time_kmh_x10_get() > 0` (speed gate is enabled)
3. `time_ctr_current_speed_x10_get() > 0` (rider is moving — not stopped)
4. `time_ctr_current_speed_x10_get() < min_speed_to_increment_time_kmh_x10` (speed below threshold)

The qualifying gap-reset path in `session_tracker.c` (ST_QUALIFYING → ST_IDLE when no session was confirmed) does **not** play `BUZZER_PATTERN_SESSION_CLOSED` — only a confirmed session that closes plays that pattern.

---

### Phase 5.1 — Buzzer Core Module

**Goal:** Implement the buzzer driver: GPIO initialisation, non-blocking pattern engine driven by an `esp_timer` at 50 ms intervals, all four predefined patterns, interrupt-on-new-pattern semantics, and the speed-low update helper.

**Inputs**
- `docs/0-draft-input.md` §Improvements item 5
- `main/Kconfig.projbuild`, `main/CMakeLists.txt` (existing)
- `config_manager` (existing — `config_mngr_buzzer_enabled_get()` is added in Phase 5.2; `buzzer_init()` is called after Phase 5.2's `config_mngr_init()`)

**Tasks**

1. **`main/Kconfig.projbuild`** — add inside the existing `menu "esport-fi32 Configuration"`:
   ```
   config ESPORT_BUZZER_GPIO
       int "Buzzer GPIO number"
       range 0 21
       default 11
   ```

2. **Create `main/inc/buzzer.h`** (public header):
   - Define timing and pattern constants (each replacement value in parentheses):
     ```c
     #define BUZZER_UNIT_MS                   (50U)
     #define BUZZER_PATTERN_QUALIFYING_UNITS  (5U)
     #define BUZZER_PATTERN_QUALIFIED_UNITS   (10U)
     #define BUZZER_PATTERN_CLOSED_BEEP_UNITS (2U)
     #define BUZZER_PATTERN_CLOSED_GAP_UNITS  (1U)
     #define BUZZER_PATTERN_CLOSED_BEEP_COUNT (3U)
     #define BUZZER_PATTERN_SPEED_LOW_UNITS   (2U)
     ```
   - Define the pattern ID enum:
     ```c
     typedef enum buzzer_pattern_id_tag {
         BUZZER_PATTERN_SESSION_QUALIFYING = 0,
         BUZZER_PATTERN_SESSION_QUALIFIED  = 1,
         BUZZER_PATTERN_SESSION_CLOSED     = 2,
         BUZZER_PATTERN_SPEED_LOW          = 3,
     } buzzer_pattern_id_t;
     ```
   - Declare the public API with full Doxygen (`\\` tags, direction annotations, blank line before `\\return`):
     ```c
     esp_err_t buzzer_init(void);
     void      buzzer_pattern_play(buzzer_pattern_id_t pattern);
     void      buzzer_stop(void);
     void      buzzer_speed_low_update(bool b_active);
     ```
   - Use `hhtemplate` structure; guard with `BUZZER_H`.

3. **Create `main/src/buzzer.c`**:
   - Declare `static const char *gp_tag = "buzzer"`.
   - Define the internal step type and pattern tables as file-scope `const` data:
     ```c
     typedef struct buzzer_step_tag {
         uint8_t on_units;
         uint8_t off_units;
     } buzzer_step_t;

     static const buzzer_step_t s_pat_qualifying[1] = { { BUZZER_PATTERN_QUALIFYING_UNITS, 0U } };
     static const buzzer_step_t s_pat_qualified[1]  = { { BUZZER_PATTERN_QUALIFIED_UNITS,  0U } };
     static const buzzer_step_t s_pat_closed[3]     = {
         { BUZZER_PATTERN_CLOSED_BEEP_UNITS, BUZZER_PATTERN_CLOSED_GAP_UNITS },
         { BUZZER_PATTERN_CLOSED_BEEP_UNITS, BUZZER_PATTERN_CLOSED_GAP_UNITS },
         { BUZZER_PATTERN_CLOSED_BEEP_UNITS, 0U },
     };
     static const buzzer_step_t s_pat_speed_low[1]  = { { BUZZER_PATTERN_SPEED_LOW_UNITS,  0U } };
     ```
   - Declare playback state protected by `static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED`:
     ```c
     static const buzzer_step_t *gp_steps        = NULL;
     static uint8_t              g_step_count     = 0U;
     static uint8_t              g_step_idx       = 0U;
     static uint8_t              g_remaining_on   = 0U;
     static uint8_t              g_remaining_off  = 0U;

     typedef enum bz_phase_tag { BZ_PHASE_ON = 0, BZ_PHASE_OFF = 1, BZ_PHASE_IDLE = 2 } bz_phase_t;
     static bz_phase_t           g_phase          = BZ_PHASE_IDLE;
     static buzzer_pattern_id_t  g_current_pat_id = BUZZER_PATTERN_SESSION_QUALIFYING;
     ```
   - Declare `static esp_timer_handle_t g_timer = NULL`.
   - Internal static helpers (declare in the Internal Function Prototypes section):
     - `buzzer_pattern_start_locked(const buzzer_step_t *p_steps, uint8_t count, buzzer_pattern_id_t id)` — loads state under the already-held spinlock, sets GPIO HIGH for the first ON unit, starts the 50 ms timer if it is not already running.
     - `buzzer_gpio_set(uint8_t level)` — thin wrapper around `gpio_set_level(CONFIG_ESPORT_BUZZER_GPIO, level)`.

   - **Timer callback `buzzer_timer_cb(void *arg)`** — runs in the `esp_timer` task; must be O(1), no heap, no NVS:
     - Enter spinlock.
     - If `g_phase == BZ_PHASE_IDLE`: exit spinlock, stop timer (or let it auto-stop if one-shot), return.
     - If `g_phase == BZ_PHASE_ON`:
       - Decrement `g_remaining_on`.
       - If `g_remaining_on == 0`:
         - If `gp_steps[g_step_idx].off_units > 0`: set GPIO LOW, `g_remaining_off = gp_steps[g_step_idx].off_units`, `g_phase = BZ_PHASE_OFF`.
         - Else: advance step via `buzzer_advance_step_locked()` (see below).
     - If `g_phase == BZ_PHASE_OFF`:
       - Decrement `g_remaining_off`.
       - If `g_remaining_off == 0`: advance step via `buzzer_advance_step_locked()`.
     - Exit spinlock.  (GPIO toggle is done inside the spinlock via `buzzer_gpio_set`; safe on single-core ESP32-C6.)

   - Internal helper `buzzer_advance_step_locked()` — runs under spinlock:
     - Increment `g_step_idx`.
     - If `g_step_idx >= g_step_count`: set GPIO LOW, `g_phase = BZ_PHASE_IDLE` (pattern done).
     - Else: set GPIO HIGH, `g_remaining_on = gp_steps[g_step_idx].on_units`, `g_phase = BZ_PHASE_ON`.

   - **`buzzer_init()`**:
     - Configure `CONFIG_ESPORT_BUZZER_GPIO` as `GPIO_MODE_OUTPUT`, no pull, initial level LOW.
     - Create a **periodic** `esp_timer` with period `BUZZER_UNIT_MS * 1000` µs (50 000 µs) and callback `buzzer_timer_cb`. **Do not start it yet** — it is started by `buzzer_pattern_start_locked()` when needed.  Starting a periodic timer and never stopping it would waste CPU; starting it only when a pattern is playing and stopping it from the callback when `g_phase == BZ_PHASE_IDLE` is efficient.  Alternative: use a **one-shot** timer that re-arms itself from the callback while `g_phase != BZ_PHASE_IDLE` — either approach is acceptable; choose whichever is simpler in the implementation.
     - Log `ESP_LOGI(gp_tag, "buzzer init: GPIO %d", CONFIG_ESPORT_BUZZER_GPIO)`.
     - Return `ESP_OK`.

   - **`buzzer_pattern_play(buzzer_pattern_id_t pattern)`**:
     - Call `config_mngr_buzzer_enabled_get()`; if `false`, return immediately (no-op).
     - Select the step array and count from `pattern` using a `switch` statement.  On unknown value, log `ESP_LOGW` and return.
     - Enter spinlock; call `buzzer_pattern_start_locked(p_steps, count, pattern)`; exit spinlock.

   - **`buzzer_stop()`**:
     - Enter spinlock; set `g_phase = BZ_PHASE_IDLE`; exit spinlock.
     - Call `buzzer_gpio_set(0U)`.
     - Call `esp_timer_stop(g_timer)` (ignore return value — timer may already be stopped).

   - **`buzzer_speed_low_update(bool b_active)`**:
     - If `b_active == true`: call `buzzer_pattern_play(BUZZER_PATTERN_SPEED_LOW)` (interrupt semantics already provided by `buzzer_pattern_play`).
     - If `b_active == false`: enter spinlock; check if `g_current_pat_id == BUZZER_PATTERN_SPEED_LOW` and `g_phase != BZ_PHASE_IDLE`; exit spinlock.  If both, call `buzzer_stop()`.  Otherwise, do nothing.

   - Follow `cctemplate` structure with all section separators and `/*** end of file ***/` footer.

4. **`main/CMakeLists.txt`** — add `"src/buzzer.c"` to the `SRCS` list.

5. **`main/src/main.c`** — add `buzzer_init()` after the `config_mngr_init()` call in the boot sequence, guarded by `ESP_ERROR_CHECK`.  Add `#include "buzzer.h"`.

**Notes**

> The timer callback runs in the `esp_timer` task.  All operations in the callback are O(1), spinlock-guarded, GPIO-only — no heap allocation, no NVS access, no logging.

> `buzzer_pattern_play()` is called from the app event loop task (session tracker FreeRTOS timer callbacks, time counter ESP timer callback).  `buzzer_speed_low_update()` is called from `time_ctr_tick_cb()` (ESP timer task).  Both are O(1) with spinlock protection and safe from any task context.

> `buzzer_gpio_set()` calls `gpio_set_level()` from inside the spinlock.  On the single-core ESP32-C6, `portENTER_CRITICAL` / `portEXIT_CRITICAL` disable interrupts; `gpio_set_level` is an O(1) register write and completes within the critical section window.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFYING)`: GPIO HIGH for 250 ms (5 × 50 ms), then LOW.
- [ ] `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFIED)`: GPIO HIGH for 500 ms (10 × 50 ms), then LOW.
- [ ] `buzzer_pattern_play(BUZZER_PATTERN_SESSION_CLOSED)`: sequence HIGH 100 ms, LOW 50 ms, HIGH 100 ms, LOW 50 ms, HIGH 100 ms, then LOW.
- [ ] `buzzer_pattern_play(BUZZER_PATTERN_SPEED_LOW)`: GPIO HIGH for 100 ms (2 × 50 ms), then LOW.
- [ ] Calling `buzzer_pattern_play()` while a pattern is in progress immediately starts the new pattern (old pattern truncated).
- [ ] `buzzer_stop()` sets GPIO LOW immediately and halts the timer.
- [ ] `buzzer_speed_low_update(false)` while `SPEED_LOW` is playing stops it (GPIO LOW).
- [ ] `buzzer_speed_low_update(false)` while `SESSION_CLOSED` is playing does **not** interrupt it.
- [ ] After `buzzer_init()`, GPIO is LOW.
- [ ] No heap allocation or NVS access occurs in the timer callback.

---

### Phase 5.2 — Config Manager: Buzzer Enable Parameter

**Goal:** Add getter and setter for `buzzer_enabled` to the configuration manager.

**Inputs**
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)

**Tasks**

1. **`main/src/config_manager.c`** — add:
   - `#define CONFIG_MNGR_KEY_BUZZER_ENABLED  ("buzzer_en")`
   - `#define CONFIG_MNGR_DEF_BUZZER_ENABLED  ((uint8_t)1U)`
   - In `config_mngr_init()`: read `"buzzer_en"`; if `ESP_ERR_NVS_NOT_FOUND`, write the default `1`.

2. **`main/inc/config_manager.h`** — declare (with complete Doxygen, following existing style):
   ```c
   bool      config_mngr_buzzer_enabled_get(void);
   esp_err_t config_mngr_buzzer_enabled_set(bool b_enabled);
   ```

3. **`main/src/config_manager.c`** — implement:
   - `config_mngr_buzzer_enabled_get()`: read NVS key `"buzzer_en"` as `uint8_t`; return `(val != 0U)`; on any NVS error return `true` (fail-safe: buzzer on by default).
   - `config_mngr_buzzer_enabled_set(b_enabled)`: write `(uint8_t)(b_enabled ? 1U : 0U)` to NVS key `"buzzer_en"`; commit; return `ESP_OK` or the NVS error code.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_buzzer_enabled_set(false)` returns `ESP_OK`; subsequent `config_mngr_buzzer_enabled_get()` returns `false`.
- [ ] `config_mngr_buzzer_enabled_set(true)` returns `ESP_OK`; subsequent `config_mngr_buzzer_enabled_get()` returns `true`.
- [ ] Value survives `config_mngr_init()` reinit (simulated reboot).
- [ ] Factory default `true` applied when NVS key is absent.

---

### Phase 5.3 — Session Tracker: Buzzer Integration

**Goal:** Call `buzzer_pattern_play()` at the three session state-transition points inside `session_tracker.c`.

**Inputs**
- `main/src/session_tracker.c` (existing)
- `buzzer.h / buzzer.c` (Phase 5.1 output)

**Tasks**

1. **`main/src/session_tracker.c`** — add `#include "buzzer.h"`.

2. **ST_IDLE → ST_QUALIFYING** (pulse event handler, inside the `s_state == ST_IDLE` branch) — after `s_state` is set to `ST_QUALIFYING`:
   ```c
   buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFYING);
   ```

3. **ST_QUALIFYING → ST_ACTIVE** (qualify timer callback, after `s_state = ST_ACTIVE`):
   ```c
   buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFIED);
   ```

4. **ST_ACTIVE → ST_IDLE idle-timeout close** (idle timer callback, `s_state == ST_ACTIVE` branch, before the state reset and `ESPORT_EVENT_SESSION_CLOSED` post):
   ```c
   buzzer_pattern_play(BUZZER_PATTERN_SESSION_CLOSED);
   ```

5. **ST_QUALIFYING → ST_IDLE gap-reset** (idle timer callback, `s_state == ST_QUALIFYING` branch): **do not add any buzzer call here** — a session that was never confirmed is not considered "closed".

**Notes**

> `buzzer_pattern_play()` is called from FreeRTOS software timer callbacks (`s_qualify_timer`, `s_idle_timer`).  These run in the FreeRTOS timer daemon task.  The buzzer call is O(1) with a spinlock and safe from any task.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] First pulse (ST_IDLE → ST_QUALIFYING): `BUZZER_PATTERN_SESSION_QUALIFYING` plays (250 ms HIGH).
- [ ] Qualify timer fires (ST_QUALIFYING → ST_ACTIVE): `BUZZER_PATTERN_SESSION_QUALIFIED` plays (500 ms HIGH).
- [ ] Idle timer fires in ST_ACTIVE: `BUZZER_PATTERN_SESSION_CLOSED` plays (3 × short beep sequence).
- [ ] Idle timer fires in ST_QUALIFYING (qualifying gap, no session confirmed): no buzzer pattern plays.
- [ ] With `buzzer_enabled == false`, none of the above patterns produce any GPIO toggle.

---

### Phase 5.4 — Time Counter: Speed-Low Beep Integration

**Goal:** Call `buzzer_speed_low_update()` from the time counter 1-second tick callback, gated on `TIME_CTR_STATE_SESSION` or `TIME_CTR_STATE_EARNING` state and the speed-to-threshold comparison.

**Inputs**
- `main/src/time_counter.c` (existing, Feature 4.3 output)
- `buzzer.h / buzzer.c` (Phase 5.1 output)
- `config_manager` (Phase 5.2 output)

**Tasks**

1. **`main/src/time_counter.c`** — add `#include "buzzer.h"`.

2. In `time_ctr_tick_cb()`, after the unconditional `device_reg_tick()` call, add the speed-low beep computation:
   ```c
   bool b_speed_low = false;
   if ((g_state == TIME_CTR_STATE_SESSION) || (g_state == TIME_CTR_STATE_EARNING))
   {
       uint32_t speed_x10 = time_ctr_current_speed_x10_get();
       uint16_t min_spd   = config_mngr_min_speed_to_increment_time_kmh_x10_get();
       b_speed_low = (min_spd > 0U) && (speed_x10 > 0U) && (speed_x10 < (uint32_t)min_spd);
   }
   buzzer_speed_low_update(b_speed_low);
   ```
   When `g_state` is `TIME_CTR_STATE_IDLE`, `b_speed_low` remains `false` and `buzzer_speed_low_update(false)` is called, which cleanly stops any residual speed-low beep.

3. In the `ESPORT_EVENT_SESSION_CLOSED` handler inside `time_counter.c` (where the state returns to `TIME_CTR_STATE_IDLE`), add an explicit `buzzer_speed_low_update(false)` call to stop the speed-low beep immediately without waiting for the next tick.

**Notes**

> `buzzer_speed_low_update()` is called from `time_ctr_tick_cb()`, which is an `esp_timer` callback (runs in the `esp_timer` task).  The call is O(1) with a spinlock and safe in this context.

> If `min_speed_to_increment_time_kmh_x10 == 0` (Feature 2 speed gate is disabled), `b_speed_low` is always `false`, and no speed-low beep is ever produced.  This is the correct behaviour: when there is no minimum speed requirement, "below threshold" has no meaning.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] In `TIME_CTR_STATE_SESSION` or `TIME_CTR_STATE_EARNING` with `min_speed > 0`, `0 < speed < min_speed`: `buzzer_speed_low_update(true)` is called each tick; GPIO pulses HIGH for 100 ms once per second.
- [ ] Speed reaches 0: `buzzer_speed_low_update(false)` is called; active speed-low beep stops immediately.
- [ ] Speed reaches or exceeds `min_speed`: `buzzer_speed_low_update(false)` is called; no more speed-low beeps.
- [ ] `min_speed == 0` (gate disabled): `buzzer_speed_low_update(false)` is called every tick; no speed-low beep is ever produced.
- [ ] State is `TIME_CTR_STATE_IDLE`: `buzzer_speed_low_update(false)` is called; no speed-low beep produced.
- [ ] `ESPORT_EVENT_SESSION_CLOSED` received: `buzzer_speed_low_update(false)` is called immediately; speed-low beep stops without waiting for the next tick.
- [ ] With `buzzer_enabled == false`: no GPIO toggle occurs regardless of speed or state.

---

### Phase 5.5 — HTTP Server: Buzzer Enable Config Field

**Goal:** Add a "Buzzer feedback" enable/disable checkbox to the `/config` web page.

**Inputs**
- `main/src/http_server_config.c` (existing)
- `config_manager` (Phase 5.2 output)

**Tasks**

1. **GET `/config` handler** — add one field to the config form HTML:
   - Label: "Buzzer feedback"
   - `<input type="checkbox" name="buzzer_enabled" value="1"` with `checked` attribute if `config_mngr_buzzer_enabled_get()` returns `true`.
   - Unchecked checkboxes are absent from the POST body in standard HTML form encoding; the POST handler must treat absence as `false`.

2. **POST `/config` handler** — add parsing for `buzzer_enabled`:
   - Call `http_srv_form_field_get(p_body, "buzzer_enabled", val_buf, sizeof(val_buf))`.
   - If the field is present (returns `ESP_OK`): `b_enabled = true`.
   - If the field is absent (returns `ESP_ERR_NOT_FOUND`): `b_enabled = false`.
   - Call `config_mngr_buzzer_enabled_set(b_enabled)`.
   - This field requires no range validation — the checkbox is a binary `true`/`false`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /config` renders the "Buzzer feedback" checkbox; it is checked when `buzzer_enabled` is `true`, unchecked when `false`.
- [ ] Submitting with the checkbox checked saves `true`; subsequent `GET /config` shows it checked.
- [ ] Submitting with the checkbox unchecked saves `false`; subsequent `GET /config` shows it unchecked.
- [ ] After disabling via the config page, no buzzer patterns play (verified by monitoring GPIO behaviour).
- [ ] After re-enabling via the config page, buzzer patterns resume normally.
- [ ] The change persists across a simulated reboot (`config_mngr_init()` reinit).

---

### Phase 5.6 — Spec Update: `docs/1-specification.md`

**Goal:** Update the firmware specification to document the buzzer hardware component, the buzzer module, the new configuration parameter, and all affected sections.

**Inputs**
- `docs/0-draft-input.md` §Improvements item 5
- `docs/1-specification.md` (current)
- All Phase 5.1–5.5 outputs

**Tasks**

1. **§2 Hardware** — add a row to the hardware table:
   - Item: "Buzzer", Details: "Active buzzer on `CONFIG_ESPORT_BUZZER_GPIO` (Kconfig, default GPIO 11). Active HIGH. Driven by the Buzzer Module."

2. **§3 Configuration Parameters table** — add one row:
   - `buzzer_enabled` — `bool` (stored as `uint8`), NVS key `"buzzer_en"`, default `true`, range `true / false`, description: "Enable/disable all buzzer audio feedback.  Configurable via the web configuration page.  When `false`, all `buzzer_*` calls are no-ops and the GPIO stays LOW."

3. **§4 Component/File Layout** — add `buzzer.h` to the `inc/` listing and `buzzer.c` to the `src/` listing.

4. **New §5.X — Buzzer Module** — insert after §5.9 (Device Registry):
   - **File:** `buzzer.c` / `buzzer.h`
   - **Responsibilities:** GPIO output control; non-blocking pattern playback via `esp_timer` (50 ms period); four predefined beep patterns; interrupt-on-new-pattern semantics; speed-low update helper; runtime enable/disable via `config_mngr_buzzer_enabled_get()`.
   - **Hardware:** `CONFIG_ESPORT_BUZZER_GPIO`, active HIGH.
   - **Beep unit:** `BUZZER_UNIT_MS` = 50 ms.
   - **Pattern table:** list all four patterns with their sequences (same table as the Overview above).
   - **Public API** listing:
     ```c
     esp_err_t buzzer_init(void);
     void      buzzer_pattern_play(buzzer_pattern_id_t pattern);
     void      buzzer_stop(void);
     void      buzzer_speed_low_update(bool b_active);
     ```
   - **Interruption rule:** a new `buzzer_pattern_play()` always interrupts the current pattern. `buzzer_speed_low_update(false)` only stops a `SPEED_LOW` pattern; it does not interrupt other patterns.
   - **Thread safety note:** the `esp_timer` callback is O(1), spinlock-guarded, and does only GPIO writes. `buzzer_pattern_play()` and `buzzer_speed_low_update()` are safe to call from any task (app event loop, FreeRTOS timer daemon, `esp_timer` callback).

5. **§5.6 Session Tracker** — add a note under the state-transition descriptions:
   - ST_IDLE → ST_QUALIFYING: `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFYING)` (5 units, 250 ms).
   - ST_QUALIFYING → ST_ACTIVE: `buzzer_pattern_play(BUZZER_PATTERN_SESSION_QUALIFIED)` (10 units, 500 ms).
   - ST_ACTIVE → ST_IDLE (idle timeout): `buzzer_pattern_play(BUZZER_PATTERN_SESSION_CLOSED)` (3 × 2-unit beeps).
   - ST_QUALIFYING → ST_IDLE (qualifying gap): no buzzer call.

6. **§5.5 Time Counter & Reward AP State Machine** — add a note to the tick callback description: `buzzer_speed_low_update(b_speed_low)` is called once per second; `b_speed_low` is `true` when `TIME_CTR_STATE_SESSION` or `TIME_CTR_STATE_EARNING` is active, `min_speed > 0`, and `0 < current_speed < min_speed`.

7. **§6.2 Configuration Page** — add "Buzzer feedback" to the field table: checkbox (`true`/`false`), description "Enable/disable all audio feedback from the buzzer".

8. **§7.1 Boot Sequence** — add `buzzer_init()` to the boot sequence, called immediately after `config_mngr_init()`.

9. **§8 NVS Layout — namespace `esport_cfg`** — add `"buzzer_en"` (`uint8`) to the key table.

10. **Module Prefix Table** — add row: `buzzer` | `buzzer_` | `BUZZER_`.

**Acceptance Criteria**

- [ ] §2 Hardware table includes the buzzer row with GPIO default and active level.
- [ ] §3 includes `buzzer_enabled` with correct type, NVS key, default, and description.
- [ ] §4 file layout lists `buzzer.h` and `buzzer.c`.
- [ ] New §5.X fully documents the Buzzer module (responsibilities, hardware, beep unit, all four patterns, public API, interruption rule, thread safety).
- [ ] §5.6 Session Tracker notes document all three `buzzer_pattern_play()` call points and the no-call case.
- [ ] §5.5 Time Counter tick description documents the `buzzer_speed_low_update()` call and its condition.
- [ ] §6.2 Configuration Page field table includes "Buzzer feedback".
- [ ] §7.1 Boot Sequence includes `buzzer_init()` after `config_mngr_init()`.
- [ ] §8 includes `"buzzer_en"` (`uint8`) in the `esport_cfg` key table.
- [ ] Module Prefix Table includes `buzzer` | `buzzer_` | `BUZZER_`.
