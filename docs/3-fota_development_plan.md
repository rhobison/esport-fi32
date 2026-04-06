# esport-fi32 FOTA Development Plan

**Version:** 2.0
**Date:** 2026-04-06
**Executor:** AI coding agents
**Spec reference:** `docs/1-specification.md`, `docs/2-development_plan.md`

---

## Overview

This plan describes how to add **Firmware Over-The-Air (FOTA) update** capability to the
esport-fi32 firmware.  The implementation is organised as a sequence of self-contained phases
that can be executed at any time after `docs/2-development_plan.md` Phase 10 is complete.

Design goals:
- Keep all FOTA code in dedicated files (`ota_manager.c/h`, `http_server_ota.c/h`) so the
  feature can be developed and reverted without touching business logic.
- Minimise changes to existing files: only `http_server.c`, `main.c`, and
  `main/CMakeLists.txt` require small additions.
- Protect the upload endpoint with HTTP Basic Auth (configurable password stored in a
  dedicated NVS namespace `esport_ota`).
- Enable automatic rollback so a corrupt or crashing image is discarded on the next boot.

### Phase Quick Reference

| Phase | Name                               | Key output files                                    |
| ----- | ---------------------------------- | --------------------------------------------------- |
| F0    | Flash Feasibility Analysis         | (analysis only; no code)                            |
| F1    | Partition Table Migration          | `partitions.csv`, `sdkconfig.defaults`              |
| F2    | OTA Manager Module                 | `ota_manager.c/h`                                   |
| F3    | HTTP OTA Handlers                  | `http_server_ota.c/h`                               |
| F4    | HTTP Server & Build Integration    | `http_server.c` (additions), `main/CMakeLists.txt`  |
| F5    | Boot Sequence Integration          | `main.c` (one added call)                           |
| F6    | Integration & Verification         | end-to-end test checklist                           |

---

## Phase F0 -- Flash Feasibility Analysis

### Goal

Verify that FOTA is possible on the target hardware given the current firmware size and flash
layout.  This phase is analysis only; no source files change.

### Findings

**Target:** ESP32-C6 with **4 MB SPI flash** (confirmed via
`CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` in `sdkconfig`).

**Current partition layout (single-app, no OTA):**

| Name       | Type | Sub-type | Offset    | Size    |
| ---------- | ---- | -------- | --------- | ------- |
| nvs        | data | nvs      | 0x009000  | 24 KB   |
| phy_init   | data | phy      | 0x00F000  | 4 KB    |
| factory    | app  | factory  | 0x010000  | 1024 KB |

> **Note:** The original feasibility analysis proposed 24 KB NVS.  The implemented design
> uses **48 KB NVS** to allow for future feature growth (see revised proposal below).

**Current firmware binary size:** ~978 KB (`build/esport-fi32.bin`).

**OTA requirement:**
- OTA requires a minimum of two equal-sized application partitions (`ota_0`, `ota_1`) plus an
  `otadata` partition (8 KB) to track which slot is active.
- The currently used `factory` partition type does **not** support rollback; the image must
  be in an `ota_0`/`ota_1` slot for rollback to work.

**Proposed OTA partition layout (initial analysis -- 24 KB NVS, not implemented):**

| Name       | Type | Sub-type | Offset    | Size    | Notes                              |
| ---------- | ---- | -------- | --------- | ------- | ---------------------------------- |
| nvs        | data | nvs      | 0x009000  | 24 KB   | unchanged -- NVS data is preserved |
| otadata    | data | ota      | 0x00F000  | 8 KB    | new -- tracks active slot          |
| phy_init   | data | phy      | 0x011000  | 4 KB    | unchanged (shifted by 8 KB)        |
| ota_0      | app  | ota_0    | 0x012000  | 2012 KB | not used -- see revised layout     |
| ota_1      | app  | ota_1    | 0x209000  | 2012 KB | not used -- see revised layout     |

> **Revised OTA partition layout (implemented -- 80 KB NVS):**
>
> This layout was chosen to maximise NVS space while wasting zero flash bytes.  The
> 32 KB gap that would otherwise sit unused between `phy_init` and the 64 KB-aligned
> `ota_0` boundary (0x18000-0x1FFFF) is absorbed into the NVS partition by moving
> `otadata` and `phy_init` forward so they sit immediately before `ota_0`.  The result
> is 80 KB NVS with **zero unused flash** across the full 4 MB device.

| Name       | Type | Sub-type | Offset    | Size    | Notes                              |
| ---------- | ---- | -------- | --------- | ------- | ---------------------------------- |
| nvs        | data | nvs      | 0x009000  | 80 KB   | maximised -- zero wasted flash     |
| otadata    | data | ota      | 0x01D000  | 8 KB    | new -- tracks active slot          |
| phy_init   | data | phy      | 0x01F000  | 4 KB    | immediately before ota_0           |
| ota_0      | app  | ota_0    | 0x020000  | 1984 KB | new -- first app slot              |
| ota_1      | app  | ota_1    | 0x210000  | 1984 KB | new -- second app slot             |

Total flash used: exactly 4096 KB (100% utilised, every byte accounted for).

**Feasibility verdict: FEASIBLE.**

- Each slot is 1984 KB.  Current app binary is ~1000 KB, leaving **~984 KB (50%) headroom**
  per slot for future growth.
- NVS is maximised at 80 KB: the original 48 KB plus the 32 KB gap that would otherwise
  sit idle between `phy_init` and the mandatory 64 KB alignment boundary of `ota_0`.
- `phy_init` sits at 0x01F000, immediately before `ota_0` at 0x020000.  Phy calibration
  is regenerated automatically on first boot after reflash; this is harmless.
