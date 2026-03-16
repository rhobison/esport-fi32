# esport-fi32

**Turn pedalling into internet time — a parental-control firmware for the ESP32-C6.**

esport-fi32 is an ESP-IDF firmware that incentivises children to exercise on a sports/exercise bike by gating Wi-Fi internet access behind time credits earned through physical activity. The more the child pedals, the more internet time they earn. When the credits run out, the Wi-Fi access point shuts down automatically.

---

## How it works

1. **Sensor input** — A bike sensor is wired to a GPIO pin on the ESP32-C6. Every pedal revolution generates a pulse that is counted by the firmware.
2. **Credit accumulation** — Each pulse adds a configurable number of seconds (`seconds_per_pulse`) to a time counter.
3. **Reward Wi-Fi** — Once continuous pedalling exceeds a warm-up threshold (`soft_ap_start_threshold_s`), the firmware enables a **Reward Soft AP** that acts as a NAT router, giving connected devices access to the internet through the home network.
4. **Countdown** — While the Reward AP is active, the time counter counts down in real time. Continued pedalling replenishes the credits. The countdown pauses automatically when there is no active internet traffic (configurable threshold), so idle screen time does not consume credits.
5. **Access cut-off** — When the counter reaches zero the Reward AP is disabled and internet access is cut off until the child earns more credits.

---

## Features

- **AP + STA simultaneous mode** with NAT — connected devices browse the internet via the home network.
- **Exercise session tracking** — sessions are detected, timed, and stored in NVS as a ring buffer with start time, duration, distance, and average speed.
- **NTP time synchronisation** — date/time is synced at boot; a configurable POSIX timezone string converts UTC timestamps to local time.
- **Always-available configuration portal** — a web UI is reachable via the home network IP or via a dedicated fallback config AP (`esport-fi32_config`) when home network access is unavailable.
- **Live status dashboard** — shows the current counter value, AP state, connected clients, NTP status, and session history.
- **REST JSON API** — for status, session history, CSV/JSON export, and daily activity aggregates (suitable for charts).
- **Fully configurable** — all parameters (SSID, password, seconds-per-pulse, thresholds, timezone, …) are stored in NVS and survive reboots.

---

## Web UI

The built-in HTTP server provides two pages and a JSON API, accessible from any browser on the same network.

### Status Dashboard (`/`)

![Status Dashboard](docs/imgs/dashboard-1.png)

![Status Dashboard](docs/imgs/dashboard-2.png)

Shows in real time:
- Time counter (credits remaining)
- Reward AP status (on / off) and connected clients
- Current exercise session info (speed, duration)
- NTP sync status
- Session history table

### Configuration Page (`/config`)

![Configuration Page](docs/imgs/config.png)

Lets you set all parameters without reflashing:
- Home Wi-Fi credentials
- Reward AP SSID & password
- Seconds earned per pulse, warm-up threshold
- Wheel circumference (for speed calculation)
- Session detection timings, debounce
- Traffic threshold for countdown pause
- Timezone (POSIX TZ string)

---

## Hardware

| Item             | Details                                               |
| ---------------- | ----------------------------------------------------- |
| MCU              | ESP32-C6                                              |
| Bike sensor GPIO | GPIO 10 (configurable via `CONFIG_ESPORT_PULSE_GPIO`) |
| GPIO pull        | Internal pull-up (sensor closes to GND)               |
| Active edge      | Falling edge                                          |

Connect the exercise bike's reed switch or hall-effect sensor between **GPIO 10** and **GND**.

---

## Getting Started

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/)
- ESP32-C6 development board

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

| Parameter                               | Default       | Description                                             |
| --------------------------------------- | ------------- | ------------------------------------------------------- |
| `wifi_ssid`                             | _(empty)_     | Home network SSID                                       |
| `wifi_password`                         | _(empty)_     | Home network password                                   |
| `soft_ap_ssid`                          | `esport-fi32` | Reward AP SSID                                          |
| `soft_ap_password`                      | `esport-fi32` | Reward AP password                                      |
| `seconds_per_pulse`                     | `3`           | Seconds of internet time earned per bike pulse          |
| `soft_ap_start_threshold_s`             | `300`         | Warm-up pedalling time (s) before AP is enabled         |
| `centimeters_per_pulse`                 | `25`          | Wheel travel per pulse (cm), used for speed display     |
| `idle_session_interval_s`               | `30`          | Gap (s) with no pulses that closes a session            |
| `start_session_interval_s`              | `10`          | Continuous pedalling (s) required to open a session     |
| `pulse_debounce_time_ms`                | `200`         | Minimum time (ms) between two accepted pulses           |
| `timezone`                              | `UTC0`        | POSIX TZ string (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`)     |
| `soft_ap_dec_time_above_threshold_kbps` | `1`           | Traffic threshold (kbps) below which countdown pauses   |
| `soft_ap_idle_throughput_timeout_s`     | `30`          | Seconds of low traffic before countdown actually pauses |

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
  inc/           # Header files for all modules
  src/
    main.c             # Startup & module initialisation
    config_manager.c   # NVS-backed configuration
    wifi_manager.c     # AP+STA+NAT Wi-Fi management
    time_manager.c     # SNTP / timezone
    pulse_input.c      # GPIO interrupt & debounce
    time_counter.c     # Credit counter & reward AP state machine
    session_tracker.c  # Exercise session detection
    session_log.c      # NVS ring-buffer session log
    http_server.c      # Web UI & REST API
docs/
  1-specification.md   # Full firmware specification
  2-development_plan.md
```

