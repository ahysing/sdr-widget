import numpy as np
import argparse
from pydsm.iso226 import iso226_spl_contour
from scipy.interpolate import CubicSpline
from scipy.optimize import least_squares
import matplotlib.pyplot as plt

def parse_args():
    parser = argparse.ArgumentParser(
        description="Overlays a fixed 2-biquad (low-shelf + high-shelf) chain on ISO 226 curves."
    )
    parser.add_argument("-g", "--graph", action="store_true", help="Shows a graph")
    parser.add_argument("--graph-type", choices=["filter", "ISO226", "loudnesscontours"], default="loudnesscontours",
                        help="Select graph type")
    parser.add_argument("-fm", "--frequencymode", choices=["hdmi", "cd", "both"], default="cd",
                        help="Select frequency mode (hdmi, cd, or both)")
    parser.add_argument("--bit-width", type=int, choices=[32, 64], help="Print coefficients as integers (32-bit Q3.29 or 64-bit)")
    args = parser.parse_args()
    return args

def float_to_q3_29(value: float) -> int:
    # 29 bits for the fractional part
    FRACTIONAL_BITS = 29
    SCALE = 1 << FRACTIONAL_BITS  # 2^29 = 536870912

    # Define 32-bit signed integer boundaries
    INT32_MIN = -2147483648  # Corresponds to -4.0
    INT32_MAX = 2147483647   # Corresponds to ~3.999999998

    # Scale and round to the nearest whole integer
    scaled_value = int(round(value * SCALE))

    # Saturation (prevent overflow/underflow)
    if scaled_value > INT32_MAX:
        return INT32_MAX
    if scaled_value < INT32_MIN:
        return INT32_MIN

    return scaled_value


def float_to_q61(value: float) -> int:
    # 61 bits for the fractional part (leaving 2 bits for integer + 1 sign bit)
    # This matches the spirit of the Q3.29 but for 64-bit
    FRACTIONAL_BITS = 61
    SCALE = 1 << FRACTIONAL_BITS

    INT64_MIN = -9223372036854775808
    INT64_MAX = 9223372036854775807

    scaled_value = int(round(value * SCALE))

    if scaled_value > INT64_MAX:
        return INT64_MAX
    if scaled_value < INT64_MIN:
        return INT64_MIN

    return scaled_value

SAMPLE_RATES_HZ = [
    ("hdmi", 48000.0),
    ("cd", 44100.0),
]

PHON_LEVELS = [55, 65, 75]
# PHON_LEVELS = [45, 55, 65, 75, 85, 90]

# Initial fixed biquad chain: (name, fc, Q, gain_db, kind)
BIQUAD_CHAIN_INIT = [
    ("low-shelf",  120.0,  0.5,   12.0, "low_shelf"),   # Positive bass boost guess
    ("high-shelf", 8000.0, 0.5,    5.0, "high_shelf"),  # Positive treble boost guess
]

NUM_BIQUADS = len(BIQUAD_CHAIN_INIT)

epsilon = 1e-7

# build Biquad-formulas (RBJ)
def biquad_low_shelf(fc, Q, gain_db, sample_rate_hz):
    A = 10**(gain_db / 40)
    w0 = 2 * np.pi * fc / sample_rate_hz
    alpha = np.sin(w0)/(2*Q)
    cosw = np.cos(w0)

    b0 =  A*((A+1) - (A-1)*cosw + 2*np.sqrt(A)*alpha)
    b1 =  2*A*((A-1) - (A+1)*cosw)
    b2 =  A*((A+1) - (A-1)*cosw - 2*np.sqrt(A)*alpha)
    a0 =     (A+1) + (A-1)*cosw + 2*np.sqrt(A)*alpha
    a1 = -2*((A-1) + (A+1)*cosw)
    a2 =     (A+1) + (A-1)*cosw - 2*np.sqrt(A)*alpha

    b0 /= a0
    b1 /= a0
    b2 /= a0
    a1 /= a0
    a2 /= a0
    return np.array([b0, b1, b2, a1, a2])


def biquad_high_shelf(fc, Q, gain_db, sample_rate_hz):
    A = 10**(gain_db / 40)
    w0 = 2 * np.pi * fc / sample_rate_hz
    alpha = np.sin(w0)/(2*Q)
    cosw = np.cos(w0)

    b0 =    A*((A+1) + (A-1)*cosw + 2*np.sqrt(A)*alpha)
    b1 = -2*A*((A-1) + (A+1)*cosw)
    b2 =    A*((A+1) + (A-1)*cosw - 2*np.sqrt(A)*alpha)
    a0 =       (A+1) - (A-1)*cosw + 2*np.sqrt(A)*alpha
    a1 =    2*((A-1) - (A+1)*cosw)
    a2 =       (A+1) - (A-1)*cosw - 2*np.sqrt(A)*alpha

    b0 /= a0
    b1 /= a0
    b2 /= a0
    a1 /= a0
    a2 /= a0
    return np.array([b0, b1, b2, a1, a2])


