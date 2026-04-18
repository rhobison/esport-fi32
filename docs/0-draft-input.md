# Initial spec draft

The esport-32fi project consists of an ESP32 C6 WiFi board that both creates a soft AP and connects to an existing WiFi network.

The goal of the project is to limit internet access to kids in the following way:
- The ESP32 is constantly connected to an WiFi network. The details of the network should be configurable: `wifi_ssid`, `wifi_password`.
- An GPIO input of the ESP32 is connected to a sports bike (exercise bike) sensor.
- The GPIO input is configured in interrupt mode and detects pulses/edges.
- The ESP32 should keep a time counter in seconds.
- The time counter should increment based on the pulses received in the GPIO input.
- For every pulse, `seconds_per_pulse` seconds should be added to the time counter. `seconds_per_pulse` is a configuration.
- After `internet_gate_threshold_s` seconds the pulses started, the ESP32 creates a Soft AP with a given `soft_ap_ssid` and `soft_ap_password` and starts decrementing the time counter in real time, but should still keep incrementing it according to the pulses received in the GPIO input. `internet_gate_threshold_s`,`soft_ap_ssid` and `soft_ap_password` are configurations.
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
    - `seconds_per_pulse`, `internet_gate_threshold_s`, `meters_per_pulse`

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
6. Implement a password to access the config page. Before the config page is displayed, the user is asked to enter a password (similar to the OTA password, basic auth). The initial/default password is `esport-fi32`. Once in the configuration page, the user has the option to change the password. A reset method must be implemented: an option is to have the BOOT button pressed for at least 5 secs during runtime to reset the password. Both config and OTA passwords must be reset in this process. A long beep of 50 buzzer units must be issued. -- planned
7. Besides the bike, the system should allow an adult/admin to give internet credits to a given user based on a list of activities. The activities are pre-registered/added via a dedicated activity manager page. Each activity should have:
   - an unique identifier (it would be nice to have this autogenerated). This ID will be used system wide to identify the activity.
   - a name/description (max 40 characters)
   - how much credit the user/device earns when a given activity is completed. This is internet time in seconds, but displayed as h:mm:ss, with autoformat the same way as the counter field in the device registration. This field should allow 0:00:00 and this has the special meaning that the number of credits for this activity is dynamic and provided at the moment the activity is done by the user.
   - a time limit/duration for the activity, in seconds but also displayed as h:mm:ss. This time limit specifies what is the maximum time the user has to complete this activity.
   - the number of times this activity can be done in a day. If the activity is 'once a day', this field would have 1. Zero is not a valid value. Max value is 255.
It should be possible to associate/insert activities to a given user/registered device (the users/devices are the same we insert via config page). It should be possible to associate/insert up to 20 activities to each user.
On a separate page, called activities, the activities should be listed by user. The page should show a combobox at the top with all users in the system. When the combobox change, the list of activities should be displayed and the total credits for the user in the format h:mm:ss as well.
For each activity for the user, a button on the left side of the activity with the text "Credit h:mm:ss", when clicked/pressed should add the corresponding credits to the user counter (the credits on the side of the combobox should reflect the added credits). Every time the 'time' is credited to the user, a counter should be decremented. If the counter reaches 0 in the current day day, the button should be grayed/disabled. Activities that were registered with time 0:00:00 must not be displayed here.
The log of the last 30 credited activities should be displayed below the list of activities, per user (the log should be shown when the user is selected in the combobox and updated when an activity is credited).
An API should be available to perform the same action the 'credit' button does (the button can call that API). The API show have a field for the credits (number of seconds). For the activities that have credit different from 0:00:00, that value should be passed here. For activities with 0:00:00, the credit is dynamic and will be provided accordingly.
This should allow in the future that certain activities are like games, where the user gain more credits the faster he/she completes the activity. For example, we can have math game, where the kid should solve simple multiplication/adition/subtraction calculations faster in order to earn the credits. The faster the solution, the more credits gained.
