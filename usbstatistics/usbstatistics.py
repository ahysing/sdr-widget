import argparse
import json
import struct
import sys
import time

try:
    import hid
except ImportError as exc:
    hid = None
    _hid_import_error = exc
else:
    _hid_import_error = None

VENDOR_ID = 0x16D0
# Keep in sync with AUDIO_VENDOR_ID / AUDIO_PRODUCT_ID_* in src/usb_descriptors.h
PRODUCT_IDS = (
    # AB-1.x (FEATURE_PRODUCT_AB1x) — current firmware
    0x075E,  # UAC1
    0x075F,  # UAC2
    # Legacy AB-1.x / Henry Audio Mk2/Mk3 Windows profiles
    0x075C,  # UAC1 legacy
    0x075D,  # UAC2 legacy
    # SDR-WIDGET (FEATURE_PRODUCT_SDR_WIDGET)
    0x0761,  # UAC1
    0x0762,  # UAC2
    # USB9023
    0x0763,  # UAC1
    0x0764,  # UAC2
    # USB5102
    0x0765,  # UAC1
    0x0766,  # UAC2
    # USB8741
    0x0767,  # UAC1
    0x0768,  # UAC2
)

# Transport counters are always collected and reported at every USB sample rate.
TRANSPORT_STATS_RATES_HZ = frozenset({
    44100, 48000, 88200, 96000, 176400, 192000,
})

USB_STATS_HID_REPORT_ID = 1
USB_STATS_HID_TRANSFER_SIZE = 64
USB_STATS_PACKET_HID_ANCHOR = 0x53
USB_STATS_PACKET_MAGIC = USB_STATS_PACKET_HID_ANCHOR  # backward-compatible alias
USB_STATS_PACKET_VERSION = 1
USB_STATS_PACKET_FORMAT = "<BBBBIIHHHIHbbIBBBBBB"
USB_STATS_PACKET_SIZE = struct.calcsize(USB_STATS_PACKET_FORMAT)
USB_STATS_PACKET_CHECKSUM_INDEX = 3
USB_STATS_HID_REPORT_SIZE = 63
USB_STATS_TAG_NONE = 0
USB_STATS_TAG_EQUALIZER_STEP_SWITCH = 1
USB_STATS_TAG_RAMP_COMPLETE = 2
USB_STATS_TAG_FREQ_CHANGE = 3
USB_STATS_TAG_SKIP = 4
USB_STATS_TAG_INSERT = 5
USB_STATS_TAG_FORCED_RESYNC = 6
USB_STATS_MIN_FIFO_IDLE = 0xFFFF
USB_STATS_FIFO_SANITY_MAX = 6144
DEVICE_WAIT_TIMEOUT_S = 5.0
DEVICE_POLL_INTERVAL_S = 0.1
HID_READ_TIMEOUT_MS = 2000
HID_READ_SIZES = (
    USB_STATS_HID_TRANSFER_SIZE,
    USB_STATS_HID_REPORT_SIZE + 1,
    USB_STATS_HID_REPORT_SIZE,
)

# Match LOUDNESS_NUM_EQUALIZER_STEPS / phon mapping in src/loudness.h.
LOUDNESS_NUM_EQUALIZER_STEPS = 14
LOUDNESS_MIN_PHON = 55
LOUDNESS_PHON_STEP_DB = 2
LOUDNESS_NEUTRAL_PHON = 80

# Step indices 0-12: 55, 57, ... 79 phon; step 13: 80 phon unity.
EQUALIZER_STEP_PHON = tuple(
    range(LOUDNESS_MIN_PHON, LOUDNESS_NEUTRAL_PHON, LOUDNESS_PHON_STEP_DB)
) + (LOUDNESS_NEUTRAL_PHON,)

# Upper phon bound for each step band; None for the unity step.
EQUALIZER_STEP_PHON_END = tuple(
    EQUALIZER_STEP_PHON[i + 1] for i in range(LOUDNESS_NUM_EQUALIZER_STEPS - 1)
) + (None,)