# frequency response
def freq_response(coeffs, frequencies_hz, sample_rate_hz):
    b0, b1, b2, a1, a2 = coeffs
    w = 2*np.pi*frequencies_hz/sample_rate_hz
    z1 = np.exp(-1j*w)
    z2 = np.exp(-2j*w)
    H = (b0 + b1*z1 + b2*z2)/(1 + a1*z1 + a2*z2)
    return H


def chain_response(params, frequencies_hz, sample_rate_hz):
    fc1, Q1, g1, fc2, Q2, g2 = params
    H = np.ones_like(frequencies_hz, dtype=complex)

    H *= freq_response(biquad_low_shelf(fc1, Q1, g1, sample_rate_hz), frequencies_hz, sample_rate_hz)
    H *= freq_response(biquad_high_shelf(fc2, Q2, g2, sample_rate_hz), frequencies_hz, sample_rate_hz)

    return 20 * np.log10(np.abs(H))


def cost(params, frequencies_hz, sample_rate_hz, g_target):
    return chain_response(params, frequencies_hz, sample_rate_hz) - g_target

def plot(args, optimized_params_per_phon, f_eval, sample_rate_hz):
    from matplotlib.ticker import ScalarFormatter, LogLocator
    fig, ax = plt.subplots(figsize=(10, 8), dpi=100)
    vline = ax.axvline(color='k', linestyle='--', linewidth=0.5, visible=False)
    text = ax.text(0.02, 0.95, '', transform=ax.transAxes)

    def on_mouse_move(event):
        if event.inaxes and event.xdata and event.xdata > 0:
            vline.set_xdata([event.xdata])
            vline.set_visible(True)
            text.set_text(f"freq: {event.xdata:.1f} Hz")
            fig.canvas.draw_idle()
        else:
            vline.set_visible(False)
            text.set_text('')
            fig.canvas.draw_idle()

    fig.canvas.mpl_connect('motion_notify_event', on_mouse_move)

    for phon in PHON_LEVELS:
        color = f"C{PHON_LEVELS.index(phon)}"
        params_opt = optimized_params_per_phon[phon]

        # ISO226 curve
        f_iso, spl_iso = iso226_spl_contour(phon, hfe=True)
        spl_interp = CubicSpline(f_iso, spl_iso)(f_eval)

        # Biquad response
        resp = chain_response(params_opt, f_eval, sample_rate_hz)

        if args.graph_type == "ISO226":
            ax.semilogx(f_eval, spl_interp, label=f"ISO226 {phon}ph", color=color)
        elif args.graph_type == "filter":
            ax.semilogx(f_eval, resp, label=f"filter {phon}ph", color=color)
        elif args.graph_type == "loudnesscontours":
            # Combined effect: perceived loudness by the ear
            # Target: spl_interp + resp = spl_ref - (80 - phon)
            y = spl_interp - resp
            ax.semilogx(f_eval, y, label=f"ISO226 {phon}ph * filter", color=color)

            # Plot the individual target for this phon level
            f_iso_x, spl_iso_x = iso226_spl_contour(phon, hfe=True)
            spl_x = CubicSpline(f_iso_x, spl_iso_x)(f_eval)
            ax.semilogx(f_eval, spl_x, ':', label=f"ISO226 {phon}ph", color=color, alpha=0.9)

    if args.graph_type == "loudnesscontours":
        f_iso_ref, spl_iso_ref = iso226_spl_contour(80, hfe=True)
        spl_ref = CubicSpline(f_iso_ref, spl_iso_ref)(f_eval)
        ax.semilogx(f_eval, spl_ref, "k--", linewidth=2.0, label="ISO226 80ph (reference)")

    ax.set_xlabel("frequency [hz]")
    ax.set_ylabel("magnitude [dB] / loudness [phon]")
    ax.yaxis.set_major_locator(plt.MultipleLocator(10))
    ax.grid(True, which="major", axis="y", ls="-", alpha=0.5)
    ax.xaxis.set_major_locator(LogLocator(base=10.0, subs=(1.0, 2.0, 5.0)))
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.ticklabel_format(style='plain', axis='x')
    ax.grid(True, which="both", axis="x", ls="-", alpha=0.3)
    ax.legend(fontsize='small', loc='upper right')
    plt.title(f"Graph type: {args.graph_type} (fs={sample_rate_hz}Hz, {NUM_BIQUADS} biquads)")
    plt.tight_layout()
    plt.show()

def print_headers(args, sample_rate_hz, phon, section_names, params_opt):
    print(f"Optimized biquad chain, fs = {sample_rate_hz} Hz, phons = {phon} phon")
    for i, name in enumerate(section_names):
        fc, Q, g = params_opt[3*i:3*i+3]
        print(f"  {name:10s}  fc = {fc:12.8f} Hz   Q = {Q:12.8f}   gain = {g:12.8f} dB")
    print()

    header = f"Biquad coefficients (a0 = 1), fs = {sample_rate_hz} Hz, phons = {phon} phon"
    print(header)

    if not args.bit_width:
        print(f"  {'name':10s}  {'b0':>18s} {'b1':>18s} {'b2':>18s} {'a0':>18s} {'a1':>18s} {'a2':>18s}")

