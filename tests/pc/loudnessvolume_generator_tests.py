import importlib.util
import math
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "loudnesscontour" / "create1loudnessvolume" / "create1loudnessvolume.py"
if not SCRIPT.exists():
    SCRIPT = ROOT / "loudnesscontour" / "create1loudnessvolume.py"
SPEC = importlib.util.spec_from_file_location("create1loudnessvolume", SCRIPT)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class LoudnessVolumeGeneratorTests(unittest.TestCase):
    def test_phon_and_volume_rows_are_one_to_one(self):
        self.assertEqual(len(GENERATOR.PHON_LEVELS), 121)
        self.assertEqual(GENERATOR.PHON_LEVELS[0], 25.0)
        self.assertEqual(GENERATOR.PHON_LEVELS[-1], 85.0)
        for index, phon in enumerate(GENERATOR.PHON_LEVELS):
            self.assertEqual(phon, 25.0 + index * 0.5)
            self.assertEqual(phon - GENERATOR.MAXIMUM_PHON, -60.0 + index * 0.5)

    def test_volume_scales_only_numerator(self):
        params = GENERATOR.BiquadFirstOrder(120.0, 12.0)
        raw = GENERATOR.biquad_low_shelf(params, 48000.0)
        baked, volume_db = GENERATOR.baked_coefficients(
            params, 48000.0, 79.0
        )
        gain = 10 ** (-6.0 / 20.0)

        self.assertEqual(volume_db, -6.0)
        self.assertTrue(math.isclose(baked.b0, raw.b0 * gain))
        self.assertTrue(math.isclose(baked.b1, raw.b1 * gain))
        self.assertEqual(baked.a1, raw.a1)

    def test_q4_28_conversion_and_saturation(self):
        self.assertEqual(GENERATOR.float_to_q4_28(1.0), 1 << 28)
        self.assertEqual(GENERATOR.float_to_q4_28(8.0), 2147483647)
        self.assertEqual(GENERATOR.float_to_q4_28(-9.0), -2147483648)

    def test_filter_mode_leaves_numerator_unscaled(self):
        params = GENERATOR.BiquadFirstOrder(120.0, 12.0)
        raw = GENERATOR.biquad_low_shelf(params, 48000.0)
        filtered, volume_db = GENERATOR.filter_coefficients(
            params, 48000.0, 79.0
        )

        self.assertEqual(volume_db, -6.0)
        self.assertTrue(math.isclose(filtered.b0, raw.b0))
        self.assertTrue(math.isclose(filtered.b1, raw.b1))
        self.assertTrue(math.isclose(filtered.a1, raw.a1))

    def test_filter_and_volume_modes_share_phon_grid(self):
        params = GENERATOR.BiquadFirstOrder(120.0, 12.0)
        for phon in GENERATOR.PHON_LEVELS:
            baked, baked_volume_db = GENERATOR.baked_coefficients(
                params, 44100.0, phon
            )
            filtered, filter_volume_db = GENERATOR.filter_coefficients(
                params, 44100.0, phon
            )
            self.assertEqual(baked_volume_db, filter_volume_db)
            self.assertEqual(baked_volume_db, phon - GENERATOR.MAXIMUM_PHON)
            self.assertEqual(baked.a1, filtered.a1)

    def test_iso226_contour_ends_at_12_5_khz_without_20khz_point(self):
        for phon in [25.0, 60.0, 80.0, 90.0, 95.0]:
            f, spl = GENERATOR.iso226_contour(phon)
            self.assertEqual(f[0], 20.0)
            self.assertEqual(f[-1], 12500.0)
            self.assertEqual(len(f), 29)
            self.assertNotIn(20000.0, f)


if __name__ == "__main__":
    unittest.main()
