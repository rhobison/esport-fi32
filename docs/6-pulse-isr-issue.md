# 6 — Pulse ISR Cache-Access-Error Crash (sporadic reset during a ride)

**Scope:** Root-cause analysis and fix for a sporadic hard reset captured via a
core dump, traced to the pulse-counting GPIO ISR in
[pulse_input.c](../main/src/pulse_input.c). Related to, but a distinct root
cause from, **Finding 2** ("Pulse GPIO has no hardware glitch filter") in
[5-potential-issues.md](5-potential-issues.md) — that finding hypothesized an
ISR-storm from electrical noise; this document shows the actual captured crash
was a flash-cache/ISR-safety violation, not a glitch storm. Finding 2's glitch
filter recommendation is still worth doing but is orthogonal to the fix here.

---

## 1. Symptom

Device resets sporadically, correlated with active use (rider pedaling). A
core dump was captured to the `coredump` flash partition (see Finding 0 in
[5-potential-issues.md](5-potential-issues.md)) and decoded offline.

## 2. Captured core dump

```
=== Core Dump Summary ===
Panic reason : n/a
Faulting task: esp_timer   PC: 0x40805398

=== RISC-V Registers ===
MCAUSE  0x00000019   MSTATUS 0x00001881
MTVEC   0x40800001   MTVAL   0x0000d422
RA      0x40805388   SP      0x40825350
A0      0x4087f3c4
A1      0x00000000
A2      0x00000000
A3      0x00000000
A4      0x00000000
A5      0x4082539c
A6      0x00000000
A7      0x00000003

=== Stack Dump (1024 bytes @ SP 0x40825350) ===
0x40825350  00 00 00 00 01 00 00 00 19 00 00 00 01 00 00 00   |................|
0x40825360  00 00 00 18 01 00 00 00 17 00 00 00 00 00 00 00   |................|
0x40825370  00 00 00 18 00 00 08 00 1c fd 87 40 88 53 80 40   |...........@.S.@|
0x40825380  00 00 00 00 00 00 00 00 0a 00 00 00 4c 1f 80 40   |............L..@|
0x40825390  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x408253a0  81 18 00 00 00 00 00 00 0a 00 00 00 d4 1f 80 40   |...............@|
0x408253b0  84 35 83 40 00 00 00 00 00 20 0c 60 01 00 00 00   |.5.@..... .`....|
0x408253c0  81 18 00 00 0b 00 00 80 a0 a2 00 00 6e 20 80 40   |............n .@|
0x408253d0  81 18 00 00 09 00 00 80 08 00 00 00 c8 cc 81 40   |...............@|
0x408253e0  00 00 00 00 00 00 00 00 00 00 00 00 6c 02 80 40   |............l..@|
0x408253f0  00 a0 00 60 0c 71 80 40 18 71 80 40 00 00 00 00   |...`.q.@.q.@....|
0x40825400  fc 53 82 40 00 00 00 00 00 00 00 00 00 00 00 00   |.S.@............|
0x40825410  14 54 82 40 ff ff ff ff 14 54 82 40 14 54 82 40   |.T.@.....T.@.T.@|
0x40825420  00 00 00 00 28 54 82 40 ff ff ff ff 28 54 82 40   |....(T.@....(T.@|
0x40825430  28 54 82 40 01 00 00 00 01 00 00 00 00 00 00 00   |(T.@............|
0x40825440  ff ff 01 00 00 00 00 00 00 00 00 00 04 00 00 00   |................|
0x40825450  00 00 00 00 00 00 00 00 00 00 00 00 58 54 82 40   |............XT.@|
0x40825460  00 00 00 00 00 00 00 00 00 00 00 00 70 54 82 40   |............pT.@|
0x40825470  ff ff ff ff 70 54 82 40 70 54 82 40 00 00 00 00   |....pT.@pT.@....|
0x40825480  84 54 82 40 ff ff ff ff 84 54 82 40 84 54 82 40   |.T.@.....T.@.T.@|
0x40825490  01 00 00 00 01 00 00 00 00 00 00 00 ff ff 01 00   |................|
0x408254a0  00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00   |................|
0x408254b0  00 00 00 00 6c 18 0c 42 03 00 00 00 60 32 0c 42   |....l..B....`2.B|
0x408254c0  0b 00 00 00 88 96 0c 42 13 00 00 00 c0 ad 0c 42   |.......B.......B|
0x408254d0  1b 00 00 00 a0 9a 0c 42 23 00 00 00 74 ac 0c 42   |.......B#...t..B|
0x408254e0  2b 00 00 00 3c 9b 0c 42 33 00 00 00 28 19 0c 42   |+...<..B3...(..B|
0x408254f0  3b 00 00 00 dc 24 0d 42 43 00 00 00 50 94 0d 42   |;....$.BC...P..B|
0x40825500  4b 00 00 00 70 62 0d 42 53 00 00 00 8c 60 0d 42   |K...pb.BS....`.B|
0x40825510  5b 00 00 00 98 26 0c 42 63 00 00 00 74 39 0c 42   |[....&.Bc...t9.B|
0x40825520  6b 00 00 00 0c 59 0d 42 73 00 00 00 90 4d 0c 42   |k....Y.Bs....M.B|
0x40825530  7b 00 00 00 c4 41 0c 42 83 00 00 00 60 4b 0c 42   |{....A.B....`K.B|
0x40825540  8b 00 00 00 10 43 0c 42 93 00 00 00 58 44 0c 42   |.....C.B....XD.B|
0x40825550  9b 00 00 00 f4 48 0c 42 a3 00 00 00 40 b0 0d 42   |.....H.B....@..B|
0x40825560  ab 00 00 00 18 74 0d 42 b3 00 00 00 d8 6f 0d 42   |.....t.B.....o.B|
0x40825570  bb 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x40825580  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x40825590  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x408255a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x408255b0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00   |................|
0x408255c0  00 00 00 00 00 00 00 00 70 41 83 40 94 41 83 40   |........pA.@.A.@|
0x408255d0  bc 41 83 40 e0 41 83 40 2c f5 87 40 00 00 00 00   |.A.@.A.@,..@....|
0x408255e0  00 00 00 00 00 00 00 00 a5 a5 a5 a5 a5 a5 a5 a5   |................|
0x408255f0  a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5 a5   |................|
... (0xA5 fill pattern to end of dump) ...

App SHA256   : 9810bd48c  (core dump v852226)
```

The App SHA256 (`9810bd48c…`) matched `build/esport-fi32.elf` **exactly** at
the time of analysis, confirming this crash occurred on the current build —
not a stale/pre-fix binary.

## 3. Decoding `MCAUSE 0x19`

`0x19` = 25 decimal. On RISC-V, cause codes 24–31 are "reserved for custom
use"; ESP32-C6 assigns:

| Value | Symbol | Meaning |
|---|---|---|
| 24 | `ETS_T1_WDT_INUM` | Interrupt watchdog |
| **25** | **`ETS_CACHEERR_INUM`** | **Cache access error** |
| 26 | `ETS_MEMPROT_ERR_INUM` | Memory protection fault |
| 27 | `ETS_ASSIST_DEBUG_INUM` | Hardware stack-guard fault |

So this was **not** a standard synchronous exception, a stack overflow, or a
memory-protection fault — it's ESP-IDF's own `esp_cache_err_int.c`:

> "The cache has an interrupt that can be raised as soon as an access to a
> cached region (flash) is done without the cache being enabled."

This explains `Panic reason: n/a` in the summary: cause 25 falls outside the
16-entry standard exception-name table used by the offline decoder, and the
human-readable "Cache access error" string requires live cache-controller
status registers that a static core dump doesn't preserve.

Cache-error interrupts are also **asynchronous** relative to the offending
memory access (they are ordinary maskable interrupts, not synchronous traps),
so the reported `PC`/`RA` reflect wherever the CPU had reached by the time the
interrupt was serviced — not necessarily the exact faulting instruction.

## 4. What was executing

Resolving the addresses against `build/esport-fi32.elf` (SHA-matched):

| Register | Address | Resolves to |
|---|---|---|
| `PC` | `0x40805398` | `esp_event_isr_post_to()` — `esp_event.c:989` |
| `RA` | `0x40805388` | `esp_event_isr_post()` — `default_event_loop.c:89` |

Both addresses are in IRAM (`0x4080….`). The **only** call site for
`esp_event_isr_post()` anywhere in this codebase was
[pulse_input.c](../main/src/pulse_input.c) — the GPIO pulse-counting ISR:

```c
static void IRAM_ATTR pulse_in_gpio_isr(void * p_arg)   // installed with ESP_INTR_FLAG_IRAM
{
    ...
    esp_event_isr_post(ESPORT_EVENT_BASE, ESPORT_EVENT_PULSE, NULL, 0, &hp_task_awoken);
}
```

`gpio_install_isr_service(ESP_INTR_FLAG_IRAM)` explicitly exempts this ISR
from ESP-IDF's usual protection that disables non-IRAM interrupts during SPI
flash write/erase — meaning this ISR is *designed* to run even while the flash
cache is disabled. **"Faulting task: esp_timer" is a red herring**: an ISR has
no TCB of its own; the summary just reports whichever task happened to be
running when the pulse interrupt preempted it.

## 5. Root cause

1. A pulse (wheel/pedal rotation) arrives and fires `pulse_in_gpio_isr()`.
2. Because the ISR is `ESP_INTR_FLAG_IRAM`, it is allowed to run even if a
   concurrent SPI flash write/erase has disabled the flash cache system-wide
   (per ESP-IDF docs: *"When the caches are disabled, all non-IRAM-safe
   interrupts will be disabled... only IRAM-safe interrupt handlers will be
   executed"*). This project performs many such writes: `nvs_commit()` in
   [activity_manager.c](../main/src/activity_manager.c#L320),
   [config_manager.c](../main/src/config_manager.c#L195),
   [device_registry.c](../main/src/device_registry.c#L320), plus OTA writes.
3. The ISR calls `esp_event_isr_post()` → `esp_event_isr_post_to()`. Somewhere
   in or shortly before that reachable call graph, code touched something
   backed by the (currently disabled) flash cache, tripping
   `ETS_CACHEERR_INUM`.
4. Because a `pulse_in_gpio_isr()` call can happen at any point (mechanical
   pulses aren't synchronized to flash-write timing), this only reproduces on
   the rare coincidence of a pulse landing inside one of the brief (sub-ms to,
   for a sector erase, tens–hundreds of ms) cache-disabled windows —
   consistent with "sporadic" and correlated with active riding (more pulses
   → more chances to hit a write window).

This is the well-documented ESP-IDF hazard: *"you must ensure that all data
and functions accessed by \[an `ESP_INTR_FLAG_IRAM`\] interrupt handler,
including the ones that handler calls, are located in IRAM or DRAM."*
(`docs/api-reference/system/intr_alloc.rst`).

## 6. Fix implemented

### Design goal
Remove the flash/cache-dependent call from ISR context, **without** silently
under-counting pulses. Both consumers of `ESPORT_EVENT_PULSE` treat every
single delivered event as exactly one physical pulse:

- [time_counter.c](../main/src/time_counter.c#L285-L317)
  `time_ctr_pulse_handler()` credits a fixed `seconds_per_pulse` **per call**.
- [session_tracker.c](../main/src/session_tracker.c#L236-L280)
  `session_trk_pulse_handler()` does `g_pulse_count++` **per call**.

A naive "just wake a task" signal (binary semaphore / plain notify) would
coalesce bursts of pulses that arrive before the consumer task is scheduled
into a single event — silently under-crediting the rider. The fix therefore
uses FreeRTOS task notifications **as a counting semaphore**, which preserves
an exact 1:1 mapping between physical pulses and posted events.

### Changes — [pulse_input.c](../main/src/pulse_input.c)
- The ISR (`pulse_in_gpio_isr`) no longer calls `esp_event_isr_post()`. It now
  only calls `vTaskNotifyGiveFromISR(g_forward_task_handle, ...)` (NULL-guarded)
  — confirmed IRAM-resident in this build (§8) — and everything else it
  touches was already a `volatile` global.
- A new dedicated task, `pulse_in_forward_task` ("pulse_fwd"), created in
  `pulse_in_init()` *before* the ISR is attached, loops:
  `ulTaskNotifyTake(pdFALSE, <bounded timeout>)` → on a non-zero return
  (meaning one "give" was consumed), calls the ordinary, task-context-safe
  `esp_event_post()`. `pdFALSE` makes each call decrement-and-return the
  pre-decrement notification value, so N pulses given by the ISR result in
  exactly N sequential `esp_event_post()` calls — no coalescing, no
  reordering.
  - Priority **10**, stack **3072 bytes**: above ordinary app tasks
    (`dev_reg_nvs_task`@3, `httpd`@5) so the hand-off adds negligible delay,
    well below system tasks (`tcpip`@18, `sys_evt`@20, `esp_timer`@22,
    `wifi`@23) so it can never contend with networking-critical scheduling.
  - `esp_event_post()` timeout: `pdMS_TO_TICKS(20)` — enough to absorb a
    transient default-loop queue burst (`CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE=32`)
    without blocking the forwarder for long.
  - Subscribed to the Task Watchdog (`esp_task_wdt_add(NULL)`); the
    `ulTaskNotifyTake` wait is bounded at 2 s (not `portMAX_DELAY`) purely so
    the task still feeds the watchdog during long idle periods (rider not
    pedaling) — real pulses still wake it immediately, so this adds no
    latency to pulse delivery. Without this bound, a subscribed task that
    blocks indefinitely with no pulses would falsely trip
    `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` and reboot the device simply because
    nobody was exercising.
- Doc comments (`pulse_input.h`, ISR/task headers) updated to describe the new
  hand-off and stop referencing `esp_event_isr_post` as the failure source for
  `pulse_in_last_post_err_get()`.

### Changes — [device_registry.c](../main/src/device_registry.c)
`device_reg_nvs_task` had the identical "blocks forever, never feeds the
watchdog while idle" hazard (`xQueueReceive(..., portMAX_DELAY)`). Since it
was already selected for Task Watchdog subscription in this change (see §7
scope), its wait was also bounded to 2 s and it now calls
`esp_task_wdt_reset()` every loop iteration regardless of whether a save
request was pending, for the same reason as `pulse_fwd` above.

## 7. Scope decision: Task Watchdog subscription

`docs/5-potential-issues.md` (Issue 1) already identified that **no task in
this firmware is subscribed to the Task Watchdog** except the idle task, even
though `CONFIG_ESP_TASK_WDT_PANIC=y` is enabled. Its recommended list is the
five system tasks: `sys_evt`, `tcpip`, `wifi`, `esp_timer`, `httpd`.

**Decision for this change:** subscribe only the two lightweight app-owned
tasks directly touched here — the new `pulse_fwd` task and the existing
`dev_reg_nvs_task` — since both were already being modified and both gate
real product behavior (pulse-driven crediting, NVS persistence). Subscribing
the five system tasks requires fetching task handles from other subsystems
(e.g. `xTaskGetHandle("sys_evt")`, `esp_timer_impl_get_timer_task_handle()`)
and is **out of scope for this change** — see the handover note below.

## 8. Verification

- **Build:** `idf.py build` completes clean with the changes above.
- **IRAM placement (critical safety property):** confirmed directly from the
  linked ELF via `riscv32-esp-elf-nm`:

  ```
  40801e42 t pulse_in_gpio_isr              (IRAM — required, IRAM_ATTR)
  4080ca06 T vTaskGenericNotifyGiveFromISR  (IRAM — required, called from the ISR)
  4080c92e T ulTaskGenericNotifyTake        (IRAM — incidental, called from task context)
  4200f166 t pulse_in_forward_task          (flash — expected/desired, task-context only)
  ```

  The two functions actually reachable from ISR context
  (`pulse_in_gpio_isr`, `vTaskGenericNotifyGiveFromISR`) are IRAM-resident;
  `pulse_in_forward_task` itself correctly lives in flash since it only ever
  runs in task context, where ESP-IDF suspends execution (rather than
  crashing) during any flash cache-disable window.
- `CONFIG_ESP_EVENT_POST_FROM_IRAM_ISR=y` was already enabled in `sdkconfig`,
  confirming `esp_event_isr_post_to()` itself was correctly IRAM-placed
  (matches its resolved address in §4) — i.e. the bug was not a missing-Kconfig
  issue, but the inherent risk of calling *any* non-trivial ESP-IDF subsystem
  from `ESP_INTR_FLAG_IRAM` context, which this fix removes entirely by moving
  the call to task context.

## 9. Trade-offs accepted

- **Latency:** one extra scheduling hop (ISR → notify → `pulse_fwd` wakes →
  `esp_event_post()` → `sys_evt` task → handlers) versus the previous direct
  ISR-to-queue post. Expected to be negligible at realistic pulse cadences;
  not expected to affect debounce/qualifying-window tolerances, but worth
  keeping in mind if those windows are ever tightened.
- **RAM:** +1 FreeRTOS task (TCB + 3072-byte stack), permanent for the
  firmware's lifetime.
- **Backpressure semantics:** `esp_event_post()` (task context) can block up
  to 20 ms instead of failing instantly like the ISR variant had to; this is
  expected to reduce `pulse_in_dropped_count_get()` counts, not increase them.

## 10. Follow-up (not done in this change)

- Subscribe the five system tasks (`sys_evt`, `tcpip`, `wifi`, `esp_timer`,
  `httpd`) to the Task Watchdog per `docs/5-potential-issues.md` Issue 1 — see
  handover prompt.
- `docs/5-potential-issues.md` Finding 2 (GPIO glitch filter) remains open and
  is unrelated to this fix; still recommended as defense-in-depth against a
  genuine electrical-noise ISR storm.
