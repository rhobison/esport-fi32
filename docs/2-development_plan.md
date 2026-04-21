# esport-fi32 Development Plan

**Version:** 1.1
**Date:** 2026-04-06
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

4. Use the NVS keys exactly as specified in spec §8 (e.g. `"wifi_ssid"`, `"spp"`, `"inet_gate_s"`).

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
   - If `s_state == TC_STATE_IDLE && s_counter >= config_mngr_internet_gate_threshold_s_get()`:
     - Set `s_state = TC_STATE_ACTIVE`.
     - Start `s_tick_timer`.
     - Call `wifi_mngr_reward_ap_set(true)`.
     - Post `ESPORT_EVENT_EARNING_STARTED`.
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with current counter value.

5. In the 1-second tick callback:
   - Lock spinlock, decrement `s_counter` (floor 0), unlock.
   - Post `ESPORT_EVENT_COUNTER_CHANGED`.
   - If `s_counter == 0`:
     - Stop `s_tick_timer`.
     - Set `s_state = TC_STATE_IDLE`.
     - Call `wifi_mngr_reward_ap_set(false)`.
     - Post `ESPORT_EVENT_EARNING_STOPPED`.

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
   - Post `ESPORT_EVENT_SESSION_OPENED` with a `uint32_t` payload = `s_pulse_count` (qualifying pulse count), so `time_counter` can retroactively credit the qualification window effort.

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

8. Duration must be capped at `UINT16_MAX` (65535 s ~ 18 h 12 min) to fit in `uint16_t`.

9. Compute `internet_earned_s` at session close:
   - Cache `seconds_per_pulse` from `config_mngr_seconds_per_pulse_get()` when the IDLE->QUALIFYING transition occurs (alongside the other config cache reads).
   - At session close: `internet_earned_s = g_pulse_count * g_seconds_per_pulse`.
   - Store in `session_trk_record_t.internet_earned_s` (uint32_t).

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] Pulses for less than `start_session_interval_s` do NOT create a session.
- [ ] Pulses for more than `start_session_interval_s` without a long gap open a session.
- [ ] A gap > `idle_session_interval_s` during the qualification window resets to IDLE.
- [ ] `session.start_time` equals the UTC time of the first qualifying pulse (not the confirmation time).
- [ ] `session.duration_s` equals `last_pulse_time - first_pulse_time` (not including idle gap).
- [ ] `avg_speed_kmh_x10` is calculated correctly (regression test with known pulse count, interval, CPP).
- [ ] `internet_earned_s` equals `pulse_count * seconds_per_pulse` (value of `seconds_per_pulse` at session start).
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
     - **Wi-Fi**: STA status + SSID + IP, reward AP status, SSID, connected client count, reward AP traffic (kbps).
     - **Current Session**: counter (s + `h:mm:ss`), threshold, state label (Idle / Qualifying / Active), current speed (km/h), pulse-crediting status.
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
- [ ] Live fields (System, Wi-Fi, Current Session) update every 2 s via JS fetch without a full page reload.
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
- [ ] `GET /api/sessions/export?format=csv` returns downloadable CSV with the correct header row (including `internet_earned_s` column).
- [ ] `GET /api/sessions/export?format=json` returns downloadable JSON with `internet_earned_s` field and attachment header.
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
   - **Dashboard page title** must include the firmware version: `"ESPort-fi32 vX.Y.Z -- Status Dashboard"`. Read the version string at runtime via `esp_app_get_description()->version` (`esp_app_desc.h`; add `esp_app_format` to `PRIV_REQUIRES` in `main/CMakeLists.txt`).
   - **Session History table** must include an "Internet Earned" column (h:mm:ss) sourced from `session_trk_record_t.internet_earned_s`.
   - Include `http_server_dashboard.h`, `http_server_api.h`, `http_server_utils.h`, and all required module headers.
   - Follow `cctemplate` structure with `gp_tag`.

3. **Update `main/src/http_server.c`**:
   - Remove `http_srv_root_get_handler()` implementation, the `HTTP_SRV_SVG_*` constant definitions, and the handler's forward declaration.
   - Add `#include "http_server_dashboard.h"`.

4. **Update `main/CMakeLists.txt`**: add `"src/http_server_dashboard.c"` to `SRCS`.

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] `GET /` returns HTTP 200 with valid HTML containing all sections from spec §6.1.
- [ ] Dashboard page title includes the firmware version string (e.g. `"ESPort-fi32 v2.0.0 -- Status Dashboard"`).
- [ ] Session History table contains the "Internet Earned" column formatted as h:mm:ss.
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
   - `ESPORT_EVENT_EARNING_STARTED` / `ESPORT_EVENT_EARNING_STOPPED` → `ESP_LOGI`.

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
          └────────────────────────────────────────────────────────────┐
                                                                       │
Phase 8  (HTTP Config+API)       ── requires phases 1-7 complete       │
Phase 9  (HTTP Dashboard)        ── requires phase 8 complete          │
Phase 9A (Refactor: Utils)       ── requires phase 9 complete          │
Phase 9B (Refactor: Config)      ── requires phase 9A complete         │
Phase 9C (Refactor: API)         ── requires phase 9A complete         │
Phase 9D (Refactor: Export)      ── requires phase 9A complete         │
Phase 9E (Refactor: Dashboard)   ── requires phases 9A + 9C complete   │
Phase 9F (Refactor: Core+Build)  ── requires phases 9B–9E complete     │
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
| `ota_manager`           | `ota_mngr_`            | `OTA_MNGR_`         |
| `http_server_ota`       | `http_srv_ota_`        | `HTTP_SRV_OTA_`     |
| `buzzer`                | `buzzer_`              | `BUZZER_`           |
| `button_reset`          | `btn_rst_`             | `BTN_RST_`          |
| `activity_manager`      | `act_mngr_`            | `ACT_MNGR_`         |
| `http_server_activities`| `http_srv_`            | `HTTP_SRV_`         |
| `dyn_act_registry`      | `dyn_act_`             | `DYN_ACT_`          |
| `dyn_nonce`             | `dyn_nonce_`           | `DYN_NONCE_`        |
| `http_server_dyn`       | `http_srv_dyn_`        | `HTTP_SRV_DYN_`     |

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
| 6.1   | 6       | Config Manager — config password param         | `config_manager.c/h`                                                   |
| 6.2   | 6       | HTTP Config Auth — Basic Auth guard            | `http_server_config.c/h`, `http_server_ota.c`                          |
| 6.3   | 6       | HTTP Config Password Change Page               | `http_server_config.c/h`, `http_server.c`                              |
| 6.4   | 6       | Button Reset Module — BOOT long-press reset    | `button_reset.c/h`, `Kconfig.projbuild`, `CMakeLists.txt`             |
| 6.5   | 6       | Integration & Build Verification               | `main.c`, `http_server.c`                                              |
| 6.6   | 6       | Spec & Document Update                         | `docs/1-specification.md`, `docs/2-development_plan.md`                |
| 7.1   | 7       | Spec Update — Activity Credits                 | `docs/1-specification.md`                                              |
| 7.2   | 7       | Activity Manager Core Module                   | `activity_manager.c/h`, `event_ids.h`, `CMakeLists.txt`                |
| 7.3   | 7       | HTTP Server: Activities Pages                  | `http_server_activities.c/h`, `http_server.c`, `http_server_config.c`  |
| 7.4   | 7       | HTTP Server: Activity Credit API               | `http_server_api.c`                                                    |
| 7.5   | 7       | Integration & Verification                     | `main.c`                                                               |
| 7.6   | 7       | Documentation & README Update                  | `docs/1-specification.md`, `docs/2-development_plan.md`, `README.md`   |
| 8.1   | 8       | Data Model: `b_is_dynamic` & Credit Validation | `activity_manager.h`, `activity_manager.c`, `http_server_activities.c`, `http_server_api.c` |
| 8.2   | 8       | MAC-Based PIN & Dual Auth Credit API           | `device_registry.h`, `device_registry.c`, `http_server_config.h`, `http_server_config.c`, `http_server_api.c` |
| 8.3   | 8       | Build Infrastructure & Demo Pages              | `main/dyn_activities/*.html`, `CMakeLists.txt`, `dyn_act_registry.h`, generated `dyn_act_registry.c` |
| 8.4   | 8       | HTTP Server: `/dyn` Page, File Server & Manage UI | `http_server_dyn.h`, `http_server_dyn.c`, `http_server_activities.c`, `http_server_api.c`, `http_server.c` |
| 8.5   | 8       | Integration & Verification                     | all prior outputs, `http_server.c`                                     |
| 8.6   | 8       | Documentation & README Update                  | `docs/1-specification.md`, `docs/2-development_plan.md`, `README.md`   |

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

3. **§5.2 WiFi Manager** — add `wifi_mngr_reward_ap_throughput_kbps()` → `uint32_t` to the public API table, with description: "Returns the combined RX+TX throughput on the reward AP in kbps over the last 1-second interval.  Returns `0` when the reward AP is inactive or when no clients are associated with the AP (background AP netif traffic such as DHCP and ARP probes is discarded)."

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
     - **Outside spinlock**: call `wifi_mngr_reward_ap_set(true)`, post `ESPORT_EVENT_EARNING_STARTED`, start the decrement tick timer.
   - Create and start the periodic save timer (`TIME_CTR_SAVE_INTERVAL_S * 1 000 000 µs`, periodic) with callback `time_ctr_save_cb`.

3. **`time_ctr_save_cb()`** — static callback: calls `config_mngr_reward_counter_s_set(time_ctr_get())`.

4. In the zero-reached-handling code inside the tick callback: call `config_mngr_reward_counter_s_set(0U)` **before** calling `wifi_mngr_reward_ap_set(false)`.

5. **`time_ctr_counter_set(uint32_t val)`** (new public function):
   - **Inside spinlock**: set `g_counter_s = val`; capture `old_state = g_state`.
   - **Outside spinlock**: call `config_mngr_reward_counter_s_set(val)`.
   - If `val > 0` and `old_state == TIME_CTR_STATE_IDLE`: transition to `AP_ACTIVE` (call `wifi_mngr_reward_ap_set(true)`, post `ESPORT_EVENT_EARNING_STARTED`, start tick timer, reset pause state). Post `ESPORT_EVENT_COUNTER_CHANGED`.
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
      - Drain accumulators: `delta = g_rx_bytes[i] + g_tx_bytes[i]`; reset `g_rx_bytes[i] = 0; g_tx_bytes[i] = 0`. Accumulators must always be drained to discard background AP netif traffic (DHCP, ARP probes) that may match a registered MAC.
      - Check if `g_entries[i].mac` matches any `sta_list.sta[j].mac` (connection check).
      - **Not connected:** set `g_throughput_kbps[i] = 0`, `g_below_ticks[i] = 0`, `g_dev_paused[i] = true`, and `continue` to the next entry. This prevents phantom throughput readings from background AP traffic for disconnected devices.
      - **Connected:** set `g_throughput_kbps[i] = (uint32_t)(delta * 8U / 1000U)`.
      - Apply the sliding-window gate (identical logic to Feature 1 Overview pseudo-code, using `g_throughput_kbps[i]`, `threshold`, `timeout`, `g_below_ticks[i]`, `g_dev_paused[i]`).
      - If `!g_dev_paused[i]` AND `g_entries[i].b_enabled` AND `g_entries[i].counter_s > 0`: decrement `g_entries[i].counter_s`, set `b_changed = true`, and if it just reached 0 set `b_zero[i] = true`.

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
- [ ] After a registered device sends traffic while connected to the AP, `device_reg_entry_throughput_kbps_get()` returns a non-zero value on the following tick.
- [ ] Per-device byte counters reset to `0` each tick; `device_reg_entry_throughput_kbps_get()` returns `0` in the tick after the device goes idle.
- [ ] `device_reg_entry_throughput_kbps_get()` returns `0` for a registered device that is not connected to the AP, even if the AP netif processes background traffic matching the device's MAC (e.g. DHCP, ARP).

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

6. **Pulse handler (`time_ctr_pulse_event_handler`)** — simplified credit logic:
   - Speed gate check (Feature 2): unchanged.
   - In both `TIME_CTR_STATE_SESSION` and `TIME_CTR_STATE_EARNING` (outside spinlock): if `b_credit` and rider != `DEVICE_REG_NO_RIDER`, call `device_reg_entry_counter_set(rider_idx, device_reg_entry_counter_get(rider_idx) + spp)`. Credits go directly to the NVS-persisted device-registry counter in both active states.
   - Post `ESPORT_EVENT_COUNTER_CHANGED` with payload `time_ctr_get()` after every pulse.

7. **Threshold timer callback** — on firing (transition `SESSION → EARNING`):
   - Call `device_reg_entry_inet_gate_lock_set(rider, false)` to clear the gate lock.
   - Post `ESPORT_EVENT_EARNING_STARTED`.
   - No credit flushing needed (credits are already in the device-registry counter).

8. **Tick callback (`time_ctr_tick_cb`)** — unconditional delegation (unchanged from prior refactor).

9. **Session-closed handler** — transitions in all states:
   - `TIME_CTR_STATE_SESSION` + `ESPORT_EVENT_SESSION_CLOSED`: cancel the threshold timer, go to `TIME_CTR_STATE_IDLE`. Gate lock **remains set** (internet blocked until gate is completed in a future session or on reboot). Credits are already persisted in the device-registry counter. Post `ESPORT_EVENT_COUNTER_CHANGED`.
   - `TIME_CTR_STATE_EARNING` + `ESPORT_EVENT_SESSION_CLOSED`: call `device_reg_entry_inet_gate_lock_set(rider, false)` to clear the gate lock, transition to `TIME_CTR_STATE_IDLE`, post `ESPORT_EVENT_EARNING_STOPPED`.

10. **`time_ctr_get()`** — return `device_reg_entry_counter_get(current_rider)` directly. No `g_session_credits` accumulator. Returns `0` if no rider is selected (`DEVICE_REG_NO_RIDER`).

11. **`time_ctr_counter_set(uint32_t val)`** (Feature 3 API) — update semantics: call `device_reg_entry_counter_set(rider_idx, val)` for the current rider. If no rider is selected, return `ESP_ERR_INVALID_STATE` and log `ESP_LOGW`.

12. **`device_registry` additions** — internet gate lock:
    - Add RAM-only `g_inet_gate_locked[DEVICE_REG_MAX_ENTRIES]` array (zero-initialised on boot).
    - `device_reg_entry_inet_gate_lock_set(idx, b_locked)` and `device_reg_entry_inet_gate_lock_get(idx)` public functions.
    - `device_reg_mac_internet_allowed()`: also check `!g_inet_gate_locked[i]`.
    - `device_reg_tick()` decrement: skip if `g_inet_gate_locked[i]`.
    - `device_reg_entry_remove()`: compact `g_inet_gate_locked` in the same loop.
    - `device_reg_init()`: zero-initialise `g_inet_gate_locked`.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] The 1-second tick fires continuously from `time_ctr_init()` regardless of session state.
- [ ] Pulses in both `SESSION` and `EARNING` states are credited directly to the rider's `device_reg` counter (NVS-persisted) immediately.
- [ ] On `ESPORT_EVENT_SESSION_OPENED`, qualifying credits are credited directly to the device-registry counter.
- [ ] On `ESPORT_EVENT_SESSION_OPENED`, if the current rider's `device_reg` counter is > 0 AND `device_reg_entry_inet_gate_lock_get(rider) == false`, the internet gate is bypassed: state goes directly to EARNING and `ESPORT_EVENT_EARNING_STARTED` is posted.
- [ ] On `ESPORT_EVENT_SESSION_OPENED`, if counter == 0 OR gate lock is already set: `device_reg_entry_inet_gate_lock_set(rider, true)` is called, state goes to SESSION, gate timer starts.
- [ ] When the threshold timer fires: `device_reg_entry_inet_gate_lock_set(rider, false)`, state -> EARNING, `ESPORT_EVENT_EARNING_STARTED` posted. No credit flushing.
- [ ] `device_reg_tick()` is called unconditionally once per second.
- [ ] Per-device counters only decrement when the gate lock is false AND the per-device traffic gate is not paused.
- [ ] `device_reg_mac_internet_allowed()` returns `false` when gate lock is true, even if counter > 0.
- [ ] `time_ctr_get()` returns `device_reg_entry_counter_get(current_rider)` directly; no in-RAM accumulator; returns `0` when no rider selected.
- [ ] Session closed in `SESSION` state: timer cancelled, state -> IDLE. Gate lock **remains true**. Counter value visible via `time_ctr_get()` (already in NVS). `ESPORT_EVENT_EARNING_STARTED` is **not** posted.
- [ ] Session closed in `EARNING` state: `device_reg_entry_inet_gate_lock_set(rider, false)`, state -> IDLE, `ESPORT_EVENT_EARNING_STOPPED` posted.
- [ ] `g_inet_gate_locked` defaults to `false` on every boot: if counter > 0 in NVS, internet access is immediately available after reboot.
- [ ] `time_ctr_counter_set()` returns `ESP_ERR_INVALID_STATE` when no rider is selected.
- [ ] Migration: a non-zero `config_mngr_reward_counter_s_get()` value is transferred to the current rider's counter on first boot.

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
| `low_speed_buzzer_threshold_s` | `uint16_t` | `"bz_spd_thr_s"` | `3` | 0–65535 |

### Beep Patterns

One beep unit = `BUZZER_UNIT_MS` = 50 ms.

| Pattern ID constant | Trigger | Sequence |
| ------------------- | ------- | -------- |
| `BUZZER_PATTERN_SESSION_QUALIFYING` | ST_IDLE → ST_QUALIFYING | 5 units ON (`BUZZER_PATTERN_QUALIFYING_UNITS`) |
| `BUZZER_PATTERN_SESSION_QUALIFIED` | ST_QUALIFYING → ST_ACTIVE | 10 units ON (`BUZZER_PATTERN_QUALIFIED_UNITS`) |
| `BUZZER_PATTERN_SESSION_CLOSED` | ST_ACTIVE → ST_IDLE (idle timeout) | 2 ON, 1 OFF, 2 ON, 1 OFF, 2 ON (`BUZZER_PATTERN_CLOSED_BEEP_UNITS`, `BUZZER_PATTERN_CLOSED_GAP_UNITS`, `BUZZER_PATTERN_CLOSED_BEEP_COUNT`) |
| `BUZZER_PATTERN_SPEED_LOW` | Per tick: SESSION or EARNING, speed below min for ≥ `low_speed_buzzer_threshold_s` consecutive ticks | 2 units ON (`BUZZER_PATTERN_SPEED_LOW_UNITS`) |

### Interruption Rule

A new call to `buzzer_pattern_play()` while a pattern is playing **immediately interrupts** the current pattern and starts the new one.  The sole exception is `buzzer_speed_low_update(false)`: it stops a `SPEED_LOW` pattern in progress but does **not** interrupt any other pattern (e.g. a `SESSION_CLOSED` pattern triggered at the same tick must not be cut short).

### Speed-Low Beep Firing Condition

Called from `time_ctr_tick_cb()` once per second.  A `g_speed_low_ticks` counter (uint16_t, file-scope in `time_counter.c`) is maintained alongside the beep logic:

- **Increment** `g_speed_low_ticks` when **all** of the following hold:
  1. `g_state == TIME_CTR_STATE_SESSION` or `g_state == TIME_CTR_STATE_EARNING` (session open)
  2. `config_mngr_min_speed_to_increment_time_kmh_x10_get() > 0` (speed gate is enabled)
  3. `time_ctr_current_speed_x10_get() > 0` (rider is moving — not stopped)
  4. `time_ctr_current_speed_x10_get() < min_speed_to_increment_time_kmh_x10` (speed below threshold)
- **Reset** `g_speed_low_ticks = 0` when any of the above conditions is false.
- Call `buzzer_speed_low_update(true)` only when the speed-low condition holds **and** `g_speed_low_ticks >= low_speed_buzzer_threshold_s`; call `buzzer_speed_low_update(false)` otherwise.
- `g_cfg_low_speed_bz_thresh_s` is a cached copy of `config_mngr_low_speed_buzzer_threshold_s_get()` refreshed by `time_ctr_config_cache_refresh()` on `ESPORT_EVENT_CONFIG_CHANGED`.

When `low_speed_buzzer_threshold_s = 0`, the condition `g_speed_low_ticks >= 0` is always true for a uint16_t, so the beep fires immediately on the first below-threshold tick (original behaviour).

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

**Goal:** Add getters and setters for `buzzer_enabled` and `low_speed_buzzer_threshold_s` to the configuration manager.

**Inputs**
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)

**Tasks**

1. **`main/src/config_manager.c`** — add:
   - `#define CONFIG_MNGR_KEY_BUZZER_ENABLED  ("buzzer_en")`
   - `#define CONFIG_MNGR_DEF_BUZZER_ENABLED  ((uint8_t)1U)`
   - `#define CONFIG_MNGR_KEY_LOW_SPEED_BZ_THRESH_S  ("bz_spd_thr_s")`
   - `#define CONFIG_MNGR_DEF_LOW_SPEED_BZ_THRESH_S  ((uint16_t)3U)`
   - In `config_mngr_init()`: read `"buzzer_en"`; if `ESP_ERR_NVS_NOT_FOUND`, write the default `1`.
   - In `config_mngr_init()`: call `config_mngr_default_u16_write(handle, CONFIG_MNGR_KEY_LOW_SPEED_BZ_THRESH_S, 3U)` to init the new key.

2. **`main/inc/config_manager.h`** — declare (with complete Doxygen, following existing style):
   ```c
   bool      config_mngr_buzzer_enabled_get(void);
   esp_err_t config_mngr_buzzer_enabled_set(bool b_enabled);
   uint16_t  config_mngr_low_speed_buzzer_threshold_s_get(void);
   esp_err_t config_mngr_low_speed_buzzer_threshold_s_set(uint16_t val);
   ```

3. **`main/src/config_manager.c`** — implement:
   - `config_mngr_buzzer_enabled_get()`: read NVS key `"buzzer_en"` as `uint8_t`; return `(val != 0U)`; on any NVS error return `true` (fail-safe: buzzer on by default).
   - `config_mngr_buzzer_enabled_set(b_enabled)`: write `(uint8_t)(b_enabled ? 1U : 0U)` to NVS key `"buzzer_en"`; commit; return `ESP_OK` or the NVS error code.
   - `config_mngr_low_speed_buzzer_threshold_s_get()`: `config_mngr_u16_get(CONFIG_MNGR_KEY_LOW_SPEED_BZ_THRESH_S, 3U)`.
   - `config_mngr_low_speed_buzzer_threshold_s_set(val)`: `config_mngr_u16_set(CONFIG_MNGR_KEY_LOW_SPEED_BZ_THRESH_S, val, 0U, UINT16_MAX)` (full range valid; 0 = immediate).

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_buzzer_enabled_set(false)` returns `ESP_OK`; subsequent `config_mngr_buzzer_enabled_get()` returns `false`.
- [ ] `config_mngr_buzzer_enabled_set(true)` returns `ESP_OK`; subsequent `config_mngr_buzzer_enabled_get()` returns `true`.
- [ ] Value survives `config_mngr_init()` reinit (simulated reboot).
- [ ] Factory default `true` applied when NVS key is absent.
- [ ] `config_mngr_low_speed_buzzer_threshold_s_set(0)` returns `ESP_OK`.
- [ ] `config_mngr_low_speed_buzzer_threshold_s_set(65535)` returns `ESP_OK`.
- [ ] Factory default `3` applied when the `bz_spd_thr_s` NVS key is absent.
- [ ] Value survives `config_mngr_init()` reinit.

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

**Goal:** Call `buzzer_speed_low_update()` from the time counter 1-second tick callback, with a configurable delay so the beep only fires after the speed has been below the threshold for `low_speed_buzzer_threshold_s` consecutive seconds.

**Inputs**
- `main/src/time_counter.c` (existing, Feature 4.3 output)
- `buzzer.h / buzzer.c` (Phase 5.1 output)
- `config_manager` (Phase 5.2 output)

**Tasks**

1. **`main/src/time_counter.c`** — add `#include "buzzer.h"`.

