#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PY="${AUDIO_BISECT_PYTHON:-python3}"
if [[ -x /c/msys64/ucrt64/bin/python3.exe ]]; then
	PY=/c/msys64/ucrt64/bin/python3.exe
fi
exec "${PY}" scripts/patch_uac2_link.py
