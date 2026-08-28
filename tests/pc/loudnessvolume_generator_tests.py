import importlib.util
import math
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "loudnesscontour" / "create1loudnessvolume.py"
SPEC = importlib.util.spec_from_file_location("create1loudnessvolume", SCRIPT)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class LoudnessVolumeGeneratorTests(unittest.TestCase):
    def test_phon_and_volume_rows_are_one_to_one(self):
        self.assertEqual(len(GENERATOR.PHON_LEVELS), 121)
        self.assertEqual(GENERATOR.PHON_LEVELS[0], 35.0)
        self.assertEqual(GENERATOR.PHON_LEVELS[-1], 95.0)
        for index, phon in enumerate(GENERATOR.PHON_LEVELS):
            self.assertEqual(phon, 35.0 + index * 0.5)
            self.assertEqual(phon - GENERATOR.MAXIMUM_PHON, -60.0 + index * 0.5)

    def test_volume_scales_only_numerator(self):
        params = [120.0, 0.5, 12.0]
        raw = GENERATOR.biquad_low_shelf(*params, 48000.0)
        baked, volume_db = GENERATOR.baked_coefficients(
            params, 48000.0, 89.0
        )
        gain = 10 ** (-6.0 / 20.0)

        self.assertEqual(volume_db, -6.0)
        for index in range(3):
            self.assertTrue(math.isclose(baked[index], raw[index] * gain))
        self.assertEqual(baked[3], raw[3])
        self.assertEqual(baked[4], raw[4])

    def test_q4_28_conversion_and_saturation(self):
        self.assertEqual(GENERATOR.float_to_q4_28(1.0), 1 << 28)
        self.assertEqual(GENERATOR.float_to_q4_28(8.0), 2147483647)
        self.assertEqual(GENERATOR.float_to_q4_28(-9.0), -2147483648)


if __name__ == "__main__":
    unittest.main()
