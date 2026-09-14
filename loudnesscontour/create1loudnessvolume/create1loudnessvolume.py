"""Generate one first order low-shelf loudness biquad with baked-in playback volume and one first order high-shelf loudness biquad

The 121 output rows pair 25.0..85.0 phon with -60.0..0.0 dB volume in
0.5 dB increments.  With --bit-width 32, rows are flat C struct initializers
using Q4.28 coefficients in {a1, b0, b1} order.
"""

import argparse
from dataclasses import dataclass

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogLocator, MultipleLocator, ScalarFormatter
from numpy import float64, ndarray
from pydsm.iso226 import iso226_spl_contour
from scipy.interpolate import CubicSpline
from scipy.optimize import least_squares


@dataclass
class BiquadSecondOrder:
    fc: float
    Q: float
    gain_db: float


@dataclass
class BiquadCoeffsSecondOrder:
    a1: float
    a2: float
    b0: float
    b1: float
    b2: float


@dataclass
class BiquadFirstOrder:
    fc: float
    gain_db: float


@dataclass
class BiquadCoeffsFirstOrder:
    a1: float
    b0: float
    b1: float


type BiquadCoeffs = BiquadCoeffsFirstOrder | BiquadCoeffsSecondOrder


SAMPLE_RATES_HZ = [
    ("hdmi", 48000.0),
    ("cd", 44100.0),
]
GAIN_RANGE = 60
REFERENCE_PHON = 80.0
MAXIMUM_PHON = 85.0
MINIMUM_PHON = MAXIMUM_PHON - GAIN_RANGE
PHON_LEVELS = [step / 2.0 for step in range(int(MINIMUM_PHON * 2), int(MAXIMUM_PHON * 2) + 1)]
#                           fc      gain
LOW_SHELF_INITIAL_PARAMS = np.array([120.0, 12.0])
LOW_SHELF_LOWER_BOUNDS = np.array([20.0, -40.0])
LOW_SHELF_UPPER_BOUNDS = np.array([300.0, 40.0])

LOW_SHELF_INITIAL_BIQUAD = BiquadFirstOrder(*LOW_SHELF_INITIAL_PARAMS)
LOW_SHELF_LOWER_BIQUAD = BiquadFirstOrder(*LOW_SHELF_LOWER_BOUNDS)
LOW_SHELF_UPPER_BIQUAD = BiquadFirstOrder(*LOW_SHELF_UPPER_BOUNDS)

#                           fc      gain
HIGH_SHELF_INITIAL_PARAMS = np.array([4000.0, -4.0])
HIGH_SHELF_LOWER_BOUNDS = np.array([2000.0, -15.0])
HIGH_SHELF_UPPER_BOUNDS = np.array([9000.0,  0.0])

HIGH_SHELF_INITIAL_BIQUAD = BiquadFirstOrder(*HIGH_SHELF_INITIAL_PARAMS)
HIGH_SHELF_LOWER_BIQUAD = BiquadFirstOrder(*HIGH_SHELF_LOWER_BOUNDS)
HIGH_SHELF_UPPER_BIQUAD = BiquadFirstOrder(*HIGH_SHELF_UPPER_BOUNDS)

INT32_MAX = 2147483647
INT32_MIN = -2147483648


def gain_db_to_dbfs(gain_db: float) -> float:
    return 10 ** (gain_db / 20.0)


def float_to_q4_28(value: float64) -> int:
    scaled = round(value * (1 << 28))
    if scaled > INT32_MAX:
        return INT32_MAX
    if scaled < INT32_MIN:
        return INT32_MIN
    return scaled


# first order IIR low shelf filter
def first_order_biquad_low_shelf(params: BiquadFirstOrder, sample_rate_hz: float) -> BiquadCoeffsFirstOrder:
    fc: float64 = params.fc
    gain_db: float64 = params.gain_db

    A = 10.0 ** (gain_db / 20.0)
    th = 2.0 * np.pi * fc / sample_rate_hz
    tan_w = np.tan(th / 2.0)

    if gain_db >= 0:
        a0 = tan_w + 1.0
        b0 = (A * tan_w + 1.0) / a0
        b1 = (A * tan_w - 1.0) / a0
        a1 = (tan_w - 1.0) / a0
    else:
        a0 = tan_w + A
        b0 = (A * (tan_w + 1.0)) / a0
        b1 = (A * (tan_w - 1.0)) / a0
        a1 = (tan_w - A) / a0

    return BiquadCoeffsFirstOrder(
        b0=b0,
        b1=b1,
        a1=a1,
    )