assert len(EQUALIZER_STEP_PHON) == LOUDNESS_NUM_EQUALIZER_STEPS
assert len(EQUALIZER_STEP_PHON_END) == LOUDNESS_NUM_EQUALIZER_STEPS


def device_label(info):
    parts = [f"{info['vendor_id']:04x}:{info['product_id']:04x}"]
    if info.get("manufacturer_string"):
        parts.append(info["manufacturer_string"])
    if info.get("product_string"):
        parts.append(info["product_string"])
    if info.get("interface_number") is not None:
        parts.append(f"if={info['interface_number']}")
    if info.get("usage_page") is not None:
        parts.append(f"usage_page={info['usage_page']:#06x}")
    return " / ".join(parts)


def _require_hid():
    if hid is None:
        msg = "hidapi package not installed. Run: pip install -r requirements.txt"
        if _hid_import_error is not None:
            msg = f"{msg} ({_hid_import_error})"
        sys.exit(msg)


def is_stats_interface(info):
    return info.get("usage_page") == 0xFF00 or info.get("interface_number") == 2


def enumerate_stats_devices():
    matches = []
    for info in hid.enumerate(VENDOR_ID, 0):
        if info["product_id"] not in PRODUCT_IDS:
            continue
        if is_stats_interface(info):
            matches.append(info)
    return matches


def list_devices(verbose=False):
    _require_hid()

    matches = enumerate_stats_devices()
    if verbose:
        for info in hid.enumerate(VENDOR_ID, 0):
            if info["product_id"] not in PRODUCT_IDS:
                matches.append(info)

    if not matches:
        print("No statistics HID interfaces found.")
        if not verbose:
            print("Try: python usbstatistics.py --list --verbose")
            for info in hid.enumerate(VENDOR_ID, 0):
                print(
                    f"  seen {info['vendor_id']:04x}:{info['product_id']:04x}"
                    f" if={info.get('interface_number')} usage_page={info.get('usage_page')}"
                )
        return

    heading = "Henry Audio USB devices:" if verbose else "Statistics HID interfaces:"
    print(heading)
    seen = set()
    for info in matches:
        path = info["path"]
        if path in seen:
            continue
        seen.add(path)
        print(f"  {device_label(info)}")
        print(f"    path={path}")


def try_open_stats_device():
    _require_hid()

    for info in enumerate_stats_devices():
        dev = hid.device()
        try:
            dev.open_path(info["path"])
            try:
                dev.set_nonblocking(0)
            except (AttributeError, OSError):
                pass
        except OSError:
            continue
        return dev, info
    return None, None


def read_hid_stats_report(dev, timeout_ms=HID_READ_TIMEOUT_MS):
    """Read one HID input report; try common Windows buffer sizes."""
    for size in HID_READ_SIZES:
        data = dev.read(size, timeout_ms=timeout_ms)
        if data:
            return data
    return []


def wait_for_stats_device(timeout_s=DEVICE_WAIT_TIMEOUT_S, verbose=False):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        dev, info = try_open_stats_device()
        if dev is not None:
            return dev, info
        if verbose:
            remaining = max(0.0, deadline - time.monotonic())
            print(f"waiting for statistics HID device ({remaining:.1f}s left)...", file=sys.stderr)
        time.sleep(DEVICE_POLL_INTERVAL_S)
    return None, None


def close_device(dev):
    if dev is None:
        return
    try:
        dev.close()
    except OSError:
        pass


def equalizer_step_from_db_spl(db_spl):
    if db_spl >= LOUDNESS_NEUTRAL_PHON:
        return LOUDNESS_NUM_EQUALIZER_STEPS - 1
    if db_spl < LOUDNESS_MIN_PHON:
        return 0
    step = (db_spl - LOUDNESS_MIN_PHON) // LOUDNESS_PHON_STEP_DB
    max_contour_step = LOUDNESS_NUM_EQUALIZER_STEPS - 2
    if step > max_contour_step:
        step = max_contour_step
    return step


