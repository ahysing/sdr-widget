#!/usr/bin/env bash
# Linear rebase: loudness from ab37942c onto audio-widget-experimental (d349fca3).
# Pre-ab37942c loudness DSP is imported as one squashed baseline (tree at 48648539).
# Squash groups: f066b235..6babbe55, 538869d3..1317e9c8
set -euo pipefail

BASE="${BASE:-d349fca3}"
BRANCH="${BRANCH:-pr/loudness-linear}"
UPSTREAM="${UPSTREAM:-audio-widget-experimental}"
LOG_DIR="${LOG_DIR:-rebase-logs}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p "$LOG_DIR"

export GIT_EDITOR=true
export UPSTREAM
export SHELL="${SHELL:-/usr/bin/bash}"
export BUILD_ANCHOR_COMMIT="${BUILD_ANCHOR_COMMIT:-HEAD}"

resolve_conflicts() {
    local commit="$1"
    while IFS= read -r path; do
        [[ -z "$path" ]] && continue
        case "$path" in
            src/loudness*|src/usb_statistics*|src/stats_*|tests/*|run-pc-tests.cmd|henryctl/*|statistics/*|src/uac2_*|Makefile|makefile.targets|Release/src/subdir.mk|Release/tests/*)
                git checkout --theirs -- "$path" 2>/dev/null || git show "$commit:$path" > "$path" 2>/dev/null || true
                ;;
            *)
                git checkout --ours -- "$path" 2>/dev/null || true
                ;;
        esac
        git add -- "$path" 2>/dev/null || true
    done < <(git diff --name-only --diff-filter=U 2>/dev/null || true)
}

finalize_pick() {
    local commit="$1"
    local msg="${2:-}"
    local squash="${3:-0}"
    bash "$ROOT/scripts/apply-loudness-integration.sh" "$commit"
    cmd.exe //c "del \\\\?\\$(cygpath -w "$ROOT")\\NUL" 2>/dev/null || rm -f NUL 2>/dev/null || true
    git reset HEAD -- "$LOG_DIR" rebase-run.log 2>/dev/null || true
    git reset HEAD -- Release/tests/pc/*.exe Release/widget.elf widget.elf 2>/dev/null || true
    rm -f Release/tests/pc/*.exe Release/widget.elf widget.elf 2>/dev/null || true
    if git diff --cached --quiet && git diff --quiet; then
        return 0
    fi
    git add -A
    git reset HEAD -- "$LOG_DIR" 2>/dev/null || true
    if [[ "$squash" == "1" && -n "$msg" ]]; then
        git commit -m "$msg"
    elif [[ -n "$msg" ]]; then
        git commit --amend -m "$msg"
    else
        git commit --amend --no-edit
    fi
}

verify_build() {
    local tag="$1"
    mkdir -p "$LOG_DIR"
    local log="$LOG_DIR/build-${tag}.log"
    {
        echo "=== Release clean ==="
        make -C Release clean
        echo "=== make audio-widget ==="
        MSYSTEM=UCRT64 SHELL=/usr/bin/bash make audio-widget
    } >"$log" 2>&1
}

verify_tests() {
    local tag="$1"
    mkdir -p "$LOG_DIR"
    local log="$LOG_DIR/test-${tag}.log"
    MSYSTEM=UCRT64 SHELL=/usr/bin/bash make test >"$log" 2>&1
}

verify_step() {
    local tag="$1"
    verify_build "$tag" || return 1
    verify_tests "$tag" || return 1
}

pick_one() {
    local commit="$1"
    local short
    short="$(git rev-parse --short "$commit")"
    echo ""
    echo "======== cherry-pick $short $(git log -1 --format=%s "$commit") ========"
    if git cherry-pick -X ours "$commit"; then
        :
    elif ! git diff --name-only --diff-filter=U 2>/dev/null | grep -q .; then
        if git diff --cached --quiet && git diff --quiet; then
            git cherry-pick --skip
            echo "Skipped empty pick $short"
            return 0
        fi
        git cherry-pick --continue
    else
        echo "Conflict on $short; hybrid resolve"
        resolve_conflicts "$commit"
        git cherry-pick --continue
    fi
    finalize_pick "$commit"
    verify_step "$short" || { echo "FAIL at $short"; return 1; }
    BUILD_ANCHOR_COMMIT="$(git rev-parse HEAD)"
    export BUILD_ANCHOR_COMMIT
    echo "PASS at $short"
}

pick_squash() {
    local start="$1"
    local end="$2"
    local msg="$3"
    local short
    short="$(git rev-parse --short "$end")"
    echo ""
    echo "======== squash ${start}..${end} ========"
    if ! git cherry-pick --no-commit -X ours "${start}^..${end}"; then
        resolve_conflicts "$end"
    fi
    finalize_pick "$end" "$msg" 1
    verify_step "squash-${short}" || { echo "FAIL squash to $short"; return 1; }
    BUILD_ANCHOR_COMMIT="$(git rev-parse HEAD)"
    export BUILD_ANCHOR_COMMIT
    echo "PASS squash to $short"
}

bootstrap() {
    local boot_ref="${BOOT_REF:-41f7c6e8}"
    local scripts_ref="${SCRIPTS_REF:-$(git rev-parse HEAD)}"
    echo "======== bootstrap on $BASE (build infra from $boot_ref) ========"
    if [[ -n "$(git status --porcelain)" ]]; then
        git stash push -u -m "linear-rebase-autostash" >/dev/null
    fi
    git checkout -B "$BRANCH" "$BASE"
    git checkout "$boot_ref" -- Makefile makefile.defs makefile.targets .gitignore
    git checkout "$boot_ref" -- Release/src/SOFTWARE_FRAMEWORK/BOARDS/SDRwdgtLite/subdir.mk
    git checkout "$scripts_ref" -- scripts/
    cmd.exe //c "del \\\\?\\$(cygpath -w "$ROOT")\\NUL" 2>/dev/null || rm -f NUL 2>/dev/null || true
    git add Makefile makefile.defs makefile.targets .gitignore scripts/
    git add Release/src/SOFTWARE_FRAMEWORK/BOARDS/SDRwdgtLite/subdir.mk
    git commit -m "Bootstrap loudness build, test, and integration scripts [AI-GEN]"
    BUILD_ANCHOR_COMMIT="$(git rev-parse HEAD)"
    export BUILD_ANCHOR_COMMIT
}

import_loudness_baseline() {
    local ref="${LOUDNESS_BASELINE_REF:-48648539}"
    echo "======== import loudness baseline tree from $ref ========"
    git checkout "$ref" -- \
        src/loudness.c src/loudness.h \
        src/loudness_fast.c src/loudness_fast.h \
        src/loudness_highres.c src/loudness_highres.h \
        src/loudness_inferred_gain.c src/loudness_inferred_gain.h \
        src/loudness_internal.h \
        src/usb_statistics.c src/usb_statistics.h \
        src/usb_statistics_descriptors.c src/usb_statistics_descriptors.h \
        src/usb_stats_hid_report_descriptor.c src/usb_stats_hid_report_descriptor.h \
        tests/pc/compiler.h tests/pc/loudness_tests.c tests/pc/usb_statistics_tests.c \
        tests/pc/loudness_equalizer_step_switch_stats_tests.c tests/pc/loudness_fast_tests.c \
        tests/pc/loudness_inferred_gain_tests.c tests/pc/audio_stats_logic_tests.c \
        run-pc-tests.cmd
    bash "$ROOT/scripts/apply-loudness-integration.sh" "$ref"
    cmd.exe //c "del \\\\?\\$(cygpath -w "$ROOT")\\NUL" 2>/dev/null || rm -f NUL 2>/dev/null || true
    git add -A
    git reset HEAD -- "$LOG_DIR" 2>/dev/null || true
    git commit -m "Import loudness DSP baseline (squashed pre-ab37942c history) [AI-GEN]"
    BUILD_ANCHOR_COMMIT="$(git rev-parse HEAD)"
    export BUILD_ANCHOR_COMMIT
    verify_step "baseline-${ref:0:8}" || exit 1
}

if [[ "${RESUME:-0}" == "1" ]]; then
    echo "======== RESUME from $(git rev-parse --short HEAD) on $BRANCH ========"
    BUILD_ANCHOR_COMMIT="$(git rev-parse HEAD)"
    export BUILD_ANCHOR_COMMIT
    verify_step "resume-$(git rev-parse --short HEAD)" || exit 1
else
    bootstrap
    import_loudness_baseline
    pick_one ab37942c
fi

pick_squash f066b235 6babbe55 "Bass boost, volume-in-EQ, henryctl, and loudness flags"
pick_one db8c8598
pick_squash 538869d3 1317e9c8 "USB statistics and loudness filter refactor"
for c in 2f0ccc97 5ef63bc3 6ea97b4c f57eb2ee 216ea8c3 e51fe2bf; do
    pick_one "$c"
done

echo ""
echo "Linear rebase complete on $BRANCH at $(git rev-parse --short HEAD)"
echo "Logs in $LOG_DIR/"