def print_stdout(args, name, b0, b1, b2, a0, a1, a2):
    if args.bit_width == 32:
        qb0 = float_to_q3_29(b0)
        qb1 = float_to_q3_29(b1)
        qb2 = float_to_q3_29(b2)
        qa0 = float_to_q3_29(a0)
        qa1 = float_to_q3_29(a1)
        qa2 = float_to_q3_29(a2)
        print(f"  {{ {qb0:11d}, {qb1:11d}, {qb2:11d}, {qa0:11d}, {qa1:11d}, {qa2:11d} }},")
        print()
    elif args.bit_width == 64:
        qb0 = float_to_q61(b0)
        qb1 = float_to_q61(b1)
        qb2 = float_to_q61(b2)
        qa0 = float_to_q61(a0)
        qa1 = float_to_q61(a1)
        qa2 = float_to_q61(a2)
        print(f"  {{ {qb0:20d}LL, {qb1:20d}LL, {qb2:20d}LL, {qa0:20d}LL, {qa1:20d}LL, {qa2:20d}LL }},")
        print()
    else:
        print(f"  {name:10s}  {b0:18.12f} {b1:18.12f} {b2:18.12f} {a0:18.12f} {a1:18.12f} {a2:18.12f}")
        print()


def main():
    args = parse_args()

    # Determine sample rates to process
    rates_to_process = []
    if args.frequencymode == "both":
        rates_to_process = [hz for _name, hz in SAMPLE_RATES_HZ]
    else:
        for name, hz in SAMPLE_RATES_HZ:
            if name == args.frequencymode:
                rates_to_process = [hz]
                break
    section_names = ["low-shelf", "high-shelf"]
    for sample_rate_hz in rates_to_process:
        # Evaluation grid
        f_min = 20.0
        f_max = 17000.0
        f_eval = np.logspace(np.log10(f_min), np.log10(f_max), 400)

        # Optimization for each phon level
        optimized_params_per_phon = {}

        for phon in PHON_LEVELS:
            # ISO226 target
            f_iso, spl_iso = iso226_spl_contour(phon, hfe=True)
            spline = CubicSpline(f_iso, spl_iso)
            spl_interp = spline(f_eval)

            # Reference function ISO226 80ph
            f_iso_ref, spl_iso_ref = iso226_spl_contour(80, hfe=True)
            spline_ref = CubicSpline(f_iso_ref, spl_iso_ref)
            spl_ref = spline_ref(f_eval)

            G_target = (spl_interp - spl_ref) + (80 - phon)

            # Initial guess from BIQUAD_CHAIN_INIT
            init = [
                BIQUAD_CHAIN_INIT[0][1], BIQUAD_CHAIN_INIT[0][2], BIQUAD_CHAIN_INIT[0][3], # low-shelf
                BIQUAD_CHAIN_INIT[1][1], BIQUAD_CHAIN_INIT[1][2], BIQUAD_CHAIN_INIT[1][3], # high-shelf
            ]

            # Bounds
            lower = [
                20,   0.1, -40,  # low-shelf (fc min, Q min, gain min)
                4000, 0.1, -40   # high-shelf
            ]
            upper = [
                300,   2.0, 40,  # low-shelf
                18000, 2.0, 40   # high-shelf
            ]

            # Clip init to bounds
            init = [min(max(init_i, lower_i + epsilon), upper_i - epsilon)
                    for init_i, lower_i, upper_i in zip(init, lower, upper)]

            res = least_squares(cost, init, bounds=(lower, upper), args=(f_eval, sample_rate_hz, G_target))
            params_opt = res.x
            optimized_params_per_phon[phon] = params_opt

        for phon in PHON_LEVELS:
            params_opt = optimized_params_per_phon[phon]
            print_headers(args, sample_rate_hz=sample_rate_hz, phon=phon, section_names=section_names, params_opt=params_opt)
            for i, name in enumerate(section_names):
                fc, Q, g = params_opt[3*i:3*i+3]

                dsp_gain = g
                if name == "low-shelf":
                    coeffs = biquad_low_shelf(fc, Q, dsp_gain, sample_rate_hz)
                elif name == "high-shelf":
                    coeffs = biquad_high_shelf(fc, Q, dsp_gain, sample_rate_hz)

                b0, b1, b2, a1, a2 = coeffs
                a0 = 1.0

                print_stdout(args, name, b0, b1, b2, a0, a1, a2)

        if args.graph:
            plot(args, optimized_params_per_phon, f_eval=f_eval, sample_rate_hz=sample_rate_hz)

if __name__ == "__main__":
    main()