def phon_range_for_step(step):
    max_step = min(len(EQUALIZER_STEP_PHON), len(EQUALIZER_STEP_PHON_END)) - 1
    step = max(0, min(step, max_step))
    return {
        "from_phon": EQUALIZER_STEP_PHON[step],
        "to_phon": EQUALIZER_STEP_PHON_END[step],
    }


def decode_last_event(tag, arg0, arg1, arg2):
    if tag == USB_STATS_TAG_FREQ_CHANGE:
        return {
            "from_khz": arg0,
            "to_khz": arg1,
        }
    if tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH:
        prev_step = equalizer_step_from_db_spl(arg0)
        new_step = max(0, min(int(arg2), LOUDNESS_NUM_EQUALIZER_STEPS - 1))
        prev_range = phon_range_for_step(prev_step)
        new_range = phon_range_for_step(new_step)
        return {
            "from_phon": arg0,
            "to_phon": arg1,
            "from_db_spl": arg0,
            "to_db_spl": arg1,
            "from_phon_range": prev_range,
            "to_phon_range": new_range,
            "equalizer_step": new_step,
            "prev_equalizer_step": prev_step,
        }
    if tag == USB_STATS_TAG_RAMP_COMPLETE:
        return {"ramp_complete": True}
    if tag == USB_STATS_TAG_SKIP:
        return {
            "skip": True,
            "freq_khz": arg0,
            "gap": (arg1 << 4) | (arg2 & 0x0F),
        }
    if tag == USB_STATS_TAG_INSERT:
        return {
            "insert": True,
            "freq_khz": arg0,
            "gap": (arg1 << 4) | (arg2 & 0x0F),
        }
    if tag == USB_STATS_TAG_FORCED_RESYNC:
        return {
            "forced_resync": True,
            "freq_khz": arg0,
        }
    if tag == USB_STATS_TAG_NONE:
        return None
    return {
        "arg0": arg0,
        "arg1": arg1,
        "arg2": arg2,
    }


def packet_checksum(packet_bytes):
    checksum = 0
    for index, value in enumerate(packet_bytes):
        if index == USB_STATS_PACKET_CHECKSUM_INDEX:
            continue
        checksum ^= value
    return checksum


def validate_packet_checksum(chunk):
    stored = chunk[USB_STATS_PACKET_CHECKSUM_INDEX]
    expected = packet_checksum(chunk)
    if stored == expected:
        return
    if stored == 0:
        # Older firmware builds leave checksum unset (zero). Magic, version,
        # zero padding, and plausibility checks still filter stray HID traffic.
        return
    raise ValueError("stats packet checksum mismatch")


def normalize_hid_payload(data):
    if not data:
        return None

    raw = bytes(data)
    if len(raw) < USB_STATS_PACKET_SIZE:
        return None

    if raw[0] == USB_STATS_HID_REPORT_ID:
        payload = raw[1:]
    elif raw[0] == USB_STATS_PACKET_HID_ANCHOR:
        payload = raw
    else:
        return None

    if len(payload) < USB_STATS_PACKET_SIZE:
        return None
    return payload


def find_hid_anchor_offset(payload):
    limit = min(8, len(payload) - USB_STATS_PACKET_SIZE + 1)
    for offset in range(max(0, limit)):
        if payload[offset] != USB_STATS_PACKET_HID_ANCHOR:
            continue
        version = payload[offset + 1]
        if version != USB_STATS_PACKET_VERSION:
            continue
        if offset > 0 and any(payload[i] != 0 for i in range(offset)):
            continue
        return offset
    return None


def transport_stats_active(frequency_hz):
    return frequency_hz in TRANSPORT_STATS_RATES_HZ


def transport_mode_for_frequency(frequency_hz):
    if transport_stats_active(frequency_hz):
        return "full"
    return "unknown"


def find_packet_offset(payload):
    return find_hid_anchor_offset(payload)


def fifo_period_was_idle(stats):
    return stats["min_fifo"] == USB_STATS_MIN_FIFO_IDLE