- All partition offsets are multiples of the 4 KB flash erase-sector size.
- `ota_0` is at 0x020000, satisfying the 64 KB MMU-page alignment required by ESP32-C6.

**Important note on the first FOTA flash:**

The migration from the single-app to the OTA layout requires a **one-time manual reflash of
the entire device** using `idf.py erase-flash flash` (erases and reprograms the entire flash,
including the partition table and the new firmware into `ota_0`).  After this initial flash,
all subsequent updates can be delivered over the air.  Because the partition table changes, a
simple app-only OTA from the old single-app firmware to the new OTA firmware is **not**
supported; a full flash reflash is required exactly once.  NVS data at 0x009000 is preserved
because the NVS partition starts at the same offset; the phy_init partition has moved, so phy
calibration is regenerated on first boot (harmless).

---

## Phase F1 -- Partition Table Migration

### Goal

Replace the built-in `partitions_singleapp.csv` with a custom `partitions.csv` that defines
the two-OTA layout derived in Phase F0.  Enable the rollback bootloader option in
`sdkconfig.defaults`.  No application source code changes.

### Inputs

- Phase F0 findings (offsets and sizes above).
- `sdkconfig` (current settings).
- `sdkconfig.defaults` (current build-time overrides).

### Tasks

1. **Create `partitions.csv`** in the project root with the following content:

   ```
   # esport-fi32 custom partition table -- OTA layout
   # Name,     Type, SubType,  Offset,    Size,  Flags
   nvs,        data, nvs,      0x9000,    80K,
   otadata,    data, ota,      0x1D000,   8K,
   phy_init,   data, phy,      0x1F000,   4K,
   ota_0,      app,  ota_0,    0x20000,   1984K,
   ota_1,      app,  ota_1,    0x210000,  1984K,
   ```

   Constraints:
   - App partition offsets must be 64 KB-aligned (0x10000 boundary) on ESP32-C6.
   - `ota_0` and `ota_1` must be equal in size.
   - `otadata` must be exactly 0x2000 (8 KB) -- ESP-IDF requirement.
   - `otadata` and `phy_init` are packed immediately before `ota_0` so the 32 KB region
     0x18000-0x1FFFF is fully absorbed by NVS, leaving zero unused flash bytes.

2. **Update `sdkconfig.defaults`** to switch to the custom partition table and enable
   rollback:

   ```
   # Partition table
   CONFIG_PARTITION_TABLE_CUSTOM=y
   CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

   # OTA rollback: if new firmware does not call
   # esp_ota_mark_app_valid_cancel_rollback(), the bootloader
   # reverts to the previous slot on the next boot.
   CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
   ```

   Do **not** manually edit the `sdkconfig` file.  The `sdkconfig.defaults` entries will
   apply on the next `idf.py reconfigure` or clean build.

3. **Delete `sdkconfig` and run `idf.py reconfigure`** to regenerate it from
   `sdkconfig.defaults`.  Verify that the generated partition table matches the Phase F0
   offsets by running `idf.py partition-table` and inspecting the output.
   (`sdkconfig` is gitignored; deleting it is safe and necessary when its partition-table
   keys must be overridden.)

4. **Run `idf.py build`** to confirm the firmware binary fits inside `ota_0` (size must
   be < 1984 KB = 2031616 bytes).  The build output reports the binary size and headroom.

### Files Changed

| File                 | Change type |
| -------------------- | ----------- |
| `partitions.csv`     | new file    |
| `sdkconfig.defaults` | modified    |
| `sdkconfig`          | regenerated (do not commit) |

### Acceptance Criteria

- [x] `idf.py build` succeeds.
- [x] `idf.py partition-table` output matches the table in Phase F0 exactly (same offsets and
      sizes).
- [x] `build/esport-fi32.bin` size is less than 2031616 bytes (1984 KB).
- [x] `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` appears in `sdkconfig` after reconfigure.
- [x] `CONFIG_PARTITION_TABLE_CUSTOM=y` and
      `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"` appear in `sdkconfig`.
- [x] `idf.py partition-table` shows zero unused flash bytes (4060 KB of partitions
      + 36 KB bootloader/PT area = 4096 KB total).

---

## Phase F2 -- OTA Manager Module

### Goal

Implement a self-contained `ota_manager` module that wraps ESP-IDF's `esp_ota_ops` API.
This module owns the OTA state machine, NVS credential storage (OTA password), firmware-valid
marking for rollback, and all version-string retrieval.  It has no dependencies on any other
esport-fi32 module except `esp_ota_ops` and `nvs_flash`.

### Inputs

- `docs/1-specification.md` §11 (coding conventions).
- `docs/2-development_plan.md` Module Prefix Table.
- ESP-IDF `esp_ota_ops.h` (`esp_ota_begin`, `esp_ota_write`, `esp_ota_end`,
  `esp_ota_set_boot_partition`, `esp_ota_mark_app_valid_cancel_rollback`,
  `esp_ota_get_running_partition`, `esp_ota_get_next_update_partition`).
- ESP-IDF `esp_app_desc.h` (`esp_app_get_description`).
- Phase F1 output (OTA partition table, rollback enabled).

### Module Prefix

| Source file    | Function / type prefix | Macro / enum prefix |
| -------------- | ---------------------- | ------------------- |
| `ota_manager`  | `ota_mngr_`            | `OTA_MNGR_`         |

### NVS Namespace

`esport_ota` -- separate from the main `esport_cfg` namespace so the OTA feature can be
developed and removed without affecting the main configuration.

