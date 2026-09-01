# USB statistics HID packet

Firmware exposes a 1 Hz statistics stream over a vendor HID interface (`usage_page=0xFF00`). The host tool [`usbstatistics/usbstatistics.py`](../usbstatistics/usbstatistics.py) reads and decodes it.

## Transport

- **Report ID:** `1`
- **HID transfer size:** 64 bytes (44-byte wire payload + zero padding to 63 bytes after report ID)
- **Rate:** one report per second (`statistics_task`, FreeRTOS priority `tskIDLE_PRIORITY + 2`)
- **HID send wait:** up to 50 ms per report for EP6 IN ready; counters are preserved and retried on the next tick if send fails
- **Endianness:** little-endian for multi-byte fields
- **Checksum:** byte index 3 is XOR of all other wire bytes

## Wire layout (version 5, 44 bytes)

| Offset | Field | Type | Semantics |
|--------|-------|------|-----------|
| 0 | `hid_anchor` | U8 | `0x53` — locates stats payload inside the 64-byte HID report |
| 1 | `version` | U8 | `5` |
| 2 | `report_seq` | U8 | Monotonic sequence (advanced only after successful HID IN) |
| 3 | `checksum` | U8 | XOR of all wire bytes except this byte |
| 4–7 | `overruns` | U32 LE | FIFO gap ≥ 2× buffer size (per period) |
| 8–11 | `underruns` | U32 LE | FIFO gap == 0 (per period) |
| 12–13 | `fifo_level` | U16 LE | Last sampled gap at end of period |
| 14–15 | `max_fifo` | U16 LE | Peak gap in period |
| 16–17 | `min_fifo` | U16 LE | Minimum gap; `0xFFFF` = idle sentinel |
| 18–21 | `deadline_misses` | U32 LE | Audio-task scheduler slips > 10 ms |
| 22–23 | `frequency_100hz` | U16 LE | USB sample rate divided by 100; Python exposes `frequency_hz` |
| 24 | `gain_dbfs_left` | S8 | Effective left-channel gain (dBFS relative to `LOUDNESS_DB_SPL_MAX`) |
| 25 | `gain_dbfs_right` | S8 | Effective right-channel gain |
| 26 | `db_spl_left` | S8 | Left listening level (dB SPL) |
| 27 | `db_spl_right` | S8 | Right listening level (dB SPL) |
| 28–31 | `event_count` | U32 LE | Tagged events in this 1 s period |
| 32 | `last_tag` | U8 | Tag of the most recent event |
| 33 | `last_arg0` | U8 | Tag-specific payload |
| 34 | `last_arg1` | U8 | Tag-specific payload |
| 35 | `last_arg2` | U8 | Tag-specific payload |
| 36 | `equalizer_step_left` | U8 | Left active loudness row (0–120); fixed 40 in bass boost |
| 37 | `equalizer_step_right` | U8 | Right active loudness row (0–120) |
| 38 | `source_has_volume_control` | U8 | `1` when USB SET_CUR host volume is authoritative |
| 39 | `bass_boost_enabled` | U8 | `1` when **active** DSP mode is bass boost |
| 40 | `gain_inferred_dbfs_left` | S8 | Peak-tracked inferred left gain |
| 41 | `gain_inferred_dbfs_right` | S8 | Peak-tracked inferred right gain |
| 42 | `loudness_enabled` | U8 | `1` when **active** DSP mode is loudness contour |
| 43 | `sample_bits` | U8 | `16` = ALT2 (16-bit), `24` = ALT1 (24-bit), `0` = stream inactive |

Python struct format: `"<BBBBIIHHHIHbbbbIBBBBBBBBbbBB"`

`bass_boost_enabled` and `loudness_enabled` reflect **active mode** (mutually
exclusive), not the raw UAC preference flags — both preferences can be `GET_CUR=1`.
See [FIRMWARE_USAGE.md](FIRMWARE_USAGE.md).

Protocol constants live in [`src/usb_statistics_descriptors.h`](../src/usb_statistics_descriptors.h).

See [LOUDNESS.md](LOUDNESS.md) for how loudness fields relate to the equalizer.

## Field update model

Counters and events are written directly into the active `usb_stats_t` buffer via `get_usb_stats()` (lock-free double buffer, no critical sections on the audio path).

Slow telemetry fields live in `stats_telemetry` and are merged into the wire packet once per second by `statistics_report_iteration()`.

| Class | Fields | Producer | Reset on 1 s report |
|-------|--------|----------|---------------------|
| Period counters | `overruns`, `underruns`, FIFO fields, `deadline_misses`, `event_count` | Audio task / events | Yes, **only after successful HID IN** (`min_fifo` → `0xFFFF`) |
| Telemetry | `frequency_100hz` | USB sample-rate apply | No |
| Telemetry | `gain_dbfs_left/right`, `source_has_volume_control` | USB SET_CUR volume handler (immediate) | No |
| Telemetry | `bass_boost_enabled`, `loudness_enabled` | Active filter mode (`loudness_bass_boost_set` / `loudness_loudness_set`) | No |
| Telemetry | `db_spl_left/right`, `equalizer_step_left/right` | Loudness equalizer selection | No |
| Telemetry | `gain_inferred_dbfs_left/right` | Loudness envelope follower | No |
| Telemetry | `sample_bits` | UAC `SET_INTERFACE` on speaker AS (ALT1→24, ALT2→16) | No |
| Last event | `last_tag`, `last_arg0..2` | `audio_stats_record_event()` | Yes → `NONE` / 0 |

USBB FIFO access for HID IN and audio endpoints is serialized with `usb_fifo_hw_lock` (global interrupt disable) so stats and audio tasks cannot interleave `Usb_reset_endpoint_fifo_access`.

## Event tags (`last_tag`)

| Value | Name | Source | `arg0` | `arg1` | `arg2` |
|-------|------|--------|--------|--------|--------|
| 0 | `NONE` | initial / after reset | 0 | 0 | 0 |
| 1 | `EQUALIZER_STEP_SWITCH` | loudness | prev dB SPL | new dB SPL | equalizer step |
| 2 | `RAMP_COMPLETE` | loudness | 0 | 0 | 0 |
| 3 | `FREQ_CHANGE` | UAC2 USB request | old kHz | new kHz | 0 |

Recording: [`audio_stats_record_event()`](../src/audio_stats_logic.h) increments `event_count` and overwrites `last_tag` / args.

## Transport statistics mode

Transport counters (`fifo_level`, `deadline_misses`, skip/insert events) are collected at **every** USB sample rate and included in each 1 Hz HID report. There is no separate heartbeat mode that zeroes transport fields.

Firmware always snapshots the active double buffer in `statistics_report_iteration()`. The host tool sets `"transport_mode": "full"` for known sample rates and only nulls `min_fifo` when the idle sentinel (`0xFFFF`) applies.

## Host usage

```bash
pip install -r usbstatistics/requirements.txt
python usbstatistics/usbstatistics.py
python usbstatistics/usbstatistics.py --json
python usbstatistics/usbstatistics.py --list
```