def is_plausible_stats_packet(stats):
    if stats["deadline_misses"] > 10000:
        return False
    if stats["event_count"] > 10000:
        return False
    if stats["overruns"] > 1000000 or stats["underruns"] > 1000000:
        return False

    if fifo_period_was_idle(stats):
        if stats["max_fifo"] != 0 or stats["fifo_level"] != 0:
            return False
        return True

    if stats["max_fifo"] > 0 and stats["min_fifo"] > stats["max_fifo"]:
        return False
    if stats["fifo_level"] > USB_STATS_FIFO_SANITY_MAX:
        return False
    if stats["max_fifo"] > USB_STATS_FIFO_SANITY_MAX:
        return False
    if stats["min_fifo"] > USB_STATS_FIFO_SANITY_MAX:
        return False

    return True


def parse_stats_payload(payload):
    offset = find_hid_anchor_offset(payload)
    if offset is None:
        raise ValueError("stats packet hid_anchor/version not found")

    version = payload[offset + 1]
    if version != USB_STATS_PACKET_VERSION:
        raise ValueError("stats packet version mismatch")

    packet_size = USB_STATS_PACKET_SIZE
    packet_format = USB_STATS_PACKET_FORMAT

    chunk = payload[offset : offset + packet_size]
    if len(chunk) < packet_size:
        raise ValueError(
            f"stats payload too short: got {len(chunk)} bytes, need {packet_size}"
        )

    if any(payload[offset + packet_size : USB_STATS_HID_REPORT_SIZE]):
        raise ValueError("stats packet padding is not zero")

    validate_packet_checksum(chunk)

    fields = struct.unpack(packet_format, chunk)
    (
        hid_anchor,
        version,
        report_seq,
        _checksum,
        overruns,
        underruns,
        fifo_level,
        max_fifo,
        min_fifo,
        deadline_misses,
        frequency_100hz,
        gain_dbfs,
        db_spl,
        event_count,
        last_tag,
        last_arg0,
        last_arg1,
        last_arg2,
        equalizer_step,
        source_volume_control,
    ) = fields

    if hid_anchor != USB_STATS_PACKET_HID_ANCHOR:
        raise ValueError("stats packet header mismatch")

    frequency_hz = frequency_100hz * 100

    stats = {
        "version": version,
        "report_seq": report_seq,
        "overruns": overruns,
        "underruns": underruns,
        "fifo_level": fifo_level,
        "max_fifo": max_fifo,
        "min_fifo": min_fifo,
        "deadline_misses": deadline_misses,
        "frequency_hz": frequency_hz,
        "gain_dbfs": gain_dbfs,
        "db_spl": db_spl,
        "event_count": event_count,
        "last_tag": last_tag,
        "equalizer_step": equalizer_step,
        "source_volume_control": 1 if source_volume_control else 0,
        "last_event": decode_last_event(last_tag, last_arg0, last_arg1, last_arg2),
    }

    if not is_plausible_stats_packet(stats):
        raise ValueError("stats packet failed plausibility check")

    return stats


def format_stats_output(stats):
    output = dict(stats)
    output["transport_mode"] = transport_mode_for_frequency(output["frequency_hz"])
    if output["min_fifo"] == USB_STATS_MIN_FIFO_IDLE:
        output["min_fifo"] = None
    return output


def format_stats_deltas(prev_stats, stats):
    """Human-readable per-report deltas for transport and contour debugging."""
    def delta(name):
        return stats[name] - prev_stats[name]

    parts = [
        f"d_deadline={delta('deadline_misses'):+d}",
        f"d_overrun={delta('overruns'):+d}",
        f"d_underrun={delta('underruns'):+d}",
        f"gain={stats['gain_dbfs']}dB",
        f"step={stats['equalizer_step']}",
    ]
    if stats["equalizer_step"] != prev_stats["equalizer_step"]:
        parts.append(
            f"STEP_CHANGE {prev_stats['equalizer_step']}->{stats['equalizer_step']}"
        )
    tag = stats["last_tag"]
    if tag == USB_STATS_TAG_SKIP:
        parts.append("SKIP")
    elif tag == USB_STATS_TAG_INSERT:
        parts.append("INSERT")
    elif tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH:
        parts.append("EQ_SWITCH_EVENT")
    return " ".join(parts)