2. Add two file-scope variables near the cached config variables:
   ```c
   static uint16_t g_cfg_low_speed_bz_thresh_s = 3U;  /* cached from config */
   static uint16_t g_speed_low_ticks           = 0U;  /* consecutive below-threshold ticks */
   ```
   Refresh `g_cfg_low_speed_bz_thresh_s` in `time_ctr_config_cache_refresh()` by calling `config_mngr_low_speed_buzzer_threshold_s_get()`.

3. In `time_ctr_tick_cb()`, after the unconditional `device_reg_tick()` call, add the speed-low beep computation:
   ```c
   bool b_speed_low = false;
   if ((g_state == TIME_CTR_STATE_SESSION) || (g_state == TIME_CTR_STATE_EARNING))
   {
       uint32_t speed_x10 = time_ctr_current_speed_x10_get();
       uint16_t min_spd   = g_cfg_min_speed_kmh_x10;
       b_speed_low = (min_spd > 0U) && (speed_x10 > 0U) && (speed_x10 < (uint32_t)min_spd);
   }
   /* Accumulate consecutive below-threshold ticks; reset when not low. */
   if (b_speed_low)
   {
       if (g_speed_low_ticks < UINT16_MAX)
       {
           g_speed_low_ticks++;
       }
   }
   else
   {
       g_speed_low_ticks = 0U;
   }
   buzzer_speed_low_update(b_speed_low && (g_speed_low_ticks >= g_cfg_low_speed_bz_thresh_s));
   ```
   When `g_state` is `TIME_CTR_STATE_IDLE`, `b_speed_low` remains `false`, `g_speed_low_ticks` is reset to zero, and `buzzer_speed_low_update(false)` is called, which cleanly stops any residual speed-low beep.

   When `g_cfg_low_speed_bz_thresh_s == 0`: `g_speed_low_ticks` increments to 1 on the first below-threshold tick and `1 >= 0` is always true, so the beep fires immediately (original behaviour preserved).

4. In the `ESPORT_EVENT_SESSION_CLOSED` handler inside `time_counter.c` (where the state returns to `TIME_CTR_STATE_IDLE`), add:
   ```c
   g_speed_low_ticks = 0U;
   buzzer_speed_low_update(false);
   ```
   to stop the speed-low beep immediately and reset the counter without waiting for the next tick.

**Notes**

> `buzzer_speed_low_update()` is called from `time_ctr_tick_cb()`, which is an `esp_timer` callback (runs in the `esp_timer` task).  The call is O(1) with a spinlock and safe in this context.

> If `min_speed_to_increment_time_kmh_x10 == 0` (Feature 2 speed gate is disabled), `b_speed_low` is always `false`, and no speed-low beep is ever produced.  This is the correct behaviour: when there is no minimum speed requirement, "below threshold" has no meaning.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] In `TIME_CTR_STATE_SESSION` or `TIME_CTR_STATE_EARNING` with `min_speed > 0`, `0 < speed < min_speed`: `buzzer_speed_low_update(true)` is **not** called until the speed has been low for `low_speed_buzzer_threshold_s` consecutive ticks.
- [ ] After `low_speed_buzzer_threshold_s` consecutive below-threshold ticks: `buzzer_speed_low_update(true)` is called; GPIO pulses HIGH for 100 ms once per second.
- [ ] If speed returns above the threshold before the delay expires: `g_speed_low_ticks` resets to 0 and no beep fires.
- [ ] Speed reaches 0: `buzzer_speed_low_update(false)` is called; active speed-low beep stops immediately.
- [ ] Speed reaches or exceeds `min_speed`: `buzzer_speed_low_update(false)` is called; no more speed-low beeps.
- [ ] `min_speed == 0` (gate disabled): `buzzer_speed_low_update(false)` is called every tick; no speed-low beep is ever produced.
- [ ] `low_speed_buzzer_threshold_s == 0` (immediate): beep fires from the first below-threshold tick (identical to original behaviour).
- [ ] State is `TIME_CTR_STATE_IDLE`: `buzzer_speed_low_update(false)` is called and `g_speed_low_ticks` is reset; no speed-low beep produced.
- [ ] `ESPORT_EVENT_SESSION_CLOSED` received: `g_speed_low_ticks` is reset to 0 and `buzzer_speed_low_update(false)` is called immediately; speed-low beep stops without waiting for the next tick.
- [ ] Config change (`ESPORT_EVENT_CONFIG_CHANGED`): `g_cfg_low_speed_bz_thresh_s` is refreshed from NVS; the new threshold takes effect on the next tick.
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

---

## Feature 6 — Config Page Password Protection

### Overview

The configuration page (`/config`) is currently accessible without authentication.  This feature
adds HTTP Basic Auth to all configuration endpoints, mirroring the pattern already used for the
OTA firmware update page.  A single admin password is stored in NVS, with a factory default of
`esport-fi32`.  A dedicated password-change page (`/config/pwd`) lets the user update the password
after authenticating.

A physical password-reset mechanism uses the ESP32-C6 BOOT button (GPIO 9): holding it for 5
seconds during runtime resets **both** the config and OTA passwords to their factory defaults and
plays a long buzzer confirmation beep.

### Design Decisions

- **Authentication method:** HTTP Basic Auth, identical to OTA.  The fixed username is `"admin"`.
- **NVS storage:** config password stored in the existing `esport_cfg` namespace under key
  `"cfg_pwd"`.  The OTA namespace `esport_ota` is not changed.
- **Protected endpoints:** `GET /config`, `POST /config`, `POST /config/reset`,
  `GET /config/pwd`, `POST /config/pwd` — all require authentication.
- **Password reset:** a new `button_reset` module monitors GPIO 9 (configurable via Kconfig)
  using a 100 ms periodic `esp_timer`.  On detecting a 5-second continuous press it resets both
  passwords and plays a `BUZZER_PATTERN_PASSWORD_RESET` pattern (50 buzzer units = 2.5 s beep).
- **URI slot budget:** two new endpoints (`GET /config/pwd`, `POST /config/pwd`) require
  increasing `cfg.max_uri_handlers` from `13U` to `15U` in `http_server.c`.

### New Configuration Parameter

| Parameter         | Type   | NVS namespace | NVS key     | Default          | Max length |
| ----------------- | ------ | ------------- | ----------- | ---------------- | ---------- |
| `config_password` | string | `esport_cfg`  | `"cfg_pwd"` | `"esport-fi32"` | 63 chars   |

---

### Phase 6.1 — Config Manager: Config Password Parameter

**Goal:** Add getter, setter, and credential-check functions for the config page password in the
configuration manager module.

**Inputs**
- `main/inc/config_manager.h`, `main/src/config_manager.c` (existing)

**Tasks**

1. **`main/inc/config_manager.h`** — declare:
   - `#define CONFIG_MNGR_CFG_PASSWORD_MAX_LEN (63U)` — maximum password length (excluding NUL).
   - `#define CONFIG_MNGR_CFG_PASSWORD_DEFAULT ("esport-fi32")` — factory default.
   - `#define CONFIG_MNGR_CFG_HTTP_USERNAME ("admin")` — fixed HTTP Basic Auth username.
   ```c
   esp_err_t config_mngr_cfg_password_get(char * p_buf, size_t len);
   esp_err_t config_mngr_cfg_password_set(const char * p_password);
   bool      config_mngr_cfg_credentials_check(const char * p_password);
   ```
   Follow the existing Doxygen comment style (multi-line, `\brief`, parameter directions, blank
   line before `\return`).

2. **`main/src/config_manager.c`** — implement:
   - `config_mngr_cfg_password_get()`: open `esport_cfg` read-only, read string key `"cfg_pwd"`,
     fall back to `CONFIG_MNGR_CFG_PASSWORD_DEFAULT` if not found, close handle.
   - `config_mngr_cfg_password_set()`: validate length (1–63), open `esport_cfg` read-write,
     write string key `"cfg_pwd"`, commit, close handle.
   - `config_mngr_cfg_credentials_check()`: read stored password via
     `config_mngr_cfg_password_get()`, compare with `strcmp`.  Return `true` on match.
   - In `config_mngr_init()`: read `"cfg_pwd"` — if `ESP_ERR_NVS_NOT_FOUND`, write factory
     default and commit.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `config_mngr_cfg_password_set("test123")` returns `ESP_OK`.
- [ ] `config_mngr_cfg_credentials_check("test123")` returns `true` after set.
- [ ] `config_mngr_cfg_credentials_check("wrong")` returns `false`.
- [ ] Factory default `"esport-fi32"` is applied when NVS key is absent.
- [ ] Value survives `config_mngr_init()` reinit (simulated reboot).
- [ ] Password longer than 63 chars is rejected by the setter.
- [ ] Empty password (`""`) is rejected by the setter.

---

### Phase 6.2 — HTTP Config Auth: Basic Auth Guard

**Goal:** Add HTTP Basic Auth to all existing config endpoints (`GET /config`, `POST /config`,
`POST /config/reset`), using the same pattern as `http_srv_ota_auth_check()`.

**Inputs**
- `main/src/http_server_config.c`, `main/inc/http_server_config.h` (existing)
- `main/src/http_server_ota.c` — reference for auth check and base64 decode patterns.
- `config_manager.h` (Phase 6.1 output)

**Tasks**

1. **`main/inc/http_server_config.h`** — add internal constants:
   ```c
   #define HTTP_SRV_CFG_AUTH_HDR_MAX     (256U)
   #define HTTP_SRV_CFG_BASIC_PREFIX_LEN (6U)
   #define HTTP_SRV_CFG_DECODED_MAX      (192U)
   ```

2. **`main/src/http_server_config.c`** — implement:
   - `static bool http_srv_cfg_auth_check(httpd_req_t * p_req)` — mirrors
     `http_srv_ota_auth_check()`:
     1. Read `"Authorization"` header; if absent → 401 +
        `WWW-Authenticate: Basic realm="esport-fi32 Config"`.
     2. Verify `"Basic "` prefix.
     3. Base64-decode credentials (reuse `http_srv_base64_decode()` — see note below).
     4. Split on `':'`, verify username == `CONFIG_MNGR_CFG_HTTP_USERNAME` and password via
        `config_mngr_cfg_credentials_check()`.
     5. Return `true` on success, `false` on any failure (401 already sent).
   - All large buffers (`hdr_buf`, `decoded`) must be `static` (handlers are serialised by httpd).
   - Add `if (!http_srv_cfg_auth_check(p_req)) { return ESP_OK; }` as the first line of:
     - `http_srv_config_get_handler()`
     - `http_srv_config_post_handler()`
     - `http_srv_config_reset_handler()`

3. **Base64 decode reuse** — the base64 decode function `http_srv_ota_base64_decode()` is currently
   `static` in `http_server_ota.c`.  To share it:
   - Move the declaration to `http_server_utils.h` as
     `int http_srv_base64_decode(const char * p_in, char * p_out, size_t out_len)`.
   - Move the implementation to `http_server_utils.c` (remove `static`; rename from
     `http_srv_ota_base64_decode` to `http_srv_base64_decode`).
   - Update `http_server_ota.c` to call `http_srv_base64_decode()` instead.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /config` without credentials returns HTTP 401 with `WWW-Authenticate` header.
- [ ] `GET /config` with valid `Authorization: Basic <base64(admin:esport-fi32)>` returns HTTP 200.
- [ ] `POST /config` without credentials returns HTTP 401.
- [ ] `POST /config/reset` without credentials returns HTTP 401.
- [ ] Wrong password returns HTTP 401.
- [ ] Wrong username returns HTTP 401.
- [ ] OTA auth continues to work unchanged (no regression).
- [ ] `http_srv_ota_base64_decode` no longer exists — replaced by `http_srv_base64_decode` in
      utils.

---

### Phase 6.3 — HTTP Config Password Change Page

**Goal:** Add a dedicated password-change page (`GET /config/pwd`, `POST /config/pwd`) for the
config page password, following the same design as the OTA password change page (`/ota/pwd`).

**Inputs**
- `main/src/http_server_config.c`, `main/inc/http_server_config.h` (Phase 6.2 output)
- `main/src/http_server_ota.c` — reference for `/ota/pwd` GET/POST handlers
- `main/src/http_server.c` — URI registration

**Tasks**

1. **`main/inc/http_server_config.h`** — declare two new handlers:
   ```c
   esp_err_t http_srv_config_pwd_get_handler(httpd_req_t * p_req);
   esp_err_t http_srv_config_pwd_post_handler(httpd_req_t * p_req);
   ```

2. **`main/src/http_server_config.c`** — implement:
   - `http_srv_config_pwd_get_handler()`:
     1. Call `http_srv_cfg_auth_check()` — 401 if not authenticated.
     2. Serve an HTML form with fields: current password, new password, confirm new password.
     3. If query string contains `saved=1`, show a success message.
     4. Include a "Back to Configuration" navigation link.
   - `http_srv_config_pwd_post_handler()`:
     1. Call `http_srv_cfg_auth_check()` — 401 if not authenticated.
     2. Read and URL-decode form fields: `current_pwd`, `new_pwd`, `confirm_pwd`.
     3. Validate current password via `config_mngr_cfg_credentials_check()`.
     4. Validate new password length (1–`CONFIG_MNGR_CFG_PASSWORD_MAX_LEN`).
     5. Validate `new_pwd == confirm_pwd`.
     6. Call `config_mngr_cfg_password_set()`.
     7. On success: redirect to `GET /config/pwd?saved=1` (HTTP 303).
     8. On validation failure: respond HTTP 400 with error description.
   - Large buffers (`form_buf`, `enc_val`, password fields) must be `static`.

3. **`main/src/http_server.c`** — register two new URI handlers:
   - `{ .uri = "/config/pwd", .method = HTTP_GET,  .handler = http_srv_config_pwd_get_handler }`
   - `{ .uri = "/config/pwd", .method = HTTP_POST, .handler = http_srv_config_pwd_post_handler }`
   - Increase `cfg.max_uri_handlers` from `13U` to `15U`.

4. **`GET /config` page** — add a link to the password change page:
   `<a href="/config/pwd">Change config password</a>`

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /config/pwd` without credentials returns HTTP 401.
- [ ] `GET /config/pwd` with valid credentials returns HTTP 200 with password form.
- [ ] `POST /config/pwd` with wrong current password returns HTTP 400.
- [ ] `POST /config/pwd` with mismatching new/confirm passwords returns HTTP 400.
- [ ] `POST /config/pwd` with valid data saves new password and redirects to `/config/pwd?saved=1`.
- [ ] After password change, old password no longer grants access to `/config`.
- [ ] New password grants access to all config endpoints.
- [ ] `GET /config` page contains a link to `/config/pwd`.

---

### Phase 6.4 — Button Reset Module: BOOT Long-Press Reset

**Goal:** Create a new `button_reset` module that monitors the ESP32-C6 BOOT button (GPIO 9) for
a 5-second long press.  When detected, both config and OTA passwords are reset to their factory
defaults and a long buzzer beep confirms the action.

**Inputs**
- `main/inc/config_manager.h` (Phase 6.1 output — `config_mngr_cfg_password_set()`)
- `main/inc/ota_manager.h` (existing — `ota_mngr_password_set()`, `OTA_MNGR_PASSWORD_DEFAULT`)
- `main/inc/buzzer.h` (existing — `buzzer_pattern_play()`)
- `main/Kconfig.projbuild` (existing)

**Tasks**

1. **`main/Kconfig.projbuild`** — add inside the existing menu:
   ```kconfig
   config ESPORT_BOOT_BUTTON_GPIO
       int "BOOT button GPIO number"
       default 9
       range 0 30
       help
           GPIO connected to the BOOT button.  Default is GPIO 9 (ESP32-C6 BOOT).

   config ESPORT_BOOT_BUTTON_RESET_HOLD_S
       int "BOOT button hold time for password reset (seconds)"
       default 5
       range 1 30
       help
           Duration in seconds the BOOT button must be held to trigger a password reset.
   ```

2. **`main/inc/button_reset.h`** — declare:
   ```c
   #define BTN_RST_POLL_INTERVAL_MS (100U)
   #define BUZZER_PATTERN_PASSWORD_RESET (4)

   esp_err_t btn_rst_init(void);
   ```
   - `BUZZER_PATTERN_PASSWORD_RESET` — new buzzer pattern ID (50 units ON = 2.5 s continuous
     beep).  The value must not collide with existing pattern IDs (0–3 are taken).
   - Doxygen: init creates the polling timer but does not start any GPIO interrupt.
   - Guard with `BUTTON_RESET_H`; use `hhtemplate` structure.

3. **`main/src/button_reset.c`** — implement:
   - Configure `CONFIG_ESPORT_BOOT_BUTTON_GPIO` as input with internal pull-up (BOOT button
     is active LOW).
   - Create a 100 ms periodic `esp_timer` (`btn_rst_poll_timer`).
   - In the timer callback:
     1. Read GPIO level.
     2. If LOW (pressed): increment `g_held_count`.
     3. If HIGH (released): reset `g_held_count = 0`.
     4. If `g_held_count >= (CONFIG_ESPORT_BOOT_BUTTON_RESET_HOLD_S * 1000 / BTN_RST_POLL_INTERVAL_MS)`:
        - Call `config_mngr_cfg_password_set(CONFIG_MNGR_CFG_PASSWORD_DEFAULT)`.
        - Call `ota_mngr_password_set(OTA_MNGR_PASSWORD_DEFAULT)`.
        - Call `buzzer_pattern_play(BUZZER_PATTERN_PASSWORD_RESET)`.
        - Log `ESP_LOGW(gp_tag, "password reset: config and OTA passwords restored to defaults")`.
        - Reset `g_held_count = 0` (prevent re-triggering until released and held again).
        - Set `g_reset_done = true`; do not re-trigger until button is released (`g_reset_done`
          cleared when GPIO reads HIGH).
   - Follow `cctemplate` structure with `gp_tag = "button_reset"`.

4. **`main/inc/buzzer.h`** — add `BUZZER_PATTERN_PASSWORD_RESET = 4` to the
   `buzzer_pattern_id_t` enum.

5. **`main/src/buzzer.c`** — add the new pattern to the pattern table:
   - Pattern: 50 units ON, 0 units OFF (single continuous beep of 2.5 s).

6. **`main/CMakeLists.txt`** — add `"src/button_reset.c"` to `SRCS`.

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `btn_rst_init()` configures GPIO and starts the polling timer.
- [ ] Holding BOOT for < 5 s does not trigger a reset.
- [ ] Holding BOOT for >= 5 s resets both config and OTA passwords to factory defaults.
- [ ] After reset, `config_mngr_cfg_credentials_check("esport-fi32")` returns `true`.
- [ ] After reset, `ota_mngr_credentials_check("esport-fi32")` returns `true`.
- [ ] Buzzer plays a 2.5 s continuous beep on reset.
- [ ] Continuing to hold the button after a reset does not re-trigger until released and held
      again.
- [ ] GPIO number is configurable via `menuconfig`.
- [ ] Hold duration is configurable via `menuconfig`.
- [ ] Module follows `cctemplate` and `hhtemplate` structure.

---

### Phase 6.5 — Integration & Build Verification

**Goal:** Wire the new `button_reset` module into `main.c`, verify the full build, and confirm
end-to-end behaviour of all password-protected flows.

**Inputs**
- All Phase 6.1–6.4 outputs.
- `main/src/main.c` (existing)

**Tasks**

1. **`main/src/main.c`** — add `#include "button_reset.h"` and call `btn_rst_init()` in
   `app_main()` after `buzzer_init()` (the button reset module depends on the buzzer being
   initialised).

2. **`main/src/http_server.c`** — verify `cfg.max_uri_handlers` is `15U` and all 14 URI
   handlers are registered correctly.

3. Run `idf.py build` and fix any remaining compilation or linker errors.

4. **End-to-end checklist:**

   | #  | Test                                                                              | Pass/Fail |
   | -- | --------------------------------------------------------------------------------- | --------- |
   | 1  | `GET /config` without credentials returns HTTP 401 with `WWW-Authenticate`        |           |
   | 2  | `GET /config` with `admin:esport-fi32` returns HTTP 200 with config form          |           |
   | 3  | `POST /config` without credentials returns HTTP 401                               |           |
   | 4  | `POST /config/reset` without credentials returns HTTP 401                         |           |
   | 5  | `GET /config/pwd` with valid auth shows password change form                      |           |
   | 6  | Change config password to `"newpass"` via `POST /config/pwd` succeeds             |           |
   | 7  | `GET /config` with old password `"esport-fi32"` returns HTTP 401                  |           |
   | 8  | `GET /config` with new password `"newpass"` returns HTTP 200                      |           |
   | 9  | OTA endpoints still work with OTA password (no regression)                        |           |
   | 10 | Hold BOOT button for 5 s → buzzer plays 2.5 s beep                               |           |
   | 11 | After BOOT reset, `admin:esport-fi32` works for `/config`                         |           |
   | 12 | After BOOT reset, `admin:esport-fi32` works for `/ota`                            |           |
   | 13 | Dashboard (`GET /`) remains accessible without authentication                     |           |
   | 14 | API endpoints (`/api/*`) remain accessible without authentication                 |           |
   | 15 | Short BOOT press (< 5 s) does not trigger reset                                  |           |

**Acceptance Criteria**

- [ ] `idf.py build` succeeds with zero errors and zero warnings (`-Werror` enforced).
- [ ] `btn_rst_init()` is called in `app_main()` after `buzzer_init()`.
- [ ] All 15 end-to-end tests pass.
- [ ] No assertion failures or watchdog triggers during 10-minute continuous operation with
      password-protected config access.

---

### Phase 6.6 — Spec & Document Update

**Goal:** Update `docs/1-specification.md` and the Module Prefix Table in
`docs/2-development_plan.md` to reflect the new config password feature, the button reset module,
and the new buzzer pattern.

**Inputs**
- All Phase 6.1–6.5 outputs.
- `docs/1-specification.md` (current)
- `docs/2-development_plan.md` (current — Module Prefix Table)

**Tasks**

1. **§2 Hardware table** — add a row:
   - BOOT button GPIO: **GPIO 9** (configurable at build time via
     `CONFIG_ESPORT_BOOT_BUTTON_GPIO`).  Internal pull-up; active LOW.

2. **§3 Configuration Parameters table** — add one row:
   - `config_password` — `string`, NVS key `"cfg_pwd"`, default `"esport-fi32"`, max 63 chars,
     description: "Password for HTTP Basic Auth on all `/config` endpoints.  Username is always
     `admin`."

3. **§4 Component/File Layout** — add `button_reset.h` to the `inc/` listing and
   `button_reset.c` to the `src/` listing.

4. **§5.1 NVS Configuration Manager** — add getter/setter/check entries for `config_password` to
   the public API table: `config_mngr_cfg_password_get()`, `config_mngr_cfg_password_set()`,
   `config_mngr_cfg_credentials_check()`.