| Key       | Type   | Default        | Description                         |
| --------- | ------ | -------------- | ----------------------------------- |
| `ota_pwd` | string | `"esport-fi32"` | Password for HTTP Basic Auth on /ota |

The username is fixed: `"admin"` (hardcoded constant `OTA_MNGR_HTTP_USERNAME`).

### State Machine

```
OTA_MNGR_STATE_IDLE
  ota_mngr_begin() called
      v
OTA_MNGR_STATE_RECEIVING
  ota_mngr_write() called repeatedly
      v
OTA_MNGR_STATE_VERIFYING
  ota_mngr_end() called -- finalises write, checks image header magic
      v (success)                 v (failure)
OTA_MNGR_STATE_READY         OTA_MNGR_STATE_IDLE
  ota_mngr_activate() called        (ota_mngr_abort() resets to IDLE from any state)
      v
  esp_ota_set_boot_partition()
  esp_restart()
```

`ota_mngr_activate()` is called by the HTTP handler immediately after `ota_mngr_end()`
returns `ESP_OK`.  It sets the new boot partition and calls `esp_restart()`.  It does not
return on success.

### Tasks

1. **Create `main/inc/ota_manager.h`**:

   ```c
   /**
    * \file
    * \brief OTA firmware update manager public API.
    *
    * Wraps ESP-IDF esp_ota_ops to provide a simple write-chunk interface
    * used by the HTTP OTA handler.  Manages password storage in NVS
    * namespace \c esport_ota.  Marks the running firmware valid at init
    * time to cancel any pending rollback.
    *
    * \date 2026-04-06
    */
   ```

   Public API to declare:

   ```c
   /** Maximum length of the OTA password (excluding null terminator). */
   #define OTA_MNGR_PASSWORD_MAX_LEN  (63U)

   /** Fixed HTTP Basic Auth username for the OTA endpoint. */
   #define OTA_MNGR_HTTP_USERNAME     ("admin")

   /**
    * \brief OTA manager state.
    */
   typedef enum ota_mngr_state_tag
   {
       OTA_MNGR_STATE_IDLE       = 0, /**< No update in progress. */
       OTA_MNGR_STATE_RECEIVING  = 1, /**< Receiving firmware chunks. */
       OTA_MNGR_STATE_VERIFYING  = 2, /**< Finalising write, verifying header. */
       OTA_MNGR_STATE_READY      = 3, /**< Image verified; ready to activate. */
   } ota_mngr_state_t;

   esp_err_t ota_mngr_init(void);
   esp_err_t ota_mngr_begin(size_t image_size);
   esp_err_t ota_mngr_write(const void *p_data, size_t len);
   esp_err_t ota_mngr_end(void);
   esp_err_t ota_mngr_abort(void);
   void      ota_mngr_activate(void);  /* does not return on success */

   ota_mngr_state_t ota_mngr_state_get(void);

   void      ota_mngr_running_version_get(char *p_buf, size_t len);

   bool      ota_mngr_credentials_check(const char *p_password);
   void      ota_mngr_password_get(char *p_buf, size_t len);
   esp_err_t ota_mngr_password_set(const char *p_password);
   ```

   Header must follow `hhtemplate` structure with `#ifndef OTA_MANAGER_H` guard.

2. **Create `main/src/ota_manager.c`**:

   - Follow `cctemplate` structure.

   - `ota_mngr_init()`:
     - Call `esp_ota_mark_app_valid_cancel_rollback()` unconditionally.  Log the result with
       `ESP_LOGI` but do not fail if it returns an error (e.g. rollback not supported on a
       `factory`-type partition); this handles the clean-build / non-OTA case gracefully.
     - Open NVS namespace `esport_ota` (`NVS_READWRITE`).
     - Read `ota_pwd`; if `ESP_ERR_NVS_NOT_FOUND`, write the default `"esport-fi32"` and commit.
     - Store the current NVS handle in a static variable `g_nvs_handle`.
     - Log the running app version: `"running fw v%s"` from
       `esp_app_get_description()->version`.
     - Return `ESP_OK`.

   - `ota_mngr_begin(size_t image_size)`:
     - If `g_state != OTA_MNGR_STATE_IDLE`, return `ESP_ERR_INVALID_STATE`.
     - Call `esp_ota_get_next_update_partition(NULL)` to obtain the inactive slot.
     - If `NULL` is returned (no OTA partition found -- e.g. still on single-app layout),
       log an error and return `ESP_ERR_NOT_SUPPORTED`.
     - Call `esp_ota_begin(p_update_partition, image_size, &g_ota_handle)`.  Pass
       `OTA_WITH_SEQUENTIAL_WRITES` as `image_size` when the caller passes 0 (unknown size).
     - Set `g_state = OTA_MNGR_STATE_RECEIVING`.
     - Return result of `esp_ota_begin`.

   - `ota_mngr_write(const void *p_data, size_t len)`:
     - If `g_state != OTA_MNGR_STATE_RECEIVING`, return `ESP_ERR_INVALID_STATE`.
     - Call `esp_ota_write(g_ota_handle, p_data, len)`.
     - Return result.

   - `ota_mngr_end()`:
     - If `g_state != OTA_MNGR_STATE_RECEIVING`, return `ESP_ERR_INVALID_STATE`.
     - Set `g_state = OTA_MNGR_STATE_VERIFYING`.
     - Call `esp_ota_end(g_ota_handle)`.  On failure, call `ota_mngr_abort()` and return
       the error.
     - On success, set `g_state = OTA_MNGR_STATE_READY` and return `ESP_OK`.

   - `ota_mngr_abort()`:
     - If `g_state == OTA_MNGR_STATE_IDLE`, return `ESP_OK` (idempotent).
     - If `g_state == OTA_MNGR_STATE_RECEIVING`, call `esp_ota_abort(g_ota_handle)`.
     - Reset `g_state = OTA_MNGR_STATE_IDLE`, zero `g_ota_handle`.
     - Return `ESP_OK`.

   - `ota_mngr_activate()`:
     - If `g_state != OTA_MNGR_STATE_READY`, log error and return without restarting.
     - Call `esp_ota_set_boot_partition(p_update_partition)`.  If it fails, log error,
       call `ota_mngr_abort()`, and return without restarting.
     - Log `"OTA update complete, rebooting..."`.
     - Call `esp_restart()` -- does not return.

   - `ota_mngr_running_version_get(char *p_buf, size_t len)`:
     - Call `snprintf(p_buf, len, "%s", esp_app_get_description()->version)`.

   - `ota_mngr_credentials_check(const char *p_password)`:
     - Read the stored password via `ota_mngr_password_get()`.
     - Return `true` if strings match (case-sensitive), `false` otherwise.
     - Never log the supplied password.

   - `ota_mngr_password_get(char *p_buf, size_t len)`:
     - Read `ota_pwd` from `g_nvs_handle`.  On failure, copy the default password and return.

   - `ota_mngr_password_set(const char *p_password)`:
     - Reject if `strlen(p_password) == 0` or `strlen(p_password) > OTA_MNGR_PASSWORD_MAX_LEN`
       (return `ESP_ERR_INVALID_ARG`).
     - Write `ota_pwd` to NVS and commit.  Return result.

   - `ota_mngr_state_get()`: return `g_state`.

   - Static variables required:
     ```c
     static const char *      gp_tag             = "ota_manager";
     static ota_mngr_state_t  g_state            = OTA_MNGR_STATE_IDLE;
     static esp_ota_handle_t  g_ota_handle        = 0;
     static const esp_partition_t * gp_update_partition = NULL;
     static nvs_handle_t      g_nvs_handle        = 0;
     ```