# second order IIR filter (kept for reference / compatibility)
def second_order_biquad_low_shelf(params: BiquadSecondOrder, sample_rate_hz: float) -> BiquadCoeffsSecondOrder:
    fc: float64 = params.fc
    q_factor: float64 = params.Q
    gain_db: float64 = params.gain_db

    amplitude = 10 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / sample_rate_hz
    alpha = np.sin(w0) / (2.0 * q_factor)
    cos_w0 = np.cos(w0)
    sqrt_a = np.sqrt(amplitude)

    b0 = amplitude * (
        (amplitude + 1.0) - (amplitude - 1.0) * cos_w0 + 2.0 * sqrt_a * alpha
    )
    b1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cos_w0)
    b2 = amplitude * (
        (amplitude + 1.0) - (amplitude - 1.0) * cos_w0 - 2.0 * sqrt_a * alpha
    )
    a0 = (amplitude + 1.0) + (amplitude - 1.0) * cos_w0 + 2.0 * sqrt_a * alpha
    a1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cos_w0)
    a2 = (amplitude + 1.0) + (amplitude - 1.0) * cos_w0 - 2.0 * sqrt_a * alpha
    
    b0 = b0 / a0
    b1 = b1 / a0
    b2 = b2 / a0
    a1 = a1 / a0
    a2 = a2 / a0
    return BiquadCoeffsSecondOrder(b0=b0, b1=b1, b2=b2, a1=a1, a2=a2)


def second_order_frequency_response(coeffs: BiquadCoeffsSecondOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    b0 = coeffs.b0
    b1 = coeffs.b1
    b2 = coeffs.b2
    a1 = coeffs.a1
    a2 = coeffs.a2
    w = 2.0 * np.pi * frequencies_hz / sample_rate_hz
    z1 = np.exp(-1j * w)
    z2 = np.exp(-2j * w)
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2)


