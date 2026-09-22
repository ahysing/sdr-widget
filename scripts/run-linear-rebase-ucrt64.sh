#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MSYSTEM="${MSYSTEM:-UCRT64}"
export SHELL="${SHELL:-/usr/bin/bash}"
export AVR32BIN="${AVR32BIN:-/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin}"
export PATH="/c/Program Files/Git/cmd:${AVR32BIN}:${PATH:-/ucrt64/bin:/usr/bin:/bin}"
cd "$ROOT"
mkdir -p rebase-logs
if [[ -n "${GIT_BRANCH:-}" ]]; then
    git checkout "$GIT_BRANCH"
elif [[ -n "${CHECKOUT_REF:-}" ]]; then
    git checkout "$CHECKOUT_REF"
fi
exec bash scripts/linear-loudness-rebase.sh