### Files Created

| File                      | Change type |
| ------------------------- | ----------- |
| `main/inc/ota_manager.h`  | new file    |
| `main/src/ota_manager.c`  | new file    |

### Acceptance Criteria

- [x] `idf.py build` succeeds.
- [x] `ota_mngr_init()` logs the running firmware version.
- [x] `ota_mngr_begin()` returns `ESP_ERR_NOT_SUPPORTED` when no OTA partition exists.
- [x] `ota_mngr_end()` transitions state to `OTA_MNGR_STATE_READY` on success.
- [x] `ota_mngr_abort()` is idempotent (safe to call in any state).
- [x] `ota_mngr_credentials_check("wrong")` returns `false`.
- [x] `ota_mngr_password_set("")` returns `ESP_ERR_INVALID_ARG`.
- [x] All functions follow `cctemplate` structure; `gp_tag` is present.
- [x] All symbols carry the `ota_mngr_` prefix.

---

## Phase F3 -- HTTP OTA Handlers

### Goal

Implement `http_server_ota.c/h`: four HTTP handlers that together provide a firmware upload
page, a Basic-Auth-protected upload endpoint, and a password management sub-page.  No other
esport-fi32 module besides `ota_manager` is used.

### Inputs

- `main/inc/ota_manager.h` (Phase F2 output).
- `main/inc/http_server_utils.h` (existing; provides `HTTP_SRV_HTML_BUF_LEN`
  and `HTTP_SRV_JSON_BUF_LEN` constants).
- `docs/1-specification.md` §11 (coding conventions).

### Module Prefix

`http_server_ota` shares the `http_srv_` prefix consistent with all other `http_server_*`
files.  All symbols in this file must be prefixed `http_srv_ota_` (functions/types) or
`HTTP_SRV_OTA_` (macros/enums).

### Routes

| Method | URI         | Description                                       | Auth required |
| ------ | ----------- | ------------------------------------------------- | ------------- |
| GET    | `/ota`      | Serve upload form HTML page                       | Yes           |
| POST   | `/ota`      | Receive binary firmware upload and flash it       | Yes           |
| GET    | `/ota/pwd`  | Serve password change form                        | Yes           |
| POST   | `/ota/pwd`  | Save new OTA password                             | Yes           |

### Basic Auth Helper (internal static)

Declare in the `Internal Function Prototypes` section:

```c
static bool http_srv_ota_auth_check(httpd_req_t *p_req);
```

Implementation:
- Call `httpd_req_get_hdr_value_len(p_req, "Authorization")` to detect the header presence.
- If absent or length is 0: respond with HTTP 401, header
  `WWW-Authenticate: Basic realm="esport-fi32 OTA"`, body `"Unauthorized"`, return `false`.
- Read header value into a stack buffer (max 256 bytes).
- Verify it starts with `"Basic "` (6 chars).  If not: respond 401, return `false`.
- Base64-decode the remainder into a `"user:password"` string.
  Use a minimal in-place Base64 decoder (implement as a separate `static` helper
  `http_srv_ota_base64_decode(const char *p_in, char *p_out, size_t out_len)`).
- Split on the first `:` character.  The part before `:` is the username; the part after
  is the password.
- Check that username equals `OTA_MNGR_HTTP_USERNAME` (string compare).
- Call `ota_mngr_credentials_check(password_part)`.
- If either check fails: respond 401, return `false`.
- Return `true` (auth passed).

### Base64 Decoder

