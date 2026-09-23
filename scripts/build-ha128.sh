#!/usr/bin/env bash
# Build Henry Audio USB DAC 128 Mk3 (AT32UC3A3128) without changing root Makefile defaults.
#
# Root Makefile stays on DA256/SPRX for DA 256 development. This script runs
# make audio-widget-mk3 (FEATURE_PRODUCT_AB1x / HW_GEN_AB1X).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export MSYSTEM="${MSYSTEM:-UCRT64}"
export MSYSTEM_PREFIX="${MSYSTEM_PREFIX:-/ucrt64}"
export AVR32BIN="${AVR32BIN:-/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin}"
# UCRT64 shell: gcc in /ucrt64/bin, make often in /usr/bin — both must precede AVR32 on PATH.
export PATH="${MSYS2_UCRT_BIN:-/ucrt64/bin}:${MSYS2_USR_BIN:-/usr/bin}:/bin:${AVR32BIN}:${PATH}"

echo "=== Mk3 firmware (FEATURE_PRODUCT_AB1x): make audio-widget-mk3 ==="
make audio-widget-mk3

echo "Built ${ROOT}/Release/widget.elf (AT32UC3A3128 / Henry Audio USB DAC 128 Mk3)"
