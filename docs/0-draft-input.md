# Initial spec draft

The esport-32fi project consists of an ESP32 C6 WiFi board that both creates a soft AP and connects to an existing WiFi network.

The goal of the project is to limit internet access to kids in the following way:
- The ESP32 is constantly connected to an WiFi network. The details of the network should be configurable: `wifi_ssid`, `wifi_password`.
- An GPIO input of the ESP32 is connected to a sports bike (exercise bike) sensor.
- The GPIO input is configured in interrupt mode and detects pulses/edges.
- The ESP32 should keep a time counter in seconds.
- The time counter should increment based on the pulses received in the GPIO input.
- For every pulse, `seconds_per_pulse` seconds should be added to the time counter. `seconds_per_pulse` is a configuration.
- After `soft_ap_start_threshold_s` seconds the pulses started, the ESP32 creates a Soft AP with a given `soft_ap_ssid` and `soft_ap_password` and starts decrementing the time counter in real time, but should still keep incrementing it according to the pulses received in the GPIO input. `soft_ap_start_threshold_s`,`soft_ap_ssid` and `soft_ap_password` are configurations.
- When the time counter reaches 0, the Soft AP should be turned off.
- The ESP32 should keep track of the current date and time. When it is turned on, it should synchronize its date/time (using NTP ?).
- The ESP32 should keep a log in its NVM memory with the following information:
  - Date/time the pulses started to be received.
  - Duration of the session. If pulses stopped for more than `idle_session_interval_s` seconds, the session should be considered as closed. `idle_session_interval_s` is a configuration.
  - Average of speed in km/h based on the pulses/time. `centimeters_per_pulse` is a configuration and will help translate pulses to speed in km/h.

Configuration:
When the ESP32 is not able to connect to `wifi_ssid`, it should enable a soft AP called `esport-fi32_config` with password `esport-fi32_config` and a web page should be available to configure all the parameters, including:
    - `wifi_ssid`, `wifi_password`
    - `soft_ap_ssid`, `soft_ap_password`
    - `seconds_per_pulse`, `soft_ap_start_threshold_s`, `meters_per_pulse`

The Configuration Web page should be available all the time, via the `esport-fi32_config` when it is not possible to connect to the `wifi_ssid`, or via the normal WiFi network (both ESP32 and a computer/phone conected to the same `wifi_ssid`).


# Improvements

1. The time left to deactivate the soft Ap (reward Ap) should decrement only if there are clients connected and there is some traffic. A configuration variable `soft_ap_dec_time_above_threshold_kbps` should determine the traffic bellow which the time is not decremented. -- planned
2. The time counter for how long the Soft Ap should be on must be incremented only if `min_speed_to_increment_time_kmh_x10`. The minimum value is 0, no maximum limit. Default value=30 (3 km/h). -- planned
3. The reward counter should persist between power-cycles. It should be saved regularly to NVM and must be restored on startup, so that kids don't loose the already gained internet time if the system reboots. The frequency of storing the counter can be around 1min. However, we must have a way to change its value in the configuration page, this way we can also give internet access without peddaling. Changes in the configuration page must be reflected immediately. -- planned
4. Currently, the Soft AP is turned on and off to control the internet access, meaning that when the Soft AP is off, nobody else can use it. Implement the internet access control per device MAC.
Up to 4 users can be registered via `/config` page. Users are identified by the MAC address and a nickname
with ~15 characters. Each user should have its own counter_s for internet. One of the users can be assigned
as the rider of the bike and only his counter is incremented with the bike pulses. The Soft AP should be on
all the time since boot and all devices can access the dashboard (for example), but they will have internet
access only if the counter is greater than 0. -- planned
5. A buzzer will be added to the hardware. The buzzer is an active buzzer, meaning it can be enabled/disabled via a GPIO to produce the sound (on/off). The GPIO pin must be configurable via the menuconfig and the default value is GPIO 11. The buzzer can be on or off for a given number of beep units. One beep unit is defined as 50ms.
The buzzer will be used in the following situations:
    - When a session is started (start qualifying), a short beep of 5 units should be issued.
    - When a session is qualified, a long beep of 10 units should be issued.
    - During the session, if the speed is below the minimum threshold, a short beep of 2 units should be issued every second while the speed is below the threshold. The beep should stop completely if the speed reaches 0.
    - When a session is closed, 3 short beeps of 2 units should be issued in sequence, with 1 unit off between them: (2 units ON, 1 unit OFF, 2 units ON, 1 unit OFF, 2 units ON).
    - All beep timings should be defined as a constant (#define, for example) according to the number of beep units.
There must be a way to disable the buzzer in the `/config` page. -- planned.
