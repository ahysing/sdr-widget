#!/usr/bin/env python3
"""
Fits a 4-stage biquad chain (low-shelf, mid-peak, high-shelf, HF-peak)
to the ISO 226 equal-loudness contour, so that the resulting filter can
mimic the perceived loudness curve in a fixed-point (Q8) C program.
"""
import numpy as np
import argparse
from pydsm.iso226 import iso226_spl_contour
from scipy.optimize import least_squares
from scipy.interpolate import CubicSpline
import matplotlib.pyplot as plt

import sys

epsilon = 1e-7

SAMPLE_RATES_HZ = [
    ("hdmi", 48000.0),
    ("cd", 44100.0),
]

PHON_LEVELS = [40, 50, 60, 70, 80]
# build Biquad-formulas (RBJ)
def biquad_low_shelf(fc, Q, gain_db, sample_rate_hz):
    A = 10**(gain_db / 40)
    w0 = 2 * np.pi * fc / sample_rate_hz
    alpha = np.sin(w0)/(2*Q)
    cosw = np.cos(w0)

    b0 =  A*((A+1) + (A-1)*cosw + 2*np.sqrt(A)*alpha)
    b1 = -2*A*((A-1) + (A+1)*cosw)
    b2 =  A*((A+1) + (A-1)*cosw - 2*np.sqrt(A)*alpha)

    a0 =      (A+1) - (A-1)*cosw + 2*np.sqrt(A)*alpha
    a1 =  2*((A-1) - (A+1)*cosw)
    a2 =      (A+1) - (A-1)*cosw - 2*np.sqrt(A)*alpha

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

def biquad_peaking(fc, Q, gain_db, sample_rate_hz):
    A = 10**(gain_db/40)
    w0 = 2*np.pi*fc/sample_rate_hz
    alpha = np.sin(w0)/(2*Q)
    cosw = np.cos(w0)

    b0 = 1 + alpha*A
    b1 = -2*np.cos(w0)
    b2 = 1 - alpha*A
    a0 = 1 + alpha/A
    a1 = -2*np.cos(w0)
    a2 = 1 - alpha/A

    # normaliser til a0 = 1
    b0 /= a0; b1 /= a0; b2 /= a0
    a1 /= a0; a2 /= a0
    return np.array([b0, b1, b2, a1, a2])


# frequency response
def freq_response(coeffs, frequencies_hz, sample_rate_hz):
    b0, b1, b2, a1, a2 = coeffs
    w = 2*np.pi*frequencies_hz/sample_rate_hz
    z1 = np.exp(-1j*w)
    z2 = np.exp(-2j*w)
    H = (b0 + b1*z1 + b2*z2)/(1 + a1*z1 + a2*z2)
    return H


# Total response
def total_response(params, frequencies_hz, sample_rate_hz):
    fc1, Q1, g1, fc2, Q2, g2, fc3, Q3, g3, fc4, Q4, g4 = params

    H = np.ones_like(frequencies_hz, dtype=complex)

    H *= freq_response(
        biquad_low_shelf(fc1, Q1, g1, sample_rate_hz),
        frequencies_hz,
        sample_rate_hz,
    )
    H *= freq_response(
        biquad_peaking(fc2, Q2, g2, sample_rate_hz),
        frequencies_hz,
        sample_rate_hz,
    )
    H *= freq_response(
        biquad_high_shelf(fc3, Q3, g3, sample_rate_hz),
        frequencies_hz,
        sample_rate_hz,
    )
    H *= freq_response(
        biquad_peaking(fc4, Q4, g4, sample_rate_hz),
        frequencies_hz,
        sample_rate_hz,
    )

    return 20 * np.log10(np.abs(H))


# cost function
def cost(params, frequencies_hz, sample_rate_hz, g_target):
    return total_response(params, frequencies_hz, sample_rate_hz) - g_target

def quantize(x):
    return float(f"{x:.8f}")

