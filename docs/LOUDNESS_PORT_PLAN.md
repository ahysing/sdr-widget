# Loudness port layer — Phase 2 and Phase 3 plan

Phase 1 consolidated RTOS deferral back into `src/loudness.c` (no separate port
files). The `#ifdef FREERTOS_USED` block after `loudness_select_equalizer_step()`
holds the queue/task path; the `#else` branch provides synchronous stubs for PC
unit tests.

`BUILD_TESTING` is retained for exposing `static` helpers to unit tests without
changing firmware linkage.

## Current state after Phase 1

| Build | Location in `loudness.c` | Deferral behavior |
|---|---|---|
| AVR32 firmware (`FREERTOS_USED`) | `#ifdef FREERTOS_USED` block ~line 301 | Queue + `LOUDNESS` task (~20 ms) |
| PC tests (no `FREERTOS_USED`) | `#else` branch in same block | `loudness_rtos_is_ready()` → `FALSE`; direct calls |

Public API (`loudness_rtos_init`, `loudness_rtos_is_ready`,
`loudness_request_frequency_change`) is declared in `loudness.h` for all non-disabled
builds.

USB call sites (`uac2_usb_specific_request.c`, `DG8SAQ_cmd.c`) still use
`#ifdef FREERTOS_USED` to choose between `loudness_request_frequency_change()`
and `loudness_change_frequency()`.

---

## Phase 2 — Test the async path on PC without FreeRTOS

**Goal:** Exercise the same deferral logic the firmware runs, deterministically on
Windows, without a real scheduler.

### 2a. Extract request dispatch from the task loop

Move the per-request handling out of the `while (TRUE)` loop into a testable function:

```c
typedef enum {
    LOUDNESS_REQUEST_FREQUENCY,
    LOUDNESS_REQUEST_VOLUME
} loudness_request_type_t;

typedef struct {
    loudness_request_type_t type;
    uint32_t value;
} loudness_request_t;

void loudness_handle_port_request(const loudness_request_t *request);
```

The FreeRTOS task becomes a thin wrapper: receive → `loudness_handle_port_request()`.

Unit-test `loudness_handle_port_request()` directly with synthetic requests
(frequency change, volume apply) without queues or tasks.

### 2b. Add a synchronous queue fake for PC

Extend `tests/pc/usb_test_mocks.h` (or a new `tests/pc/freertos_queue_fake.c`) with:

- `xQueueCreate` / `xQueueSend` / `xQueueReceive` backed by a small ring buffer
- `loudness_test_drain_port_queue(void)` — process all pending requests synchronously

Compile the RTOS deferral path on PC behind a `LOUDNESS_PORT_FAKE_QUEUE` define for integration tests that want the full queue path without FreeRTOS.

### 2c. Add PC integration tests

New tests in `tests/pc/`:

1. `loudness_request_frequency_change(48000)` → drain queue → verify coefficients changed
2. `loudness_usb_volume_changed_left()` with port “ready” → drain → verify step switch
3. Packet-boundary / highres stride state preserved across queued frequency changes

### 2d. Align step-change detection (optional, separate from BUILD_TESTING)

`loudness_db_spl_step_changed()` uses hysteresis on firmware and a simpler
`last_db_spl_x10` comparison under `BUILD_TESTING`. Keep `BUILD_TESTING` for static
exposure, but consider driving hysteresis tests via
`loudness_test_should_change_equalizer_step()` instead of changing production
behavior in test builds.

---

## Phase 3 — Collapse caller `#ifdef` blocks

**Goal:** Single notification API at USB/audio call sites; port layer decides sync vs async.

### 3a. Unified notify API

```c
void loudness_notify_frequency_change(uint32_t frequency);
void loudness_notify_volume_changed(S16 volume_q8);  // optional consolidation
```

Implemented in the `#ifdef FREERTOS_USED` block in `loudness.c` (queue + task) or synchronously on PC (`#else` branch).

### 3b. Update call sites

Replace patterns like:

```c
#ifdef FREERTOS_USED
    loudness_request_frequency_change(current_freq.frequency);
#else
    if (current_freq.frequency != 0) {
        loudness_change_frequency(current_freq.frequency);
    }
#endif
```

with:

```c
loudness_notify_frequency_change(current_freq.frequency);
```

Files to update:

- `src/uac2_usb_specific_request.c`
- `src/DG8SAQ_cmd.c`
- Any other direct `loudness_change_frequency` / `loudness_request_frequency_change` callers

### 3c. Standardize stored SPL state on `last_db_spl_x10` (completed)

The dual SPL variable exists for ISR/task concurrency on firmware. After Phase 2
queue tests exist, evaluate whether a single published value plus port-level
serialization is sufficient, reducing `#ifdef FREERTOS_USED` in `loudness.c`.

### 3d. Documentation

Update [LOUDNESS.md](LOUDNESS.md) with the signal-chain diagram, per-channel state, highres paths, and idle bypass (done). Port-layer deferral remains documented in this file.

---

## Suggested order of work

1. Phase 2a — extract `loudness_handle_port_request()` (low risk, immediate testability)
2. Phase 2c — add drain-queue integration tests on PC
3. Phase 2b — queue fake (if direct handler tests are not enough)
4. Phase 3a–3b — unified notify API and caller cleanup
5. Phase 3c — SPL variable unification (only if measurements confirm safety)

## Success criteria

- [ ] `loudness.c` has no `#ifdef FREERTOS_USED` blocks larger than a few lines
- [ ] PC tests cover queued frequency and volume deferral paths
- [ ] No `#ifdef FREERTOS_USED` at USB sample-rate notification call sites
- [ ] Firmware behavior unchanged (PC loudness tests + device smoke test)
- [ ] `BUILD_TESTING` retained for static test accessors only
