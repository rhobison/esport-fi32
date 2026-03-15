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
  - Average of speed in km/h based on the pulses/time. `meters_per_pulse` is a configuration and will help translate pulses to speed in km/h.

Configuration:
When the ESP32 is not able to connect to `wifi_ssid`, it should enable a soft AP called `esport-fi32_config` with password `esport-fi32_config` and a web page should be available to configure all the parameters, including:
    - `wifi_ssid`, `wifi_password`
    - `soft_ap_ssid`, `soft_ap_password`
    - `seconds_per_pulse`, `soft_ap_start_threshold_s`, `meters_per_pulse`

The Configuration Web page should be available all the time, via the `esport-fi32_config` when it is not possible to connect to the `wifi_ssid`, or via the normal WiFi network (both ESP32 and a computer/phone conected to the same `wifi_ssid`).


# Improvements

1. The time left to deactivate the soft Ap (reward Ap) should decrement only if there are clients connected and there is some traffic. A configuration variable `soft_ap_dec_time_above_threshold_kbps` should determine the traffic bellow which the time is not decremented.
