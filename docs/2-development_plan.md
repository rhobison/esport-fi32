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
   - `CONFIG_ESPORT_CONFIG_AP_SSID` string, default `"esport-fi32_config"`, max 32 characters.
   - `CONFIG_ESPORT_CONFIG_AP_PASSWORD` string, default `"esport-fi32_config"`, max 64 characters.
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

Implement `wifi_manager.c` fully: AP+STA initialisation, config AP lifecycle (auto-enable on STA failure, disable on connection), reward AP enable/disable, NAPT, STA reconnection.

### Inputs

- `docs/1-specification.md` §5.2, §7.2
- `firmware/inc/wifi_manager.h`, `firmware/inc/event_ids.h`
- `config_manager` (Phase 1 output)

### Tasks

1. Implement `wifi_mngr_init()`:
   - Init TCP/IP stack: `esp_netif_init()`, `esp_netif_create_default_wifi_ap()`, `esp_netif_create_default_wifi_sta()`.
   - Init WiFi with `WIFI_MODE_APSTA`.
   - Register event handlers for `WIFI_EVENT` and `IP_EVENT`.
   - Attempt STA connection using `config_mngr_wifi_ssid_get/password()`. If SSID is empty, skip STA and enable config AP immediately.

2. Implement config AP logic:
   - SSID `CONFIG_ESPORT_CONFIG_AP_SSID`, password `CONFIG_ESPORT_CONFIG_AP_PASSWORD`, channel 1, max 4 clients.
   - Enable on `WIFI_EVENT_STA_DISCONNECTED` (after exhausting retry without IP).
   - Disable on `IP_EVENT_STA_GOT_IP`.
   - Post `ESPORT_EVENT_STA_CONNECTED` / `ESPORT_EVENT_STA_DISCONNECTED` on app event loop.

3. Implement STA reconnection: retry every 10 seconds indefinitely using an `esp_timer`.

4. Implement `wifi_mngr_reward_ap_set(bool enable)`:
   - When `enable == true`: configure AP with `config_mngr_soft_ap_ssid_get/password()`, set subnet `192.168.5.0/24`, start AP.
   - When `enable == false`: stop AP interface.
   - Enable NAPT (`ip_napt_enable`) on AP netif pointing to STA netif after AP start.
   - Guard against double-enable: if already in desired state, return `ESP_OK` immediately.

5. Implement `wifi_mngr_sta_is_connected()`, `wifi_mngr_reward_ap_is_active()`, `wifi_mngr_reward_ap_client_count()`, `wifi_mngr_sta_ip_get()`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] On boot with valid `wifi_ssid`: STA connects, config AP is NOT enabled.
- [ ] On boot with invalid/empty `wifi_ssid`: config AP `esport-fi32_config` is active.
- [ ] After STA disconnection, config AP is re-enabled within 1 reconnect cycle.
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
   - On success: call `time_mngr_timezone_apply()` if `timezone` changed; schedule WiFi reconnect if wifi credentials changed; redirect to `GET /config` with query `?saved=1`.
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
    g_below_ticks = 0
    g_paused      = false
    decrement counter by 1
else:
    g_below_ticks++
    if g_below_ticks >= timeout:
        g_paused = true
        do NOT decrement
```

- `timeout = 0` means pause immediately on the first below-threshold tick.
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

3. **`main/src/http_server_dashboard.c`**:
   - In the countdown section of the dashboard HTML, add:
     ```html
     <span id="pause-indicator" style="display:none;">⏸ Paused (low traffic)</span>
     ```
   - In the dashboard's JavaScript auto-refresh handler (polling `/api/status`), add:
     ```js
     document.getElementById('pause-indicator').style.display =
         data.countdown_paused ? 'inline' : 'none';
     ```

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Config page renders both new fields with correct current values.
- [ ] Submitting values `0` and `65535` for both fields saves and reflects correctly on reload.
- [ ] Submitting a value of `65536` or a non-numeric string returns HTTP 400.
- [ ] `/api/status` JSON contains `"countdown_paused"` key in all states.
- [ ] Dashboard pause indicator is hidden when `countdown_paused` is `false`.
- [ ] Dashboard pause indicator shows "⏸ Paused (low traffic)" when `countdown_paused` is `true`.
- [ ] Dashboard auto-refresh interval is unchanged.

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
speed_kmh_x10 = (centimeters_per_pulse * 36) / last_pulse_interval_ms
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

3. **`main/src/http_server_dashboard.c`**:
   - Add a "Current speed" display line in the live stats section, e.g. `<span id="current-speed">0</span> km/h × 10`.
   - In the dashboard JS auto-refresh handler, update the element:
     ```js
     document.getElementById('current-speed').textContent = data.current_speed_kmh_x10;
     ```

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] Config page renders the new field with the correct current value.
- [ ] Submitting `0` and `65535` saves and reflects correctly on reload.
- [ ] Submitting `65536` or a non-numeric string returns HTTP 400.
- [ ] `/api/status` JSON contains `"current_speed_kmh_x10"` in all states.
- [ ] Dashboard live speed display updates on each auto-refresh cycle.
- [ ] Dashboard auto-refresh interval is unchanged.