Implement `http_srv_ota_base64_decode` as a `static` function.  It must handle standard
RFC 4648 Base64 (uppercase + lowercase + `+` + `/` + `=` padding).  The output is a
null-terminated string.  Return the number of decoded bytes, or -1 on invalid input.  No
external library; implement with a look-up table.

### Tasks

1. **Create `main/inc/http_server_ota.h`**:

   ```c
   esp_err_t http_srv_ota_get_handler(httpd_req_t *p_req);
   esp_err_t http_srv_ota_post_handler(httpd_req_t *p_req);
   esp_err_t http_srv_ota_pwd_get_handler(httpd_req_t *p_req);
   esp_err_t http_srv_ota_pwd_post_handler(httpd_req_t *p_req);
   ```

   Guard with `HTTP_SERVER_OTA_H`; follow `hhtemplate` structure.

2. **Create `main/src/http_server_ota.c`**:

   a. **`http_srv_ota_get_handler` (GET /ota)**:
      - Call `http_srv_ota_auth_check(p_req)`.  If it returns `false`, return `ESP_OK`
        (the 401 response was already sent by the auth helper).
      - Use `httpd_resp_set_type(p_req, "text/html")`.
      - Send a self-contained HTML page via `httpd_resp_sendstr_chunk()` containing:
        - Page title: `"ESPort-fi32 -- Firmware Update"`.
        - Section "Running Firmware": displays `ota_mngr_running_version_get()` result.
        - Section "Upload New Firmware":
          - A `<progress>` bar (id `"upload-progress"`, hidden initially).
          - A `<input type="file" id="fw-file" accept=".bin">` selector.
          - A `"Flash Firmware"` button (id `"flash-btn"`).
          - A `<div id="upload-status">` for status messages.
        - Section "OTA Password": a link `<a href="/ota/pwd">Change OTA password</a>`.
        - Section "Navigation": link back to `<a href="/">Dashboard</a>`.
        - Inline `<script>`:
          ```javascript
          document.getElementById('flash-btn').addEventListener('click', function() {
            var f = document.getElementById('fw-file').files[0];
            if (!f) { document.getElementById('upload-status').textContent =
              'Please select a .bin file.'; return; }
            var xhr = new XMLHttpRequest();
            xhr.open('POST', '/ota', true);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            xhr.upload.onprogress = function(e) {
              if (e.lengthComputable) {
                var p = document.getElementById('upload-progress');
                p.style.display = 'block';
                p.value = e.loaded; p.max = e.total;
              }
            };
            xhr.onload = function() {
              document.getElementById('upload-status').textContent =
                (xhr.status === 200) ? 'Update successful. Rebooting...' :
                'Error: ' + xhr.responseText;
            };
            xhr.onerror = function() {
              document.getElementById('upload-status').textContent =
                'Connection lost (device is rebooting).';
            };
            xhr.send(f);
          });
          ```
        - Terminate chunked response with `httpd_resp_sendstr_chunk(p_req, NULL)`.
      - Return `ESP_OK`.

   b. **`http_srv_ota_post_handler` (POST /ota)**:
      - Call `http_srv_ota_auth_check(p_req)`.  If it returns `false`, return `ESP_OK`.
      - Read `Content-Length` header.  If present and parseable, pass the value as
        `image_size` to `ota_mngr_begin()`; otherwise pass `OTA_WITH_SEQUENTIAL_WRITES`.
      - Call `ota_mngr_begin(image_size)`.  On failure, send HTTP 500 with the
        `esp_err_to_name()` string and return `ESP_FAIL`.
      - Allocate a stack buffer of `OTA_MNGR_RECV_BUF_SIZE` bytes
        (`#define OTA_MNGR_RECV_BUF_SIZE (1024U)` -- defined in `http_server_ota.c`
        Internal Constants section).
      - Loop:
        - Call `httpd_req_recv(p_req, buf, OTA_MNGR_RECV_BUF_SIZE)`.
        - If `0 == ret`: break (all data received).  `HTTPD_SOCK_ERR_TIMEOUT` and negative
          return values indicate errors -- call `ota_mngr_abort()`, send HTTP 500, return
          `ESP_FAIL`.
        - Call `ota_mngr_write(buf, (size_t)ret)`.  On failure, call `ota_mngr_abort()`,
          send HTTP 500, return `ESP_FAIL`.
      - Call `ota_mngr_end()`.  On failure, send HTTP 500, return `ESP_FAIL`.
      - Send HTTP 200 response: `"OK"`.
      - Call `ota_mngr_activate()`.  This function does not return on success.  If it
        returns (failure path), send HTTP 500 and return `ESP_FAIL`.
      - Note: `httpd_req_recv()` returns `HTTPD_SOCK_ERR_TIMEOUT` if the socket times out.
        The default `httpd` recv timeout is sufficient for LAN uploads.  Do **not** disable
        the task watchdog; the httpd task is not subscribed to the WDT by default.

   c. **`http_srv_ota_pwd_get_handler` (GET /ota/pwd)**:
      - Call `http_srv_ota_auth_check(p_req)`.  If it returns `false`, return `ESP_OK`.
      - Send an HTML page with a `<form method="POST" action="/ota/pwd">` containing:
        - A current-password `<input type="password" name="current_pwd">` field.
        - A new-password `<input type="password" name="new_pwd">` field.
        - A confirm-password `<input type="password" name="confirm_pwd">` field.
        - Password length note: max `OTA_MNGR_PASSWORD_MAX_LEN` characters.
        - A "Save" button.
        - A navigation link back to `/ota`.

   d. **`http_srv_ota_pwd_post_handler` (POST /ota/pwd)**:
      - Call `http_srv_ota_auth_check(p_req)`.  If it returns `false`, return `ESP_OK`.
      - Read the POST body (URL-encoded, max `HTTP_SRV_HTML_BUF_LEN` bytes via
        `httpd_req_recv()`).  Null-terminate the buffer.
      - Parse fields `current_pwd`, `new_pwd`, `confirm_pwd` using the same approach as
        `http_server_config.c` (find `key=value`, URL-decode `+` as space and `%XX` as hex).
      - Validate:
        - `ota_mngr_credentials_check(current_pwd)` must be `true`; else respond HTTP 400
          `"Current password incorrect"` and return `ESP_OK`.
        - `strlen(new_pwd) > 0` and `<= OTA_MNGR_PASSWORD_MAX_LEN`; else respond HTTP 400
          `"Password too short or too long"` and return `ESP_OK`.
        - `strcmp(new_pwd, confirm_pwd) == 0`; else respond HTTP 400 `"Passwords do not match"`
          and return `ESP_OK`.
      - Call `ota_mngr_password_set(new_pwd)`.  On failure respond HTTP 500.
      - On success: redirect to `GET /ota/pwd?saved=1` with HTTP 303 and render a brief
        success banner.

