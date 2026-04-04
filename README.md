# ESPort-fi32

**Turn pedalling into internet time — a parental-control firmware for the ESP32-C6.**

ESPort-fi32 is an ESP-IDF firmware that incentivises children to exercise on a sports/exercise bike by gating Wi-Fi internet access behind time credits earned through physical activity. The more the child pedals, the more internet time they earn. When the credits run out, internet access is revoked for that device until more credits are earned.

---

## How it works

1. **Sensor input** — A bike sensor is wired to a GPIO pin on the ESP32-C6. Every pedal revolution generates a pulse that is counted by the firmware.
2. **Credit accumulation** — Each pulse adds a configurable number of seconds (`seconds_per_pulse`) to the current rider's time counter, provided the instantaneous speed meets the configurable minimum (`min_speed_to_increment_time_kmh_x10`). Set the minimum to zero to credit every pulse regardless of speed.
3. **Always-on Reward Wi-Fi** — The **Reward Soft AP** is active from boot and acts as a NAT router. Children's devices stay connected at all times and can always reach the gateway (status dashboard, config page). Internet access is gated per device.
4. **Per-device internet gating** — Up to 4 devices can be registered in a **device registry** (MAC address, nickname, individual time counter, enabled toggle). Only registered devices with remaining credits and the enabled flag set can route traffic to the internet. An IP-layer filter silently drops internet-bound packets from unauthorised devices.
5. **Current rider** — A "current rider" selector on the config page binds one registered device to the bike sensor. Credits earned during exercise go to that device's counter.
6. **Countdown** — Each registered device's counter counts down independently (one second per tick) while the device is connected, enabled, and has credits remaining. The countdown pauses automatically per device when there is no active internet traffic (configurable threshold), so idle screen time does not consume credits.
7. **Access cut-off** — When a device's counter reaches zero, internet access is revoked for that device only. The Reward AP stays on and other devices with remaining credits are unaffected.

---

## Features

- **AP + STA simultaneous mode** with NAT — connected devices browse the internet via the home network.
- **Always-on Reward AP** — the Soft AP is active from boot; children's devices stay connected at all times. Internet access is gated per device, not by toggling the AP.
- **Per-device internet access control** — a device registry (up to 4 entries) stores MAC, nickname, individual time counter, and enabled flag per device. An IP-layer filter enforces access: only registered, enabled devices with remaining credits can reach the internet.
- **Current rider selection** — binds the bike sensor to one registered device; earned credits go to that device's counter.
- **Exercise session tracking** — sessions are detected, timed, and stored in NVS as a ring buffer with start time, duration, distance, and average speed.
- **NTP time synchronisation** — date/time is synced at boot; a configurable POSIX timezone string converts UTC timestamps to local time.
- **Always-available configuration portal** — a web UI is reachable via the home network IP or via the reward AP (`192.168.5.1`) at all times.
- **Live status dashboard** — shows per-device counters and internet status, AP state, connected clients, NTP status, session history, bar charts, and export actions.
- **Device management on config page** — add, remove, rename devices; set per-device counter (h:mm:ss), toggle enabled, select current rider. Connected but unregistered stations are listed for quick registration (useful when Android MAC randomisation produces an unexpected address).
- **REST JSON API** — for status (including per-device array), session history, CSV/JSON export, and daily activity aggregates (suitable for charts).
- **Speed-gated crediting** — a configurable minimum speed (`min_speed_to_increment_time_kmh_x10`) prevents credits accumulating when pedalling too slowly. The gate is disabled when set to zero.
- **Per-device traffic-gated countdown** — each device's countdown pauses independently when no meaningful internet traffic is detected, preventing credits from draining during idle screen time.
- **Persistent per-device counters** — device registry data is saved to NVS every 60 seconds and on counter-reaching-zero; counters survive power cycles.
- **Fully configurable** — all parameters (SSID, password, seconds-per-pulse, thresholds, timezone, ...) are stored in NVS and can be changed at runtime via the web UI without reflashing.

---

## Web UI

The built-in HTTP server provides two pages and a JSON API, accessible from any browser on the same network.

### Status Dashboard (`/`)

![Status Dashboard](docs/imgs/dashboard-1.png)

![Status Dashboard](docs/imgs/dashboard-2.png)

Shows in real time:
- Per-device internet status table (nickname, counter, internet active, connected, throughput, pause state)
- Current speed and speed-gate status (crediting or gated)
- Reward AP status and connected clients
- Current exercise session info (state, qualification progress, live speed)
- NTP sync status and system uptime
- Session history table (last 20 sessions)
- Session bar charts: daily average speed and daily total duration
- Export Reports panel (download CSV or JSON)

### Configuration Page (`/config`)

![Configuration Page](docs/imgs/config.png)

Lets you set all parameters without reflashing:
- Home Wi-Fi credentials
- Reward AP SSID & password
- Seconds earned per pulse, warm-up threshold
- Minimum speed required to earn credits
- Wheel circumference (for speed calculation)
- Session detection timings, debounce
- Traffic threshold for per-device countdown pause
- Timezone (POSIX TZ string)
- **Registered Devices** table: per-device nickname, MAC (read-only), counter (h:mm:ss, editable with auto-format), enabled toggle, current-rider radio, remove button
- **Add Device** form: MAC address input (auto-formatted), nickname
- **Connected Unregistered Stations**: lists AP-connected devices not in the registry with a one-click Add button (handles Android MAC randomisation)