def main():
    parser = argparse.ArgumentParser(description="Fits a 4-stage biquad chain to ISO 226 equal-loudness contour.")
    parser.add_argument("-g", "--graph", action="store_true", help="Shows a graph")
    parser.add_argument("-fm", "--frequencymode", choices=["hdmi", "cd"], default="cd", help="Select frequency mode (hdmi or cd)")
    args = parser.parse_args()

    sample_rate_hz = 0.0
    for name, hz in SAMPLE_RATES_HZ:
        if name == args.frequencymode:
            sample_rate_hz = hz
            break

    if args.graph:
        from matplotlib.ticker import ScalarFormatter, LogLocator
        fig, ax = plt.subplots(figsize=(10, 8), dpi=100)
        # For the hover line
        vline = ax.axvline(color='k', linestyle='--', linewidth=0.5, visible=False)
        text = ax.text(0.02, 0.95, '', transform=ax.transAxes)

        def on_mouse_move(event):
            if event.inaxes:
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
        # Sample ISO226 equal-loudness contour at 'phon' phons.
        # iso226_spl_contour returns (frequencies_Hz, spl_dB) tabulated points.
        f_iso, spl_iso = iso226_spl_contour(phon, hfe=True)

        # Evaluation grid: log-spaced within the range of 20 Hz to 17000 Hz.
        # the CubicSpline is only evaluated inside its interpolation domain.
        f_min = 20.0
        f_max = 17000.0
        f_eval = np.logspace(np.log10(f_min), np.log10(f_max), 400)

        # Interpolate the ISO 226 SPL contour onto the log-spaced grid.
        spline = CubicSpline(f_iso, spl_iso)
        spl_interp = spline(f_eval)

        # Normalize to an EQ-curve: the target is the equal-loudness contour
        # relative to 1 kHz. Where the ear is less sensitive (high SPL required
        # in ISO226), the filter must BOOST by the same amount, so the target
        # gain is POSITIVE there.
        idx_1k = np.argmin(np.abs(f_eval - 1000))
        G_target = spl_interp - spl_interp[idx_1k]

        if args.graph:
            # Use different colors for different phons
            color = f"C{PHON_LEVELS.index(phon)}"
            ax.semilogx(f_eval, spl_interp, label=f"is226 {phon}ph", color=color, linestyle='-')

        # Bounds per issue specification:
        #   low-shelf : fc (hz), Q,  gain (dB)
        #   mid-peak  : fc (hz), Q,  gain (dB)
        #   high-shelf: fc (hz), Q,  gain (dB)
        #   HF-peak   : fc (hz), Q,  gain (dB)

        lower = [
            80,   4.7, 8.0,
            1100,  1.0, -2.0,
            7000, 1.0,  0.5,
            13000, 1.0,  0.5,
        ]

        # Initial guess (must lie strictly inside the bounds).

        init = [
                # 120,  1e-10,  40.0,  # low-shelf
                # 1490,  1e-10,  1.0,  # mid-peak
                # 9100, 1e-10,   2.5,  # high-shelf
                # 17000, 1e-10,   2.0,  # HF-peak
                120,  5.8,  10.0,  # low-shelf
                1490,  1.4,  1.0,  # mid-peak
                9100, 1.3,   2.5,  # high-shelf
                17000, 1.4,   2.0,  # HF-peak
        ]

        upper = [
            160,   10.0,  40.0,
            1900,   2.0,  3.0,
            11500,  1.8,  4.0,
            19000, 2.0,  4.0,
        ]
        #lower = [v - 1 for v in init]
        #upper =  [v + 1 for v in init]
        # Safety: clip init into the (open) bounds to keep least_squares happy.

        init = [
            min(
                max(init_i, lower_i + epsilon),
                upper_i - epsilon,
            )
            for init_i, lower_i, upper_i in zip(init, lower, upper)
        ]


        # Optimize
        res = least_squares(
            cost,
            init,
            bounds=(lower, upper),
            args=(f_eval, sample_rate_hz, G_target)
        )
        params_opt = init

        # Quantize to 8 decimal places (matches the C-side Q8 fixed-point precision).
        quantized = [quantize(x) for x in params_opt]

        section_names = ["low-shelf", "mid-peak", "high-shelf", "HF-peak"]
        print(f"ISO 226 fit at {phon} phon, fs = {sample_rate_hz} Hz")
        print(f"Optimizer status: {res.status} ({res.message})")
        print(f"Final cost (0.5 * sum sq residuals): {res.cost:.6f}")

        err = total_response(params_opt, f_eval, sample_rate_hz) - G_target
        print(f"Max |error|: {np.max(np.abs(err)):.4f} dB")
        print(f"RMS  error : {np.sqrt(np.mean(err**2)):.4f} dB")

        print("\nOptimized biquad parameters:")
        for i, name in enumerate(section_names):
            fc, Q, g = quantized[3 * i:3 * i + 3]
            print(f"  {name:10s}  fc = {fc:12.8f} Hz   Q = {Q:12.8f}   gain = {g:12.8f} dB")

        print("\nFlat list:", quantized)
        print()
        idx_1k = np.argmin(np.abs(f_eval - 1000))

        print(
            phon,
            f_eval[idx_1k],
            spl_interp[idx_1k]
        )


        if args.graph:
            # Generate label with parameters
            # use the three parameters to label the lines; 164.64455070 Hz Q 1.50000000 -0.00334619 dB
            fc_l, Q_l, g_l = quantized[0], quantized[1], quantized[2]
            label_eq = f"EQ {phon}ph: {fc_l:.8f} Hz Q {Q_l:.8f} {g_l:.8f} dB"

            resp = total_response(params_opt, f_eval, sample_rate_hz)
            # G_target = spl_interp - spl_interp[idx_1k], and `resp` fits G_target.
            # So spl_interp ≈ spl_interp[idx_1k] + resp — add the filter boost on top
            # of the 1 kHz reference level to overlay the EQ on the ISO226 curve.
            eq_curve = spl_interp[idx_1k] + resp

            color = f"C{PHON_LEVELS.index(phon)}"
            ax.semilogx(f_eval, eq_curve, '--', label=label_eq, color=color)

    if args.graph:
        ax.set_xlabel("frequency [hz]")
        ax.set_ylabel("loudness [phon]")

        # Draw vertical grid lines for every 10 phon
        # If the Y-axis is labeled "loudness [phon]", 10 phon intervals are horizontal.
        ax.yaxis.set_major_locator(plt.MultipleLocator(10))
        ax.grid(True, which="major", axis="y", ls="-", alpha=0.5)

        # Draw a number on the x-axis twice as often.
        # Do not draw x-axis numbers as an exponential.
        # Increase frequency of x-axis ticks.
        ax.xaxis.set_major_locator(LogLocator(base=10.0, subs=(1.0, 2.0, 5.0)))
        ax.xaxis.set_major_formatter(ScalarFormatter())
        ax.ticklabel_format(style='plain', axis='x')

        ax.grid(True, which="both", axis="x", ls="-", alpha=0.3)
        ax.legend(fontsize='small', loc='upper right')
        plt.tight_layout()
        plt.show()

if __name__ == "__main__":
    main()