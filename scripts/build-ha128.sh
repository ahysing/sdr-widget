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

# Same tokens as Makefile AUDIO_WIDGET_DEFAULTS (including BUILD_* from git), then Mk3 profile.
BASE="$(make -f Makefile -s --eval 'test:; @echo $(AUDIO_WIDGET_DEFAULTS)' test)"
HA128_AUDIO_WIDGET_DEFAULTS="${BASE} \
	-UFEATURE_PRODUCT_HA256 -UFEATURE_SPDIF_CMD -UHW_GEN_SPRX \
	-DFEATURE_PRODUCT_AB1x -DHW_GEN_AB1X"

echo "=== clean Release (full object rebuild for Mk3 flags) ==="
make -C Release clean

echo "=== compile (Mk3 / AB1x, ${PARTNAME}) ==="
make audio-widget \
	PARTNAME="${PARTNAME}" \
	AUDIO_WIDGET_DEFAULTS="${HA128_AUDIO_WIDGET_DEFAULTS}"

echo "=== link ${PARTNAME} (Release/makefile still uses uc3a3256 on link line) ==="
(
	cd Release
	make -n all | sed 's/-mpart=uc3a3256/-mpart=uc3a3128/' | sh
)

echo "Built ${ROOT}/Release/widget.elf (AT32UC3A3128 / Henry Audio USB DAC 128 Mk3)"