3. Internal function prototypes (in the `Internal Function Prototypes` section of
   `http_server_ota.c`):

   ```c
   static bool http_srv_ota_auth_check(httpd_req_t *p_req);
   static int  http_srv_ota_base64_decode(const char *p_in, char *p_out, size_t out_len);
   ```

### Files Created

| File                          | Change type |
| ----------------------------- | ----------- |
| `main/inc/http_server_ota.h`  | new file    |
| `main/src/http_server_ota.c`  | new file    |

### Security notes

- Never log the supplied password or the decoded Authorization header value.
- Base64 decode buffer is sized to prevent overflow: `Authorization:` header maximum length
  is capped at 256 bytes; decoded result is at most 192 bytes, well within the on-stack
  `user:password` buffer.
- The `ota_mngr_credentials_check()` comparison uses `strcmp()`, which is not
  constant-time.  For a home-network device this is acceptable; for higher-security
  environments consider `mbedtls_ct_memcmp()`.
- Transmitting credentials in HTTP Basic Auth over plain HTTP is acceptable for use on the
  LAN; no TLS is required for this use case since the attacker would need LAN access to
  mount a MITM attack, and the reward AP uses WPA2 encryption.

### Acceptance Criteria

- [x] `idf.py build` succeeds.
- [x] GET /ota without `Authorization` header returns HTTP 401 with
  `WWW-Authenticate: Basic realm="esport-fi32 OTA"`.
- [x] GET /ota with correct credentials returns HTTP 200 HTML with a file-input form.
- [x] POST /ota with wrong credentials returns HTTP 401.
- [x] `http_srv_ota_base64_decode` correctly decodes `"YWRtaW46ZXNwb3J0LWZpMzI="` to
  `"admin:esport-fi32"`.
- [x] Password change: POST /ota/pwd with wrong current password returns HTTP 400.
- [x] Password change: POST /ota/pwd with mismatched new/confirm passwords returns HTTP 400.
- [x] All symbols carry the `http_srv_ota_` / `HTTP_SRV_OTA_` prefix.

---

## Phase F4 -- HTTP Server & Build Integration

### Goal

Register the four OTA URI handlers in `http_server.c`, add `app_update` to the component
requirements, and add the new `.c` files to `main/CMakeLists.txt`.  These are purely additive
changes to existing files -- no existing logic is removed or modified.

### Inputs

- `main/src/http_server.c` (existing).
- `main/CMakeLists.txt` (existing).
- `main/inc/http_server_ota.h` (Phase F3 output).
- `main/inc/ota_manager.h` (Phase F2 output).

### Tasks

1. **Update `main/CMakeLists.txt`**:
   - Add `app_update` to the `REQUIRES` list (provides `esp_ota_ops.h`).
   - Add the two new source files to `SRCS`:
     ```cmake
     "src/ota_manager.c"
     "src/http_server_ota.c"
     ```

2. **Update `main/src/http_server.c`**:

   a. Add at the top of the Includes section:
      ```c
      #include "http_server_ota.h"
      ```

   b. Inside `http_srv_init()`, increase `cfg.max_uri_handlers` from `9U` to `13U` (adds
      four new handlers).

   c. After the last existing `httpd_register_uri_handler()` call (for
      `sc_uri_api_sessions_daily`), add:

      ```c
      static const httpd_uri_t sc_uri_ota_get = {
          .uri     = "/ota",
          .method  = HTTP_GET,
          .handler = http_srv_ota_get_handler,
      };
      static const httpd_uri_t sc_uri_ota_post = {
          .uri     = "/ota",
          .method  = HTTP_POST,
          .handler = http_srv_ota_post_handler,
      };
      static const httpd_uri_t sc_uri_ota_pwd_get = {
          .uri     = "/ota/pwd",
          .method  = HTTP_GET,
          .handler = http_srv_ota_pwd_get_handler,
      };
      static const httpd_uri_t sc_uri_ota_pwd_post = {
          .uri     = "/ota/pwd",
          .method  = HTTP_POST,
          .handler = http_srv_ota_pwd_post_handler,
      };
      (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_get);
      (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_post);
      (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_pwd_get);
      (void)httpd_register_uri_handler(gp_server_handle, &sc_uri_ota_pwd_post);
      ```