def second_order_filter_response(params: BiquadSecondOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    coeffs = second_order_biquad_low_shelf(params, sample_rate_hz)
    return 20.0 * np.log10(
        np.abs(second_order_frequency_response(coeffs, frequencies_hz, sample_rate_hz))
    )


def second_order_filter_coefficients(params: BiquadSecondOrder, sample_rate_hz: float, phon: float) -> tuple[BiquadCoeffsSecondOrder, float]:
    coeffs = second_order_biquad_low_shelf(params, sample_rate_hz)
    volume_db = phon - MAXIMUM_PHON
    return coeffs, volume_db


def first_order_biquad_high_shelf(params: BiquadFirstOrder, sample_rate_hz: float) -> BiquadCoeffsFirstOrder:
    fc: float64 = params.fc
    gain_db: float64 = params.gain_db

    # Standard hifi 1. ordens High-Shelf ( bilinear transform analogi )
    A = 10.0 ** (gain_db / 20.0)
    th = 2.0 * np.pi * fc / sample_rate_hz
    
    tan_w = np.tan(th / 2.0)
    
    if gain_db >= 0:
        # Boost-konfigurasjon
        a0 = tan_w + 1.0
        b0 = (tan_w + A) / a0
        b1 = (tan_w - A) / a0
        a1 = (tan_w - 1.0) / a0
    else:
        # Cut-konfigurasjon
        a0 = A * tan_w + 1.0
        b0 = (A * (tan_w + 1.0)) / a0
        b1 = (A * (tan_w - 1.0)) / a0
        a1 = (A * tan_w - 1.0) / a0

    return BiquadCoeffsFirstOrder(
        b0=b0,
        b1=b1,
        a1=a1
    )


def first_order_frequency_response(coeffs: BiquadCoeffsFirstOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    b0 = coeffs.b0
    b1 = coeffs.b1
    a1 = coeffs.a1
    
    w = 2.0 * np.pi * frequencies_hz / sample_rate_hz
    z1 = np.exp(-1j * w)
    
    return (b0 + b1 * z1) / (1.0 + a1 * z1)


def first_order_low_shelf_filter_response(params: BiquadFirstOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    coeffs = first_order_biquad_low_shelf(params, sample_rate_hz)
    return 20.0 * np.log10(
        np.abs(first_order_frequency_response(coeffs, frequencies_hz, sample_rate_hz))
    )


def first_order_high_shelf_filter_response(params: BiquadFirstOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    coeffs = first_order_biquad_high_shelf(params, sample_rate_hz)
    return 20.0 * np.log10(
        np.abs(first_order_frequency_response(coeffs, frequencies_hz, sample_rate_hz))
    )


def first_order_filter_response(params: BiquadFirstOrder, frequencies_hz: ndarray, sample_rate_hz: float) -> ndarray:
    return first_order_high_shelf_filter_response(params, frequencies_hz, sample_rate_hz)


def first_order_filter_coefficients(params: BiquadFirstOrder, sample_rate_hz: float, phon: float) -> tuple[BiquadCoeffsFirstOrder, float]:
    coeffs = first_order_biquad_low_shelf(params, sample_rate_hz)
    volume_db = phon - MAXIMUM_PHON
    return coeffs, volume_db


def first_order_baked_coefficients(params: BiquadFirstOrder, sample_rate_hz: float, phon: float) -> tuple[BiquadCoeffsFirstOrder, float]:
    coeffs = first_order_biquad_low_shelf(params, sample_rate_hz)
    b0 = coeffs.b0
    b1 = coeffs.b1
    a1 = coeffs.a1
    volume_db = phon - MAXIMUM_PHON
    volume_gain = 10 ** (volume_db / 20.0)
    return BiquadCoeffsFirstOrder(
        b0=b0 * volume_gain,
        b1=b1 * volume_gain,
        a1=a1,
    ), volume_db


def first_order_print_row(
    args: argparse.Namespace,
    sample_rate_hz: float,
    phon: float,
    params: BiquadFirstOrder,
    coeffs: BiquadCoeffsFirstOrder,
    volume_db: float,
) -> None:
    b0 = coeffs.b0
    b1 = coeffs.b1
    a1 = coeffs.a1
    
    fc = params.fc
    gain_db = params.gain_db
    
    if args.bit_width == 32:
        values = [float_to_q4_28(value) for value in (a1, b0, b1)]
        has_volume = "biquad" if args.coeff_mode == "filter" else "biquad * volume"
        print(
            "    {{ {:11d}, {:11d}, {:11d} }},"
            "  /* phon={:.1f} volume={:.1f} dB fs={:.0f} Hz {} */".format(*values, phon, volume_db, sample_rate_hz, has_volume)
        )
    else:
        print(
            f"phon={phon:4.1f} volume={volume_db:5.1f} dB frequency={sample_rate_hz} Hz "
            f"fc={fc:12.8f} gain={gain_db:12.8f} dB "
            f"b0={b0:18.12f} b1={b1:18.12f} "
            f"a1={a1:18.12f}"
        )
        

def optimization_cost(params: ndarray, frequencies_hz: ndarray, sample_rate_hz: float, target_db: ndarray) -> ndarray:
    low_shelf = BiquadFirstOrder(params[0], params[1])
    high_shelf = BiquadFirstOrder(params[2], params[3])

    H = first_order_low_shelf_filter_response(low_shelf, frequencies_hz, sample_rate_hz)
    H += first_order_high_shelf_filter_response(high_shelf, frequencies_hz, sample_rate_hz)
    return (H - target_db)


def iso226_contour(phon: float) -> tuple[ndarray, ndarray]:
    """Return ISO-226 contour, extrapolating pydsm's 90-phon ceiling."""
    if phon <= 90.0:
        return iso226_spl_contour(phon, hfe=False)

    frequencies, spl_90 = iso226_spl_contour(90.0, hfe=False)
    frequencies_895, spl_895 = iso226_spl_contour(89.5, hfe=False)
    if not np.array_equal(frequencies, frequencies_895):
        raise RuntimeError("ISO-226 contour frequency grids do not match")
    slope_per_phon = (spl_90 - spl_895) / 0.5
    return frequencies, spl_90 + (phon - 90.0) * slope_per_phon


def optimize_contours(sample_rate_hz: float, frequencies_hz: ndarray) -> dict[float, ndarray]:
    ref_f, ref_spl = iso226_contour(REFERENCE_PHON)
    reference = CubicSpline(ref_f, ref_spl)(frequencies_hz)
    optimized = {}
    initial = np.concatenate((LOW_SHELF_INITIAL_PARAMS.copy(), HIGH_SHELF_INITIAL_PARAMS.copy()), axis=0)

    for phon in PHON_LEVELS:
        iso_f, iso_spl = iso226_contour(phon)
        contour = CubicSpline(iso_f, iso_spl)(frequencies_hz)
        target = (contour - reference) + (REFERENCE_PHON - phon)
        result = least_squares(
            optimization_cost,
            initial,
            bounds=(
                np.concatenate((LOW_SHELF_LOWER_BOUNDS, HIGH_SHELF_LOWER_BOUNDS), axis=0),
                np.concatenate((LOW_SHELF_UPPER_BOUNDS, HIGH_SHELF_UPPER_BOUNDS), axis=0)
            ),
            args=(frequencies_hz, sample_rate_hz, target),
        )
        optimized[phon] = result.x
        initial = result.x
    return optimized


def second_order_baked_coefficients(params: BiquadSecondOrder, sample_rate_hz: float, phon: float) -> tuple[BiquadCoeffsSecondOrder, float]:
    coeffs = second_order_biquad_low_shelf(params, sample_rate_hz)
    b0 = coeffs.b0
    b1 = coeffs.b1
    b2 = coeffs.b2
    a1 = coeffs.a1
    a2 = coeffs.a2
    volume_db = phon - MAXIMUM_PHON
    volume_gain = 10 ** (volume_db / 20.0)
    return BiquadCoeffsSecondOrder(
        b0=b0 * volume_gain,
        b1=b1 * volume_gain,
        b2=b2 * volume_gain,
        a1=a1,
        a2=a2,
    ), volume_db


def second_order_print_row(args: argparse.Namespace, sample_rate_hz: float, phon: float, params: BiquadSecondOrder) -> None:
    coeffs, volume_db = second_order_filter_coefficients(params, sample_rate_hz, phon) if args.coeff_mode == "filter" else second_order_baked_coefficients(params, sample_rate_hz, phon)
    b0 = coeffs.b0
    b1 = coeffs.b1
    b2 = coeffs.b2
    a1 = coeffs.a1
    a2 = coeffs.a2
    
    fc = params.fc
    Q = params.Q
    gain_db = params.gain_db
    
    if args.bit_width == 32:
        values = [float_to_q4_28(value) for value in (a1, a2, b0, b1, b2)]
        print(
            "    {{ {:11d}, {:11d}, {:11d}, {:11d}, {:11d} }},"
            "  /* phon={:.1f} volume={:.1f} dB */".format(*values, phon, volume_db)
        )
    else:
        print(
            f"phon={phon:4.1f} volume={volume_db:5.1f} dB "
            f"fc={fc:12.8f} Q={Q:12.8f} gain={gain_db:12.8f} dB "
            f"b0={b0:18.12f} b1={b1:18.12f} b2={b2:18.12f} "
            f"a1={a1:18.12f} a2={a2:18.12f}"
        )


def plot_results(
    args: argparse.Namespace,
    optimized: dict[float, ndarray],
    frequencies_hz: ndarray,
    sample_rate_hz: float,
) -> None:
    # Slightly wider figure to give ample room on the right
    fig, ax = plt.subplots(figsize=(13, 8), dpi=100)
    vline = ax.axvline(x=50.0, color="k", linestyle="--", linewidth=0.75, visible=False)

    # Phon levels subset to plot and display (every 5 phon step: 25, 30, ..., 85)
    plotted_phons = [PHON_LEVELS[i] for i in range(0, len(PHON_LEVELS), 10)]

    # Interpolators for interactive lookup
    contour_splines = {}
    response_splines = {}

    for i, phon in enumerate(plotted_phons):
        color = f"C{i % 10}"
        iso_f, iso_spl = iso226_contour(phon)
        spl_interp = CubicSpline(iso_f, iso_spl)(frequencies_hz)

        optimized_phon = optimized[phon]
        lowshelf_biquad = BiquadFirstOrder(
            fc=optimized_phon[0], gain_db=optimized_phon[1]
        )
        highshelf_biquad = BiquadFirstOrder(
            fc=optimized_phon[2], gain_db=optimized_phon[3]
        )
        resp = first_order_low_shelf_filter_response(
            lowshelf_biquad, frequencies_hz, sample_rate_hz
        ) + first_order_high_shelf_filter_response(
            highshelf_biquad, frequencies_hz, sample_rate_hz
        )

        contour_splines[phon] = CubicSpline(frequencies_hz, spl_interp)
        response_splines[phon] = CubicSpline(frequencies_hz, resp)

        if args.graph_type == "ISO226":
            ax.semilogx(
                frequencies_hz,
                spl_interp,
                label=f"ISO226 {phon:.0f} ph",
                color=color,
            )
        elif args.graph_type == "filter":
            ax.semilogx(
                frequencies_hz,
                resp,
                label=f"filter {phon:.0f} ph",
                color=color,
            )
        elif args.graph_type == "loudnesscontours":
            y_compensated = spl_interp - resp
            ax.semilogx(
                frequencies_hz,
                y_compensated,
                label=f"ISO226 {phon:.0f} ph * filter",
                color=color,
            )
            # Unfiltered curve with dashed line in the same color
            ax.semilogx(
                frequencies_hz,
                spl_interp,
                linestyle="--",
                linewidth=1.2,
                color=color,
                alpha=0.8,
                label=f"ISO226 {phon:.0f} ph (unfiltered)",
            )

    if args.graph_type == "loudnesscontours":
        ref_f, ref_spl = iso226_contour(REFERENCE_PHON)
        spl_ref = CubicSpline(ref_f, ref_spl)(frequencies_hz)
        ax.semilogx(
            frequencies_hz,
            spl_ref,
            "k:",
            linewidth=2.0,
            label="ISO226 80 ph (reference)",
        )

    # Box alignment & positioning based on graph type
    if args.graph_type == "filter":
        text_x, text_y = 0.50, 0.96
        ha = "center"
    else:  # "loudnesscontours" or "ISO226"
        text_x, text_y = 0.98, 0.96
        ha = "right"

    info_box = ax.text(
        text_x,
        text_y,
        "",
        transform=ax.transAxes,
        ha=ha,
        va="top",
        fontsize="small",
        family="monospace",
        bbox={
            "boxstyle": "round,pad=0.6",
            "facecolor": "white",
            "edgecolor": "#666666",
            "alpha": 0.90,
        },
        zorder=10,
    )

    def generate_box_text(freq: float) -> str:
        lines = [f"Frequency: {freq:6.1f} Hz\n" + "-" * 38]
        split = (len(plotted_phons) + 1) // 2
        col_a, col_b = plotted_phons[:split], plotted_phons[split:]
        col_width = 19

        for i in range(max(len(col_a), len(col_b))):
            row = []
            if i < len(col_a):
                p = col_a[i]
                if args.graph_type == "loudnesscontours" or args.graph_type == "filter":
                    val = float(response_splines[p](freq))
                    cell = f"{p:4.1f} ph: {val:+5.2f} dB"
                else:  # "ISO226"
                    val = float(contour_splines[p](freq))
                    cell = f"{p:4.1f} ph: {val:5.1f} dB"
                row.append(cell.ljust(col_width))

            if i < len(col_b):
                p = col_b[i]
                if args.graph_type == "loudnesscontours" or args.graph_type == "filter":
                    val = float(response_splines[p](freq))
                    cell = f"{p:4.1f} ph: {val:+5.2f} dB"
                else:  # "ISO226"
                    val = float(contour_splines[p](freq))
                    cell = f"{p:4.1f} ph: {val:5.1f} dB"
                row.append(cell)
            lines.append(" ".join(row))

        return "\n".join(lines)

    # Set default display for 150 Hz
    defaut_frequency_hz = 150.0
    info_box.set_text(generate_box_text(defaut_frequency_hz))

    def on_mouse_move(event):
        if event.inaxes == ax and event.xdata and event.xdata > 0:
            freq = float(np.clip(event.xdata, frequencies_hz[0], frequencies_hz[-1]))
            vline.set_xdata([freq])
            vline.set_visible(True)
            info_box.set_text(generate_box_text(freq))
        else:
            vline.set_visible(False)
            info_box.set_text(generate_box_text(defaut_frequency_hz))
        fig.canvas.draw_idle()

    fig.canvas.mpl_connect("motion_notify_event", on_mouse_move)

    ax.grid(True, which="both", axis="x", ls="-", alpha=0.3)
    ax.set_xlim(20.0, 17000.0)
    spotify_freqs = [60, 150, 400, 1000, 2400, 15000]    
    spotify_labels = ["60", "150", "400", "1K", "2.4K", "15K"]
    ax.set_xticks(spotify_freqs)
    ax.set_xticklabels(spotify_labels, fontsize=10, weight="bold")
    ax.grid(True, which="major", axis="y", ls="-", alpha=0.5)
    for f in spotify_freqs:
        ax.axvline(x=f, color="#A8C3D4", linestyle="-.", linewidth=0.8, alpha=0.6, zorder=1)
    ax.set_xlabel("Frekvens [Hz]\n(Spotify EQ)")
    ax.set_ylabel("Volum [dB] / loudness [phon]")
    ax.legend(loc="lower left", fontsize="small", framealpha=0.95)
    fig.tight_layout(pad=1.0)
 
    # Place legend outside on the bottom-right corner
    ax.legend(
        fontsize="small",
        loc="lower left",
        bbox_to_anchor=(1.02, 0.0),
        borderaxespad=0.0,
    )

    plt.title(f"Type: {args.graph_type} (fs={sample_rate_hz:.0f} Hz)")
    plt.tight_layout(rect=[0, 0, 0.97, 1])
    plt.show()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Optimize one first order low-shelf and one first order high-shelf ISO 226 loudness biquad per 0.5 phon and "
            "bake the corresponding -60..0 dB playback volume into b0/b1."
        )
    )
    parser.add_argument(
        "-g", "--graph", action="store_true", help="Show response graph"
    )
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
    parser.add_argument(
        "--coeff-mode",
        choices=["filterandvolume", "filter"],
        default="filterandvolume",
        help=(
            "filterandvolume: bake playback volume into b0/b1; "
            "filter: low-shelf only, no volume scaling"
        ),
    )
    parser.add_argument(
        "--filter",
        choices=["lowshelf", "highshelf"],
        default="lowshelf",
        help=(
            "choose between the low-shelf or the high-shelf to stdout; "
            "high-shelf does not bake playback volume into b0/b1"
        ),
    )
    return parser.parse_args()


# Backward compatibility aliases
biquad_low_shelf = first_order_biquad_low_shelf
baked_coefficients = first_order_baked_coefficients
filter_coefficients = first_order_filter_coefficients
biquad_high_shelf = first_order_biquad_high_shelf


def main() -> None:
    args = parse_args()
    if args.frequencymode == "both":
        rates = [rate for _name, rate in SAMPLE_RATES_HZ]
    else:
        rates = [rate for name, rate in SAMPLE_RATES_HZ if name == args.frequencymode]

    frequencies_hz = np.logspace(np.log10(20.0), np.log10(17000.0), 400)
    for sample_rate_hz in rates:
        optimized = optimize_contours(sample_rate_hz, frequencies_hz)
        for phon in PHON_LEVELS:
            optimized_phon = optimized[phon]
            if args.filter == "lowshelf":
                params = BiquadFirstOrder(fc=optimized_phon[0], gain_db=optimized_phon[1])
                coeffs, volume_db = (
                    first_order_filter_coefficients(params, sample_rate_hz, phon)
                    if args.coeff_mode == "filter"
                    else first_order_baked_coefficients(params, sample_rate_hz, phon)
                )
                first_order_print_row(args, sample_rate_hz, phon, params, coeffs, volume_db)
            elif args.filter == "highshelf":
                params = BiquadFirstOrder(fc=optimized_phon[2], gain_db=optimized_phon[3])
                coeffs = first_order_biquad_high_shelf(params, sample_rate_hz)
                volume_db = phon - MAXIMUM_PHON
                first_order_print_row(args, sample_rate_hz, phon, params, coeffs, volume_db)
        if args.graph:
            plot_results(args, optimized, frequencies_hz, sample_rate_hz)