5. **New §5.X — Button Reset Module** — insert after the Buzzer Module section:
   - **File:** `button_reset.c` / `button_reset.h`
   - **Responsibilities:** poll the BOOT button via a 100 ms periodic `esp_timer`; on a
     5-second continuous press, reset both config and OTA passwords to factory defaults and play
     `BUZZER_PATTERN_PASSWORD_RESET`.
   - **Hardware:** `CONFIG_ESPORT_BOOT_BUTTON_GPIO` (default GPIO 9), active LOW, internal
     pull-up.
   - **Public API:** `btn_rst_init()`.
   - **Thread safety:** timer callback is O(1); password set functions handle their own NVS
     locking.

6. **§5.8 HTTP Server** — document that `GET /config`, `POST /config`, `POST /config/reset`,
   `GET /config/pwd`, and `POST /config/pwd` are protected by HTTP Basic Auth (realm
   `"esport-fi32 Config"`, username `"admin"`, password from NVS key `"cfg_pwd"`).

7. **§5.10 Buzzer Module** (or wherever the buzzer patterns are listed) — add
   `BUZZER_PATTERN_PASSWORD_RESET = 4`: 50 units ON (2.5 s continuous beep), triggered by
   BOOT button long press.

8. **§6.2 Configuration Page** — note that the page requires HTTP Basic Auth.  Add a "Change
   config password" link description.

9. **§6.X (new) — Config Password Change — `GET /config/pwd` and `POST /config/pwd`** — document
   the password change flow (current password, new password, confirm, redirect on success).

10. **§7.1 Boot Sequence** — add `btn_rst_init()` to the boot sequence, called after
    `buzzer_init()`.

11. **§8 NVS Layout — namespace `esport_cfg`** — add `"cfg_pwd"` (`string`, max 63+1 chars) to
    the key table.

12. **§10 Factory Defaults & NVS Recovery** — document that holding the BOOT button for 5 s
    resets config and OTA passwords to factory defaults.

13. **Module Prefix Table** (in `docs/2-development_plan.md`) — add row:
    `button_reset` | `btn_rst_` | `BTN_RST_`.

**Acceptance Criteria**

- [ ] §2 Hardware table includes the BOOT button row with GPIO default, pull mode, and active
      level.
- [ ] §3 includes `config_password` with correct type, NVS key, default, and description.
- [ ] §4 file layout lists `button_reset.h` and `button_reset.c`.
- [ ] §5.1 lists all three config password API functions.
- [ ] New §5.X fully documents the Button Reset module.
- [ ] §5.8 documents Basic Auth on all five config endpoints.
- [ ] §5.10 (Buzzer) includes `BUZZER_PATTERN_PASSWORD_RESET` with pattern description.
- [ ] §6.2 notes auth requirement and password-change link.
- [ ] New §6.X documents the config password change page.
- [ ] §7.1 Boot Sequence includes `btn_rst_init()` after `buzzer_init()`.
- [ ] §8 includes `"cfg_pwd"` in the `esport_cfg` key table.
- [ ] §10 documents the BOOT button password reset procedure.
- [ ] Module Prefix Table includes `button_reset` | `btn_rst_` | `BTN_RST_`.

---

## Feature 7 — Activity Credits

### Overview

This feature lets a parent/admin define a **global pool of up to 30 activities** (e.g. "Read for 30 min", "Math homework", "Tidy room") and assign them to any of the registered devices (users). On a dedicated **Activities award page** (`GET /activities`), the admin selects a user from a combobox; the page shows that user's assigned activities with a "Credit h:mm:ss" button per activity. Clicking the button instantly adds the configured internet-time credits to the user's counter.

Each activity has a configurable **daily limit** (how many times per day a user can earn credits for it). Once reached, the button is greyed out. The daily count resets at midnight local time and persists across reboots in NVS so the system cannot be "gamed" by power-cycling. A **credit log** (last 30 entries per user, NVS-backed) is shown at the bottom of the page.

A separate **Activity Manager page** (`GET /activities/manage`) lets the admin create, edit, and delete activities in the global pool, and manage which activities are assigned to which users.

