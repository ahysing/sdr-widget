#!/usr/bin/env bash
# Hybrid rebase: pr/loudnessandvolume onto audio-widget-experimental.
# - Conflicts: experimental by default; loudness chain files from the commit being applied
# - Post-pick: scripts/apply-loudness-integration.sh
# - Clean firmware build + PC tests after every commit; squash with next on failure

set -euo pipefail

UPSTREAM="${UPSTREAM:-audio-widget-experimental}"
LOUDNESS_TIP="${LOUDNESS_TIP:-origin/pr/loudnessandvolume}"
BRANCH="${BRANCH:-pr/loudnessandvolume-rebased}"
LOG_DIR="${LOG_DIR:-rebase-logs}"
STATE_FILE="${LOG_DIR}/state.txt"
START_INDEX="${START_INDEX:-0}"
RESET_BRANCH="${RESET_BRANCH:-0}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p "$LOG_DIR"
export GIT_EDITOR=true
export UPSTREAM

merge_base="$(git merge-base "$UPSTREAM" "$LOUDNESS_TIP")"
mapfile -t COMMITS < <(git log --reverse --format=%H "$merge_base..$LOUDNESS_TIP")

loudness_favor_path() {
    case "$1" in
        src/loudness*|src/usb_statistics*|src/stats_*|tests/*|run-pc-tests.cmd|henryctl/*|statistics/*|src/uac2_device_audio_task.c|src/uac2_taskAK5394A.c|src/uac2_usb_specific_request.c|src/uac2_usb_descriptors.c|src/uac2_usb_descriptors.h|Release/tests/*|Release/src/subdir.mk)
            return 0
            ;;
        Makefile|makefile.targets)
            return 0
            ;;
    esac
    return 1
}

resolve_conflicts_hybrid() {
    local commit="$1"
    while IFS= read -r path; do
        [[ -z "$path" ]] && continue
        if loudness_favor_path "$path" && git cat-file -e "$commit:$path" 2>/dev/null; then
            git checkout --theirs -- "$path" 2>/dev/null || git show "$commit:$path" > "$path"
        elif git cat-file -e "HEAD:$path" 2>/dev/null; then
            git checkout --ours -- "$path"
        else
            git rm -f -- "$path" 2>/dev/null || rm -f -- "$path"
        fi
        git add -- "$path" 2>/dev/null || true
    done < <(git diff --name-only --diff-filter=U)
}

strip_accidental_artifacts() {
    git reset HEAD -- "$LOG_DIR" rebase-run.log scripts/ 2>/dev/null || true
    rm -f rebase-run.log
}

run_clean_build() {
    local tag="$1"
    local log="$LOG_DIR/build-${tag}.log"
    {
        echo "=== Release clean ==="
        make -C Release clean
        echo "=== make audio-widget ==="
        make audio-widget
    } >"$log" 2>&1
}

run_tests() {
    local tag="$1"
    local log="$LOG_DIR/test-${tag}.log"
    if grep -q '^test:' Makefile 2>/dev/null; then
        make test >"$log" 2>&1
    else
        echo "SKIP: no make test target yet" >"$log"
        return 0
    fi
}

verify_commit() {
    local short="$1"
    run_clean_build "$short" || return 1
    run_tests "$short" || return 1
    return 0
}

squash_messages() {
    local a="$1"
    local b="$2"
    printf "%s\n\n%s" "$(git log -1 --format=%B "$a")" "$(git log -1 --format=%B "$b")"
}

in_cherry_pick() {
    local cp_head
    cp_head="$(git rev-parse --git-path CHERRY_PICK_HEAD 2>/dev/null || true)"
    [[ -n "$cp_head" && -f "$cp_head" ]]
}

pick_is_empty() {
    in_cherry_pick && git diff --cached --quiet && git diff --quiet
}

cherry_pick_commit() {
    local commit="$1"
    if git cherry-pick -X ours "$commit"; then
        return 0
    fi

    if pick_is_empty; then
        echo "Empty cherry-pick for $(git rev-parse --short "$commit"); skipping"
        GIT_EDITOR=true git cherry-pick --skip
        return 0
    fi

    echo "Conflict on $(git rev-parse --short "$commit"); hybrid resolve"
    resolve_conflicts_hybrid "$commit"
    strip_accidental_artifacts

    if pick_is_empty; then
        echo "Cherry-pick empty after hybrid resolve; skipping $(git rev-parse --short "$commit")"
        GIT_EDITOR=true git cherry-pick --skip
        return 0
    fi

    GIT_EDITOR=true git cherry-pick --continue
}

finalize_pick() {
    local commit="$1"
    bash "$ROOT/scripts/apply-loudness-integration.sh" "$commit"
    strip_accidental_artifacts
    if ! git diff --cached --quiet || ! git diff --quiet; then
        git add -A
        strip_accidental_artifacts
        git commit --amend --no-edit
    fi
}

bootstrap_build_fixes() {
    local changed=0
    if grep -q "date '+%Z'" Makefile 2>/dev/null; then
        perl -pi -e "s/date '\\+%Z'/date '\\+%z'/" Makefile
        changed=1
    fi
    if ! grep -q '"/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin"' make-widget 2>/dev/null; then
        perl -pi -e 's#for d in \\#for d in \\\n\t"/c/Program Files (x86)/Atmel/AVR Tools/AVR Toolchain/bin" \\#' make-widget
        changed=1
    fi
    if ! grep -q 'WIDGET_CONTROL_CFLAGS' Makefile 2>/dev/null; then
        perl -0777 -i -pe 's/(WIDGET_DEFAULTS \?=.*\n)/$1WIDGET_CONTROL_CFLAGS = $(filter-out $(PARTNAME),$(WIDGET_DEFAULTS)) $(WIDGET_LOUDNESS_FLAGS)\n/s' Makefile
        perl -pi -e 's/\$\(WIDGET_DEFAULTS\) \$\(CFLAGS_COMMON\)/\$(WIDGET_CONTROL_CFLAGS) \$(CFLAGS_COMMON)/' Makefile
        changed=1
    fi
    if [[ "$changed" == "1" ]]; then
        git add Makefile make-widget
        git commit -m "Bootstrap Windows/MSYS build fixes for rebase validation"
    fi
}

echo "Merge base: $merge_base"
echo "Upstream:   $UPSTREAM ($(git rev-parse --short "$UPSTREAM"))"
echo "Commits:    ${#COMMITS[@]}"

if [[ "$START_INDEX" == "0" && "$RESET_BRANCH" == "1" ]]; then
    git checkout -B "$BRANCH" "$UPSTREAM"
    bootstrap_build_fixes
elif [[ "$START_INDEX" == "0" ]]; then
    git checkout "$BRANCH"
fi

i="$START_INDEX"
while (( i < ${#COMMITS[@]} )); do
    commit="${COMMITS[$i]}"
    short="$(git rev-parse --short "$commit")"
    msg="$(git log -1 --format=%s "$commit")"
    echo ""
    echo "======== [$((i+1))/${#COMMITS[@]}] $short $msg ========"
    echo "$i" > "$STATE_FILE"

    cherry_pick_commit "$commit"
    finalize_pick "$commit"

    if verify_commit "$short"; then
        echo "PASS build+test at $short"
        ((i++)) || true
        continue
    fi

    echo "FAIL build+test at $short"
    if (( i + 1 >= ${#COMMITS[@]} )); then
        echo "No next commit to squash with; stopping."
        exit 1
    fi

    next="${COMMITS[$((i+1))]}"
    next_short="$(git rev-parse --short "$next")"
    echo "Squashing $short with $next_short and retrying"
    combined_msg="$(squash_messages "$commit" "$next")"

    git reset --soft HEAD~1
    if ! git cherry-pick --no-commit -X ours "$next"; then
        resolve_conflicts_hybrid "$next"
    fi
    bash "$ROOT/scripts/apply-loudness-integration.sh" "$next"
    strip_accidental_artifacts
    git add -A
    strip_accidental_artifacts
    git commit -m "$combined_msg"

    if verify_commit "${short}+${next_short}"; then
        echo "PASS after squash $short + $next_short"
        ((i+=2)) || true
    else
        echo "Still failing after squash $short + $next_short; stopping for manual fix."
        exit 1
    fi
done

echo ""
echo "Rebase complete on $BRANCH ($(git rev-parse --short HEAD))"
echo "Logs in $LOG_DIR/"