3. **Update `main/src/http_server_config.c`**:
   - Below the "Reset to Factory Defaults" button, add a horizontal divider and a
     "Firmware Update" link styled as a button that navigates to `/ota`:
     ```html
     <div style="margin:1.5em 0;border-top:1px solid #ccc;"></div>
     <div class="btn-right">
       <a href="/ota" ...>Firmware Update</a>
     </div>
     ```
   - This is a small addition to the existing `http_srv_config_get_handler` chunked output.

### Files Modified

| File                              | Change          |
| --------------------------------- | --------------- |
| `main/CMakeLists.txt`             | add REQUIRES + SRCS entries |
| `main/src/http_server.c`          | add include + 4 URI registrations |
| `main/src/http_server_config.c`   | add /ota link below Reset button |

### Acceptance Criteria

- [x] `idf.py build` succeeds with zero errors.
- [x] No existing tests (from Phase 10) regress.
- [ ] GET /ota is reachable in a browser and returns the upload form after Basic Auth.
- [ ] GET /ota/pwd is reachable and returns the password change form.

---

## Phase F5 -- Boot Sequence Integration

### Goal

Call `ota_mngr_init()` from `main.c` as part of the boot sequence.  This is the only change
to `main.c`.  `ota_mngr_init()` marks the running firmware as valid (cancels any active
rollback window) and opens the OTA NVS namespace.  It must run after NVS is initialised
but before the HTTP server starts.

### Inputs

- `main/src/main.c` (existing boot sequence, Phase 10 output).
- `main/inc/ota_manager.h` (Phase F2 output).

### Boot Sequence Position

Add `ota_mngr_init()` as step 2c in `docs/1-specification.md` §7.1
(between `buzzer_init()` at 2b and `esp_event_loop_create_default()` at step 3):

```
2c. ota_mngr_init()           <- mark firmware valid; open esport_ota NVS namespace
```

The placement guarantees that:
- NVS is initialised (step 2 `config_mngr_init()` calls `nvs_flash_init()`).
- The firmware-valid mark is set early, before any blocking operation that could trigger a
  rollback.
- The HTTP server (step 5) registers OTA routes that depend on `ota_mngr_*` already being
  initialised.

### Tasks

1. **Add `#include "ota_manager.h"`** to `main.c` in the Includes section.

2. **Add the call** after the `ESP_ERROR_CHECK(buzzer_init())` line:
   ```c
   ESP_ERROR_CHECK(ota_mngr_init());
   ```

3. **Update `docs/1-specification.md` §7.1** boot sequence table to include step 2c
   (see Phase F6 task 3).

### Files Modified

| File             | Change                                            |
| ---------------- | ------------------------------------------------- |
| `main/src/main.c`| add include + one `ESP_ERROR_CHECK(ota_mngr_init())` line |

### Acceptance Criteria

- [x] `idf.py build` succeeds.
- [ ] On boot, the log contains: `"ota_manager: running fw v<version>"`.
- [ ] On boot, no crash or assertion related to `esp_ota_mark_app_valid_cancel_rollback`.
- [ ] When running from `ota_0` or `ota_1`, the rollback pending flag is cleared.

---

## Phase F6 -- Integration & Verification

### Goal

Run the full end-to-end OTA flow on the device, update `docs/1-specification.md` to include
the new /ota routes and boot step, and document test results.

### Inputs

- All Phase F1--F5 outputs.
- `docs/1-specification.md` (current version).
- A second firmware binary with a different `PROJECT_VER` (e.g. `"2.1.0"`) built from a
  copy of the project.

### Tasks

1. **Flash the device** for the first time after the partition table change:
   ```bash
   idf.py erase-flash flash
   ```
   This is the mandatory one-time full reflash.  Subsequent updates will be OTA.

2. **Run end-to-end test checklist** (see table below).

3. **Update `docs/1-specification.md`**:

   a. Add step 2c to §7.1 Boot Sequence:
      ```
      2c. ota_mngr_init()           <- mark firmware valid; open esport_ota NVS namespace
      ```

   b. Add the two OTA routes to §4 System Architecture diagram and the HTTP Server
      responsibilities list in §5.8:
      ```
      GET /ota            firmware upload page (Basic Auth)
      POST /ota           receive and flash firmware binary (Basic Auth)
      GET /ota/pwd        OTA password change page (Basic Auth)
      POST /ota/pwd       save new OTA password (Basic Auth)
      ```

   c. Add new §6.7 "Firmware Update -- `GET /ota` and `POST /ota`":
      ```
      Protected by HTTP Basic Auth.
      Username: "admin" (hardcoded).
      Password: configurable via POST /ota/pwd, stored in NVS namespace esport_ota key ota_pwd.
      Default password: "esport-fi32".

      GET /ota: serves an HTML upload page showing the running firmware version and a
      file-input form.

      POST /ota: receives application/octet-stream body (the .bin file), writes it to the
      inactive OTA slot incrementally, verifies the image header, sets the new boot
      partition, and reboots. The browser receives HTTP 200 "OK" just before the reboot.

      On reboot, the bootloader loads the new image.  ota_mngr_init() marks it valid.
      If the device crashes before reaching ota_mngr_init(), the bootloader rolls back to
      the previous slot automatically.
      ```

   d. Add `esport_ota` namespace to §8 NVS Layout:

      | Key       | Type   | Content                              |
      | --------- | ------ | ------------------------------------ |
      | `ota_pwd` | string | OTA Basic Auth password (max 63 chars) |

4. **Update `docs/2-development_plan.md`** Module Prefix Table:

   | Source file       | Function / type prefix | Macro / enum prefix |
   | ----------------- | ---------------------- | ------------------- |
   | `ota_manager`     | `ota_mngr_`            | `OTA_MNGR_`         |
   | `http_server_ota` | `http_srv_ota_`        | `HTTP_SRV_OTA_`     |