A **JSON API endpoint** (`POST /api/activities/credit`) exposes the same credit action programmatically. This enables future **gamification**: a client-side game (e.g. a math-quiz web app running on the child's device) can dynamically calculate earned credits based on performance, then call the API to award them. The game is entirely client-side; the firmware only receives and applies the final credit amount. The activity's `time_limit_s` field serves as a reference that games use for their credit-scaling logic. The optional `completion_time_s` field in the API call is stored in the credit log for future analytics.

Key behaviours:
- **30 activities** max in the global pool; each has an auto-generated unique integer ID (1-based, never reused).
- Each activity carries: `name` (≤40 chars), `credit_s` (0 = dynamic/API-provided), `time_limit_s` (gamification reference), `daily_limit` (1–255 times/day, same cap for all users it is assigned to).
- An activity with `credit_s = 0` is a **dynamic-credit** activity: it is visible in the Activity Manager but **hidden** on the award page. It can only be credited via the JSON API (with the caller supplying the exact `credits_s`).
- The same activity can be assigned to multiple users independently. Up to **20 activities** per user.
- Both `/activities` and `/activities/manage` are protected by the same HTTP Basic Auth as `/config`.
- A new event `ESPORT_EVENT_ACTIVITY_CREDITED` is posted whenever credits are applied, allowing future integrations.

### Suggested Improvements

> The following enhancements are recommended and worth discussing with the project owner before finalising the plan:

1. **Buzzer confirmation beep** — Add a new buzzer pattern `BUZZER_PATTERN_ACTIVITY_CREDIT` (e.g. 2 ON, 1 OFF, 2 ON = "double ding") that fires whenever an activity is credited. This gives the child instant audible confirmation that their credit has landed, consistent with the existing buzzer feedback for bike sessions.

2. **`completion_time_s` in the credit API** — Include this optional field now (0 = not provided). Client-side games fill it in when they complete a timed challenge. The firmware stores it in the credit log entry. This costs nothing in the current implementation and unlocks rich analytics in the future (e.g. plotting completion-time trends per activity).

3. **`GET /api/activities` read endpoint** — Expose the activity list (with per-user daily status) as a JSON API so client-side games can discover available activities, their `time_limit_s`, and their `credit_s` without scraping the HTML page. This is necessary for the gamification use case.

4. **Activity credits in the status dashboard** — Add a "Credits today" column to the Devices table on the status dashboard showing the sum of `credits_s` awarded to each user today via activities. This makes achievements visible at a glance alongside the bike-earned credits, and requires only a small change to `http_server_dashboard.c` and `http_server_api.c`.

5. **Gamification pattern documentation** — Document the recommended client-side game integration pattern in the spec and plan: (a) game fetches `GET /api/activities?device_idx=N` to list available activities and their `time_limit_s`; (b) game runs a timed challenge, measures `completion_time_s`; (c) game computes `credits_s` using whatever formula it wants (e.g. `min(credit_s, credit_s * time_limit_s / completion_time_s)` capped at 2×); (d) game calls `POST /api/activities/credit` with `device_idx`, `act_id`, `credits_s`, `completion_time_s`. The firmware does not validate the formula — it trusts the caller, consistent with the admin-only auth on the API.

### New Configuration Parameter

| Parameter | Type | NVS key | Default | Description |
| --------- | ---- | ------- | ------- | ----------- |
| `activity_credit_buzzer_en` | bool (uint8) | `"ac_bz_en"` | `true` | Enable/disable the buzzer beep (`BUZZER_PATTERN_ACTIVITY_CREDIT`) played when an activity credit is applied.  When `false`, no beep is played but all other crediting behaviour is unchanged. |

### New NVS Namespace: `esport_act`

| NVS Key | Type | Description |
| ------- | ---- | ----------- |
| `ac_cnt` | uint8 | Number of activities in pool (0–30) |
| `ac_nxt` | uint32 | Next auto-increment activity ID (starts at 1; never reset to 0 after deletion) |
| `ac_N` (N=0..29) | blob (`act_mngr_entry_t`) | Activity entry at global pool slot N |
| `ua_N` (N=0..3) | blob (`act_mngr_user_assigns_t`) | User N's list of assigned activity IDs |
| `ud_N` (N=0..3) | blob (`act_mngr_user_daily_t`) | User N's daily done counts + current day |
| `ul_N_hd` (N=0..3) | uint8 | User N credit log write head (ring buffer) |
| `ul_N_cnt` (N=0..3) | uint8 | User N credit log entry count |
| `ul_N_K` (N=0..3, K=0..29) | blob (`act_credit_log_entry_t`) | User N, credit log entry K |

All keys fit within the NVS 15-character key limit.

### Data Model

```c
#define ACT_MNGR_MAX_ACTIVITIES       (30U)
#define ACT_MNGR_MAX_ASSIGNS_PER_USER (20U)
#define ACT_MNGR_MAX_CREDIT_LOG       (30U)
#define ACT_MNGR_NAME_MAX_LEN         (40U)
#define ACT_MNGR_NO_ID                (0U)    /* reserved; never a valid activity ID */
#define ACT_MNGR_SAVE_INTERVAL_S      (60U)   /* periodic NVS save interval */

typedef struct act_mngr_entry_tag {
    uint32_t id;                                  /* auto-generated, 1-based, never reused */
    char     name[ACT_MNGR_NAME_MAX_LEN + 1U];   /* null-terminated, max 40 chars          */
    uint32_t credit_s;                            /* 0 = dynamic (API-provided)             */
    uint32_t time_limit_s;                        /* max completion time (gamification ref) */
    uint8_t  daily_limit;                         /* max times per day; 1–255               */
} act_mngr_entry_t;

typedef struct act_mngr_user_assigns_tag {
    uint32_t act_ids[ACT_MNGR_MAX_ASSIGNS_PER_USER]; /* activity IDs; ACT_MNGR_NO_ID = empty */
    uint8_t  count;
} act_mngr_user_assigns_t;

typedef struct act_mngr_user_daily_tag {
    uint32_t date_ymd;                         /* YYYYMMDD; 0 = uninitialized               */
    uint8_t  done[ACT_MNGR_MAX_ACTIVITIES];    /* done count per activity SLOT (0-based)    */
} act_mngr_user_daily_t;

typedef struct act_credit_log_entry_tag {
    int64_t  timestamp_utc;    /* Unix timestamp when credited                              */
    uint32_t act_id;           /* activity ID                                               */
    uint32_t credits_s;        /* seconds awarded                                           */
    uint32_t completion_time_s;/* 0 if not provided; gamification analytics                 */
} act_credit_log_entry_t;
```

### Quick Reference

| Phase | Feature | Name                                       | Key output files                                                                      |
| ----- | ------- | ------------------------------------------ | ------------------------------------------------------------------------------------- |
| 7.1   | 7       | Spec Update — Activity Credits             | `docs/1-specification.md`                                                             |
| 7.2   | 7       | Activity Manager Core Module               | `activity_manager.c/h`, `event_ids.h`, `CMakeLists.txt`, `config_manager.c/h`        |
| 7.3   | 7       | HTTP Server: Activities Pages              | `http_server_activities.c/h`, `http_server.c`, `http_server_config.c`                |
| 7.4   | 7       | HTTP Server: Activity Credit API           | `http_server_api.c`                                                                   |
| 7.5   | 7       | Integration & Verification                 | `main.c`                                                                              |
| 7.6   | 7       | Documentation & README Update              | `docs/1-specification.md`, `docs/2-development_plan.md`, `README.md`                 |

---

### Phase 7.1 — Spec Update

#### Goal

Update `docs/1-specification.md` to document the Activity Manager module, the new NVS namespace, the two new web pages, and all new API endpoints. All later phases implement what this spec describes.

#### Inputs

- `docs/0-draft-input.md` §Improvements item 7
- `docs/1-specification.md` (current)

#### Tasks

1. **§1 Overview** — add a bullet: "A parent/admin can define a global pool of activities and assign them to registered devices. Crediting an activity adds pre-configured (or API-specified) internet time to the corresponding device counter. Both the management and award pages are protected by HTTP Basic Auth."

2. **§3 Configuration Parameters table** — add one row under `esport_cfg`:
   - `activity_credit_buzzer_en` — `bool` (stored as `uint8`), NVS key `"ac_bz_en"`, default `true`, description: "Enable/disable the `BUZZER_PATTERN_ACTIVITY_CREDIT` beep on activity credit.  When `false`, no beep plays."  Note in the table footer that activity pool data lives in the separate `esport_act` namespace (§8).

3. **§4 System Architecture** — add an `Activity Manager` block to the architecture diagram, connected to the `Device Registry` and `HTTP Server`.

4. **§4 Component/File Layout** — add `activity_manager.h` to `inc/` and `activity_manager.c`, `http_server_activities.c` to `src/`; add `http_server_activities.h` to `inc/`.

5. **New §5.X — Activity Manager Module** — insert after the Button Reset Module section:

   **File:** `activity_manager.c` / `activity_manager.h`

   **Responsibilities:**
   - Maintain a global pool of up to `ACT_MNGR_MAX_ACTIVITIES` (30) activity entries in NVS namespace `esport_act`.
   - Auto-generate monotonically increasing unique integer IDs (uint32_t, 1-based, stored in NVS key `ac_nxt`, never reset on deletion).
   - Manage user-activity assignments: up to `ACT_MNGR_MAX_ASSIGNS_PER_USER` (20) activity IDs per device slot. Assignments are stored per-user in NVS.
   - Track per-user daily done counts (indexed by activity slot, not ID) in NVS. Lazily resets to zero when the local calendar day (YYYYMMDD) changes. The date is checked and the reset applied on every call to `act_mngr_activity_credit()` and `act_mngr_user_daily_done_get()`.
   - Maintain a per-user credit log as a 30-entry NVS ring buffer (newest-first read order).
   - `act_mngr_activity_credit()` — the single write path: validates the request, applies the daily-reset check, verifies the daily limit is not exceeded, adds `credits_s` to the target device's counter via `device_reg_entry_counter_set()`, increments the daily done count, appends a credit log entry, and posts `ESPORT_EVENT_ACTIVITY_CREDITED`.
   - Expose a `act_mngr_daily_reset_check()` function (callable from the time-counter tick once per minute) that lazily resets all users' daily counts when the date has changed.

   **NVS Namespace:** `esport_act`

   *(include the key table from the "New NVS Namespace" section above)*

   **Public API:**

   ```c
   esp_err_t act_mngr_init(void);

   /* Activity pool CRUD */
   esp_err_t act_mngr_activity_add(const char *p_name, uint32_t credit_s,
                                   uint32_t time_limit_s, uint8_t daily_limit,
                                   uint32_t *p_id_out);
   esp_err_t act_mngr_activity_remove(uint32_t id);
   esp_err_t act_mngr_activity_update(uint32_t id, const char *p_name,
                                      uint32_t credit_s, uint32_t time_limit_s,
                                      uint8_t daily_limit);
   esp_err_t act_mngr_activity_get(uint32_t id, act_mngr_entry_t *p_out);
   esp_err_t act_mngr_activity_slot_get(uint8_t slot, act_mngr_entry_t *p_out);
   uint8_t   act_mngr_activity_count(void);

   /* User-activity assignment */
   esp_err_t act_mngr_user_assign(uint8_t dev_idx, uint32_t act_id);
   esp_err_t act_mngr_user_unassign(uint8_t dev_idx, uint32_t act_id);
   uint8_t   act_mngr_user_assign_count(uint8_t dev_idx);
   esp_err_t act_mngr_user_assigns_get(uint8_t dev_idx, act_mngr_user_assigns_t *p_out);
   bool      act_mngr_user_is_assigned(uint8_t dev_idx, uint32_t act_id);

   /* Daily done-count tracking */
   uint8_t   act_mngr_user_daily_done_get(uint8_t dev_idx, uint32_t act_id);
   bool      act_mngr_user_daily_limit_reached(uint8_t dev_idx, uint32_t act_id);
   void      act_mngr_daily_reset_check(void);  /* call periodically; resets on day change */

   /* Credit an activity — the single write path */
   esp_err_t act_mngr_activity_credit(uint8_t dev_idx, uint32_t act_id,
                                      uint32_t credits_s, uint32_t completion_time_s);

   /* Credit log (newest first) */
   uint8_t   act_mngr_credit_log_count(uint8_t dev_idx);
   uint8_t   act_mngr_credit_log_read(uint8_t dev_idx, act_credit_log_entry_t *p_out,
                                      uint8_t max_count);
   ```

   **Thread safety:** all in-RAM state (pool array, assignment arrays, daily counts, log indices) is protected by a single `portMUX_TYPE g_act_mux = portMUX_INITIALIZER_UNLOCKED` spinlock. NVS writes happen outside the spinlock. `device_reg_entry_counter_set()` handles its own locking.

6. **§5.10 Buzzer Module** — add `BUZZER_PATTERN_ACTIVITY_CREDIT = 5` to the pattern table: 2 ON, 1 OFF, 2 ON ("double ding", 250 ms total); triggered by `act_mngr_activity_credit()` when `config_mngr_activity_credit_buzzer_en_get()` returns `true`.

6a. **§5.1 NVS Configuration Manager** — add getter/setter entries for `activity_credit_buzzer_en` to the public API:
   ```c
   bool      config_mngr_activity_credit_buzzer_en_get(void);
   esp_err_t config_mngr_activity_credit_buzzer_en_set(bool b_enabled);
   ```

7. **§6 Web Interface** — add two new subsections:

   **§6.X Activities Award Page — `GET /activities`**

   Protected by HTTP Basic Auth (same credentials as `/config`).

   Serves an HTML page with:
   - A user combobox at the top (all registered devices from Device Registry).
   - On combobox change: a JavaScript `fetch('/api/activities?device_idx=N')` call populates:
     - An activities table showing each assigned activity with a "Credit h:mm:ss" button on the left.  The button text shows the activity's `credit_hms` value.  The button is greyed/disabled (`disabled` attribute) when `done_today >= daily_limit`.
     - A "Total credits" line showing the selected user's current counter (`counter_hms` from `/api/status`).
   - Clicking a Credit button calls `POST /api/activities/credit` (JSON, via `fetch`) with `device_idx`, `act_id`, `credits_s` (= activity's `credit_s`), `completion_time_s = 0`.  On success, the button's daily counter increments and the total-credits display updates.
   - Activities with `credit_s == 0` are **not shown** on this page (dynamic-credit activities are admin/API-only).
   - A credit log table at the bottom (fetched from `GET /api/activities/log?device_idx=N`) showing the last 30 credited activities: timestamp (local), activity name, credits awarded (h:mm:ss).
   - The page is self-contained (no external CSS/JS resources).

   **§6.X Activity Manager Page — `GET /activities/manage` and `POST /activities/manage`**

   Protected by HTTP Basic Auth.

   Serves an HTML form page with two sections:

   *Global Activity Pool section:*
   - A table listing all activities in the pool. Each row: ID (read-only), name (text input), credit (h:mm:ss input), time limit (h:mm:ss input), daily limit (number input), Delete button.
   - An "Add Activity" sub-form with fields: name, credit (h:mm:ss), time limit (h:mm:ss), daily limit.
   - On submit (`POST /activities/manage`): processes `action=add_activity`, `action=update_activity&id=N`, or `action=delete_activity&id=N`.

   *Activity Assignments section:*
   - For each registered device: a sub-table showing its assigned activities with an "Unassign" button per activity, and a dropdown/select to add a new assignment from the global pool.
   - On submit: processes `action=assign&dev_idx=N&act_id=M` or `action=unassign&dev_idx=N&act_id=M`.

   All actions redirect to `GET /activities/manage?saved=1` on success; return HTTP 400 on validation error.

8. **§6.3 JSON Status API (`GET /api/status`)** — add an optional note that the `"devices"` array may include a future `"activity_credits_today_s"` field (not implemented in phase 7, reserved for future use).

9. **§6.X JSON Activities API — `GET /api/activities`**

   Query parameter: `device_idx` (optional, 0–3). When present, filters to activities assigned to that user and includes per-user daily status.

   Returns:
   ```json
   {
     "activities": [
       {
         "id": 1,
         "slot": 0,
         "name": "Read for 30 min",
         "credit_s": 1800,
         "credit_hms": "0:30:00",
         "time_limit_s": 1800,
         "time_limit_hms": "0:30:00",
         "daily_limit": 1,
         "done_today": 0,
         "available": true
       }
     ]
   }
   ```
   `"available"`: `done_today < daily_limit`. Only activities where `credit_s > 0` appear when `device_idx` is specified. When `device_idx` is absent, returns all pool activities without `done_today` / `available` fields.

10. **§6.X Activity Credit API — `POST /api/activities/credit`**

    Protected by HTTP Basic Auth.

    **Request body** (`application/json`):
    ```json
    {
      "device_idx":        0,
      "act_id":            1,
      "credits_s":         1800,
      "completion_time_s": 0
    }
    ```
    - `device_idx`: 0–3 (must be a registered device).
    - `act_id`: must exist in the pool and be assigned to the device.
    - `credits_s`: must be > 0.  For activities with `credit_s > 0`, the caller should pass that value.  For dynamic activities (`credit_s == 0`), the caller supplies its own calculated value.
    - `completion_time_s`: optional analytics field (0 if unused).

    **Response (200 OK)**:
    ```json
    {
      "ok": true,
      "new_counter_s": 7200,
      "new_counter_hms": "2:00:00"
    }
    ```

    **Error responses**: HTTP 400 with `{"error": "<description>"}` for invalid input; HTTP 401 without credentials; HTTP 429 with `{"error": "daily limit reached"}` when daily limit is exhausted.

11. **§6.X Credit Log API — `GET /api/activities/log`**

    Query parameter: `device_idx` (required, 0–3).

    Returns (newest first):
    ```json
    {
      "log": [
        {
          "timestamp_utc":    1741905000,
          "timestamp_local":  "2026-03-14T08:30:00",
          "act_id":           1,
          "act_name":         "Read for 30 min",
          "credits_s":        1800,
          "credits_hms":      "0:30:00",
          "completion_time_s": 0
        }
      ]
    }
    ```

12. **§7.1 Boot Sequence** — add `act_mngr_init()` after `device_reg_init()` and before `wifi_mngr_init()`.

13. **§8 NVS Layout** — add namespace `esport_act` with the full key table from the "New NVS Namespace" section above.

14. **§9 Event Bus** — add `ESPORT_EVENT_ACTIVITY_CREDITED` with description: "Posted (no payload) when an activity credit is applied via `act_mngr_activity_credit()`. Consumers may use this to update live displays."

15. **Module Prefix Table** — add rows:
    - `activity_manager` | `act_mngr_` | `ACT_MNGR_`
    - `http_server_activities` | `http_srv_` | `HTTP_SRV_`

#### Acceptance Criteria

- [ ] §1 Overview mentions the activity credit feature.
- [ ] §4 architecture diagram includes the Activity Manager block.
- [ ] §4 file layout lists `activity_manager.h/c` and `http_server_activities.h/c`.
- [ ] New §5.X fully documents the Activity Manager module (data model, NVS, full API, daily-reset logic, thread safety).
- [ ] §5.10 Buzzer module documents `BUZZER_PATTERN_ACTIVITY_CREDIT = 5` and its guard on `config_mngr_activity_credit_buzzer_en_get()`.
- [ ] §5.1 lists `config_mngr_activity_credit_buzzer_en_get/set()` in the public API.
- [ ] §3 includes `activity_credit_buzzer_en` (`"ac_bz_en"`) with correct type, default, and description.
- [ ] New §6.X fully documents `GET /activities` with combobox, credit buttons, and credit log.
- [ ] New §6.X fully documents `GET /activities/manage` and `POST /activities/manage`.
- [ ] New §6.X fully documents `GET /api/activities` with full JSON schema.
- [ ] New §6.X fully documents `POST /api/activities/credit` request/response schema including `completion_time_s` and HTTP 429 for daily limit exhaustion.
- [ ] New §6.X fully documents `GET /api/activities/log` JSON schema.
- [ ] §7.1 Boot Sequence includes `act_mngr_init()`.
- [ ] §8 includes `esport_act` namespace with complete key table.
- [ ] §9 includes `ESPORT_EVENT_ACTIVITY_CREDITED`.
- [ ] Module Prefix Table includes `activity_manager` and `http_server_activities`.

---

### Phase 7.2 — Activity Manager Core Module

#### Goal

Create `activity_manager.c` / `activity_manager.h` — the central store for the global activity pool, per-user assignments, daily done-count tracking, credit log ring buffers, and the credit-apply function. Add `ESPORT_EVENT_ACTIVITY_CREDITED` to `event_ids.h`. Add `BUZZER_PATTERN_ACTIVITY_CREDIT` to the buzzer module.

#### Inputs

- `docs/1-specification.md` §5.X (Phase 7.1 output)
- `main/inc/event_ids.h` (existing)
- `main/inc/device_registry.h` (existing — `device_reg_entry_counter_get/set`)
- `main/inc/buzzer.h` (existing)
- `main/inc/time_manager.h` (existing — `time_mngr_utc_get`, `time_mngr_is_synced`)
- `main/CMakeLists.txt` (existing)

#### Tasks

1. **`main/inc/event_ids.h`** — add `ESPORT_EVENT_ACTIVITY_CREDITED` to `esport_event_id_t` with the next sequential explicit integer value.

2. **`main/inc/buzzer.h`** — add `BUZZER_PATTERN_ACTIVITY_CREDIT = 5` to the `buzzer_pattern_id_t` enum (after `BUZZER_PATTERN_PASSWORD_RESET = 4`).

3. **`main/src/buzzer.c`** — add the new pattern to the internal pattern table:
   - `s_pat_act_credit[3]`: `{ 2 ON, 1 OFF }`, `{ 2 ON, 0 OFF }` — sequence: 2 units HIGH, 1 unit LOW, 2 units HIGH (a "double ding", 250 ms total).
   - Add a case for `BUZZER_PATTERN_ACTIVITY_CREDIT` in the `switch` inside `buzzer_pattern_play()`.

4. **`main/inc/config_manager.h`** — declare (with full Doxygen, following existing style):
   ```c
   bool      config_mngr_activity_credit_buzzer_en_get(void);
   esp_err_t config_mngr_activity_credit_buzzer_en_set(bool b_enabled);
   ```

5. **`main/src/config_manager.c`** — implement:
   - Add `#define CONFIG_MNGR_KEY_ACT_CREDIT_BZ_EN  ("ac_bz_en")` and `#define CONFIG_MNGR_DEF_ACT_CREDIT_BZ_EN  ((uint8_t)1U)`.
   - In `config_mngr_init()`: read `"ac_bz_en"`; if `ESP_ERR_NVS_NOT_FOUND`, write the default `1`.
   - `config_mngr_activity_credit_buzzer_en_get()`: read NVS key `"ac_bz_en"` as `uint8_t`; return `(val != 0U)`; on any NVS error return `true` (fail-safe: beep on by default).
   - `config_mngr_activity_credit_buzzer_en_set(b_enabled)`: write `(uint8_t)(b_enabled ? 1U : 0U)` to NVS key `"ac_bz_en"`; commit; return result.

6. **Create `main/inc/activity_manager.h`**:
   - Define all `#define` constants (each replacement value in parentheses): `ACT_MNGR_MAX_ACTIVITIES`, `ACT_MNGR_MAX_ASSIGNS_PER_USER`, `ACT_MNGR_MAX_CREDIT_LOG`, `ACT_MNGR_NAME_MAX_LEN`, `ACT_MNGR_NO_ID`, `ACT_MNGR_SAVE_INTERVAL_S`.
   - Define the four typedefs with tag names: `act_mngr_entry_t`, `act_mngr_user_assigns_t`, `act_mngr_user_daily_t`, `act_credit_log_entry_t`.
   - Declare all public API functions with full Doxygen (`\\` tags, `\\param[in/out]`, blank line before `\\return`).
   - Use `hhtemplate` structure; guard with `ACTIVITY_MANAGER_H`.

5. **Create `main/src/activity_manager.c`**:

   a. **File-scope state** protected by `static portMUX_TYPE g_act_mux = portMUX_INITIALIZER_UNLOCKED`:
   - `static act_mngr_entry_t g_pool[ACT_MNGR_MAX_ACTIVITIES]` — activity pool (slot-indexed).
   - `static uint8_t g_pool_count = 0U` — active entries.
   - `static uint32_t g_next_id = 1U` — auto-increment counter.
   - `static act_mngr_user_assigns_t g_assigns[DEVICE_REG_MAX_ENTRIES]`.
   - `static act_mngr_user_daily_t g_daily[DEVICE_REG_MAX_ENTRIES]`.
   - Per-user credit log ring buffer indices (not under spinlock — accessed only from HTTP task, sequentially):
     - `static uint8_t g_log_head[DEVICE_REG_MAX_ENTRIES]` — write index.
     - `static uint8_t g_log_count[DEVICE_REG_MAX_ENTRIES]` — entry count.

   b. **Internal static helpers** (declare in Internal Function Prototypes section):
   - `act_mngr_pool_slot_find(uint32_t id)` → `int8_t`: finds slot for activity ID; returns -1 if not found. Called under spinlock.
   - `act_mngr_pool_save(uint8_t slot)`: saves `g_pool[slot]` to NVS key `"ac_N"` where N=slot.
   - `act_mngr_pool_meta_save(void)`: saves `ac_cnt` and `ac_nxt` to NVS.
   - `act_mngr_assigns_save(uint8_t dev_idx)`: saves `g_assigns[dev_idx]` to NVS key `"ua_N"`.
   - `act_mngr_daily_save(uint8_t dev_idx)`: saves `g_daily[dev_idx]` to NVS key `"ud_N"`.
   - `act_mngr_log_save(uint8_t dev_idx, uint8_t slot, const act_credit_log_entry_t *p_entry)`: saves a log entry blob to NVS key `"ul_N_K"`.
   - `act_mngr_date_ymd_get(void)` → `uint32_t`: returns current local date as YYYYMMDD using `localtime_r(time_mngr_utc_get())`.
   - `act_mngr_daily_reset_if_needed(uint8_t dev_idx)`: checks `g_daily[dev_idx].date_ymd` vs `act_mngr_date_ymd_get()`; if different, zeroes `done[]`, updates `date_ymd`, calls `act_mngr_daily_save(dev_idx)`.

   c. **`act_mngr_init()`**:
   - Open namespace `esport_act` NVS_READWRITE.
   - Read `ac_cnt` → `g_pool_count` (validate ≤ `ACT_MNGR_MAX_ACTIVITIES`; on invalid reset to 0).
   - Read `ac_nxt` → `g_next_id` (validate > 0; on 0 reset to 1).
   - For each slot `< g_pool_count`: read `ac_N` blob into `g_pool[N]`.
   - For each device index `0..DEVICE_REG_MAX_ENTRIES-1`: read `ua_N` blob into `g_assigns[N]` (on missing key, zero-init); read `ud_N` blob into `g_daily[N]` (on missing key, zero-init); read `ul_N_hd` → `g_log_head[N]`; read `ul_N_cnt` → `g_log_count[N]`.
   - Close handle.
   - Log pool count at `ESP_LOGI`.

   d. **`act_mngr_activity_add()`**:
   - Validate: `p_name` non-NULL, `strlen(p_name) >= 1 && <= ACT_MNGR_NAME_MAX_LEN`; `daily_limit >= 1`; `g_pool_count < ACT_MNGR_MAX_ACTIVITIES`.
   - Under spinlock: assign slot = `g_pool_count`; fill `g_pool[slot]` with `id = g_next_id++`, name, credit_s, time_limit_s, daily_limit; increment `g_pool_count`.
   - Outside spinlock: call `act_mngr_pool_save(slot)` and `act_mngr_pool_meta_save()`.
   - Write `*p_id_out = g_pool[slot].id`.
   - Return `ESP_ERR_INVALID_ARG` on validation failure; `ESP_ERR_NO_MEM` if pool full.

   e. **`act_mngr_activity_remove(id)`**:
   - Under spinlock: find slot via `act_mngr_pool_slot_find(id)`; return `ESP_ERR_NOT_FOUND` if missing.
   - Compact array: shift `g_pool[slot+1..count-1]` left by one. Decrement `g_pool_count`.
   - For each device: compact `g_assigns[N].act_ids[]` by removing any element equal to `id`; decrement `count`.
   - Outside spinlock: rewrite all affected pool blobs, delete stale last blob key (`"ac_N"` where N = old count-1) via `nvs_erase_key()`; rewrite all assignment blobs; save metadata.

   f. **`act_mngr_activity_credit(dev_idx, act_id, credits_s, completion_time_s)`**:
   - Validate: `dev_idx < DEVICE_REG_MAX_ENTRIES`; `credits_s > 0`; `act_id != ACT_MNGR_NO_ID`.
   - Outside spinlock: find slot; if -1 return `ESP_ERR_NOT_FOUND`.
   - Check `act_mngr_user_is_assigned(dev_idx, act_id)`; if false return `ESP_ERR_INVALID_STATE`.
   - Call `act_mngr_daily_reset_if_needed(dev_idx)`.
   - Under spinlock: read `g_daily[dev_idx].done[slot]`; compare with `g_pool[slot].daily_limit`; if already at limit, return `ESP_ERR_NOT_ALLOWED`.
   - Increment `g_daily[dev_idx].done[slot]`.
   - Outside spinlock: call `act_mngr_daily_save(dev_idx)`.
   - Get current counter: `old_ctr = device_reg_entry_counter_get(dev_idx)`.
   - Set new counter: `device_reg_entry_counter_set(dev_idx, old_ctr + credits_s)`.
   - Build `act_credit_log_entry_t` entry (timestamp, act_id, credits_s, completion_time_s).
   - Under spinlock: write to log ring buffer (`g_log_head`, `g_log_count` per user).
   - Outside spinlock: call `act_mngr_log_save(dev_idx, head_slot, &entry)`; update `ul_N_hd` and `ul_N_cnt` in NVS.
   - If `config_mngr_activity_credit_buzzer_en_get()` returns `true`: call `buzzer_pattern_play(BUZZER_PATTERN_ACTIVITY_CREDIT)`.
   - Post `ESPORT_EVENT_ACTIVITY_CREDITED` (no payload) via `esp_event_post`.
   - Return `ESP_OK`.

   g. **`act_mngr_credit_log_read(dev_idx, p_out, max_count)`**:
   - Reads up to `min(max_count, g_log_count[dev_idx])` entries from NVS in reverse-chronological order (newest first), starting from `(g_log_head[dev_idx] - 1 + MAX) % MAX` and stepping backwards.
   - Returns actual count written to `p_out`.

   h. Implement all remaining getters/setters/query functions following the patterns above.

   i. Follow `cctemplate` structure with `gp_tag = "act_mngr"`, all section separators, and `/*** end of file ***/` footer.

6. **`main/CMakeLists.txt`** — add `"src/activity_manager.c"` to the `SRCS` list.

7. **`main/src/time_counter.c`** — in `time_ctr_tick_cb()`, add a periodic daily-reset check: add a `static uint16_t g_act_reset_ticks = 0U` counter; increment it each tick; when `g_act_reset_ticks >= 60U`, call `act_mngr_daily_reset_check()` and reset to 0. Add `#include "activity_manager.h"`.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `act_mngr_activity_add()` with 30 different activities returns `ESP_OK` each time; 31st call returns `ESP_ERR_NO_MEM`.
- [ ] Adding an activity with empty name or `daily_limit = 0` returns `ESP_ERR_INVALID_ARG`.
- [ ] `act_mngr_activity_get(id, &e)` returns `ESP_OK` and correct fields for a known ID.
- [ ] `act_mngr_activity_get(0xDEAD, &e)` (unknown ID) returns `ESP_ERR_NOT_FOUND`.
- [ ] `act_mngr_activity_remove(id)` removes from pool; `act_mngr_activity_count()` decrements.
- [ ] Remove propagates: assigned slots holding that ID are cleaned from all users.
- [ ] After `act_mngr_init()` reinit (simulated reboot), all pool entries, assignments, log head/count, and `g_next_id` are restored from NVS.
- [ ] `act_mngr_user_assign(dev_idx, act_id)` returns `ESP_OK`; `act_mngr_user_is_assigned()` returns `true`.
- [ ] Assigning a 21st activity to one user returns `ESP_ERR_NO_MEM`.
- [ ] `act_mngr_activity_credit(dev_idx, act_id, credits_s, 0)`: device counter increments by `credits_s`.
- [ ] Calling credit on an activity not assigned to the user returns `ESP_ERR_INVALID_STATE`.
- [ ] Calling credit on a non-existent activity ID returns `ESP_ERR_NOT_FOUND`.
- [ ] Calling credit with `credits_s = 0` returns `ESP_ERR_INVALID_ARG`.
- [ ] `act_mngr_user_daily_limit_reached()` returns `false` initially; returns `true` after `daily_limit` credits.
- [ ] Credit beyond `daily_limit` on the same day returns `ESP_ERR_NOT_ALLOWED`.
- [ ] Daily count resets on simulated day change: set `g_daily[N].date_ymd` to yesterday's YYYYMMDD; next call to `act_mngr_activity_credit()` resets counts and succeeds.
- [ ] Credit log ring buffer: after 35 credits to the same user, `act_mngr_credit_log_count()` returns 30 (capped); newest entries are kept.
- [ ] Credit log order: `act_mngr_credit_log_read()` returns entries newest-first.
- [ ] Log survives reboot: after reinit, previously logged entries are readable.
- [ ] `ESPORT_EVENT_ACTIVITY_CREDITED` is posted exactly once per successful credit.
- [ ] `BUZZER_PATTERN_ACTIVITY_CREDIT` plays the correct 2-on 1-off 2-on sequence when `activity_credit_buzzer_en` is `true`.
- [ ] With `activity_credit_buzzer_en == false`: `act_mngr_activity_credit()` succeeds but no buzzer pattern plays.
- [ ] `config_mngr_activity_credit_buzzer_en_set(false)` returns `ESP_OK`; value survives reinit.
- [ ] Factory default `true` applied when NVS key `"ac_bz_en"` is absent.
- [ ] `act_mngr_daily_reset_check()` is called from `time_ctr_tick_cb()` approximately once per minute.
- [ ] No heap allocation, no NVS access, and no ESP event posting occur inside the spinlock.

---

### Phase 7.3 — HTTP Server: Activities Pages

#### Goal

Create `http_server_activities.c` / `http_server_activities.h` handling four routes: `GET /activities/manage`, `POST /activities/manage`, `GET /activities`, and the authentication guard common to all four. Register the routes in `http_server.c` and increase `max_uri_handlers`.

#### Inputs

- `docs/1-specification.md` §6.X (Phase 7.1 output)
- `main/inc/activity_manager.h` (Phase 7.2 output)
- `main/inc/device_registry.h` (existing)
- `main/src/http_server_config.c` — reference for `http_srv_cfg_auth_check()` (already declared in `http_server_config.h`)
- `main/inc/http_server_utils.h` (existing helpers)
- `main/src/http_server.c` (existing — URI registration)

#### Tasks

1. **Create `main/inc/http_server_activities.h`** (internal header):
   - Declare:
     ```c
     esp_err_t http_srv_activities_manage_get_handler(httpd_req_t *p_req);
     esp_err_t http_srv_activities_manage_post_handler(httpd_req_t *p_req);
     esp_err_t http_srv_activities_get_handler(httpd_req_t *p_req);
     ```
   - Guard with `HTTP_SERVER_ACTIVITIES_H`; use `hhtemplate` structure.

2. **Create `main/src/http_server_activities.c`**:

   a. **`http_srv_activities_manage_get_handler()`** (GET /activities/manage):
   - Call `http_srv_cfg_auth_check(p_req)`; return `ESP_OK` if auth fails (401 already sent).
   - Allocate HTML buffer from heap (`HTTP_SRV_HTML_BUF_LEN` bytes); return HTTP 500 on failure.
   - Build HTML page with two sections:

     *Global Activity Pool section:*
     - A table listing all `act_mngr_activity_count()` activities (`act_mngr_activity_slot_get()` for each slot).
     - Each row: hidden `<input name="act_id" value="N">`, name `<input type="text" name="act_name_N" maxlength="40">`, credit h:mm:ss `<input type="text" name="act_credit_N">` with auto-format `oninput` (same pattern as device counter field in `/config`), time limit h:mm:ss `<input type="text" name="act_limit_N">`, daily limit `<input type="number" name="act_daily_N" min="1" max="255">`, Update button (`name="action" value="update_N"`), Delete button (`name="action" value="delete_N"`).
     - Below table: "Add Activity" sub-form — name, credit h:mm:ss, time limit h:mm:ss, daily limit, Add button (`name="action" value="add_activity"`).

     *Activity Assignments section:*
     - For each registered device (`device_reg_entry_count()` entries): a sub-section showing the device nickname + a table of its assigned activities with Unassign buttons. Below the table: a `<select>` of all pool activities not yet assigned to this device, with an Assign button.
     - Unassign: `name="action" value="unassign_N_M"` where N=dev_idx, M=act_id.
     - Assign: `name="action" value="assign_N"` with a `<select name="new_act_N">` of available activity IDs.

   - Include a "Dashboard" link to `/`, an "Activities" link to `/activities`, and a "Config" link to `/config`.

2. **`main/src/http_server_config.c`** — add the activity credit buzzer enable field to `GET /config` and `POST /config` handlers:
   - **GET handler**: add one checkbox field to the config form HTML:
     - Label: "Activity credit beep"
     - `<input type="checkbox" name="activity_credit_buzzer_en" value="1"` with `checked` attribute if `config_mngr_activity_credit_buzzer_en_get()` returns `true`.
   - **POST handler**: parse `activity_credit_buzzer_en`:
     - Field present → `b_enabled = true`; field absent → `b_enabled = false` (standard checkbox behaviour).
     - Call `config_mngr_activity_credit_buzzer_en_set(b_enabled)`.
     - No range validation required.
   - Place the field in the config form directly below the existing "Buzzer feedback" (`buzzer_enabled`) checkbox, so both buzzer-related settings are grouped together.

   b. **`http_srv_activities_manage_post_handler()`** (POST /activities/manage):
   - Call `http_srv_cfg_auth_check(p_req)`.
   - Read body (URL-encoded, up to `HTTP_SRV_POST_BODY_MAX_LEN`).
   - Parse `action` field.
   - **`action == "add_activity"`**: parse name (validate ≤40 chars, non-empty), credit h:mm:ss (`hms_to_s()`), time limit h:mm:ss, daily limit (1–255). Call `act_mngr_activity_add()`. Handle `ESP_ERR_NO_MEM` ("Pool full — max 30 activities") and `ESP_ERR_INVALID_ARG` as HTTP 400.
   - **`action == "update_N"`** (N = activity ID): parse and validate same fields; call `act_mngr_activity_update(N, ...)`. Return HTTP 400 on `ESP_ERR_NOT_FOUND` or `ESP_ERR_INVALID_ARG`.
   - **`action == "delete_N"`**: call `act_mngr_activity_remove(N)`. Ignore `ESP_ERR_NOT_FOUND` (idempotent).
   - **`action == "assign_N"`** (N = dev_idx): parse `new_act_N` field as act_id. Call `act_mngr_user_assign(N, act_id)`. Handle `ESP_ERR_NO_MEM` ("Max 20 activities per user"), `ESP_ERR_NOT_FOUND`, `ESP_ERR_INVALID_STATE` ("Already assigned").
   - **`action == "unassign_N_M"`** (N = dev_idx, M = act_id): call `act_mngr_user_unassign(N, M)`.
   - On success: redirect HTTP 303 to `/activities/manage?saved=1`.
   - On error: respond HTTP 400 with error description.

   c. **`http_srv_activities_get_handler()`** (GET /activities):
   - Call `http_srv_cfg_auth_check(p_req)`.
   - Allocate HTML buffer; return HTTP 500 on failure.
   - Build HTML page:
     - `<h2>Activities</h2>`
     - A `<select id="user-select">` combobox populated with all registered devices (from `device_reg_entry_get()`). Default to device 0 if any are registered.
     - A `<div id="total-credits">` placeholder updated by JS.
     - A `<div id="activity-list">` placeholder populated by JS on combobox change.
     - A `<div id="credit-log">` placeholder populated by JS on combobox change.
     - Inline JavaScript:
       - `loadUser(dev_idx)`: calls `fetch('/api/activities?device_idx=' + dev_idx)` and `fetch('/api/activities/log?device_idx=' + dev_idx)`.
       - On activities response: builds the activity table with "Credit h:mm:ss" buttons. Buttons call `creditActivity(dev_idx, act_id, credits_s, btn)`. Disabled if `!data.available`.
       - `creditActivity(dev_idx, act_id, credits_s, btn)`: shows a `confirm()` dialog with the credit amount before posting. If confirmed, sends `POST /api/activities/credit` with JSON body; on success updates the total-credits display. If user cancels, does nothing.
       - `fetch('/api/status')` to update total credits display (using `devices[dev_idx].counter_hms`).
       - On log response: builds the credit log table. A shaded "Earlier" divider row is inserted between today's entries and older ones (comparison uses `timestamp_local.substring(0,10)` vs current date).
       - Calls `loadUser(0)` on page load; calls `loadUser(dev_idx)` on combobox `change` event.
     - Navigation to `/activities/manage` and `/config` is provided as blue `a.btn`-styled buttons (consistent with dashboard Export buttons).
     - The page is self-contained; no external CSS/JS.

   d. Define any needed local buffer-size constants at the top of the file (e.g. `ACT_HTML_BUF_LEN`). All buffers > 512 bytes are heap-allocated. Stack variables ≤ 512 bytes total in any call chain.

   e. Define a static helper `hms_str_to_s(const char *p_str, uint32_t *p_out)` that splits on `:`, expects exactly two separators, converts tokens with `strtoul`, and validates minutes and seconds 0–59. Returns `ESP_ERR_INVALID_ARG` on bad format.

   f. Follow `cctemplate` structure with `gp_tag = "http_srv_act"`.

3. **`main/src/http_server.c`** — register four new URI handlers:
   ```c
   { .uri = "/activities/manage", .method = HTTP_GET,  .handler = http_srv_activities_manage_get_handler  }
   { .uri = "/activities/manage", .method = HTTP_POST, .handler = http_srv_activities_manage_post_handler }
   { .uri = "/activities",        .method = HTTP_GET,  .handler = http_srv_activities_get_handler         }
   ```
   Increase `cfg.max_uri_handlers` from `15U` to `18U` (adding 3 new routes; one more is added in Phase 7.4).

4. **`main/CMakeLists.txt`** — add `"src/http_server_activities.c"` to `SRCS`.

#### Notes

> Stack budget: no function in `http_server_activities.c` may allocate more than 512 bytes of local variables on the stack at any point. All HTML and POST body buffers are heap-allocated.

> `hms_str_to_s()` is local to this translation unit (static). Do not move it to `http_server_utils.c` unless another file needs it — keep it `static`.

> The `GET /activities` page uses JavaScript to dynamically load activity lists and the credit log. The page itself is very thin HTML; all dynamic content is populated by `fetch()` calls after load.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /activities/manage` without credentials returns HTTP 401.
- [ ] `GET /activities/manage` with valid credentials returns HTTP 200 with both form sections.
- [ ] `POST /activities/manage` with `action=add_activity` and valid fields adds an activity; subsequent `GET` shows it.
- [ ] `POST /activities/manage` with an activity name longer than 40 chars returns HTTP 400.
- [ ] `POST /activities/manage` with `daily_limit=0` returns HTTP 400.
- [ ] `POST /activities/manage` with `action=add_activity` when pool is full returns HTTP 400 "Pool full".
- [ ] `POST /activities/manage` with `action=update_N` updates name/credit/limit; GET shows updated values.
- [ ] `POST /activities/manage` with `action=delete_N` removes activity; GET no longer shows it.
- [ ] `POST /activities/manage` with `action=assign_N` assigns activity to user; GET assignment section shows it.
- [ ] `POST /activities/manage` with `action=unassign_N_M` removes assignment.
- [ ] Assigning a 21st activity to one user returns HTTP 400 "Max 20 activities per user".
- [ ] `GET /activities` without credentials returns HTTP 401.
- [ ] `GET /activities` with valid credentials returns HTTP 200 with HTML containing user combobox and JS.
- [ ] `GET /activities` page JavaScript (by inspection): `loadUser()`, `creditActivity()`, combobox `change` handler are present.
- [ ] Clicking a **Credit** button shows a `confirm()` dialog before posting; cancelling aborts the request.
- [ ] The credit log table shows a shaded "Earlier" divider row separating today's entries from older ones.
- [ ] Navigation buttons on both pages use `a.btn` (blue, same as dashboard Export buttons).
- [ ] `GET /activities` page includes "Dashboard", "Manage Activities", and "Config" nav buttons.
- [ ] `GET /activities/manage` page includes "Dashboard", "Activities", and "Config" nav buttons.
- [ ] Credit h:mm:ss input field in manage page includes `oninput` auto-format handler (`hmsInput`).
- [ ] "Add Activity" sub-form row is rendered in the same table as the pool rows, with an empty first cell aligning columns identically.
- [ ] `hms_str_to_s()` correctly parses `"1:30:00"` to 5400, `"0:00:00"` to 0, and rejects `"abc"` and `"1:2"`.
- [ ] No local variable block exceeds 512 bytes on the stack in any handler.
- [ ] All three new routes are registered in `http_server.c`; `max_uri_handlers` is `21U`.

---

### Phase 7.4 — HTTP Server: Activity Credit API

#### Goal

Add three new JSON API endpoints to `http_server_api.c`: `GET /api/activities`, `POST /api/activities/credit`, and `GET /api/activities/log`. Register the new route in `http_server.c`.

#### Inputs

- `docs/1-specification.md` §6.X (Phase 7.1 output)
- `main/src/http_server_api.c` (existing)
- `main/inc/activity_manager.h` (Phase 7.2 output)
- `main/inc/device_registry.h`, `main/inc/time_manager.h` (existing)
- `main/src/http_server.c` (Phase 7.3 output — `max_uri_handlers` already at 18U)
- `main/inc/http_server_config.h` — for `http_srv_cfg_auth_check()` (credit endpoint requires auth)

#### Tasks

1. **`main/src/http_server_api.c`** — add `#include "activity_manager.h"` and `#include "http_server_config.h"`.

2. **`GET /api/activities` handler** (`http_srv_api_activities_get_handler`):
   - Parse optional query parameter `device_idx` (0–3) from the URI using `httpd_req_get_url_query_str()` + `httpd_query_key_value()`. If absent, set `dev_idx = 0xFF` (no user filter).
   - Build JSON response:
     ```
     { "activities": [ ... ] }
     ```
   - Iterate `act_mngr_activity_count()` slots; for each slot call `act_mngr_activity_slot_get()`.
   - When `dev_idx != 0xFF`: only include activities that `act_mngr_user_is_assigned(dev_idx, entry.id)` returns `true` for AND have `entry.credit_s > 0`. Include `"done_today"` and `"available"` fields for each.
   - When `dev_idx == 0xFF`: include all activities; omit `"done_today"` and `"available"`.
   - Format `credit_s` and `time_limit_s` as h:mm:ss strings (`h:mm:ss` format: `"%u:%02u:%02u"`).
   - Heap-allocate the JSON buffer (4 096 bytes is sufficient for 30 activities).
   - Return `Content-Type: application/json`.

3. **`POST /api/activities/credit` handler** (`http_srv_api_activities_credit_handler`):
   - Call `http_srv_cfg_auth_check(p_req)`; if auth fails, return `ESP_OK` (401 already sent).
   - Read request body (up to 256 bytes); content-type should be `application/json`.
   - Parse JSON manually using `strstr` / `sscanf` for the four fields (`device_idx`, `act_id`, `credits_s`, `completion_time_s`). Do not use a third-party JSON parser. The body format is fixed and simple:
     - Extract `"device_idx"`: parse integer using `strstr("\"device_idx\"")` + `strtoul`.
     - Similarly for `act_id`, `credits_s`, `completion_time_s` (optional; default 0).
   - Validate: `device_idx` 0–3; `act_id` != 0; `credits_s` > 0.
   - Call `act_mngr_activity_credit(device_idx, act_id, credits_s, completion_time_s)`.
   - Map return codes to HTTP responses:
     - `ESP_OK` → HTTP 200 `{"ok":true,"new_counter_s":<N>,"new_counter_hms":"<H:MM:SS>"}`.
     - `ESP_ERR_NOT_FOUND` → HTTP 400 `{"error":"activity not found"}`.
     - `ESP_ERR_INVALID_STATE` → HTTP 400 `{"error":"activity not assigned to user"}`.
     - `ESP_ERR_NOT_ALLOWED` → HTTP 429 `{"error":"daily limit reached"}`.
     - `ESP_ERR_INVALID_ARG` → HTTP 400 `{"error":"invalid arguments"}`.
   - All response JSON is written into a small stack buffer (`char resp[128]` is sufficient).

4. **`GET /api/activities/log` handler** (`http_srv_api_activities_log_get_handler`):
   - Parse required query parameter `device_idx` (0–3); return HTTP 400 `{"error":"device_idx required"}` if absent or invalid.
   - Call `act_mngr_credit_log_count(dev_idx)` then `act_mngr_credit_log_read(dev_idx, p_log, count)`.
   - For each entry: look up activity name via `act_mngr_activity_get(entry.act_id, &act)` (name may be missing if activity was deleted — use `"(deleted)"` as fallback).
   - Format `timestamp_utc` to local time string using `localtime_r` + `strftime`.
   - Format `credits_s` as h:mm:ss.
   - Build JSON `{"log":[...]}` array. Heap-allocate 4 096 bytes for the buffer.

5. **`main/src/http_server.c`** — register one new URI handler:
   ```c
   { .uri = "/api/activities/credit", .method = HTTP_POST, .handler = http_srv_api_activities_credit_handler }
   ```
   Increase `cfg.max_uri_handlers` from `18U` to `19U`.
   Also register:
   ```c
   { .uri = "/api/activities",      .method = HTTP_GET, .handler = http_srv_api_activities_get_handler      }
   { .uri = "/api/activities/log",  .method = HTTP_GET, .handler = http_srv_api_activities_log_get_handler  }
   ```
   Increase `cfg.max_uri_handlers` from `19U` to `21U`.

   > **Note:** `GET /api/activities` and `GET /api/activities/log` are unauthenticated (read-only), consistent with the existing `/api/*` convention. Only `POST /api/activities/credit` requires auth.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /api/activities` (no `device_idx`) returns HTTP 200 JSON with all pool activities; no `done_today` or `available` fields.
- [ ] `GET /api/activities?device_idx=0` returns only activities assigned to device 0 with `credit_s > 0`; includes `"done_today"` and `"available"` for each.
- [ ] Dynamic-credit activities (`credit_s == 0`) are excluded from the response when `device_idx` is specified.
- [ ] `GET /api/activities?device_idx=0`: `"available"` is `false` when `done_today >= daily_limit`.
- [ ] `POST /api/activities/credit` without credentials returns HTTP 401.
- [ ] `POST /api/activities/credit` with valid JSON body returns HTTP 200 with `new_counter_s` and `new_counter_hms`.
- [ ] `POST /api/activities/credit` when daily limit reached returns HTTP 429 `{"error":"daily limit reached"}`.
- [ ] `POST /api/activities/credit` for unknown activity ID returns HTTP 400.
- [ ] `POST /api/activities/credit` for an activity not assigned to the user returns HTTP 400.
- [ ] `POST /api/activities/credit` with `credits_s = 0` returns HTTP 400.
- [ ] `POST /api/activities/credit` with missing `completion_time_s` defaults to 0 (no error).
- [ ] `GET /api/activities/log?device_idx=0` returns HTTP 200 with `{"log":[...]}` (empty array when no credits yet).
- [ ] `GET /api/activities/log` (no `device_idx`) returns HTTP 400.
- [ ] Credit log entries appear newest-first; `act_name` is `"(deleted)"` for activities removed after crediting.
- [ ] All three new routes are registered in `http_server.c`; `max_uri_handlers` is `21U`.
- [ ] Stack usage in all three handlers is ≤ 512 bytes of local variables.

---

### Phase 7.5 — Integration & Verification

#### Goal

Wire the `activity_manager` module into `main.c`, confirm the build, and verify end-to-end behaviour.

#### Inputs

- All Phase 7.1–7.4 outputs.
- `main/src/main.c` (existing)

#### Tasks

1. **`main/src/main.c`** — add `#include "activity_manager.h"` and call `act_mngr_init()` after `device_reg_init()` and before `wifi_mngr_init()`, guarded by `ESP_ERROR_CHECK`.

2. Run `idf.py build` and fix any remaining compilation or linker errors.

3. **End-to-end checklist:**

   | #  | Test                                                                                                               | Pass/Fail |
   | -- | ------------------------------------------------------------------------------------------------------------------ | --------- |
   | 1  | `GET /activities/manage` without auth → HTTP 401                                                                   |           |
   | 2  | `GET /activities/manage` with auth → HTTP 200 with both form sections                                             |           |
   | 3  | Add activity "Read 30 min" (credit 0:30:00, limit 0:30:00, daily 1) → appears in GET                              |           |
   | 4  | Assign activity to device 0 via manage page → device 0 shows it on `/activities`                                  |           |
   | 5  | `GET /activities` page: combobox shows registered devices; JS `loadUser()` present                                |           |
   | 6  | `POST /api/activities/credit` with valid body → device counter increments; HTTP 200 returned                      |           |
   | 7  | Credit button on `/activities` page → calls API, counter updates, button disabled after daily limit               |           |
   | 8  | `POST /api/activities/credit` a second time on same day (daily_limit=1) → HTTP 429                                |           |
   | 9  | Buzzer plays "double ding" on successful credit                                                                    |           |
   | 10 | `GET /api/activities/log?device_idx=0` → credit log shows entry with correct timestamp and credits_hms            |           |
   | 11 | Power cycle (reboot): activity pool, assignments, credit log, and daily done counts restored from NVS             |           |
   | 12 | Simulated day change (set `date_ymd` to yesterday in NVS, reinit): daily done count resets; button re-enabled     |           |
   | 13 | Delete activity from manage page → removed from pool and from all user assignment lists                           |           |
   | 14 | `GET /api/activities?device_idx=0`: dynamic-credit activity (`credit_s=0`) is excluded from response             |           |
   | 15 | `POST /api/activities/credit` for unassigned activity → HTTP 400 "activity not assigned to user"                  |           |
   | 16 | `GET /activities/manage` shows 30 activities in pool; add button returns HTTP 400 "Pool full" on 31st add attempt |           |
   | 17 | Assigning 21st activity to a user returns HTTP 400 "Max 20 activities per user"                                   |           |
   | 18 | `ESPORT_EVENT_ACTIVITY_CREDITED` fires exactly once per successful API credit call                                 |           |
   | 19 | All existing endpoints (`/`, `/config`, `/api/status`, `/ota`, etc.) respond correctly — no regression            |           |
   | 20 | Set `activity_credit_buzzer_en = false` on `/config` page → buzzer silent on next credit; counter still increments |           |
   | 21 | Re-enable `activity_credit_buzzer_en = true` → buzzer resumes on next credit                                       |           |

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings (`-Werror` enforced).
- [ ] `act_mngr_init()` is called in `app_main()` after `device_reg_init()`.
- [ ] All 21 end-to-end tests pass.
- [ ] No assertion failures or watchdog triggers during 10-minute continuous operation.
- [ ] `max_uri_handlers` in `http_server.c` is `21U`.

---

### Phase 7.6 — Documentation & README Update

#### Goal

Update all project documentation to reflect the complete Feature 7 implementation. Bring `docs/1-specification.md` up to date with all new modules, APIs, and configuration parameters. Update `README.md` to describe the Activity Credits feature for end users and developers.

#### Inputs

- All Phase 7.1–7.5 outputs.
- `docs/1-specification.md` (current — Phase 7.1 output)
- `README.md` (existing)
- `docs/2-development_plan.md` (current)

#### Tasks

1. **`docs/1-specification.md`** — verify all sections updated by Phase 7.1 are consistent with the final implementation.  Correct any discrepancies between the spec and the implemented code:
   - §3: Confirm `activity_credit_buzzer_en` row is present with NVS key `"ac_bz_en"`, type uint8, default `true`.
   - §4 Architecture diagram: confirm Activity Manager block is present.
   - §4 File Layout: confirm `activity_manager.h/c` and `http_server_activities.h/c` are listed.
   - §5.X Activity Manager: confirm full module spec (data model, NVS, API, daily-reset, thread safety) is accurate.
   - §5.1 Config Manager: confirm `config_mngr_activity_credit_buzzer_en_get/set()` are listed.
   - §5.10 Buzzer: confirm `BUZZER_PATTERN_ACTIVITY_CREDIT = 5` and the `activity_credit_buzzer_en` guard are documented.
   - §6 Web Interface: confirm `GET /activities`, `GET /activities/manage`, `GET /api/activities`, `POST /api/activities/credit`, `GET /api/activities/log` are all documented.
   - §6.2 Config Page: confirm "Activity credit beep" checkbox field is listed in the fields table.
   - §7.1 Boot Sequence: confirm `act_mngr_init()` step is present.
   - §8 NVS Layout: confirm `esport_act` namespace with full key table is present; confirm `"ac_bz_en"` is in `esport_cfg` table.
   - §9 Event Bus: confirm `ESPORT_EVENT_ACTIVITY_CREDITED` is listed.
   - Module Prefix Table (in `docs/2-development_plan.md`): confirm `activity_manager` and `http_server_activities` rows are present.

2. **`README.md`** — add or update a section describing Feature #7 (Activity Credits):
   - Section title: "Activity Credits"
   - Describe the concept: admin-defined activity pool, user assignments, daily limits, credit log.
   - Describe the two new web pages: `/activities` (award page) and `/activities/manage` (admin manager page), both requiring the config password.
   - Describe the JSON API: `GET /api/activities`, `POST /api/activities/credit`, `GET /api/activities/log`.
   - Describe gamification: the `time_limit_s` field and `completion_time_s` in the credit API enable client-side games to compute dynamic credit amounts.
   - Mention the buzzer confirmation beep and the "Activity credit beep" config toggle on the `/config` page.
   - Keep the section concise and user-facing; refer readers to `docs/1-specification.md` for full technical detail.

3. **`docs/2-development_plan.md`** — make any final corrections:
   - Verify the Module Prefix Table includes `activity_manager | act_mngr_ | ACT_MNGR_` and `http_server_activities | http_srv_ | HTTP_SRV_`.
   - Verify the global and local Quick Reference tables are consistent and include Phase 7.6.

#### Acceptance Criteria

- [ ] `docs/1-specification.md` §3 includes `activity_credit_buzzer_en` (`"ac_bz_en"`) under `esport_cfg`.
- [ ] `docs/1-specification.md` §5.1 lists `config_mngr_activity_credit_buzzer_en_get/set()`.
- [ ] `docs/1-specification.md` §5.X Activity Manager fully documents the module.
- [ ] `docs/1-specification.md` §6.2 Config Page field table includes "Activity credit beep" checkbox.
- [ ] `docs/1-specification.md` §8 includes both the `esport_act` namespace and `"ac_bz_en"` in `esport_cfg`.
- [ ] `README.md` contains a clear "Activity Credits" section describing the feature for new users.
- [ ] `README.md` mentions the `/activities` and `/activities/manage` pages and auth requirement.
- [ ] `README.md` mentions the gamification API (`POST /api/activities/credit` with `completion_time_s`).
- [ ] `README.md` mentions the "Activity credit beep" config toggle.
- [ ] `docs/2-development_plan.md` Module Prefix Table includes both new module rows.
- [ ] All three documents are internally consistent with each other.

---

## Feature 8 — Dynamic Activities (Mini-Games)

### Overview

Feature 8 introduces the infrastructure for **dynamic activities** — mini-games implemented as self-contained HTML/JS pages embedded directly in the firmware binary and served by the existing HTTP server.  Kids can play a mini-game on any browser on the local network and self-credit internet time when they finish, without needing the admin password.

Key changes:

- **`b_is_dynamic` flag** — a new `uint8_t b_is_dynamic` field on `act_mngr_entry_t` marks an activity as a dynamic activity (mini-game).  On the admin award page (`/activities`) and the user-scoped read API, dynamic activities are hidden.  On the admin manage page they are visible with a combobox that links the activity to an embedded HTML page.
- **Credit validation** — `credit_s == 0` is no longer valid for any activity.  For dynamic activities, `credit_s` is the reference/maximum credit the game may award; the game computes the actual credit (capped at `credit_s` for PIN-authenticated API calls).
- **MAC-based PIN** — `device_reg_pin_compute()` computes a deterministic 8-hex-char PIN from the CRC32 of a registered device's 6-byte MAC address.  Kids use this PIN to self-credit without knowing the admin password.
- **Dual auth on the credit API** — `POST /api/activities/credit` accepts either admin Basic Auth (existing) or a valid `pin` field (new).  Admin calls are uncapped; PIN calls are capped at `activity.credit_s`.
- **Embedded HTML files** — dynamic activity pages live in `main/dyn_activities/` as plain HTML/JS/CSS files.  CMake embeds them at build time via `EMBED_FILES`.  A `dyn_act_registry` C module (auto-generated by CMake at configure time) provides a compile-time table of all embedded files.
- **User-facing `/dyn` page** — no admin auth required; automatically identifies the requesting device by resolving the connection's source IP against the lwIP ARP table, matching the resulting MAC address against the device registry, and displaying only that device's assigned dynamic activities as launch buttons.  If the device is not registered, an error message is shown.  No device selection dropdown is exposed — a kid cannot access another user's activities.
- **Two demo pages** — `dyn_activity1.html` and `dyn_activity2.html` as a proof of concept.

### Design Decisions

| Decision | Choice | Rationale |
| -------- | ------ | --------- |
| PIN algorithm | CRC32 of 6-byte MAC → 8 uppercase hex chars | No external crypto dependency; deterministic; sufficient for a home network |
| Activity name → file linkage | Activity `name` in `act_mngr_entry_t` must match a `p_name` in `g_dyn_act_registry[]`; enforced at HTTP layer, not in the core module | Keeps `activity_manager.c` independent of build infrastructure |
| Credit cap for PIN calls | `credits_s` silently capped to `act_entry.credit_s` server-side | Prevents a modified game from awarding excessive credits without breaking the user flow |
| Admin calls | Not capped | Admin controls the credit value directly |
| Dual auth on same endpoint | Same `POST /api/activities/credit`, two auth paths | Avoids duplicating API endpoints; admin flow is unchanged |
| User identification on `/dyn` | Source IP → lwIP ARP lookup → MAC → device registry match; no user-selectable dropdown | The requesting device is always in the ARP table (it just sent the HTTP request); auto-detection prevents a child from selecting and viewing another user's activities |
| File serving | Single wildcard handler `GET /dyn_activities/*` | One handler covers all embedded files regardless of count |
| Registry generation | CMake `file(WRITE ...)` auto-generates `dyn_act_registry.c` at configure time into `${CMAKE_CURRENT_BINARY_DIR}/generated/`; `CONFIGURE_DEPENDS` on `file(GLOB ...)` triggers re-config when files are added/removed | No manual maintenance of two files; generated file stays out of the source tree |
| `dyn_act_registry.h` | Static header in `main/inc/`; declares extern symbols only | No generation needed; stable API |

### New Files

| File | Description |
| ---- | ----------- |
| `main/dyn_activities/dyn_activity1.html` | Demo dynamic activity 1 — proof-of-concept credit claim |
| `main/dyn_activities/dyn_activity2.html` | Demo dynamic activity 2 — proof-of-concept credit claim |
| `main/inc/dyn_act_registry.h` | Registry table declaration (`g_dyn_act_registry`, `g_dyn_act_count`) |
| `main/inc/http_server_dyn.h` | Handler declarations for `/dyn` page and `/dyn_activities/*` file server |
| `main/src/http_server_dyn.c` | `/dyn` page and `/dyn_activities/*` file server |

### Modified Files

| File | Change |
| ---- | ------ |
| `main/inc/activity_manager.h` | Add `b_is_dynamic` to `act_mngr_entry_t`; update `add`/`update` signatures |
| `main/src/activity_manager.c` | Implement `b_is_dynamic`; NVS backward compat; reject `credit_s == 0` |
| `main/inc/device_registry.h` | Add `DEVICE_REG_PIN_LEN`, `device_reg_pin_compute()` |
| `main/src/device_registry.c` | Implement `device_reg_pin_compute()` |
| `main/inc/http_server_config.h` | Add `http_srv_cfg_auth_check_silent()` |
| `main/src/http_server_config.c` | Refactor shared auth logic; implement `http_srv_cfg_auth_check_silent()` |
| `main/src/http_server_activities.c` | `is_dynamic` checkbox + dynamic name combobox; POST parsing; filter change |
| `main/src/http_server_api.c` | Update `GET /api/activities` filter; add `GET /api/dyn`; update `POST /api/activities/credit` for dual auth + cap |
| `main/src/http_server.c` | Register new routes; increase `max_uri_handlers` to `24U` |
| `main/CMakeLists.txt` | Add `EMBED_FILES` glob; CMake-generate `dyn_act_registry.c`; add `http_server_dyn.c` to `SRCS` |

### New NVS Changes

`act_mngr_entry_t` grows by 1 byte (`uint8_t b_is_dynamic`).  Existing `ac_N` blobs in NVS are smaller than the new `sizeof(act_mngr_entry_t)`.  `act_mngr_init()` handles this transparently by zero-initialising the struct before reading the blob, so all pre-existing activities get `b_is_dynamic = 0` (static).

### New Endpoints

| Method | URI | Auth | Description |
| ------ | --- | ---- | ----------- |
| GET | `/dyn` | None | User-facing dynamic activities launch page |
| GET | `/dyn_activities/*` | None | Serve an embedded dynamic activity HTML file |
| GET | `/api/dyn` | None (read-only) | Returns PIN + assigned dynamic activities for a device |

### Modified Endpoints

| Method | URI | Change |
| ------ | --- | ------ |
| POST | `/api/activities/credit` | Accepts optional `pin` field; dual auth (admin Basic Auth OR PIN); credit cap for PIN path |

### Quick Reference

| Phase | Feature | Name | Key output files |
| ----- | ------- | ---- | ---------------- |
| 8.1 | 8 | Data Model: `b_is_dynamic` & Credit Validation | `activity_manager.h`, `activity_manager.c`, `http_server_activities.c`, `http_server_api.c` |
| 8.2 | 8 | MAC-Based PIN & Dual Auth Credit API | `device_registry.h`, `device_registry.c`, `http_server_config.h`, `http_server_config.c`, `http_server_api.c` |
| 8.3 | 8 | Build Infrastructure & Demo Pages | `main/dyn_activities/*.html`, `CMakeLists.txt`, `dyn_act_registry.h`, generated `dyn_act_registry.c` |
| 8.4 | 8 | HTTP Server: `/dyn` Page, File Server & Manage UI | `http_server_dyn.h`, `http_server_dyn.c`, `http_server_activities.c`, `http_server_api.c`, `http_server.c` |
| 8.5 | 8 | Integration & Verification | all prior outputs, `http_server.c` |
| 8.6 | 8 | Documentation & README Update | `docs/1-specification.md`, `docs/2-development_plan.md`, `README.md` |
| 8.7 | 8 | Commit Message | — |

---

### Phase 8.1 — Data Model: `b_is_dynamic` Field and Credit Validation

#### Goal

Add `uint8_t b_is_dynamic` to `act_mngr_entry_t`, update all activity manager API signatures to carry this field, add NVS backward-compatibility handling for old blobs, enforce `credit_s > 0` for all activities, and update the activity listing filters in the HTTP handlers and JSON API.

#### Inputs

- `main/inc/activity_manager.h` (Feature 7 output)
- `main/src/activity_manager.c` (Feature 7 output)
- `main/src/http_server_activities.c` (Feature 7 output)
- `main/src/http_server_api.c` (Feature 7 output)

#### Tasks

1. **`main/inc/activity_manager.h`** — update `act_mngr_entry_t`:

   ```c
   typedef struct act_mngr_entry_tag {
       uint32_t id;
       char     name[ACT_MNGR_NAME_MAX_LEN + 1U];
       uint32_t credit_s;
       uint32_t time_limit_s;
       uint8_t  daily_limit;
       uint8_t  b_is_dynamic;  /* non-zero = dynamic activity (mini-game); 0 = static */
   } act_mngr_entry_t;
   ```

   - Update `act_mngr_activity_add()` signature: add `uint8_t b_is_dynamic` as the fifth parameter (before `*p_id_out`):
     ```c
     esp_err_t act_mngr_activity_add(const char *p_name, uint32_t credit_s,
                                     uint32_t time_limit_s, uint8_t daily_limit,
                                     uint8_t b_is_dynamic, uint32_t *p_id_out);
     ```
   - Update `act_mngr_activity_update()` signature: add `uint8_t b_is_dynamic` as the last parameter (after `daily_limit`):
     ```c
     esp_err_t act_mngr_activity_update(uint32_t id, const char *p_name,
                                        uint32_t credit_s, uint32_t time_limit_s,
                                        uint8_t daily_limit, uint8_t b_is_dynamic);
     ```
   - Update Doxygen for both functions to document `b_is_dynamic` with `\\param[in]`.

2. **`main/src/activity_manager.c`**:

   a. **`act_mngr_activity_add()` validation**: add `if (credit_s == 0U) { return ESP_ERR_INVALID_ARG; }` immediately after the existing name-length and `daily_limit` checks.  `credit_s == 0` is now always invalid.

   b. **`act_mngr_activity_add()` implementation**: store `b_is_dynamic` in `g_pool[slot].b_is_dynamic` when filling the new slot.

   c. **`act_mngr_activity_update()` implementation**: update `g_pool[slot].b_is_dynamic` to the provided value.

   d. **`act_mngr_init()` — NVS backward compatibility**: when reading an `ac_N` blob:
      1. Query only the size first: `size_t loaded_size = 0U; nvs_get_blob(handle, key, NULL, &loaded_size);`
      2. Zero-initialise the target struct before loading: `memset(&g_pool[slot], 0, sizeof(act_mngr_entry_t));`
      3. Read only `min(loaded_size, sizeof(act_mngr_entry_t))` bytes:
         ```c
         size_t read_size = (loaded_size < sizeof(act_mngr_entry_t))
                            ? loaded_size : sizeof(act_mngr_entry_t);
         nvs_get_blob(handle, key, &g_pool[slot], &read_size);
         ```
      4. Old blobs (without `b_is_dynamic`) will have `b_is_dynamic = 0` from the zero-init, correctly marking them as static.  On the next `act_mngr_activity_update()` call for that slot, the full new blob size is saved.

3. **`main/src/http_server_activities.c`**:

   a. **POST `/activities/manage`, `action=add_activity`**:
      - Parse `is_dynamic`: call `http_srv_form_field_get(p_body, "is_dynamic", val_buf, sizeof(val_buf))`.  Set `uint8_t b_is_dynamic = (result == ESP_OK && val_buf[0] == '1') ? 1U : 0U;`.
      - **Credit validation**: if `credit_s_parsed == 0U`, return HTTP 400 with body `"Credit cannot be zero"`.
      - Pass `b_is_dynamic` to `act_mngr_activity_add()`.

   b. **POST `/activities/manage`, `action=update_N`**: apply the same `is_dynamic` parsing and `credit_s > 0` validation; pass `b_is_dynamic` to `act_mngr_activity_update()`.

   c. **GET `/activities` page JavaScript** (`loadUser()`): verify that no client-side filter on `credit_s` remains.  The API already performs the correct filter; the page renders whatever the API returns without additional JS-level filtering.

4. **`main/src/http_server_api.c`** — `http_srv_api_activities_get_handler()`:
   - Change filter for the user-scoped query (`dev_idx != 0xFF`): exclude activities where `entry.b_is_dynamic != 0U` (was: `entry.credit_s == 0U`).
   - When `dev_idx == 0xFF` (admin view, no filter): include ALL activities regardless of `b_is_dynamic` so the manage page can list and edit them.

#### Notes

> `act_mngr_entry_t` grows by 1 byte.  With typical 4-byte struct alignment the size rounds up to the next alignment boundary.  The NVS backward-compatibility code in `act_mngr_init()` handles blobs of any size ≤ `sizeof(act_mngr_entry_t)` by zero-padding the remainder.

> The rejection of `credit_s == 0` in `act_mngr_activity_add()` is a deliberate breaking change.  The HTTP POST handler is the only caller that could previously pass 0; it now returns HTTP 400 before reaching the core function.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `act_mngr_activity_add("test", 0U, 60U, 1U, 0U, &id)` returns `ESP_ERR_INVALID_ARG` (`credit_s == 0` rejected).
- [ ] `act_mngr_activity_add("static", 600U, 60U, 1U, 0U, &id)` returns `ESP_OK`; retrieved entry has `b_is_dynamic == 0`.
- [ ] `act_mngr_activity_add("game", 600U, 60U, 1U, 1U, &id)` returns `ESP_OK`; retrieved entry has `b_is_dynamic == 1`.
- [ ] NVS backward compatibility: loading an old-format blob (size < `sizeof(act_mngr_entry_t)`) in `act_mngr_init()` yields `b_is_dynamic == 0` for that activity.
- [ ] After loading an old blob and calling `act_mngr_activity_update()`, the next `act_mngr_init()` restores the full new struct correctly.
- [ ] `GET /api/activities?device_idx=0`: dynamic activities (`b_is_dynamic == 1`) are excluded; static activities are included.
- [ ] `GET /api/activities` (no `device_idx`): ALL activities (static AND dynamic) are returned.
- [ ] `POST /activities/manage` with `credit=0:00:00` → HTTP 400 `"Credit cannot be zero"`.
- [ ] `POST /activities/manage` with `is_dynamic=1`, `credit=0:10:00` → HTTP 200; activity created with `b_is_dynamic = 1`.
- [ ] `POST /activities/manage` with `action=update_N`, `is_dynamic=0` on a previously dynamic activity → entry updated to `b_is_dynamic = 0`.
- [ ] All Feature 7 acceptance criteria continue to pass (no regression).

---

### Phase 8.2 — Device Registry: MAC-Based PIN and Dual Auth Credit API

#### Goal

Add `device_reg_pin_compute()` to the device registry module so a deterministic PIN can be computed from a device's registered MAC address.  Update `POST /api/activities/credit` to support PIN-based self-crediting alongside existing admin Basic Auth, and enforce a credit cap for PIN-authenticated calls.

#### Inputs

- `main/inc/device_registry.h` (existing)
- `main/src/device_registry.c` (existing)
- `main/inc/http_server_config.h` (existing)
- `main/src/http_server_config.c` (existing)
- `main/src/http_server_api.c` (Phase 8.1 output)

#### Tasks

1. **`main/inc/device_registry.h`** — declare:

   ```c
   #define DEVICE_REG_PIN_LEN (8U)   /* PIN length in chars, excluding NUL */

   esp_err_t device_reg_pin_compute(uint8_t dev_idx, char *p_pin_out);
   ```

   - `p_pin_out` must point to a buffer of at least `DEVICE_REG_PIN_LEN + 1U` bytes.
   - Doxygen: "Computes a deterministic PIN for the device registered at `dev_idx`.  The PIN is the CRC32 of the device's 6-byte MAC address formatted as 8 uppercase hex characters (e.g. `\"A3B7F201\"`).  Returns `ESP_ERR_INVALID_ARG` if `dev_idx >= DEVICE_REG_MAX_ENTRIES` or the slot's MAC is all-zero (unregistered).  Returns `ESP_OK` on success; `p_pin_out` is NUL-terminated."

2. **`main/src/device_registry.c`** — implement `device_reg_pin_compute()`:
   - `#include "esp_rom_crc.h"` (ESP-IDF ROM CRC helpers).
   - Validate `dev_idx < DEVICE_REG_MAX_ENTRIES`.
   - Retrieve the entry: `device_reg_entry_t entry; device_reg_entry_get(dev_idx, &entry);`
   - Check for a non-zero MAC (registered device):
     ```c
     static const uint8_t zero_mac[6U] = { 0U };
     if (memcmp(entry.mac, zero_mac, sizeof(zero_mac)) == 0) { return ESP_ERR_INVALID_ARG; }
     ```
   - Compute CRC32: `uint32_t crc = esp_rom_crc32_be(0U, entry.mac, 6U);`
   - Format as 8 uppercase hex chars: `snprintf(p_pin_out, DEVICE_REG_PIN_LEN + 1U, "%08" PRIX32, crc);`
   - Return `ESP_OK`.

3. **`main/inc/http_server_config.h`** — declare:

   ```c
   bool http_srv_cfg_auth_check_silent(httpd_req_t *p_req);
   ```

   Doxygen: "Validates admin credentials from the `Authorization` header.  Unlike `http_srv_cfg_auth_check()`, this function does **not** send an HTTP 401 response on failure; it simply returns `false`.  Use when the caller intends to try an alternative auth method before deciding to reject the request."

4. **`main/src/http_server_config.c`** — refactor auth logic and implement the new function:

   a. Extract a `static bool http_srv_cfg_credentials_valid(httpd_req_t *p_req)` helper that performs the Base64 decode and `strcmp` credential check but sends **no** HTTP response.  All large static buffers (`hdr_buf`, `decoded`) live inside this helper.

   b. Rewrite `http_srv_cfg_auth_check(p_req)`: call `http_srv_cfg_credentials_valid(p_req)`; on `false`, send the HTTP 401 + `WWW-Authenticate: Basic realm="esport-fi32 Config"` response, then return `false`.  Behaviour for all existing callers is unchanged.

   c. Implement `http_srv_cfg_auth_check_silent(p_req)`: `return http_srv_cfg_credentials_valid(p_req);`.

5. **`main/src/http_server_api.c`** — update `http_srv_api_activities_credit_handler()`:

   a. Add `#include "device_registry.h"` and `#include "http_server_config.h"`.

   b. **Parse optional `pin` field** from the JSON body after existing field parsing:
      - Find `"pin"` key: `const char *p_pin_key = strstr(p_body, "\"pin\"");`
      - If found, locate the opening quote of the value, extract chars until the closing quote (up to `DEVICE_REG_PIN_LEN` chars); store in `char received_pin[DEVICE_REG_PIN_LEN + 1U]` on the stack.
      - If `"pin"` key is absent, set `received_pin[0] = '\0'`.

   c. **Dual auth logic** (replace the existing `http_srv_cfg_auth_check()` call at the top of the handler):

      ```c
      bool b_admin = http_srv_cfg_auth_check_silent(p_req);
      bool b_pin   = false;

      if (!b_admin && (received_pin[0] != '\0')) {
          char expected_pin[DEVICE_REG_PIN_LEN + 1U];
          if ((device_reg_pin_compute((uint8_t)device_idx, expected_pin) == ESP_OK) &&
              (strncmp(received_pin, expected_pin, DEVICE_REG_PIN_LEN) == 0)) {
              b_pin = true;
          }
      }

      if (!b_admin && !b_pin) {
          httpd_resp_set_status(p_req, "403 Forbidden");
          httpd_resp_set_type(p_req, "application/json");
          httpd_resp_sendstr(p_req, "{\"error\":\"unauthorized\"}");
          return ESP_OK;
      }
      ```

   d. **Credit cap for PIN-authenticated calls** — add immediately before the `act_mngr_activity_credit()` call:

      ```c
      if (!b_admin && b_pin) {
          act_mngr_entry_t act_entry;
          if ((act_mngr_activity_get(act_id, &act_entry) == ESP_OK) &&
              (credits_s > act_entry.credit_s)) {
              credits_s = act_entry.credit_s;   /* cap to reference credit */
          }
      }
      ```

   e. The success JSON response (`new_counter_s`, `new_counter_hms`) must reflect the (possibly capped) `credits_s`.

#### Notes

> `esp_rom_crc32_be()` is a ROM function available in all ESP32 variants via `<esp_rom_crc.h>`.  The same 6-byte MAC always produces the same 32-bit result across reboots.

> `received_pin` and `expected_pin` are ≤ 9 bytes including NUL — stack allocation is safe.

> The credit cap silently adjusts `credits_s` rather than rejecting the call.  This keeps the game flow seamless when rounding or small calculation errors cause the submitted value to marginally exceed the reference.

> `http_srv_cfg_credentials_valid()` is `static` and is not declared in any header.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `device_reg_pin_compute(0, buf)` returns `ESP_OK` for a registered device; `buf` is exactly 8 uppercase hex chars, NUL-terminated.
- [ ] `device_reg_pin_compute(N, buf)` returns `ESP_ERR_INVALID_ARG` for an unregistered slot (zero MAC) or invalid index.
- [ ] The same device always yields the same PIN on repeated calls (deterministic).
- [ ] Two devices with different MACs yield different PINs.
- [ ] `POST /api/activities/credit` with valid admin Basic Auth and no `pin` field → HTTP 200 (existing behaviour preserved).
- [ ] `POST /api/activities/credit` with no admin auth and the correct `pin` for `device_idx` → HTTP 200.
- [ ] `POST /api/activities/credit` with no admin auth and a wrong `pin` → HTTP 403 `{"error":"unauthorized"}`.
- [ ] `POST /api/activities/credit` with no admin auth and no `pin` field → HTTP 403.
- [ ] PIN-authenticated call with `credits_s > act_entry.credit_s` → HTTP 200; counter incremented by `act_entry.credit_s` (capped value).
- [ ] Admin-authenticated call with `credits_s > act_entry.credit_s` → HTTP 200; counter incremented by the original `credits_s` (no cap).
- [ ] `http_srv_cfg_auth_check_silent()` returns `true`/`false` without sending any HTTP response.
- [ ] All existing admin-only endpoints (`GET /config`, `POST /config`, etc.) are unaffected (no regression).

---

### Phase 8.3 — Build Infrastructure and Demo Dynamic Activity Pages

#### Goal

Create the `main/dyn_activities/` directory with two demo HTML pages, configure CMake to embed all files in that directory as binary blobs, and create `dyn_act_registry.h` together with a CMake-generated `dyn_act_registry.c` that maps logical activity names to embedded data pointers and sizes.

#### Inputs

- `main/CMakeLists.txt` (existing)

#### Tasks

1. **Create directory `main/dyn_activities/`**.

2. **Create `main/dyn_activities/dyn_activity1.html`** — a self-contained HTML5 page:

   - On load, reads `pin`, `device_idx`, `act_id`, `credits_s` from URL query parameters:
     ```javascript
     const params    = new URLSearchParams(window.location.search);
     const pin       = params.get('pin')        ?? '';
     const deviceIdx = parseInt(params.get('device_idx') ?? '0', 10);
     const actId     = parseInt(params.get('act_id')     ?? '0', 10);
     const creditsS  = parseInt(params.get('credits_s')  ?? '0', 10);
     ```
   - Displays `<h1>Dynamic Activity 1</h1>` and a `<p>` showing `"Available credits: " + formatHMS(creditsS)`.
   - A single `"Claim Credits"` button.  When clicked:
     - Disables the button to prevent double submission.
     - Sends `POST /api/activities/credit` with JSON body:
       `{"device_idx":<N>,"act_id":<N>,"credits_s":<N>,"completion_time_s":0,"pin":"<PIN>"}`.
     - HTTP 200 → shows green success banner `"Credits claimed! Counter: " + data.new_counter_hms`.
     - HTTP 429 → shows `"Daily limit reached — come back tomorrow!"`.
     - HTTP 403 → shows `"Unauthorised — please use your own device."`.
     - Other errors → shows `"Error claiming credits (HTTP <status>)."`.
   - Helper `function formatHMS(s)` formats seconds as `"H:MM:SS"`.
   - White (`#ffffff`) background; centred content; large sans-serif font (child-friendly); no external resources.

3. **Create `main/dyn_activities/dyn_activity2.html`** — identical structure and logic to `dyn_activity1.html` with:
   - `<h1>` title: `"Dynamic Activity 2"`.
   - Background colour `#e8f4f8` (light blue) instead of white.

4. **Create `main/inc/dyn_act_registry.h`**:

   ```c
   #ifndef DYN_ACT_REGISTRY_H
   #define DYN_ACT_REGISTRY_H

   //=== Includes ===============================================================

   #include <stddef.h>
   #include <stdint.h>

   //=== Constants ==============================================================

   /** Maximum length of a dynamic activity logical name, excluding NUL. */
   #define DYN_ACT_NAME_MAX_LEN (32U)

   //=== Types ==================================================================

   /** One entry in the dynamic activity registry. */
   typedef struct dyn_act_entry_tag {
       const char    *p_name;   /**< Logical name (filename without path/extension). */
       const uint8_t *p_data;   /**< Pointer to embedded HTML start.                 */
       size_t         size;     /**< Byte size of the HTML data.                     */
   } dyn_act_entry_t;

   //=== Globals ================================================================

   /** Auto-generated compile-time table of all embedded dynamic activity files. */
   extern const dyn_act_entry_t g_dyn_act_registry[];

   /** Number of entries in #g_dyn_act_registry. */
   extern const uint8_t g_dyn_act_count;

   #endif /* DYN_ACT_REGISTRY_H */
   /*** end of file ***/
   ```

   Guard with `DYN_ACT_REGISTRY_H`; `hhtemplate` structure.

5. **Update `main/CMakeLists.txt`** — add the following block **before** `idf_component_register(...)`:

   ```cmake
   # ---- Dynamic Activity Files -----------------------------------------------
   # Collect all HTML files; CONFIGURE_DEPENDS re-triggers CMake when files
   # are added or removed from the directory.
   file(GLOB DYN_ACT_HTMLS
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/dyn_activities/*.html")

   # Auto-generate dyn_act_registry.c into the build directory so the source
   # tree is not modified.
   set(_DYN_REG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
   set(_DYN_REG_C   "${_DYN_REG_DIR}/dyn_act_registry.c")
   file(MAKE_DIRECTORY "${_DYN_REG_DIR}")

   set(_REG  "/* Auto-generated by CMake – do not edit manually. */\n")
   string(APPEND _REG "#include \"dyn_act_registry.h\"\n\n")
   set(_EXT  "")
   set(_TBL  "")
   set(_CNT  0)

   foreach(_HTML ${DYN_ACT_HTMLS})
       get_filename_component(_FNAME "${_HTML}" NAME_WE)
       file(RELATIVE_PATH _REL "${CMAKE_CURRENT_SOURCE_DIR}" "${_HTML}")
       string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" _SYM "${_REL}")
       string(APPEND _EXT
           "extern const uint8_t _binary_${_SYM}_start[];\n"
           "extern const uint8_t _binary_${_SYM}_end[];\n")
       string(APPEND _TBL
           "    { \"${_FNAME}\","
           " _binary_${_SYM}_start,"
           " (size_t)(_binary_${_SYM}_end - _binary_${_SYM}_start) },\n")
       math(EXPR _CNT "${_CNT} + 1")
   endforeach()

   string(APPEND _REG "${_EXT}\n")
   string(APPEND _REG "const dyn_act_entry_t g_dyn_act_registry[] = {\n${_TBL}};\n\n")
   string(APPEND _REG "const uint8_t g_dyn_act_count = (uint8_t)(${_CNT}U);\n")
   string(APPEND _REG "\n/*** end of file ***/\n")

   file(WRITE "${_DYN_REG_C}" "${_REG}")
   # ---------------------------------------------------------------------------
   ```

   Then update `idf_component_register(...)`:
   - Add `"${_DYN_REG_DIR}/dyn_act_registry.c"` and `"src/http_server_dyn.c"` (created in Phase 8.4) to `SRCS`.
   - Add `"${_DYN_REG_DIR}"` to `INCLUDE_DIRS` (ensures the compiler finds the generated file's directory).
   - Add `EMBED_FILES ${DYN_ACT_HTMLS}`.

   **Note on adding new mini-games**: place a `.html` file in `main/dyn_activities/`, then run `idf.py reconfigure` (or any CMake configure step) followed by `idf.py build`.  The `CONFIGURE_DEPENDS` flag causes the build system to detect the new file and trigger reconfiguration automatically on the next build.

#### Notes

> `file(WRITE ...)` executes at CMake **configure** time (before compilation), so the generated `.c` file exists when the compiler is invoked.  No `add_custom_command` is needed.

> The ESP-IDF symbol name for an embedded file is derived from the path relative to the component source directory, with all non-alphanumeric characters replaced by `_`, prefixed by `_binary_` and suffixed by `_start`/`_end`.  The CMake `REGEX REPLACE "[^a-zA-Z0-9_]" "_" _SYM "${_REL}"` applies exactly the same substitution.

> `${CMAKE_CURRENT_BINARY_DIR}/generated/` is inside the build tree and must **not** be committed to source control.  Add `build/` (or the specific generated path) to `.gitignore` if not already covered.

> `EMBED_FILES` accepts absolute paths in all tested ESP-IDF versions when passed as a CMake list variable.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `g_dyn_act_count == 2U` (two demo pages registered).
- [ ] `strcmp(g_dyn_act_registry[0].p_name, "dyn_activity1") == 0`.
- [ ] `strcmp(g_dyn_act_registry[1].p_name, "dyn_activity2") == 0`.
- [ ] `g_dyn_act_registry[0].size > 100U` and `g_dyn_act_registry[0].p_data` is non-NULL.
- [ ] Adding `dyn_activity3.html` to `main/dyn_activities/`, running `idf.py reconfigure && idf.py build`, yields `g_dyn_act_count == 3U` with the new file in the registry.
- [ ] `dyn_activity1.html` is valid HTML5 with: URL parameter reading, a "Claim Credits" button, `POST /api/activities/credit` `fetch()` call, and all four response handlers (success, 429, 403, other error).
- [ ] `dyn_activity2.html` has identical logic with `"Dynamic Activity 2"` title and a visually distinct background colour.
- [ ] Both demo pages are self-contained (no external resources).

---

### Phase 8.4 — HTTP Server: User Dynamic Activities Page, File Server, and Manage UI

#### Goal

Create `http_server_dyn.c/.h` with `GET /dyn` (user-facing launch page) and `GET /dyn_activities/*` (embedded file server).  Add `GET /api/dyn` to `http_server_api.c`.  Update `GET /activities/manage` to include an `is_dynamic` checkbox with a JS-driven combobox listing embedded activity names.  Register all new routes in `http_server.c`.

#### Inputs

- `main/inc/dyn_act_registry.h` (Phase 8.3 output)
- `main/inc/activity_manager.h` (Phase 8.1 output)
- `main/inc/device_registry.h` (Phase 8.2 output)
- `main/src/http_server_activities.c` (Phase 8.1 output)
- `main/src/http_server_api.c` (Phase 8.2 output)
- `main/src/http_server.c` (existing)

#### Tasks

1. **Create `main/inc/http_server_dyn.h`**:

   ```c
   esp_err_t http_srv_dyn_page_get_handler(httpd_req_t *p_req);
   esp_err_t http_srv_dyn_file_get_handler(httpd_req_t *p_req);
   ```

   Guard with `HTTP_SERVER_DYN_H`; `hhtemplate` structure.

2. **Create `main/src/http_server_dyn.c`**:

   a. **`http_srv_dyn_page_get_handler()` (GET /dyn)** — no admin auth required:

   First, implement a file-scope static helper:

   ```c
   static esp_err_t http_srv_dyn_detect_device(httpd_req_t *p_req, uint8_t *p_dev_idx_out);
   ```

   This helper:
   1. Obtains the underlying socket: `int sock = httpd_req_to_sockfd(p_req);`
   2. Gets the peer address: `struct sockaddr_in peer; socklen_t addrlen = sizeof(peer); getpeername(sock, (struct sockaddr *)&peer, &addrlen);`
   3. Converts to a lwIP `ip4_addr_t`: `ip4_addr_t target_ip; target_ip.addr = peer.sin_addr.s_addr;`
   4. Looks up the ARP table (must hold the lwIP core lock while the pointer is valid):
      ```c
      #include "lwip/etharp.h"
      #include "lwip/tcpip.h"
      struct eth_addr *p_eth = NULL;
      ip4_addr_t      *p_found_ip = NULL;
      uint8_t          mac[6U];
      bool             b_found = false;
      LOCK_TCPIP_CORE();
      int8_t arp_r = etharp_find_addr(NULL, &target_ip, &p_eth, &p_found_ip);
      if ((arp_r >= 0) && (p_eth != NULL)) {
          memcpy(mac, p_eth->addr, 6U); /* copy before releasing lock */
          b_found = true;
      }
      UNLOCK_TCPIP_CORE();
      if (!b_found) { return ESP_ERR_NOT_FOUND; }
      ```
   5. Searches the device registry for the resolved MAC:
      ```c
      static const uint8_t zero_mac[6U] = { 0U };
      for (uint8_t i = 0U; i < DEVICE_REG_MAX_ENTRIES; i++) {
          device_reg_entry_t entry;
          device_reg_entry_get(i, &entry);
          if ((memcmp(entry.mac, zero_mac, 6U) != 0) &&
              (memcmp(entry.mac, mac,      6U) == 0)) {
              *p_dev_idx_out = i;
              return ESP_OK;
          }
      }
      return ESP_ERR_NOT_FOUND;
      ```

   The page handler itself:
   - Calls `http_srv_dyn_detect_device(p_req, &dev_idx)`.
   - On `ESP_ERR_NOT_FOUND`: heap-allocate a small HTML buffer (512 bytes) and return an error page:
     `<h1>Device not registered</h1><p>Your device is not registered on this network. Please ask the administrator to add it.</p><p><a href="/">← Home</a></p>`
     with HTTP 200 (the page itself explains the situation; no HTTP error code is needed for a browser).
   - On `ESP_OK`: heap-allocate HTML buffer (2 048 bytes); return HTTP 500 on allocation failure.
   - Build the HTML page:
     - `<h1>My Activities</h1>` followed by `<p>Hello, <b><nickname></b></p>` (nickname read via `device_reg_entry_get(dev_idx, &entry)`).
     - A `<div id="activity-list"></div>` placeholder.
     - Inline JavaScript:
       - `const DEVICE_IDX = <dev_idx>;` — the detected index embedded as a JS constant at render time.
       - `function formatHMS(s)` — formats seconds as `"H:MM:SS"`.
       - On `DOMContentLoaded`: call `fetch('/api/dyn?device_idx=' + DEVICE_IDX)`.  On success: iterate `data.activities`; for each entry build a `<button>` that, when clicked, navigates to `/dyn_activities/<name>?pin=<data.pin>&device_idx=<DEVICE_IDX>&act_id=<act_id>&credits_s=<credit_s>`.  If `!entry.available`, the button is disabled and labelled `"(done today)"`.  Populate `activity-list` with the result.  On fetch failure show `"Failed to load activities."`.
   - Include a **Dashboard** button (blue `a.btn`) linking to `/`.
   - No external resources.
   - `Content-Type: text/html`.
   - Required headers: `#include "lwip/etharp.h"`, `#include "lwip/tcpip.h"`, `#include <sys/socket.h>`, `#include <netinet/in.h>`.

   b. **`http_srv_dyn_file_get_handler()` (GET /dyn_activities/*)**:
   - Extract the filename portion from `p_req->uri`: advance past the `/dyn_activities/` prefix (16 chars).
   - Strip a trailing `.html` extension if present (find the last `.` and NUL-terminate there) to allow both `/dyn_activities/dyn_activity1` and `/dyn_activities/dyn_activity1.html`.
   - Search `g_dyn_act_registry[]` (loop `0..g_dyn_act_count`) for an entry whose `p_name` matches the extracted name via `strcmp`.
   - If found: `httpd_resp_set_type(p_req, "text/html"); httpd_resp_send(p_req, (const char *)p_entry->p_data, (ssize_t)p_entry->size);`
   - If not found: `httpd_resp_send_err(p_req, HTTPD_404_NOT_FOUND, "Dynamic activity not found");`
   - Follow `cctemplate` structure; `gp_tag = "http_srv_dyn"`.

3. **`main/src/http_server_api.c`** — add `static esp_err_t http_srv_api_dyn_get_handler(httpd_req_t *p_req)`:

   - `#include "dyn_act_registry.h"` at the top of the file.
   - Parse required query parameter `device_idx` (0–3) via `httpd_req_get_url_query_str()` + `httpd_query_key_value()`.  Return HTTP 400 `{"error":"device_idx required"}` if absent or out of range.
   - Validate registration: call `device_reg_entry_get(dev_idx, &entry)`; check for non-zero nickname or MAC; return HTTP 400 `{"error":"device not registered"}` if unregistered.
   - Compute PIN: `char pin[DEVICE_REG_PIN_LEN + 1U]; device_reg_pin_compute(dev_idx, pin);`
   - Collect assigned dynamic activities:
     - `act_mngr_user_assigns_t assigns; act_mngr_user_assigns_get(dev_idx, &assigns);`
     - For each `act_id` in `assigns.act_ids[]` where `act_id != ACT_MNGR_NO_ID`: call `act_mngr_activity_get(act_id, &act_entry)`.  Include only entries where `act_entry.b_is_dynamic != 0U`.
     - For each included entry: compute `done_today = act_mngr_user_daily_done_get(dev_idx, act_id)` and `available = !act_mngr_user_daily_limit_reached(dev_idx, act_id)`.
   - Build JSON response into a heap-allocated 1 024-byte buffer:
     ```json
     {
       "pin": "A3B7F201",
       "device_idx": 0,
       "activities": [
         {
           "act_id": 5,
           "name": "dyn_activity1",
           "credit_s": 600,
           "credits_hms": "0:10:00",
           "time_limit_s": 600,
           "time_limit_hms": "0:10:00",
           "done_today": 0,
           "available": true
         }
       ]
     }
     ```
   - Return `Content-Type: application/json`.

4. **`main/src/http_server_activities.c`** — update `http_srv_activities_manage_get_handler()`:

   a. **`is_dynamic` checkbox and dynamic name combobox** in the "Add Activity" sub-form:
   - After the existing `<input type="text" id="act_name_add" name="act_name">`, add:
     ```html
     <label>
       <input type="checkbox" id="is_dyn_add" name="is_dynamic" value="1"
              onchange="onIsDynChange(this,'add')">
       Dynamic activity (mini-game)
     </label>
     <select id="dyn_name_add" name="act_name_dyn" style="display:none" disabled>
     ```
   - Populate the `<select>` server-side: loop `0..g_dyn_act_count`; for each entry emit `<option value="%s">%s</option>` using `g_dyn_act_registry[i].p_name`.  Close `</select>`.
   - Add `#include "dyn_act_registry.h"` at the top of the file.

   b. **Inline JavaScript** (add once in the page `<script>` block):
   ```javascript
   function onIsDynChange(cb, sfx) {
       var t = document.getElementById('act_name_' + sfx);
       var s = document.getElementById('dyn_name_' + sfx);
       if (cb.checked) {
           t.style.display = 'none';  t.disabled = true;
           s.style.display = '';      s.disabled = false;
       } else {
           t.style.display = '';      t.disabled = false;
           s.style.display = 'none';  s.disabled = true;
       }
   }
   ```

   c. **POST handler** — `http_srv_activities_manage_post_handler()`, `action=add_activity` and `action=update_N`:
   - After parsing `b_is_dynamic`: if `b_is_dynamic == 1U`, read `act_name_dyn` field instead of `act_name` as the activity name.
   - **Name registry validation**: if `b_is_dynamic == 1U`, loop `g_dyn_act_registry[]` and confirm the name matches a `p_name` entry via `strcmp`.  If no match: HTTP 400 `"Dynamic activity name not found in firmware registry"`.

5. **`main/src/http_server.c`** — register three new URI handlers and increase the handler count:

   ```c
   { .uri = "/dyn",              .method = HTTP_GET, .handler = http_srv_dyn_page_get_handler  }
   { .uri = "/dyn_activities/*", .method = HTTP_GET, .handler = http_srv_dyn_file_get_handler  }
   { .uri = "/api/dyn",          .method = HTTP_GET, .handler = http_srv_api_dyn_get_handler   }
   ```

   Increase `cfg.max_uri_handlers` from `21U` to `24U`.

   For the wildcard route `/dyn_activities/*`: ESP-IDF `esp_http_server` supports a trailing `*` wildcard when the `httpd_config_t.uri_match_fn` is set to `httpd_uri_match_wildcard`.  If the server configuration in `http_server.c` does not already set this field, add:
   ```c
   cfg.uri_match_fn = httpd_uri_match_wildcard;
   ```
   in the `httpd_config_t` initialisation block.  Exact-match routes already registered continue to take precedence over the wildcard route.

   Add `#include "http_server_dyn.h"` to `http_server.c`.

6. **`main/CMakeLists.txt`** — confirm `"src/http_server_dyn.c"` is present in `SRCS` (added in Phase 8.3 step 5; verify here).

#### Notes

> Stack budget: all HTML buffers in `http_srv_dyn_page_get_handler()` must be heap-allocated.  `http_srv_dyn_file_get_handler()` requires no heap allocation — it sends the in-flash embedded blob directly.

> **ARP lookup safety**: `etharp_find_addr()` returns a pointer into the lwIP ARP table, which is only valid while the lwIP core lock (`LOCK_TCPIP_CORE`) is held.  The MAC must be copied into a local `uint8_t mac[6U]` buffer before `UNLOCK_TCPIP_CORE()` is called.  `getpeername()` is a POSIX socket call and does not require the lwIP lock.

> **ARP availability**: the requesting device has just sent an HTTP request, so its ARP entry is guaranteed to be present and fresh in the lwIP ARP cache.

> `GET /api/dyn` intentionally exposes the PIN in the JSON response.  The PIN is a non-secret CRC32 derivation of the MAC address (MAC addresses are visible on the local LAN).  The PIN is valid only for the specific `device_idx` it was computed for; the server enforces this in `POST /api/activities/credit`.

> The `disabled` attribute on the inactive name input (text vs. combobox) ensures that only one `act_name` or `act_name_dyn` field is submitted in the form body at a time, simplifying POST parsing.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and warnings.
- [ ] `GET /dyn` from a registered device (device 0): returns HTTP 200 HTML containing the device nickname and an `activity-list` div; `const DEVICE_IDX = 0` embedded as a JS constant; no `<select>` device dropdown.
- [ ] `GET /dyn` from an unregistered device (IP not in ARP table, or MAC not in registry): returns HTTP 200 with the "Device not registered" error page.
- [ ] `GET /dyn` does not expose any other device's activities (cross-user access blocked by design).
- [ ] `GET /api/dyn?device_idx=0` returns HTTP 200 JSON with `"pin"` (8 uppercase hex chars) and `"activities"` array.
- [ ] `GET /api/dyn?device_idx=0`: only dynamic activities (`b_is_dynamic == 1`) assigned to device 0 are returned; static activities are excluded.
- [ ] `GET /api/dyn?device_idx=0`: `"available": false` when `done_today >= daily_limit`.
- [ ] `GET /api/dyn` (no `device_idx`) returns HTTP 400 `{"error":"device_idx required"}`.
- [ ] `GET /dyn_activities/dyn_activity1` returns HTTP 200 with the embedded HTML content.
- [ ] `GET /dyn_activities/dyn_activity1.html` returns HTTP 200 (`.html` extension stripped correctly).
- [ ] `GET /dyn_activities/dyn_activity2` returns HTTP 200.
- [ ] `GET /dyn_activities/nonexistent` returns HTTP 404.
- [ ] `GET /activities/manage` form: `is_dynamic` checkbox is present; checking it hides the name text input and shows the dynamic name `<select>` combobox populated with `"dyn_activity1"` and `"dyn_activity2"`.
- [ ] `POST /activities/manage` with `is_dynamic=1`, `act_name_dyn=dyn_activity1`, `credit=0:10:00`, `daily_limit=1` → HTTP 303 redirect (activity created with `b_is_dynamic = 1`).
- [ ] `POST /activities/manage` with `is_dynamic=1`, `act_name_dyn=nonexistent` → HTTP 400 `"Dynamic activity name not found in firmware registry"`.
- [ ] `cfg.uri_match_fn = httpd_uri_match_wildcard` is set in the `httpd_config_t` initialisation.
- [ ] `max_uri_handlers == 24U` in `http_server.c`.
- [ ] No local variable block exceeds 512 bytes on the stack in any new handler.

---

### Phase 8.5 — Integration & Verification

#### Goal

Confirm the build succeeds with all Feature 8 changes integrated and verify the complete end-to-end dynamic activities flow.

#### Inputs

All Phase 8.1–8.4 outputs, `main/src/main.c` (no changes needed — `act_mngr_init()` was already wired in Feature 7).

#### Tasks

1. Run `idf.py build`; resolve any compilation or linker errors.

2. **End-to-end checklist:**

   | # | Test | Pass/Fail |
   | - | ---- | --------- |
   | 1 | `POST /activities/manage`: add static activity with credit `0:00:00` → HTTP 400 "Credit cannot be zero" | |
   | 2 | `POST /activities/manage`: add static activity with credit `0:10:00` → HTTP 303; activity appears in manage page | |
   | 3 | `POST /activities/manage`: add dynamic activity (`is_dynamic=1`, `act_name_dyn=dyn_activity1`, credit `0:10:00`, daily limit 2) → HTTP 303 | |
   | 4 | `POST /activities/manage` with `is_dynamic=1`, `act_name_dyn=nonexistent` → HTTP 400 | |
   | 5 | `GET /activities/manage`: `is_dynamic` checkbox present; checking it toggles name text input ↔ combobox | |
   | 6 | `GET /activities` admin award page: dynamic activity NOT listed; static activity IS listed | |
   | 7 | `GET /api/activities?device_idx=0`: dynamic activity excluded; static activity included | |
   | 8 | `GET /api/activities` (no `device_idx`): both static and dynamic activities appear | |
   | 9 | Assign both activities to device 0 via manage page | |
   | 10 | `GET /api/dyn?device_idx=0`: returns `"pin"` (8 uppercase hex chars) and the dynamic activity in `"activities"` | |
   | 11 | `GET /dyn` from device 0's browser: page loads, shows device 0's nickname, activity list populated via JS; no device dropdown present | |
   | 12 | Clicking the dynamic activity button navigates to `/dyn_activities/dyn_activity1?pin=<PIN>&device_idx=0&act_id=<ID>&credits_s=600` | |
   | 12a | `GET /dyn` from a device with a MAC not in the registry returns the "Device not registered" error page | |
   | 12b | Device 0 cannot see device 1's activities by any URL manipulation on the `/dyn` page | |
   | 13 | `GET /dyn_activities/dyn_activity1` returns HTTP 200 with embedded HTML | |
   | 14 | On `dyn_activity1.html`: "Claim Credits" button → `POST /api/activities/credit` with PIN → HTTP 200; device counter increments by 600 s | |
   | 15 | On `dyn_activity1.html`: second claim (daily limit 2, one done) → HTTP 200; counter increments again | |
   | 16 | On `dyn_activity1.html`: third claim (daily limit reached) → HTTP 429; page shows "Daily limit reached" | |
   | 17 | `POST /api/activities/credit` with wrong PIN → HTTP 403 | |
   | 18 | `POST /api/activities/credit` (admin auth) with `credits_s=9999`, `act.credit_s=600` → HTTP 200; counter increments by 9999 (uncapped) | |
   | 19 | `POST /api/activities/credit` (PIN auth) with `credits_s=9999`, `act.credit_s=600` → HTTP 200; counter increments by 600 (capped) | |
   | 20 | Power cycle: activity pool, `b_is_dynamic` field, assignments, and daily counts restored from NVS | |
   | 21 | `GET /dyn_activities/nonexistent` → HTTP 404 | |
   | 22 | All existing endpoints (`/`, `/config`, `/api/status`, `/ota`, `/activities`, `/activities/manage`) respond correctly — no regression | |

#### Acceptance Criteria

- [x] `idf.py build` succeeds with zero errors and zero warnings (`-Werror` enforced).
- [ ] All 22 end-to-end tests pass.
- [x] `max_uri_handlers` in `http_server.c` is `24U`.
- [ ] No assertion failures or watchdog triggers during 10-minute continuous operation.

---

### Phase 8.6 — Documentation & README Update

#### Goal

Update `docs/1-specification.md`, `docs/2-development_plan.md` (Module Prefix Table), and `README.md` to reflect the complete Feature 8 implementation.

#### Inputs

- All Phase 8.1–8.5 outputs.
- `docs/1-specification.md` (current).
- `docs/2-development_plan.md` (current).
- `README.md` (current).

#### Tasks

1. **`docs/1-specification.md`**:

   - **§4 Component/File Layout**: add `dyn_act_registry.h` and `http_server_dyn.h` to `inc/`; add `http_server_dyn.c` to `src/`; add a `dyn_activities/` directory entry noting that `dyn_act_registry.c` is auto-generated into the build directory and is not part of the source tree.

   - **§5.X Activity Manager — Data Model**: update `act_mngr_entry_t` to show the `b_is_dynamic` field.  Update `act_mngr_activity_add()` and `act_mngr_activity_update()` signatures.  Add a note: "`credit_s == 0` is rejected by `act_mngr_activity_add()` — all activities must specify a non-zero reference credit."  Document the NVS backward-compatibility rule: "Old blobs (pre-Feature 8) are zero-padded on load; `b_is_dynamic` defaults to `0` (static)."

   - **§5.X Activity Manager — Listing**: update the filter description for `GET /api/activities?device_idx=N` from "excludes activities where `credit_s == 0`" to "excludes activities where `b_is_dynamic == 1`".  Note that the unscoped query returns all activities.

   - **§5.X Device Registry**: add `device_reg_pin_compute()` to the public API table: "Computes a deterministic 8-hex-char PIN (CRC32 of the 6-byte MAC) for the device at `dev_idx`.  Used for PIN-authenticated credit API calls."

   - **New §5.X — Dynamic Activity Registry Module**:
     - **File**: `dyn_act_registry.h` / auto-generated `dyn_act_registry.c` (build directory).
     - **Responsibilities**: compile-time table mapping logical activity names (filename without path/extension) to embedded data pointers and sizes.  Provides `g_dyn_act_registry[]` and `g_dyn_act_count`.
     - **Adding new mini-games**: place a `.html` file in `main/dyn_activities/`, run `idf.py reconfigure`, rebuild and reflash.
     - **Public data**: `extern const dyn_act_entry_t g_dyn_act_registry[]`, `extern const uint8_t g_dyn_act_count`.

   - **New §5.X — HTTP Server Dynamic Module**:
     - **File**: `http_server_dyn.c` / `http_server_dyn.h`.
     - **Routes**: `GET /dyn` (user-facing, no auth); `GET /dyn_activities/*` (wildcard, serves embedded files).
     - **`GET /dyn`**: auto-detects the requesting device via source IP → lwIP ARP table → MAC → device registry lookup; renders the detected device's assigned dynamic activities as launch buttons; shows an error page if the device is unregistered.  JavaScript calls `GET /api/dyn?device_idx=N` (with the server-embedded device index) to populate the button list.  Clicking a button navigates to the embedded activity page with `pin`, `device_idx`, `act_id`, `credits_s` query parameters.
     - **`GET /dyn_activities/<name>`**: looks up `<name>` (`.html` extension stripped) in `g_dyn_act_registry[]`; serves the embedded blob as `text/html`; HTTP 404 if not found.

   - **§6.X `GET /api/activities?device_idx=N`**: update filter note to `b_is_dynamic == 1`.

   - **New §6.X — `GET /api/dyn`**:
     - Query parameter: `device_idx` (required, 0–3).
     - Returns the device's PIN and a list of its assigned dynamic activities with daily status.
     - Document the full JSON schema (as specified in Phase 8.4 task 3).

   - **§6.X `POST /api/activities/credit`**: add to the request body table:
     - `pin` — string, optional — 8 uppercase hex chars.  If admin credentials are present, `pin` is ignored.  If no admin credentials, a valid `pin` matching the device's computed PIN is required; missing or wrong PIN returns HTTP 403.  For PIN-authenticated calls, `credits_s` is silently capped to `activity.credit_s`.

   - **§6.X `/activities/manage`**: document the `is_dynamic` checkbox: when checked, the activity name field becomes a `<select>` combobox listing names from `g_dyn_act_registry[]`.  The POST handler validates that the selected name exists in the registry.

   - **§8 NVS Layout — `esport_act`**: update the `act_mngr_entry_t` blob description to include the `b_is_dynamic` byte and the backward-compatibility note.

2. **`README.md`** — add a section **"Dynamic Activities (Mini-Games)"**:
   - Describe the concept: mini-games are HTML pages embedded in the firmware; kids play them and earn internet credits autonomously.
   - Describe the admin setup workflow: in `/activities/manage`, tick the "Dynamic activity" checkbox; choose the mini-game name from the dropdown; set a reference credit value and daily limit; assign the activity to a user.
   - Describe the kid workflow: navigate to `/dyn`, select your profile, launch a mini-game, click "Claim Credits".
   - Describe the PIN system: device-bound PIN (CRC32 of MAC, 8 hex chars) injected automatically; only allows crediting for the selected device.
   - Describe the credit cap: PIN-authenticated calls cannot exceed the activity's reference credit.
   - Describe how to add new mini-games: create a self-contained `.html` file in `main/dyn_activities/`, run `idf.py reconfigure`, rebuild and reflash.
   - Keep the section concise; refer to `docs/1-specification.md` for full technical detail.

3. **`docs/2-development_plan.md`** — Module Prefix Table: add two rows (see task below).

#### Acceptance Criteria

- [x] `docs/1-specification.md` §4 file layout includes `dyn_act_registry.h`, `http_server_dyn.h/c`, and `dyn_activities/`.
- [x] `docs/1-specification.md` §5.X Activity Manager data model includes `b_is_dynamic`; add/update signatures updated; `credit_s == 0` rejection and NVS backward compat noted.
- [x] `docs/1-specification.md` §5.X Device Registry documents `device_reg_pin_compute()`.
- [x] `docs/1-specification.md` includes a new §5.X for the Dynamic Activity Registry module.
- [x] `docs/1-specification.md` includes a new §5.X for the HTTP Server Dynamic module.
- [x] `docs/1-specification.md` §6.X `GET /api/dyn` is documented with full JSON schema.
- [x] `docs/1-specification.md` §6.X `POST /api/activities/credit` documents the `pin` field, HTTP 403, and credit cap.
- [x] `docs/1-specification.md` §8 NVS `act_mngr_entry_t` blob reflects `b_is_dynamic` and backward-compat note.
- [x] `README.md` contains a clear "Dynamic Activities (Mini-Games)" section covering admin setup, kid workflow, PIN system, credit cap, and adding new games.
- [x] `docs/2-development_plan.md` Module Prefix Table includes `dyn_act_registry | dyn_act_ | DYN_ACT_` and `http_server_dyn | http_srv_dyn_ | HTTP_SRV_DYN_`.
- [x] All three documents are internally consistent with each other.

---

### Phase 8.7 — Commit Message

```
feat: implement dynamic activities (mini-games) infrastructure

Introduces the infrastructure for dynamic activities — self-contained
HTML/JS mini-games embedded in the firmware binary and served by the
ESP32 HTTP server.  Kids can play mini-games and self-credit internet
time using a device-bound PIN without requiring the admin password.

Data model:
- Add `b_is_dynamic` field to `act_mngr_entry_t`; existing NVS blobs
  are migrated transparently via zero-padding on load.
- Reject `credit_s == 0` in `act_mngr_activity_add()` for all
  activities; the reference credit is now always required.
- Activity listing on `GET /activities` and `GET /api/activities` now
  filters out dynamic activities (was filtering `credit_s == 0`);
  admin-unscoped `GET /api/activities` still returns all activities.

PIN authentication:
- Add `device_reg_pin_compute()`: deterministic 8-hex-char CRC32 PIN
  derived from a device's registered MAC address.
- `POST /api/activities/credit` now supports dual authentication:
  - Admin Basic Auth: existing behaviour, no credit cap.
  - PIN auth (`"pin"` field in body): `credits_s` capped to
    `activity.credit_s`; HTTP 403 on wrong or missing PIN.
- Add `http_srv_cfg_auth_check_silent()` (non-sending credential
  check helper used by the dual-auth logic).

Dynamic activity files:
- Add `main/dyn_activities/` with two proof-of-concept demo pages:
  `dyn_activity1.html` and `dyn_activity2.html`.
- CMake `EMBED_FILES` with `CONFIGURE_DEPENDS` glob embeds all HTML
  files at build time; adding a file triggers auto-reconfiguration.
- CMake auto-generates `dyn_act_registry.c` (build directory) at
  configure time, providing a compile-time table of all embedded
  files (name → data pointer + size).

HTTP server:
- `GET /dyn`: user-facing launch page (no admin auth); auto-detects
  the requesting device by IP→ARP→MAC lookup; shows only that device's
  activities; unregistered devices see an error page.
- `GET /dyn_activities/*`: wildcard handler serves embedded HTML files.
- `GET /api/dyn?device_idx=N`: returns PIN + dynamic activity list.
- `/activities/manage`: `is_dynamic` checkbox with JS toggle between
  free-text name input and dynamic-name combobox; POST validates name
  against the firmware registry.
- `max_uri_handlers` increased from 21 to 24.

Documentation:
- Update docs/1-specification.md (§4, §5, §6, §8).
- Update README.md with "Dynamic Activities (Mini-Games)" section.
- Update Module Prefix Table in docs/2-development_plan.md.
```

---

## Feature 9 — Gzip Compression of Embedded Dynamic Activity Files

### Overview

Feature 9 reduces the flash footprint of dynamic activity HTML files by gzip-compressing
them at CMake configure time and embedding the compressed blobs in the firmware binary
instead of the raw source.  The HTTP server sends the compressed bytes directly to the
browser with a `Content-Encoding: gzip` response header; the browser decompresses
transparently.

The feature is entirely backward-compatible: files listed in a CMakeLists opt-out variable
are embedded uncompressed, and the HTTP handler always checks a per-entry flag to decide
which response header to send.

Key changes:

- **CMake build** — at configure time, each HTML file is compressed to a `.html.gz` sibling
  in `${CMAKE_CURRENT_BINARY_DIR}/generated/gz/`.  If the gzip binary is unavailable, the
  build aborts with a clear error message.  An opt-out list (`DYN_ACT_NO_COMPRESS`) lets
  individual files be excluded from compression and embedded raw.
- **`dyn_act_registry.h`** — add a `b_gzip` flag to `dyn_act_entry_t` so the HTTP handler
  knows which entries are compressed.
- **`http_server_dyn.c`** — `http_srv_dyn_file_get_handler()` sets
  `Content-Encoding: gzip` before calling `httpd_resp_send()` when `b_gzip == 1`; no
  header is added for uncompressed entries.
- **Documentation** — update `README.md` and `docs/4-dynamic_activities_plan.md`.

### Design Decisions

| Decision | Choice | Rationale |
| -------- | ------ | --------- |
| Compression tool | `gzip` (host system, required) | Present on all Linux and macOS build hosts; no CMake module or Python dependency needed |
| Compressed file location | `${CMAKE_CURRENT_BINARY_DIR}/generated/gz/` | Keeps compressed artefacts inside the build tree; never committed to source control |
| `Content-Encoding` check | Always send `Content-Encoding: gzip` for compressed entries; no `Accept-Encoding` inspection | All browsers on the home LAN support gzip; avoids heap allocation for decompression |
| Opt-out mechanism | `DYN_ACT_NO_COMPRESS` list variable in `main/CMakeLists.txt` | Explicit; visible in one place; easy to extend |
| `b_gzip` flag | `uint8_t` in `dyn_act_entry_t` | Zero-cost struct addition; allows mixed compressed/uncompressed registry at runtime |
| `Content-Type` | Always `text/html` regardless of compression | `Content-Encoding` describes the transfer encoding, not the media type |

### Modified Files

| File | Change |
| ---- | ------ |
| `main/CMakeLists.txt` | Add gzip step; populate compressed or raw into `EMBED_FILES`; update registry codegen to emit `b_gzip` |
| `main/inc/dyn_act_registry.h` | Add `uint8_t b_gzip` to `dyn_act_entry_t` |
| `main/src/http_server_dyn.c` | Set `Content-Encoding: gzip` header when `b_gzip == 1` |
| `README.md` | Note compression in "Adding new mini-games" section |
| `docs/4-dynamic_activities_plan.md` | Update Flash Storage Constraints to reflect compressed sizes |

---

### Phase 9.1 — CMake Build Infrastructure

#### Goal

Extend `main/CMakeLists.txt` to compress each HTML file with `gzip` at configure time,
store the `.html.gz` output in the build tree, embed the compressed blobs via
`EMBED_FILES`, and update the auto-generated `dyn_act_registry.c` to set `b_gzip = 1`
for compressed entries and `b_gzip = 0` for uncompressed ones.

#### Inputs

- `main/CMakeLists.txt` (Feature 8 output)
- `main/dyn_activities/*.html` (existing HTML files)

#### Tasks

1. **Verify `gzip` availability** — add the following block immediately after the
   existing `file(GLOB DYN_ACT_HTMLS ...)` call:

   ```cmake
   find_program(_GZIP_EXEC gzip REQUIRED)
   if(NOT _GZIP_EXEC)
       message(FATAL_ERROR "gzip not found on PATH. Install gzip to build this project.")
   endif()
   ```

   `find_program(... REQUIRED)` is supported in CMake ≥ 3.18 (ESP-IDF 5.x ships ≥ 3.20).

2. **Create the compressed-output directory**:

   ```cmake
   set(_DYN_GZ_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/gz")
   file(MAKE_DIRECTORY "${_DYN_GZ_DIR}")
   ```

3. **Define the opt-out list** — place this immediately before the `foreach` loop so
   it is easy to locate and extend:

   ```cmake
   # Files listed here (basenames only, e.g. "dyn_activity1.html") are embedded
   # uncompressed.  All other HTML files in dyn_activities/ are gzip-compressed.
   set(DYN_ACT_NO_COMPRESS "")
   ```

4. **Replace the existing `foreach` loop** with the new version that handles both
   compressed and uncompressed files.  The loop must:

   a. For each `_HTML` in `DYN_ACT_HTMLS`:
      - Get `_FNAME_FULL` (e.g. `dyn_activity1.html`) and `_FNAME` (e.g. `dyn_activity1`).
      - Check whether `_FNAME_FULL` appears in `DYN_ACT_NO_COMPRESS`.
      - **Compressed path** (default): set `_GZ_OUT` to
        `"${_DYN_GZ_DIR}/${_FNAME_FULL}.gz"`, run
        `execute_process(COMMAND gzip -9 -c "${_HTML}" OUTPUT_FILE "${_GZ_OUT}")`,
        derive the ESP-IDF symbol name from `_FNAME_FULL.gz`, append the embedded
        file path to `_EMBED_LIST`, emit `b_gzip = 1` in the registry entry.
      - **Uncompressed path** (opt-out): use `_HTML` directly; derive symbol from
        `_FNAME_FULL`; emit `b_gzip = 0`.

   Full CMake snippet for the loop:

   ```cmake
   set(_EMBED_LIST "")

   foreach(_HTML ${DYN_ACT_HTMLS})
       get_filename_component(_FNAME_FULL "${_HTML}" NAME)      # dyn_activity1.html
       get_filename_component(_FNAME      "${_HTML}" NAME_WE)   # dyn_activity1

       list(FIND DYN_ACT_NO_COMPRESS "${_FNAME_FULL}" _NO_COMPRESS_IDX)

       if(_NO_COMPRESS_IDX EQUAL -1)
           # --- compressed ---
           set(_GZ_OUT "${_DYN_GZ_DIR}/${_FNAME_FULL}.gz")
           execute_process(
               COMMAND "${_GZIP_EXEC}" -9 -c "${_HTML}"
               OUTPUT_FILE "${_GZ_OUT}"
               RESULT_VARIABLE _GZIP_RESULT)
           if(NOT _GZIP_RESULT EQUAL 0)
               message(FATAL_ERROR "gzip failed for ${_HTML} (exit code ${_GZIP_RESULT})")
           endif()
           list(APPEND _EMBED_LIST "${_GZ_OUT}")
           set(_EMBED_FILE "${_GZ_OUT}")
           set(_B_GZIP "1")
       else()
           # --- uncompressed (opt-out) ---
           list(APPEND _EMBED_LIST "${_HTML}")
           set(_EMBED_FILE "${_HTML}")
           set(_B_GZIP "0")
       endif()

       # Derive ESP-IDF EMBED_FILES symbol name from the filename only.
       get_filename_component(_EMBED_FNAME "${_EMBED_FILE}" NAME)
       string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" _SYM "${_EMBED_FNAME}")

       string(APPEND _EXT
           "extern const uint8_t _binary_${_SYM}_start[];\n"
           "extern const uint8_t _binary_${_SYM}_end[];\n")
       string(APPEND _TBL
           "    { \"${_FNAME}\","
           " _binary_${_SYM}_start,"
           " _binary_${_SYM}_end,"
           " ${_B_GZIP}U },\n")
       math(EXPR _CNT "${_CNT} + 1")
   endforeach()
   ```

5. **Update `idf_component_register(...)`** — replace `EMBED_FILES ${DYN_ACT_HTMLS}` with
   `EMBED_FILES ${_EMBED_LIST}`.  No other changes to this call are needed.

#### Notes

> `execute_process()` runs at CMake **configure** time, so the compressed files exist
> before the compiler is invoked.  No `add_custom_command` or `add_custom_target` is
> needed.

> The compressed output files live under `${CMAKE_CURRENT_BINARY_DIR}/generated/gz/`,
> which is already inside the build tree and therefore covered by the `.gitignore` entry
> for `build/`.

> `gzip -9 -c` writes to stdout; the output is redirected via `OUTPUT_FILE`.  The `-c`
> flag leaves the source file untouched.

> **Re-compression on source change**: `execute_process()` runs unconditionally on every
> CMake configure.  In normal development this is triggered by `idf.py reconfigure`, which
> the developer already runs after adding or modifying activity files.  For full
> incremental compression (re-run only when the source changes), a `add_custom_command`
> approach would be needed; this is out of scope for the current feature.

> If a `.html.gz` file already exists from a previous configure, `execute_process` with
> `OUTPUT_FILE` silently overwrites it — this is the desired behaviour.

#### Acceptance Criteria

- [ ] `idf.py reconfigure && idf.py build` succeed with zero errors and zero warnings.
- [ ] `${CMAKE_CURRENT_BINARY_DIR}/generated/gz/dyn_activity1.html.gz` and
      `dyn_activity2.html.gz` exist after configure.
- [ ] Each `.html.gz` file is a valid gzip stream (`file dyn_activity1.html.gz` reports
      `gzip compressed data`).
- [ ] `EMBED_FILES` contains the `.html.gz` paths (not the raw `.html` paths) for
      compressed entries.
- [ ] A file listed in `DYN_ACT_NO_COMPRESS` is embedded as the raw `.html`; its
      registry entry has `b_gzip = 0`.
- [ ] Removing an HTML file and running `idf.py reconfigure` removes it from the registry.
- [ ] Build fails with `FATAL_ERROR` if `gzip` is not on the `PATH`.

---

### Phase 9.2 — Registry Header Update

#### Goal

Add a `b_gzip` field to `dyn_act_entry_t` in `main/inc/dyn_act_registry.h` so the HTTP
handler can inspect it at runtime without hard-coding knowledge of which files are
compressed.

#### Inputs

- `main/inc/dyn_act_registry.h` (Feature 8 output)

#### Tasks

1. **Add `uint8_t b_gzip`** to `dyn_act_entry_t`:

   ```c
   typedef struct dyn_act_entry_tag
   {
       const char *    p_name; /**< Logical name (filename without path or extension). */
       const uint8_t * p_data; /**< Pointer to the first byte of the embedded data.    */
       const uint8_t * p_end;  /**< Pointer one past the last byte (size = p_end - p_data). */
       uint8_t         b_gzip; /**< Non-zero if the embedded data is gzip-compressed.  */
   } dyn_act_entry_t;
   ```

2. **Update the file-level Doxygen comment** to mention that `b_gzip` is set by the
   CMake code generator and that the HTTP handler uses it to decide the
   `Content-Encoding` response header.

3. No other changes to this file are required.

#### Acceptance Criteria

- [ ] `dyn_act_entry_t` has four fields: `p_name`, `p_data`, `p_end`, `b_gzip`.
- [ ] `idf.py build` succeeds; the auto-generated `dyn_act_registry.c` compiles without
      warnings (`b_gzip` initialiser present for every entry).
- [ ] No other source file needs modification solely because of this struct change
      (the only consumer of the struct besides the registry itself is `http_server_dyn.c`,
      updated in Phase 9.3).

---

### Phase 9.3 — HTTP Handler Update

#### Goal

Update `http_srv_dyn_file_get_handler()` in `main/src/http_server_dyn.c` to send
`Content-Encoding: gzip` when serving a compressed entry, and send no `Content-Encoding`
header for uncompressed entries.

#### Inputs

- `main/src/http_server_dyn.c` (Feature 8 output)
- `main/inc/dyn_act_registry.h` (Phase 9.2 output)

#### Tasks

1. **In the registry match block**, replace the current response send sequence:

   ```c
   httpd_resp_set_type(p_req, "text/html");
   httpd_resp_send(p_req, (const char *)g_dyn_act_registry[i].p_data,
       (ssize_t)(g_dyn_act_registry[i].p_end - g_dyn_act_registry[i].p_data));
   ```

   with:

   ```c
   httpd_resp_set_type(p_req, "text/html");
   if (g_dyn_act_registry[i].b_gzip != 0U)
   {
       httpd_resp_set_hdr(p_req, "Content-Encoding", "gzip");
   }
   httpd_resp_send(p_req, (const char *)g_dyn_act_registry[i].p_data,
       (ssize_t)(g_dyn_act_registry[i].p_end - g_dyn_act_registry[i].p_data));
   ```

2. No other changes to this file are required.

#### Notes

> `httpd_resp_set_hdr()` must be called **before** `httpd_resp_send()` because
> `httpd_resp_send()` flushes the complete response (headers + body) in one call.

> No `Accept-Encoding` check is performed.  All browsers on the home LAN support gzip;
> the added complexity of a fallback path is not justified.

> The `Content-Type` remains `text/html` for both compressed and uncompressed files.
> `Content-Encoding` is a transfer-encoding header, not a media-type modifier.

#### Acceptance Criteria

- [ ] `GET /dyn_activities/<name>` for a compressed entry returns HTTP 200 with headers:
      `Content-Type: text/html` and `Content-Encoding: gzip`.
- [ ] `GET /dyn_activities/<name>` for an uncompressed entry returns HTTP 200 with header:
      `Content-Type: text/html` and **no** `Content-Encoding` header.
- [ ] A browser can load and render a compressed activity page without errors.
- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] No other handler or route is affected.

---

### Phase 9.4 — Integration & Verification

#### Goal

Verify the complete end-to-end flow: compressed HTML embedded in firmware, served with the
correct headers, rendered correctly by a browser, and capable of completing a full
credit-claim flow.

#### Inputs

- All Phase 9.1–9.3 outputs.
- Existing Feature 8 infrastructure (Phases 8.1–8.5 complete).

#### Tasks

1. **Build**: run `idf.py reconfigure && idf.py build`.  Confirm zero errors and zero
   warnings.

2. **Verify compressed artefacts**:
   - Check that `build/generated/gz/dyn_activity1.html.gz` exists.
   - Run `file build/generated/gz/dyn_activity1.html.gz` → must report `gzip compressed
     data`.
   - Run `gzip -d -c build/generated/gz/dyn_activity1.html.gz | head -3` → must output
     valid HTML.

3. **Measure size reduction**:
   - Compare raw `.html` size vs. `.html.gz` size for each activity file.
   - Document ratio in a comment in `main/CMakeLists.txt` or in test results.

4. **HTTP response header test**:
   - Load `http://<AP_IP>/dyn_activities/dyn_activity1` in a browser DevTools **Network**
     tab.
   - Confirm `Content-Encoding: gzip` and `Content-Type: text/html` are present.
   - Confirm the page renders correctly (no garbled text).

5. **Uncompressed opt-out test**:
   - Add `dyn_activity1.html` to `DYN_ACT_NO_COMPRESS` in `CMakeLists.txt`.
   - Run `idf.py reconfigure && idf.py build && idf.py flash`.
   - Load the page; confirm **no** `Content-Encoding` header; page renders correctly.
   - Revert the change.

6. **Credit claim test** (regression):
   - Navigate to `/dyn`; launch a compressed activity; complete the credit-claim POST.
   - Confirm HTTP 200 and correct `new_counter_hms` in the response.

7. **Missing `gzip` test**:
   - Temporarily rename `gzip` on `PATH` (or set `PATH` to exclude it).
   - Run `idf.py reconfigure`; confirm `FATAL_ERROR` message is emitted and configure
     stops.
   - Restore `gzip`.

#### Acceptance Criteria

- [ ] `idf.py build` succeeds with zero errors and zero warnings.
- [ ] `build/generated/gz/*.html.gz` files exist for all non-opted-out HTML files.
- [ ] HTTP response for a compressed activity includes `Content-Encoding: gzip` and
      `Content-Type: text/html`.
- [ ] HTTP response for an uncompressed (opt-out) activity omits `Content-Encoding`.
- [ ] Page loads and renders correctly in Chrome, Firefox, and Safari (or equivalent
      mobile browsers).
- [ ] Full credit-claim flow works end-to-end for a compressed activity (HTTP 200, counter
      updated).
- [ ] Missing `gzip` on host causes `cmake` configure to abort with a clear error.
- [ ] No regression in existing Feature 8 acceptance criteria (Phases 8.1–8.5).

---

### Phase 9.5 — Documentation Update

#### Goal

Update `README.md` and `docs/4-dynamic_activities_plan.md` to reflect that activity HTML
files are gzip-compressed at build time before being embedded in the firmware.

#### Inputs

- All Phase 9.1–9.4 outputs.
- `README.md` (Feature 8 output)
- `docs/4-dynamic_activities_plan.md` (current)

#### Tasks

1. **`README.md` — "Adding new mini-games" section**:
   - After the existing step list, add a note that files are automatically gzip-compressed
     at build time and that no manual compression step is needed.
   - Mention the `DYN_ACT_NO_COMPRESS` opt-out variable for edge cases.

2. **`docs/4-dynamic_activities_plan.md` — Flash Storage Constraints section**:
   - Update the per-file size budget table to show both uncompressed and expected
     compressed sizes (adding a "Compressed target" column).
   - Add a note that the hard-maximum figures apply to the **uncompressed** source file
     size (the authoritative limit for firmware build-time flash use is the compressed
     size, but the source size cap is retained to keep files readable and maintainable).

3. No changes to `docs/1-specification.md` are required: compression is a build
   infrastructure detail not visible in the HTTP API contract (from the client's
   perspective the response is still `text/html`; `Content-Encoding` is a hop-by-hop
   transport detail fully handled by the browser).

#### Acceptance Criteria

- [ ] `README.md` "Adding new mini-games" mentions automatic gzip compression and the
      `DYN_ACT_NO_COMPRESS` opt-out.
- [ ] `docs/4-dynamic_activities_plan.md` Flash Storage Constraints table includes a
      compressed size column.
- [ ] No other documentation sections contradict the compression behaviour.

---

### Phase 9.6 — Commit Message

```
feat: gzip-compress dynamic activity HTML files before embedding

Reduces flash consumption of dynamic activity pages by compressing them
with gzip at CMake configure time and embedding the compressed blobs in
the firmware binary.  The HTTP server sends the compressed bytes directly
with Content-Encoding: gzip; the browser decompresses transparently.

Build:
- main/CMakeLists.txt: find_program(gzip REQUIRED); compress each HTML
  file to ${CMAKE_CURRENT_BINARY_DIR}/generated/gz/<name>.html.gz using
  execute_process(COMMAND gzip -9 -c ...) at configure time.
- DYN_ACT_NO_COMPRESS list variable allows per-file opt-out; opted-out
  files are embedded raw (b_gzip = 0 in the registry).
- EMBED_FILES now references the .html.gz paths for compressed entries.
- Auto-generated dyn_act_registry.c emits b_gzip initialiser per entry.

Registry:
- dyn_act_entry_t gains a uint8_t b_gzip field (1 = compressed, 0 = raw).

HTTP server:
- http_srv_dyn_file_get_handler(): sets Content-Encoding: gzip before
  httpd_resp_send() when b_gzip != 0; no header added for raw entries.
- No Accept-Encoding inspection — all browsers on the home LAN support
  gzip.

Documentation:
- README.md: note automatic compression and DYN_ACT_NO_COMPRESS opt-out.
- docs/4-dynamic_activities_plan.md: add compressed-size column to flash
  budget table.
```
