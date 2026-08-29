"""Patch lowshelf_no_volume_* LUT tables in src/loudness_fast.c from filter coefficients.

Uses the same phon grid and optimizer as create1loudnessvolume.py (--coeff-mode filter).
Alternatively, derive rows by removing baked volume from lowshelf_and_volume_* tables.

Default: regenerate via filter_coefficients (matches create1loudnessvolume.py --coeff-mode filter).
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
LOUDNESS_FAST = ROOT / "src" / "loudness_fast.c"

import importlib.util

SPEC = importlib.util.spec_from_file_location(
    "create1loudnessvolume",
    ROOT / "loudnesscontour" / "create1loudnessvolume.py",
)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def float_to_q4_28(value):
    return GENERATOR.float_to_q4_28(value)


def format_table(array_name, rows):
    lines = []
    for phon, volume_db, a1, a2, b0, b1, b2 in rows:
        lines.append(
            "    {{ {:11d}, {:11d}, {:11d}, {:11d}, {:11d} }},"
            "  /* phon={:.1f} volume={:.1f} dB */".format(
                a1, a2, b0, b1, b2, phon, volume_db
            )
        )
    return (
        f"static const biquad_quotients_fast_t\n"
        f"{array_name}[LOUDNESS_NUM_EQUALIZER_STEPS] = {{\n"
        + "\n".join(lines)
        + "\n};"
    )


def rows_from_filter_mode(sample_rate_hz):
    frequencies_hz = np.logspace(np.log10(20.0), np.log10(17000.0), 400)
    optimized = GENERATOR.optimize_contours(sample_rate_hz, frequencies_hz)
    rows = []
    for phon in GENERATOR.PHON_LEVELS:
        coeffs, volume_db = GENERATOR.filter_coefficients(
            optimized[phon], sample_rate_hz, phon
        )
        b0, b1, b2, a1, a2 = coeffs
        rows.append(
            (
                phon,
                volume_db,
                float_to_q4_28(a1),
                float_to_q4_28(a2),
                float_to_q4_28(b0),
                float_to_q4_28(b1),
                float_to_q4_28(b2),
            )
        )
    return rows


def rows_from_inverse_volume(text, and_volume_name):
    pattern = (
        rf"{and_volume_name}\[LOUDNESS_NUM_EQUALIZER_STEPS\] = \{{"
        r"(?P<body>.*?)\n\}};"
    )
    match = re.search(pattern, text, re.DOTALL)
    if not match:
        raise ValueError(f"Could not find table {and_volume_name}")
    row_re = re.compile(
        r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},"
        r"\s*/\*\s*phon=([\d.]+)\s+volume=([-\d.]+)\s+dB\s*\*/"
    )
    rows = []
    for m in row_re.finditer(match.group("body")):
        a1, a2, b0, b1, b2 = (int(m.group(i)) for i in range(1, 6))
        phon = float(m.group(6))
        volume_db = float(m.group(7))
        gain = 10 ** (volume_db / 20.0)
        q28 = 1 << 28
        b0_f = (b0 / q28) / gain
        b1_f = (b1 / q28) / gain
        b2_f = (b2 / q28) / gain
        rows.append(
            (
                phon,
                volume_db,
                a1,
                a2,
                float_to_q4_28(b0_f),
                float_to_q4_28(b1_f),
                float_to_q4_28(b2_f),
            )
        )
    if len(rows) != len(GENERATOR.PHON_LEVELS):
        raise ValueError(f"{and_volume_name}: expected 121 rows, got {len(rows)}")
    return rows


def replace_table(text, array_name, table_body):
    marker = f"{array_name}[LOUDNESS_NUM_EQUALIZER_STEPS] = {{"
    start = text.find(marker)
    if start == -1:
        raise ValueError(f"Array {array_name} not found")
    open_brace = text.find("{", start)
    close_brace = text.find("\n};", open_brace)
    return text[: open_brace + 1] + "\n" + table_body + text[close_brace:]


def parse_args():
    parser = argparse.ArgumentParser(description="Update lowshelf_no_volume_* LUTs")
    parser.add_argument(
        "--method",
        choices=["filter", "inverse"],
        default="filter",
        help="filter: optimize filter coeffs; inverse: unscale and_volume tables",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    text = LOUDNESS_FAST.read_text(encoding="utf-8")

    if args.method == "inverse":
        rows_441 = rows_from_inverse_volume(text, "lowshelf_and_volume_44100hz")
        rows_48 = rows_from_inverse_volume(text, "lowshelf_and_volume_48000hz")
    else:
        rows_441 = rows_from_filter_mode(44100.0)
        rows_48 = rows_from_filter_mode(48000.0)

    body_441 = "\n".join(
        line
        for line in format_table("lowshelf_no_volume_44100hz", rows_441).splitlines()[
            2:-1
        ]
    )
    body_48 = "\n".join(
        line
        for line in format_table("lowshelf_no_volume_48000hz", rows_48).splitlines()[
            2:-1
        ]
    )

    text = replace_table(text, "lowshelf_no_volume_44100hz", body_441 + "\n")
    text = replace_table(text, "lowshelf_no_volume_48000hz", body_48 + "\n")
    LOUDNESS_FAST.write_text(text, encoding="utf-8")
    print("Updated lowshelf_no_volume_* in", LOUDNESS_FAST)


if __name__ == "__main__":
    main()