---

## Hardware

| Item             | Details                                               |
| ---------------- | ----------------------------------------------------- |
| MCU              | ESP32-C6                                              |
| Bike sensor GPIO | GPIO 10 (configurable via `CONFIG_ESPORT_PULSE_GPIO`) |
| GPIO pull        | Internal pull-up (sensor closes to GND)               |
| Active edge      | Falling edge                                          |

Connect the exercise bike's reed switch or hall-effect sensor between **GPIO 10** and **GND**.

![Circuit](docs/imgs/hw_circuit.png)

---

## Getting Started

### Prerequisites

- [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c6/get-started/) (or compatible v5.x)
- ESP32-C6 development board

### Dev Container (recommended)

A ready-to-use Docker-based development environment is included. It pre-installs ESP-IDF v5.5.3, CMake 4.2.0, Doxygen 1.15.0, and all required toolchains.

1. Install [Docker](https://docs.docker.com/get-docker/) and the [VS Code Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers).
2. Open the repository in VS Code and accept the **Reopen in Container** prompt.
3. The container builds automatically. After it starts, the full ESP-IDF toolchain is available in the integrated terminal.

### Build & Flash

```bash
idf.py set-target esp32c6
idf.py build
idf.py -p PORT flash monitor
```

### First-time configuration

1. On first boot (or when home Wi-Fi credentials are not yet configured) the device creates a fallback AP:
   - **SSID:** `esport-fi32_config`
   - **Password:** `esport-fi32_config`
2. Connect to that network and open **http://192.168.4.1/config** in a browser.
3. Enter your home Wi-Fi credentials, the Reward AP name/password, and any other settings.
4. Save and reboot. The device will connect to your home network.
5. The configuration page remains accessible via the home network IP from that point on.

---

## Configuration Parameters

All parameters are stored in NVS and can be changed at runtime via the web UI.

| Parameter                               | Default | Description                                                                                   |
| --------------------------------------- | ------- | --------------------------------------------------------------------------------------------- |
| `wifi_ssid`                             | `""`    | Home network SSID                                                                             |
| `wifi_password`                         | `""`    | Home network password                                                                         |
| `soft_ap_ssid`                          | `"esport-fi32"` | Reward AP SSID                                                                      |
| `soft_ap_password`                      | `"esport-fi32"` | Reward AP password                                                                  |
| `seconds_per_pulse`                     | `3`     | Seconds of internet time earned per bike pulse                                                |
| `soft_ap_start_threshold_s`             | `300`   | Session duration (s) before earned credits start counting down                                |
| `centimeters_per_pulse`                 | `300`    | Wheel travel per pulse (cm), used for speed display                                           |
| `idle_session_interval_s`               | `30`    | Gap (s) with no pulses that closes a session                                                  |
| `start_session_interval_s`              | `10`    | Continuous pedalling (s) required to open a session                                           |
| `pulse_debounce_time_ms`                | `10`    | Minimum time (ms) between two accepted pulses                                                 |
| `timezone`                              | `"UTC0"` | POSIX TZ string (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`)                                        |
| `soft_ap_dec_time_above_threshold_kbps` | `1`     | Per-device traffic threshold (kbps) below which countdown pauses                              |
| `soft_ap_idle_throughput_timeout_s`     | `30`    | Seconds of low traffic per device before countdown actually pauses                             |
| `min_speed_to_increment_time_kmh_x10`   | `30`    | Minimum speed (km/h x 10, e.g. `30` = 3.0 km/h) required for a pulse to earn credits; `0` disables the gate |

---

## REST API

| Endpoint               | Method     | Description                   |
| ---------------------- | ---------- | ----------------------------- |
| `/`                    | GET        | Status dashboard (HTML)       |
| `/config`              | GET / POST | Configuration form (HTML)     |
| `/api/status`          | GET        | Live state as JSON            |
| `/api/sessions`        | GET        | Session history as JSON       |
| `/api/sessions/export` | GET        | Download CSV or JSON report   |
| `/api/sessions/daily`  | GET        | Daily aggregates for charting |

---

## Project Structure

```
main/
  inc/                        # Header files for all modules
  src/
    main.c                    # Startup & module initialisation
    config_manager.c          # NVS-backed configuration
    device_registry.c         # Per-device MAC registry, counters & NVS persistence
    wifi_manager.c            # AP+STA+NAT Wi-Fi management & per-MAC frame filter
    time_manager.c            # SNTP / timezone
    pulse_input.c             # GPIO interrupt, debounce & speed calculation
    time_counter.c            # Credit counter & reward AP state machine
    session_tracker.c         # Exercise session detection
    session_log.c             # NVS ring-buffer session log
    http_server.c             # HTTP server core (init, URI registration)
    http_server_utils.c       # Shared HTML/JSON helpers
    http_server_config.c      # GET & POST /config handlers (incl. device management)
    http_server_api.c         # GET /api/status and /api/sessions handlers
    http_server_export.c      # GET /api/sessions/export handler
    http_server_dashboard.c   # GET / status dashboard handler
docs/
  1-specification.md          # Full firmware specification
  2-development_plan.md       # Phased development plan
```

## Finished assembly

![Assembly 1](docs/imgs/assembly-1.jpg)

![Assembly 2](docs/imgs/assembly-2.jpg)

