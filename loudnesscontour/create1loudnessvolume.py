"""Generate one low-shelf loudness biquad with baked-in playback volume.

The 121 output rows pair 35.0..95.0 phon with -60.0..0.0 dB volume in
0.5 dB increments.  With --bit-width 32, rows are flat C struct initializers
using Q4.28 coefficients in {a1, a2, b0, b1, b2} order.
"""

import argparse

import matplotlib.pyplot as plt
import numpy as np
from pydsm.iso226 import iso226_spl_contour
from scipy.interpolate import CubicSpline
from scipy.optimize import least_squares


SAMPLE_RATES_HZ = [
    ("hdmi", 48000.0),
    ("cd", 44100.0),
]
PHON_LEVELS = [step / 2.0 for step in range(70, 191)]
REFERENCE_PHON = 80.0
MAXIMUM_PHON = 95.0
INITIAL_PARAMS = np.array([120.0, 0.5, 12.0])
LOWER_BOUNDS = np.array([20.0, 0.1, -40.0])
UPPER_BOUNDS = np.array([300.0, 2.0, 40.0])


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Optimize one low-shelf ISO 226 loudness biquad per 0.5 phon and "
            "bake the corresponding -60..0 dB playback volume into b0/b1/b2."
        )
    )
    parser.add_argument("-g", "--graph", action="store_true", help="Show response graph")
    parser.add_argument(
        "--graph-type",
        choices=["filter", "ISO226", "loudnesscontours"],
        default="loudnesscontours",
        help="Select graph type",
    )
    parser.add_argument(
        "-fm",
        "--frequencymode",
        choices=["hdmi", "cd", "both"],
        default="cd",
        help="Select base sample rate",
    )
    parser.add_argument(
        "--bit-width",
        type=int,
        choices=[32],
        help="Print flat Q4.28 C struct initializers",
    )
    return parser.parse_args()


def float_to_q4_28(value):
    scaled = int(round(value * (1 << 28)))
    if scaled > 2147483647:
        return 2147483647
    if scaled < -2147483648:
        return -2147483648
    return scaled


def biquad_low_shelf(fc, q_factor, gain_db, sample_rate_hz):
    amplitude = 10 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / sample_rate_hz
    alpha = np.sin(w0) / (2.0 * q_factor)
    cos_w0 = np.cos(w0)
    sqrt_a = np.sqrt(amplitude)

    b0 = amplitude * (
        (amplitude + 1.0)
        - (amplitude - 1.0) * cos_w0
        + 2.0 * sqrt_a * alpha
    )
    b1 = 2.0 * amplitude * (
        (amplitude - 1.0) - (amplitude + 1.0) * cos_w0
    )
    b2 = amplitude * (
        (amplitude + 1.0)
        - (amplitude - 1.0) * cos_w0
        - 2.0 * sqrt_a * alpha
    )
    a0 = (
        (amplitude + 1.0)
        + (amplitude - 1.0) * cos_w0
        + 2.0 * sqrt_a * alpha
    )
    a1 = -2.0 * (
        (amplitude - 1.0) + (amplitude + 1.0) * cos_w0
    )
    a2 = (
        (amplitude + 1.0)
        + (amplitude - 1.0) * cos_w0
        - 2.0 * sqrt_a * alpha
    )
    return np.array([b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0])


def frequency_response(coeffs, frequencies_hz, sample_rate_hz):
    b0, b1, b2, a1, a2 = coeffs
    w = 2.0 * np.pi * frequencies_hz / sample_rate_hz
    z1 = np.exp(-1j * w)
    z2 = np.exp(-2j * w)
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2)


def filter_response(params, frequencies_hz, sample_rate_hz):
    coeffs = biquad_low_shelf(*params, sample_rate_hz)
    return 20.0 * np.log10(np.abs(frequency_response(
        coeffs, frequencies_hz, sample_rate_hz
    )))


def optimization_cost(params, frequencies_hz, sample_rate_hz, target_db):
    return filter_response(params, frequencies_hz, sample_rate_hz) - target_db


def iso226_contour(phon):
    """Return ISO-226 contour, extrapolating pydsm's 90-phon ceiling."""
    if phon <= 90.0:
        return iso226_spl_contour(phon, hfe=True)

    frequencies, spl_90 = iso226_spl_contour(90.0, hfe=True)
    frequencies_895, spl_895 = iso226_spl_contour(89.5, hfe=True)
    if not np.array_equal(frequencies, frequencies_895):
        raise RuntimeError("ISO-226 contour frequency grids do not match")
    slope_per_phon = (spl_90 - spl_895) / 0.5
    return frequencies, spl_90 + (phon - 90.0) * slope_per_phon


