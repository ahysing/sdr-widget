#!/usr/bin/env bash
# Build Henry Audio USB DAC 128 Mk3 (AT32UC3A3128) without changing root Makefile defaults.
#
# Root Makefile stays on HA256/SPRX for DA 256 development. This script overrides
# compile flags for Mk3 (AB1x), cleans Release objects (avoids stale SPRX .o files),
# runs make audio-widget, then re-links widget.elf for uc3a3128.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export MSYSTEM="${MSYSTEM:-UCRT64}"
export AVR32BIN="${AVR32BIN:-/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin}"
export PATH="${MSYS2_UCRT_BIN:-/c/msys64/ucrt64/bin}:${MSYS2_USR_BIN:-/c/msys64/usr/bin}:/usr/bin:/bin:${AVR32BIN}:${PATH}"

PARTNAME=-mpart=uc3a3128
LOUDNESS_DISABLE="${LOUDNESS_DISABLE:-0}"
USBSTATISTICS_DISABLE="${USBSTATISTICS_DISABLE:-0}"

# Echo AUDIO_WIDGET_DEFAULTS only — never run the real `test` target (PC unit tests).
if make -f Makefile -s --no-print-directory print-audio-widget-defaults >/dev/null 2>&1; then
	BASE="$(make -f Makefile -s --no-print-directory print-audio-widget-defaults)"
else
	BASE="$(
		make -f Makefile -s --no-print-directory \
			--eval 'print-audio-widget-defaults:; @echo $(AUDIO_WIDGET_DEFAULTS)' \
			print-audio-widget-defaults
	)"
fi

HA128_AUDIO_WIDGET_DEFAULTS="${BASE} \
	-UFEATURE_PRODUCT_HA256 -UFEATURE_SPDIF_CMD -UHW_GEN_SPRX \
	-DFEATURE_PRODUCT_AB1x -DHW_GEN_AB1X"

# 74d50f2a … c66d226a: UAC2 calls LOUDNESS_PROCESS_UAC2_STEREO_PACKET before 19a749d7 adds it to loudness.h.
if grep -q 'LOUDNESS_PROCESS_UAC2_STEREO_PACKET' src/uac2_device_audio_task.c 2>/dev/null \
	&& ! grep -q 'LOUDNESS_PROCESS_UAC2_STEREO_PACKET' src/loudness.h 2>/dev/null; then
	PY="${AUDIO_BISECT_PYTHON:-python3}"
	if [[ -x /c/msys64/ucrt64/bin/python3.exe ]]; then
		PY=/c/msys64/ucrt64/bin/python3.exe
	fi
	if [[ -f scripts/patch_uac2_link.py ]]; then
		echo "=== UAC2 link fix (LOUDNESS_PROCESS_UAC2_STEREO_PACKET in loudness.h) ==="
		"${PY}" scripts/patch_uac2_link.py
	fi
	if [[ -f scripts/patch_uac2_loudness_c.py ]] \
		&& grep -q 'loudness_uac2_packet_filter_enabled' src/uac2_device_audio_task.c 2>/dev/null \
		&& ! grep -q 'loudness_uac2_packet_filter_enabled' src/loudness.c 2>/dev/null; then
		echo "=== UAC2 link fix (loudness_uac2_packet_filter_enabled in loudness.c) ==="
		"${PY}" scripts/patch_uac2_loudness_c.py
	fi
fi

echo "=== clean Release (full object rebuild for Mk3 flags) ==="
make -C Release clean

echo "=== compile (Mk3 / AB1x, ${PARTNAME}, LOUDNESS_DISABLE=${LOUDNESS_DISABLE}) ==="
make audio-widget \
	PARTNAME="${PARTNAME}" \
	LOUDNESS_DISABLE="${LOUDNESS_DISABLE}" \
	USBSTATISTICS_DISABLE="${USBSTATISTICS_DISABLE}" \
	AUDIO_WIDGET_DEFAULTS="${HA128_AUDIO_WIDGET_DEFAULTS}"

echo "=== link ${PARTNAME} (Release/makefile still uses uc3a3256 on link line) ==="
(
	cd Release
	# make -n prints "Nothing to be done" when widget.elf is fresh; only run avr32-gcc link lines.
	make -n all 2>/dev/null \
		| sed 's/-mpart=uc3a3256/-mpart=uc3a3128/' \
		| grep -E '[[:space:]]*avr32-gcc' \
		| sh
)

echo "Built ${ROOT}/Release/widget.elf (AT32UC3A3128 / Henry Audio USB DAC 128 Mk3)"
