import importlib.util
import struct
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[2] / "usbstatistics" / "usbstatistics.py"
SPEC = importlib.util.spec_from_file_location("usbstatistics", MODULE_PATH)
usbstatistics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(usbstatistics)


def build_v5_payload():
    values = (
        usbstatistics.USB_STATS_PACKET_HID_ANCHOR,
        usbstatistics.USB_STATS_PACKET_VERSION,
        7,
        0,
        1,
        2,
        100,
        110,
        90,
        3,
        1920,
        -6,
        -20,
        89,
        75,
        4,
        usbstatistics.USB_STATS_TAG_EQUALIZER_STEP_SWITCH,
        95,
        82,
        94,
        108,
        80,
        1,
        1,
        -3,
        -4,
        0,
        24,
    )
    packet = bytearray(struct.pack(usbstatistics.USB_STATS_PACKET_FORMAT, *values))
    packet[usbstatistics.USB_STATS_PACKET_CHECKSUM_INDEX] = (
        usbstatistics.packet_checksum(packet)
    )
    packet.extend(
        bytes(usbstatistics.USB_STATS_HID_REPORT_SIZE - len(packet))
    )
    return packet


class UsbStatisticsV5Tests(unittest.TestCase):
    def test_parse_stereo_v5_packet(self):
        stats = usbstatistics.parse_stats_payload(build_v5_payload())

        self.assertEqual(stats["version"], 5)
        self.assertEqual(stats["frequency_hz"], 192000)
        self.assertEqual(stats["gain_dbfs_left"], -6)
        self.assertEqual(stats["gain_dbfs_right"], -20)
        self.assertEqual(stats["db_spl_left"], 89)
        self.assertEqual(stats["db_spl_right"], 75)
        self.assertEqual(stats["equalizer_step_left"], 108)
        self.assertEqual(stats["equalizer_step_right"], 80)
        self.assertEqual(stats["bass_boost_enabled"], 1)
        self.assertEqual(stats["loudness_enabled"], 0)
        self.assertEqual(stats["sample_bits"], 24)

    def test_reject_version_4_packet(self):
        payload = build_v5_payload()
        payload[1] = 4
        payload[usbstatistics.USB_STATS_PACKET_CHECKSUM_INDEX] = 0
        payload[usbstatistics.USB_STATS_PACKET_CHECKSUM_INDEX] = (
            usbstatistics.packet_checksum(
                payload[: usbstatistics.USB_STATS_PACKET_SIZE]
            )
        )
        with self.assertRaises(ValueError):
            usbstatistics.parse_stats_payload(payload)


if __name__ == "__main__":
    unittest.main()