def optimize_contours(sample_rate_hz, frequencies_hz):
    ref_f, ref_spl = iso226_contour(REFERENCE_PHON)
    reference = CubicSpline(ref_f, ref_spl)(frequencies_hz)
    optimized = {}
    initial = INITIAL_PARAMS.copy()

    for phon in PHON_LEVELS:
        iso_f, iso_spl = iso226_contour(phon)
        contour = CubicSpline(iso_f, iso_spl)(frequencies_hz)
        target = (contour - reference) + (REFERENCE_PHON - phon)
        result = least_squares(
            optimization_cost,
            initial,
            bounds=(LOWER_BOUNDS, UPPER_BOUNDS),
            args=(frequencies_hz, sample_rate_hz, target),
        )
        optimized[phon] = result.x
        initial = result.x
    return optimized


def baked_coefficients(params, sample_rate_hz, phon):
    b0, b1, b2, a1, a2 = biquad_low_shelf(*params, sample_rate_hz)
    volume_db = phon - MAXIMUM_PHON
    volume_gain = 10 ** (volume_db / 20.0)
    return np.array([
        b0 * volume_gain,
        b1 * volume_gain,
        b2 * volume_gain,
        a1,
        a2,
    ]), volume_db


def print_row(args, sample_rate_hz, phon, params):
    coeffs, volume_db = baked_coefficients(params, sample_rate_hz, phon)
    b0, b1, b2, a1, a2 = coeffs
    if args.bit_width == 32:
        values = [float_to_q4_28(value) for value in (a1, a2, b0, b1, b2)]
        print(
            "    {{ {:11d}, {:11d}, {:11d}, {:11d}, {:11d} }},"
            "  /* phon={:.1f} volume={:.1f} dB */".format(
                *values, phon, volume_db
            )
        )
    else:
        print(
            "phon={:4.1f} volume={:5.1f} dB "
            "fc={:12.8f} Q={:12.8f} gain={:12.8f} dB "
            "b0={:18.12f} b1={:18.12f} b2={:18.12f} "
            "a1={:18.12f} a2={:18.12f}".format(
                phon,
                volume_db,
                params[0],
                params[1],
                params[2],
                b0,
                b1,
                b2,
                a1,
                a2,
            )
        )


def plot_results(args, optimized, frequencies_hz, sample_rate_hz):
    fig, ax = plt.subplots(figsize=(10, 8), dpi=100)
    ref_f, ref_spl = iso226_contour(REFERENCE_PHON)
    reference = CubicSpline(ref_f, ref_spl)(frequencies_hz)

    for phon in PHON_LEVELS:
        iso_f, iso_spl = iso226_contour(phon)
        contour = CubicSpline(iso_f, iso_spl)(frequencies_hz)
        response = filter_response(optimized[phon], frequencies_hz, sample_rate_hz)
        if args.graph_type == "ISO226":
            values = contour
        elif args.graph_type == "filter":
            values = response
        else:
            values = contour - response
        ax.semilogx(frequencies_hz, values, alpha=0.35)

    if args.graph_type == "loudnesscontours":
        ax.semilogx(
            frequencies_hz,
            reference,
            "k--",
            linewidth=2.0,
            label="ISO226 80 phon reference",
        )
    ax.set_xlabel("frequency [Hz]")
    ax.set_ylabel("magnitude [dB] / loudness [phon]")
    ax.grid(True, which="both", alpha=0.3)
    ax.set_title(f"One low-shelf loudness biquad (fs={sample_rate_hz:.0f} Hz)")
    plt.tight_layout()
    plt.show()


def main():
    args = parse_args()
    if args.frequencymode == "both":
        rates = [rate for _name, rate in SAMPLE_RATES_HZ]
    else:
        rates = [
            rate for name, rate in SAMPLE_RATES_HZ
            if name == args.frequencymode
        ]

    frequencies_hz = np.logspace(np.log10(20.0), np.log10(17000.0), 400)
    for sample_rate_hz in rates:
        optimized = optimize_contours(sample_rate_hz, frequencies_hz)
        for phon in PHON_LEVELS:
            print_row(args, sample_rate_hz, phon, optimized[phon])
        if args.graph:
            plot_results(args, optimized, frequencies_hz, sample_rate_hz)


if __name__ == "__main__":
    main()