def is_valid_stats_packet(stats, last_report_seq):
    if stats["report_seq"] == last_report_seq:
        return False

    return is_plausible_stats_packet(stats)


def process_hid_report(data, last_report_seq, args):
    payload = normalize_hid_payload(data)
    if payload is None:
        if args.debug and data:
            print(f"reject normalize: {bytes(data).hex()}", file=sys.stderr)
        return last_report_seq, False, None

    try:
        stats = parse_stats_payload(payload)
    except ValueError as exc:
        if args.debug:
            print(f"reject parse ({exc}): {payload.hex()}", file=sys.stderr)
        return last_report_seq, False, None

    if not is_valid_stats_packet(stats, last_report_seq):
        if args.debug:
            print(f"reject validate seq={stats['report_seq']} last={last_report_seq}", file=sys.stderr)
        return last_report_seq, False, None

    if args.debug and last_report_seq is not None:
        expected_seq = (last_report_seq + 1) & 0xFF
        if stats["report_seq"] != expected_seq:
            print(
                f"resync report_seq: expected {expected_seq}, got {stats['report_seq']}",
                file=sys.stderr,
            )
    stats_output = format_stats_output(stats)
    print(json.dumps(stats_output))
    sys.stdout.flush()
    return stats["report_seq"], True, stats


def is_empty_packet(data: dict) -> bool:
    return False


def main():
    parser = argparse.ArgumentParser(description="Read USB audio statistics from Henry Audio firmware (HID)")
    parser.add_argument("--list", action="store_true", help="List statistics HID interfaces and exit")
    parser.add_argument("--verbose", action="store_true", help="Print read-loop status to stderr")
    parser.add_argument(
        "--deltas",
        action="store_true",
        help="Print per-report transport/contour deltas to stderr",
    )
    parser.add_argument("--debug", action="store_true", help="Print rejected HID reads on stderr")
    args = parser.parse_args()

    if args.list:
        list_devices(verbose=args.verbose)
        return

    _require_hid()

    dev, info = wait_for_stats_device(verbose=args.verbose)
    if dev is None:
        print("Device not found within 5 seconds.", file=sys.stderr)
        list_devices()
        sys.exit(1)

    print(f"Using {device_label(info)}", file=sys.stderr)

    last_report_seq = None
    prev_stats = None
    accepted = 0
    empty_reads = 0
    while True:
        try:
            data = read_hid_stats_report(dev)
            if not data:
                empty_reads += 1
                if args.verbose:
                    print("waiting for HID report...", file=sys.stderr)
                    if empty_reads == 3:
                        print(
                            "hint: no bytes from firmware yet — start playback, "
                            "or reflash if stats HID send was broken; "
                            "use --debug to inspect rejected packets",
                            file=sys.stderr,
                        )
                continue
            empty_reads = 0
            last_report_seq, accepted_now, stats = process_hid_report(data, last_report_seq, args)
            if accepted_now:
                accepted += 1
                if args.deltas and prev_stats is not None:
                    print(format_stats_deltas(prev_stats, stats), file=sys.stderr)
                prev_stats = stats
                if args.verbose:
                    print(f"accepted report_seq={last_report_seq} (#{accepted})", file=sys.stderr)
            elif args.verbose:
                print(
                    f"ignored HID packet ({len(data)} bytes); use --debug for details",
                    file=sys.stderr,
                )

        except OSError as exc:
            if args.verbose:
                print(f"HID read error ({exc}), waiting to reconnect...", file=sys.stderr)
            close_device(dev)
            dev, info = wait_for_stats_device(verbose=args.verbose)
            if dev is None:
                sys.exit(f"HID Error: {exc}")
            last_report_seq = None
            prev_stats = None
            accepted = 0
            print(f"Reconnected to {device_label(info)}", file=sys.stderr)
        except KeyboardInterrupt:
            break

    close_device(dev)


if __name__ == "__main__":
    main()
