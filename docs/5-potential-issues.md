# 5 — Potential Issues: Long-Run Network Loss / Stability

**Scope:** Investigation of the reported symptom — *the device runs for a couple of
hours/days and then loses network connectivity (possibly crashing; no logs were
captured).*

**Method:** Static review of the full firmware (`main/src`, `main/inc`), the
ESP-IDF project configuration (`sdkconfig`), and the design docs under `docs/`.
Each finding below was reasoned through to confirm it is a *plausible real cause*
of the symptom before inclusion. Confidence and severity are stated explicitly,
because without on-device logs the exact trigger cannot be proven — several of
these can occur together.

> **Most important first step (diagnostics):** the symptom description ("not sure
> if it crashes, I don't have the logs") means the single highest-value change is
> to make the failure *observable* and *self-recovering*. See
> [§0 Diagnostics](#0-make-the-failure-observable-do-this-first) and
> [Issue 1](#issue-1--no-auto-recovery-watchdog-a-wedged-task-stays-wedged-forever).

---

## Severity / likelihood summary

| # | Issue | Severity | Confidence it matches the symptom |
|---|-------|----------|-----------------------------------|
| 1 | No auto-recovery: Task WDT is non-panic and only watches the idle task | High | High |
| 2 | STA reconnect chain can stop permanently after one synchronous error | High | High |
| 3 | Heap exhaustion / fragmentation from large per-request allocations on a 2 s refresh loop | High | Medium–High |
| 4 | NAPT (NAT) table exhaustion over long uptime — internet dies, AP stays up | Medium–High | Medium |
| 5 | lwIP `netif->input`/`linkoutput` swapped from the wrong task (no core locking) | Medium | Medium |
| 6 | NVS commits run inside the `esp_timer` task and can stall reconnects | Medium | Medium |
| 7 | Default Wi-Fi modem-sleep in AP+STA causes intermittent STA drops | Medium | Medium |
| 8 | Stale NAPT / default-netif state while STA is down | Low | Low |

---

## 0. Make the failure observable (do this first)

The firmware exposes uptime in `/api/status` but **no heap or reset-reason
telemetry**, and there is no periodic diagnostic log. Before/while applying
fixes, add cheap observability so the next failure is diagnosable remotely:

- Add to the `/api/status` JSON (in
  [http_server_api.c](../main/src/http_server_api.c#L117)):
  - `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`
  - `esp_reset_reason()` captured once at boot (distinguishes panic / WDT /
    brownout / power-cycle)
- Log free heap + min-free heap once a minute (e.g. from
  [time_ctr_tick_cb](../main/src/time_counter.c#L582)).
- Confirm `CONFIG_ESP_CONSOLE_UART` stays enabled in the field build, or route a
  ring-buffer of the last N log lines into NVS / the dashboard.

A steadily falling `minimum_free_heap` points at Issues 3/5/6; a `reset_reason`
of `TASK_WDT`/`INT_WDT`/`panic` points at Issues 1/5; an AP-up-but-no-internet
state with healthy heap points at Issue 4.

---

## Issue 1 — No auto-recovery watchdog: a wedged task stays wedged forever

**Severity: High · Confidence: High**

### Evidence
`sdkconfig`:
```
CONFIG_ESP_TASK_WDT_EN=y
# CONFIG_ESP_TASK_WDT_PANIC is not set        ← WDT does NOT reboot
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y    ← only the idle task is watched
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
```
No application code calls `esp_task_wdt_add()` to subscribe the networking-
critical tasks (`sys_evt`, `tcpip`, `wifi`, `esp_timer`, `httpd`).

### Reasoning
With panic disabled, the Task Watchdog only *prints a warning* — it never
resets the chip. And because only the **idle task** is subscribed, a hang or
deadlock in the event-loop, lwIP, or Wi-Fi task does not even trigger the
warning as long as the idle task still gets scheduled. The consequence is
exactly the reported failure mode: *"it loses connectivity, I'm not sure if it
crashes, and it needs a power cycle."* There is no mechanism that brings the
device back automatically.

### Mitigation
- Enable a hard recovery path:
  ```
  CONFIG_ESP_TASK_WDT_PANIC=y      # reboot on WDT timeout
  ```
  and/or arm the **RTC watchdog** so a fully hung CPU still reboots.
- Add a lightweight **connectivity supervisor** (its own task or reuse the 1 s
  tick): if `wifi_mngr_sta_is_connected()` has been false for more than a
  configurable timeout (e.g. 10–15 min) *and* an SSID is configured, call
  `esp_restart()`. This guarantees recovery even if a lower-level subsystem
  wedges.
- Subscribe the long-lived application tasks to the Task WDT and feed it.

---

## Issue 2 — STA reconnect chain can terminate permanently after one error

**Severity: High · Confidence: High**

### Evidence
[wifi_manager.c](../main/src/wifi_manager.c#L520) — `wifi_mngr_sta_connect()`:
```c
esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
if (ESP_OK != ret) { ...; return ret; }      // just returns
ret = esp_wifi_connect();
if (ESP_OK != ret) { ESP_LOGE(...); }         // logged, nothing rescheduled
return ret;
```
[wifi_manager.c](../main/src/wifi_manager.c#L556) — the reconnect timer callback
ignores the return value:
```c
static void wifi_mngr_reconnect_timer_cb(void * p_arg)
{
    ESP_LOGI(gp_tag, "Retrying STA connection...");
    wifi_mngr_sta_connect();                  // return value discarded
}
```
The **only** place that (re)arms the 10 s reconnect timer is the
`WIFI_EVENT_STA_DISCONNECTED` handler
([wifi_manager.c](../main/src/wifi_manager.c#L818), arming the timer at
[L841](../main/src/wifi_manager.c#L841)).

### Reasoning
The reconnect state machine relies on the invariant *"every failed connection
attempt eventually produces a `WIFI_EVENT_STA_DISCONNECTED`, which re-arms the
10 s retry timer."* That invariant breaks whenever `esp_wifi_connect()` (or
`esp_wifi_set_config`) returns an error **synchronously** — i.e. the driver
never starts an association attempt, so no `STA_DISCONNECTED` event is posted.
Cases that can return synchronously after days of uptime include transient
internal Wi-Fi errors (`ESP_ERR_WIFI_CONN`), a momentary `ESP_ERR_WIFI_STATE`,
or contention with another `esp_wifi_*` call. When that happens, **no further
retry is ever scheduled** and the STA stays disconnected until a manual power
cycle — matching the report precisely.

A related dead-end: if the STA *associates* but never obtains a DHCP lease
(router DHCP hiccup), there is neither a `GOT_IP` nor necessarily another
`DISCONNECTED`, so the device can sit "associated but no IP" indefinitely.

### Mitigation
- Make the retry self-healing: in `wifi_mngr_reconnect_timer_cb()`, if
  `wifi_mngr_sta_connect()` returns non-`ESP_OK`, restart the one-shot timer
  (`esp_timer_start_once(gp_reconnect_timer, WIFI_MNGR_RECONNECT_PERIOD_US)`) so
  the chain can never break.
- Add a "connected-but-no-IP" guard: when starting a connect attempt, also arm a
  timeout; if no `IP_EVENT_STA_GOT_IP` arrives within e.g. 30 s, force
  `esp_wifi_disconnect()` + reconnect.
- The connectivity supervisor from Issue 1 is the backstop for any case missed
  here.

---

## Issue 3 — Heap exhaustion / fragmentation from large per-request allocations

**Severity: High · Confidence: Medium–High**

### Evidence
The dashboard handler allocates ~16 KB plus three arrays on **every** page load
([http_server_dashboard.c](../main/src/http_server_dashboard.c#L94)):
```c
char *  p_buf  = malloc(HTTP_SRV_HTML_BUF_LEN);   // 16384 bytes
... p_hist  = malloc(HTTP_SRV_HIST_MAX * sizeof(*p_hist));
... p_graph = malloc(HTTP_SRV_GRAPH_MAX * sizeof(*p_graph));
... p_bins  = malloc(HTTP_SRV_DAILY_WINDOW_DAYS * sizeof(*p_bins));
```
The status API allocates 8 KB on **every** poll
([http_server_api.c](../main/src/http_server_api.c#L119),
`HTTP_SRV_JSON_BUF_LEN = 8192`), and the dashboard's own JavaScript polls it
**every 2 seconds, forever**
([dashboard `setInterval(refresh,2000)`](../main/src/http_server_dashboard.c#L540)).
`/api/sessions` and `/api/sessions/daily` also each `malloc(8192)` plus a
`50 × sizeof(record)` array. Heap poisoning/tracing is **off**
(`CONFIG_HEAP_POISONING_DISABLED=y`, `CONFIG_HEAP_TRACING_OFF=y`), so there is no
in-field leak detection.

### Reasoning
The allocations are individually balanced (every path `free()`s), so this is not
a classic leak. The risk is **large-block availability over time on a
RAM-constrained part**. The ESP32-C6 has no PSRAM; the same internal heap serves
the Wi-Fi driver's *dynamic* RX/TX buffers
(`CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`,
`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y`) and all lwIP pbufs. A browser tab left
open on the dashboard generates a perpetual 8 KB alloc/free cycle, and any
opened dashboard tab adds 16 KB spikes. Under fragmentation (mixed lifetimes of
Wi-Fi buffers, TLS during OTA, session/JSON buffers), a moment arrives where a
**contiguous** 8/16 KB block can't be satisfied. Two failure shapes follow:
1. the handler `malloc` returns NULL → 500 (benign-ish), but
2. more importantly, the Wi-Fi driver's own dynamic-buffer allocations begin to
   fail → dropped frames, association loss, and difficulty re-associating —
   i.e. *loss of network connectivity*, especially when several devices stream
   at once.

This is consistent with a failure that appears only after **hours/days** and
under real multi-client load, not on the bench.

### Mitigation
- Add the heap telemetry from §0 first to confirm a downward `min_free_heap`
  trend.
- Shrink and **share** the response buffers: use a single statically-allocated
  scratch buffer guarded by a mutex (the HTTP server is effectively serialized
  per socket), instead of multiple multi-KB `malloc`s per request.
- Stream the dashboard with smaller chunks (it already uses
  `httpd_resp_sendstr_chunk`); the 16 KB working buffer can be a few hundred
  bytes reused per chunk.
- Raise the JS poll interval (e.g. 2 s → 5 s) and/or stop polling when the tab is
  hidden (`document.hidden`).
- Reserve Wi-Fi headroom: consider raising static RX/TX buffer counts so the
  driver is less dependent on the fragmenting heap, and cap concurrent
  `httpd` sockets.

---

## Issue 4 — NAPT (NAT) table exhaustion: internet dies while the AP stays up

**Severity: Medium–High · Confidence: Medium**

### Evidence
`sdkconfig`:
```
CONFIG_LWIP_IPV4_NAPT=y
CONFIG_LWIP_IPV4_NAPT_PORTMAP=y
```
All reward-AP client internet traffic is NATed through the STA interface
(`esp_netif_napt_enable(gp_netif_ap)` in
[wifi_manager.c](../main/src/wifi_manager.c#L793)). `IP_NAPT_MAX` is left at the
lwIP default (not overridden in `sdkconfig`).

### Reasoning
The lwIP NAPT translation table is a **fixed-size** array. Each outbound flow
consumes an entry; entries are only reclaimed on timeout. Children streaming
video/games open many short-lived TCP/UDP flows; with `TCP_MSL=60000` (60 s)
and NAPT's own timeouts, stale entries linger. Over a long session the table can
**fill**, after which new outbound connections are silently dropped while
existing/idle ones and the local dashboard keep working. The user-visible result
is "the internet stopped working" even though the AP is still associated — a
very common ESP32 SoftAP+NAT long-run complaint and a plausible match for
"loses network connection" if the symptom is *clients lose internet* rather than
*the device disappears*.

### Mitigation
- Increase the NAT table size (`IP_NAPT_MAX`) via a custom `lwipopts`/sdkconfig
  override sized for the expected number of devices × concurrent flows.
- Tune NAPT/TCP timeouts so dead flows are reclaimed faster.
- As a stopgap, the connectivity supervisor (Issue 1) bouncing the STA also
  resets NAT state; document that a periodic STA reconnect clears a saturated
  table.
- Add a diagnostic counter for "AP→internet packets dropped" to confirm.

---

## Issue 5 — lwIP netif function pointers swapped from the wrong task

**Severity: Medium · Confidence: Medium**

### Evidence
`CONFIG_LWIP_TCPIP_CORE_LOCKING is not set`.
[wifi_mngr_ap_hooks_install()](../main/src/wifi_manager.c#L709) directly rewrites
the lwIP `struct netif` pointers:
```c
gp_orig_ap_input      = p_netif->input;
gp_orig_ap_linkoutput = p_netif->linkoutput;
p_netif->input        = wifi_mngr_ap_input_hook;
p_netif->linkoutput   = wifi_mngr_ap_linkoutput_hook;
```
This runs from the `WIFI_EVENT_AP_START` handler
([wifi_manager.c](../main/src/wifi_manager.c#L813)), i.e. in the **system event
task**, while the **tcpip task** may simultaneously be calling
`netif->input(...)` to deliver a received frame.

### Reasoning
Without TCPIP core locking, lwIP data structures must only be mutated from the
tcpip thread (or under the core lock). Here the pointers are swapped from a
different task. A frame arriving during the swap can dereference a torn/half-
updated `input` pointer, or the original pointer can be captured inconsistently
with `linkoutput`. The window is small and only opens on each AP (re)start, but
AP restarts happen whenever the reward-AP config is re-applied, and the failure
mode (jump through a bad function pointer in the network path) is catastrophic —
a crash or total AP-traffic stall. Low frequency, high impact; over days the
probability accumulates.

### Mitigation
- Perform the pointer swap inside the tcpip context, e.g. wrap it with
  `esp_netif_tcpip_exec()` (runs the callback in the tcpip task), or take the
  lwIP core lock (`LOCK_TCPIP_CORE()` / enable
  `CONFIG_LWIP_TCPIP_CORE_LOCKING`).
- Prefer the supported `esp_netif`/lwIP hook mechanisms (e.g. a netif
  status/input callback) over hand-patching function pointers.
- If byte accounting is the only goal, consider deriving throughput from
  `esp_wifi`/`esp_netif` statistics instead of intercepting `input`/
  `linkoutput`.

---

## Issue 6 — NVS commits inside the `esp_timer` callback chain

**Severity: Medium · Confidence: Medium**

### Evidence
The 1 s tick runs in the `esp_timer` task and, every 60 s when counters are
dirty, performs several blocking NVS commits:
[device_reg_tick()](../main/src/device_registry.c#L748) →
[device_reg_counters_save_all()](../main/src/device_registry.c#L1002) →
`device_reg_entry_ctr_save()` does `nvs_open`/`nvs_set_u32`/`nvs_commit`/
`nvs_close` per device. The **same** `esp_timer` task also dispatches the
Wi-Fi **reconnect** one-shot ([wifi_manager.c](../main/src/wifi_manager.c#L258),
`dispatch_method = ESP_TIMER_TASK`) and the gate/threshold timers.

### Reasoning
`nvs_commit()` performs flash erase/write and **blocks**. As flash ages over
weeks/months, commit latency grows. Because persistence shares the high-priority
`esp_timer` task with the Wi-Fi reconnect timer and the 1 s cadence, a long
commit:
- delays a scheduled STA reconnect (compounds Issue 2), and
- can starve lower-priority tasks (including idle) long enough to trip the Task
  WDT — which, given Issue 1, just prints rather than recovering.
This degrades slowly with uptime/flash wear, matching "after hours/days."

### Mitigation
- Move NVS persistence off the timer task: have the tick `xQueueSend` a
  "save" request to a dedicated low-priority worker task that performs the
  commits.
- Batch all device counters into a **single** blob/commit instead of one
  open+commit per device.
- Re-evaluate the 60 s save cadence and the per-second counter writes against
  flash endurance and partition size.

---

## Issue 7 — Default Wi-Fi modem-sleep in AP+STA causes intermittent STA drops

**Severity: Medium · Confidence: Medium**

### Evidence
No call to `esp_wifi_set_ps()` exists anywhere in `main/src`, so the driver keeps
the default `WIFI_PS_MIN_MODEM`. `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y`
is also set.

### Reasoning
Running STA power-save while simultaneously hosting a SoftAP is a well-known
source of beacon-timeout disconnects and missed downlink frames on ESP32-family
parts. Each spurious STA disconnect triggers the reconnect path (Issue 2's
fragility) and the AP-side DNS-forward/NAPT churn (Issue 8), so frequent drops
both raise the odds of hitting the permanent dead-end and degrade AP-client
internet. Because the device is mains/USB-powered and the AP must stay awake
anyway, modem-sleep yields no benefit here — only instability.

### Mitigation
- Call `esp_wifi_set_ps(WIFI_PS_NONE)` after `esp_wifi_start()` in
  [wifi_mngr_init()](../main/src/wifi_manager.c#L175). This typically eliminates
  a large class of long-run STA disconnects in AP+STA designs.

---

## Issue 8 — Stale NAPT / default-netif state while the STA is down

**Severity: Low · Confidence: Low**

### Evidence
The `WIFI_EVENT_STA_DISCONNECTED` handler
([wifi_manager.c](../main/src/wifi_manager.c#L818)) sets `gb_sta_connected =
false` and schedules a reconnect, but does **not** reset the default netif or the
AP DHCP-advertised DNS. NAPT remains enabled pointing at the now-invalid STA
route.

### Reasoning
While the STA is disconnected, AP clients keep being handed the previously
forwarded DNS server and their NATed traffic targets a dead default route, so it
is silently dropped. This is mostly cosmetic because it self-heals on reconnect
(`GOT_IP` re-forwards DNS and re-enables NAPT), but during long outages it
presents as "the network is up but nothing works," and repeated forward/restart
of the AP DHCP server on every flap adds churn. Worth tightening once Issues 2/7
are addressed.

### Mitigation
- On `STA_DISCONNECTED`, optionally point AP clients at the local gateway
  (192.168.5.1) for DNS or mark internet unavailable, and restore on reconnect.
- Ensure DHCP-server stop/start churn is minimized (only re-forward DNS when the
  STA DNS actually changed).

---

## Recommended order of work

1. **Observability + auto-recovery** (§0, Issue 1) — makes the next failure
   diagnosable and stops it from being permanent. Lowest effort, highest payoff.
2. **Reconnect robustness** (Issue 2) and **disable modem-sleep** (Issue 7) —
   small, targeted code changes that directly address STA loss.
3. **Heap telemetry → buffer reduction** (Issue 3) — confirm with data, then
   shrink/share buffers.
4. **NAPT sizing** (Issue 4) and **NVS off the timer task** (Issue 6).
5. **Thread-safe netif hook** (Issue 5) — correctness hardening of the byte-
   accounting path.

None of these are mutually exclusive; Issues 1, 2, and 7 together are the most
likely to stop the reported "loses connectivity and never comes back" behaviour,
while Issues 3 and 4 are the most likely explanations if the device stays up but
*clients* lose internet.

---

# Part 2 — Follow-up: `other_wdt` reboot loop during a ride session

**New symptom (post-fixes):** during a live rider session (sensor pulsing,
internet time incrementing) the device rebooted once, then entered a continuous
reboot loop. The operator could not log in; telemetry showed nothing unusual
except the reset reason **`other_wdt`**.

**Method:** static re-review of the full boot path
([main.c](../main/src/main.c)), the auto-recovery code added in Part 1
([wifi_manager.c](../main/src/wifi_manager.c)), the pulse ISR
([pulse_input.c](../main/src/pulse_input.c)), the per-second tick / NVS paths
([time_counter.c](../main/src/time_counter.c),
[device_registry.c](../main/src/device_registry.c)), and the watchdog / panic /
coredump configuration ([sdkconfig](../sdkconfig)).

## What `other_wdt` rules out — and points to

`other_wdt` is `esp_reset_reason() == ESP_RST_WDT`
([mapping](../main/src/http_server_telemetry.c#L81)). After the Part 1 fixes this
value is now **diagnostic by elimination**:

| If it were… | the reset reason would read | Config evidence |
|-------------|------------------------------|-----------------|
| A watched task hanging | `task_wdt` | `CONFIG_ESP_TASK_WDT_PANIC=y` ([sdkconfig](../sdkconfig#L1214)) |
| Interrupts disabled too long | `int_wdt` | `CONFIG_ESP_INT_WDT=y` ([sdkconfig](../sdkconfig#L1210)) |
| The new supervisor `esp_restart()` | `software` | sets a SW reset hint |
| An ordinary crash/abort | `panic` | `PANIC_PRINT_REBOOT` ([sdkconfig](../sdkconfig#L1175)) |

`ESP_RST_WDT` means the reset fired on a path that stored **no panic/WDT hint** —
on the ESP32-C6 this is almost always the **RTC / bootloader watchdog**
(`CONFIG_BOOTLOADER_WDT_TIME_MS=9000`, [sdkconfig](../sdkconfig#L510)) catching a
**hang during boot / early init**, or a lockup so hard the panic handler could
not run. A *repeating* `other_wdt` loop implies persistent (NVS / RTC) state that
drives the device back into the same early failure every boot — consistent with
"rebooted mid-session, then looped, dashboard never came up."

## Findings

### Finding 0 — The failure was undiagnosable (no crash capture) · **FIXED**
There was **no core dump** configured (`CONFIG_ESP_COREDUMP_*` absent from
[sdkconfig](../sdkconfig)), **no coredump partition**
([partitions.csv](../partitions.csv)), and panic reboot delay was 0 with no log
retention. That is exactly why telemetry "showed nothing." Note also
`CONFIG_ESP_SYSTEM_NO_BACKTRACE=y` ([sdkconfig](../sdkconfig#L1181)).

### Finding 1 — The new connectivity supervisor can itself loop-reboot · open
[wifi_mngr_supervisor_timer_cb()](../main/src/wifi_manager.c#L713) calls
[esp_restart()](../main/src/wifi_manager.c#L741) after the STA is down 10 min
while an SSID is configured. If the home Wi-Fi is simply unreachable at the ride
location, the device reboots every ~10 min — tearing down the reward AP each
time. Reports as `software` (so probably not the exact reset observed) but it is
a real reboot-cycling contributor. *Recommended:* bounce only the STA, never the
AP; add exponential backoff + a reboot counter. (Safe mode below now suppresses
this supervisor while a loop is in progress.)

### Finding 2 — Pulse GPIO has no hardware glitch filter (ISR-storm risk) · open
[pulse_in_init()](../main/src/pulse_input.c#L151) uses `GPIO_INTR_ANYEDGE` with
**software** debounce only — the ISR still runs on *every* electrical edge
([ISR](../main/src/pulse_input.c#L286)). A dirty reed switch / EMI while pedaling
can produce an edge burst that starves the CPU in interrupt context, matching
"started during a ride." The C6 has unused hardware glitch filters
(`CONFIG_SOC_GPIO_SUPPORT_PIN_GLITCH_FILTER=y`,
[sdkconfig](../sdkconfig#L124)). *Recommended:* enable a pin/flex glitch filter
on the pulse GPIO. (Safe mode below disables the pulse ISR while looping.)

### Finding 3 — No reboot-loop backoff / safe mode · **FIXED**
Nothing broke a reboot loop: any cause restarted instantly and indefinitely,
killing the AP each cycle and preventing login.

### Finding 4 — Boot-time NVS reads scale with session churn · open
[act_mngr_init()](../main/src/activity_manager.c#L119) (up to 30 pool blobs +
per-device blobs, then an auto-heal loop that *commits* at boot) and
[device_reg_init()](../main/src/device_registry.c#L216) read NVS that is written
every 60 s during a session on an 80 K nvs partition
([partitions.csv](../partitions.csv#L3)). On a fragmented partition, NVS garbage
collection can add seconds to these reads — a plausible path to the 9 s RTC-WDT
boot hang. *Recommended:* move high-churn counters to a dedicated/larger nvs and
avoid committing inside the boot-time auto-heal loop.

### Finding 5 — AP input hook reads packet bytes before the length check · **FIXED**
[wifi_mngr_ap_input_hook()](../main/src/wifi_manager.c#L831) dereferenced
`p->payload + 6..11` (the source MAC, for per-device byte accounting) *before*
the `p->len >= MIN_ETHERNET_IPV4_LEN` guard — an out-of-bounds read on a runt
pbuf in the RX hot path. Fixed by moving a length check
(`p->len < WIFI_MNGR_ETH_HEADER_LEN`, i.e. < 14 bytes) to the very top of the
hook, before any payload dereference; the pre-existing global byte counter
(which never touched the payload) was left unconditional, and runt frames are
now counted only globally and passed through untouched instead of being
parsed.

### Finding 6 — Brownout at least-sensitive level · low
`CONFIG_ESP_BROWNOUT_DET_LVL=7` ([sdkconfig](../sdkconfig#L1068)); a marginal USB
supply could reset under TX/buzzer spikes, but that reports `brownout`, so it is
probably not this symptom. Confirm via reset-reason history once Finding 0 is in
place.

## Fixes applied in this change (scope: 0 + 3)

**Finding 0 — crash capture (`other_wdt` becomes decodable):**
- Carved a **128 K `coredump` partition** out of the two OTA slots (each
  1984 K → 1920 K; the app is ~1.06 MB so it still fits comfortably) —
  [partitions.csv](../partitions.csv).
- Enabled ELF core dump to flash with a CRC32 integrity check and boot-time
  validation in [sdkconfig.defaults](../sdkconfig.defaults)
  (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`, `…_DATA_FORMAT_ELF`,
  `…_CHECKSUM_CRC32`, `…_CHECK_BOOT`).
- After the next field failure, decode with:
  `idf.py coredump-info` / `idf.py coredump-debug`, or
  `esp-coredump info_corefile -c <coredump.bin> build/esport-fi32.elf`.
- *Optional follow-up for richer RISC-V backtraces:* set
  `CONFIG_ESP_SYSTEM_USE_FRAME_POINTER=y` (clears `…_NO_BACKTRACE`) at a small
  size/perf cost.
- *Build notes (two IDF gotchas hit while enabling this):*
  (1) The `espcoredump` component is **not** pulled into the build automatically,
  so its `CONFIG_ESP_COREDUMP_*` symbols never appear and the defaults are
  silently dropped. It is now listed under `PRIV_REQUIRES` in
  [main/CMakeLists.txt](../main/CMakeLists.txt) to force it into the build closure.
  (2) `sdkconfig.defaults` is only merged when `sdkconfig` does **not** already
  exist; editing defaults alone does nothing. Delete `sdkconfig` and run
  `idf.py reconfigure` to re-apply. Enabling coredump-to-flash also auto-raises
  `CONFIG_FREERTOS_ISR_STACKSIZE` (1536 → 2096) — expected. The pre-existing
  per-device `CONFIG_ESPORT_REWARD_AP_SSID="esport-fi32_1"` override was moved
  into `sdkconfig.defaults` so it survives regeneration.

**Finding 3 — reboot-loop guard + safe mode:**
- Added an `RTC_NOINIT`-backed consecutive-fast-reboot counter in
  [main.c](../main/src/main.c) (`boot_guard_evaluate()`), magic-guarded so a true
  power-on starts fresh. A one-shot 60 s timer (`boot_guard_clear_cb()`) clears
  it once the device proves a stable uptime, so only *rapid* loops accumulate.
- After 5 fast reboots the device boots in **safe mode**: the reward AP and
  HTTP dashboard come up normally, but the pulse ISR, the 1 s tick (periodic NVS
  commits) and session tracking are skipped, and the Wi-Fi STA connect +
  connectivity supervisor are suppressed via the new
  [wifi_mngr_safe_mode_set()](../main/inc/wifi_manager.h). This both removes the
  most likely loop triggers (Findings 1, 2, 4) and guarantees the operator can
  log in to read the reset reason and the captured core dump.

Together these make the next `other_wdt` self-evident (core dump + reset-reason
history) and stop the device from bricking itself in the field. Findings 1, 2,
and 4 remain open as the likely *root* triggers to address next (Finding 5 is
now fixed).