### End-to-End Test Checklist

| #  | Test                                                                              | Pass/Fail |
| -- | --------------------------------------------------------------------------------- | --------- |
| F1 | After first full flash, device boots and all existing features work normally      |           |
| F2 | GET /ota without credentials returns HTTP 401 with WWW-Authenticate header        |           |
| F3 | GET /ota with correct credentials (admin / esport-fi32) returns HTML upload form  |           |
| F4 | GET /ota shows the running firmware version string (e.g. "2.0.0")                |           |
| F5 | Upload a valid .bin with a higher version; device reboots into the new firmware   |           |
| F6 | After successful OTA, GET /api/status `fw_version` field shows the new version    |           |
| F7 | After successful OTA, GET /ota shows the new version as "Running Firmware"        |           |
| F8 | Upload the old version .bin; device accepts it (downgrades allowed)               |           |
| F9 | Simulate a bad image (truncated file): POST /ota returns HTTP 500; device stays   |           |
|    | on old firmware; GET /ota still serves the form with the original version         |           |
| F10| POST /ota with wrong credentials returns HTTP 401                                 |           |
| F11| POST /ota/pwd with wrong current password returns HTTP 400                        |           |
| F12| POST /ota/pwd with mismatched passwords returns HTTP 400                          |           |
| F13| POST /ota/pwd with valid data saves new password; new password accepted on next   |           |
|    | GET /ota attempt; old password rejected                                           |           |
| F14| Rollback test: build a firmware that calls esp_restart() before ota_mngr_init(); |           |
|    | flash it via OTA; verify the bootloader rolls back to the previous slot           |           |
| F15| NVS data (registered devices, config, session log) survives an OTA update         |           |
| F16| OTA upload from the reward AP (192.168.5.1/ota) works without STA connection     |           |

### Acceptance Criteria

- [ ] `idf.py build` succeeds.
- [ ] All 16 end-to-end tests pass.
- [ ] `docs/1-specification.md` updated with new OTA routes, boot step, and NVS entry.
- [ ] `docs/2-development_plan.md` Module Prefix Table updated.
- [ ] No existing Phase 10 tests regress.

---

## Dependency Graph

```
Phase F0 (Feasibility)
   |
   v
Phase F1 (Partition Table)
   |
   v
Phase F2 (OTA Manager)
   |
   v
Phase F3 (HTTP OTA Handlers)  <-- depends on F2
   |
   v
Phase F4 (HTTP Server + Build) <-- depends on F2, F3
   |
   v
Phase F5 (Boot Sequence)       <-- depends on F2
   |
   v
Phase F6 (Integration)         <-- depends on F1, F2, F3, F4, F5
```

## Module Prefix Additions

The following rows must be added to the Module Prefix Table in `docs/2-development_plan.md`:

| Source file       | Function / type prefix | Macro / enum prefix |
| ----------------- | ---------------------- | ------------------- |
| `ota_manager`     | `ota_mngr_`            | `OTA_MNGR_`         |
| `http_server_ota` | `http_srv_ota_`        | `HTTP_SRV_OTA_`     |

## Files Changed Summary

| File                              | Phase | Change type  |
| --------------------------------- | ----- | ------------ |
| `partitions.csv`                  | F1    | new file     |
| `sdkconfig.defaults`              | F1    | modified     |
| `main/inc/ota_manager.h`          | F2    | new file     |
| `main/src/ota_manager.c`          | F2    | new file     |
| `main/inc/http_server_ota.h`      | F3    | new file     |
| `main/src/http_server_ota.c`      | F3    | new file     |
| `main/CMakeLists.txt`             | F4    | add 2 SRCS + 1 REQUIRES  |
| `main/src/http_server.c`          | F4    | add include + 4 URI registrations |
| `main/src/http_server_config.c`   | F4    | add /ota link below Reset button |
| `main/src/main.c`                 | F5    | add include + 1 init call |
| `docs/1-specification.md`         | F6    | update boot seq, routes, NVS layout |
| `docs/2-development_plan.md`      | F6    | add module prefix rows |

## Notes for AI Agents

- Follow all coding conventions from `docs/1-specification.md` §11: BARR-C:2018 naming,
  Yoda notation, `\\` Doxygen tags, `hhtemplate`/`cctemplate` structure, no Unicode.
- The OTA manager must compile and link even when the partition table does not have OTA
  partitions (it returns `ESP_ERR_NOT_SUPPORTED` from `ota_mngr_begin()` instead of
  crashing).  This allows Phase F2 to be built before Phase F1 is complete.
- The `esp_ota_mark_app_valid_cancel_rollback()` call in `ota_mngr_init()` must NOT be
  wrapped in `ESP_ERROR_CHECK` -- it returns an error on first boot from `factory` partition
  (before the partition table migration) and that is a normal, non-fatal condition.
- `OTA_WITH_SEQUENTIAL_WRITES` erases flash lazily (one sector at a time as data arrives)
  rather than erasing the entire partition upfront.  This significantly reduces the time
  before the first chunk write begins and is the preferred mode for HTTP upload where the
  total size may not be known upfront.
- The httpd task is **not** registered with the Task WDT by default in ESP-IDF.  The OTA
  write loop does not need to manually feed the watchdog.
- Do not add FOTA capability to `idf_component.yml` -- `app_update` is a standard ESP-IDF
  component already available in the build system; `main/CMakeLists.txt` `REQUIRES` is
  sufficient.
- The `partitions.csv` file must be placed in the **project root** (same directory as the
  top-level `CMakeLists.txt`), not inside `main/`.
