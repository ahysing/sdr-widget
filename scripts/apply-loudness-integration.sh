#!/usr/bin/env bash
# Apply hybrid integration after each loudness cherry-pick onto audio-widget-experimental.
# Experimental wins for mobo/platform; loudness wins for DSP/USB loudness chain.

set -euo pipefail

UPSTREAM="${UPSTREAM:-audio-widget-experimental}"
COMMIT="${1:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

loudness_paths_from_commit() {
    local commit="$1"
    git diff-tree --no-commit-id --name-only -r "$commit" 2>/dev/null | while read -r p; do
        case "$p" in
            src/loudness*|src/usb_statistics*|src/stats_*|tests/*|run-pc-tests.cmd|henryctl/*|statistics/*|src/uac2_device_audio_task.c|src/uac2_taskAK5394A.c|src/uac2_usb_specific_request.c|Makefile|makefile.targets|Release/src/subdir.mk|Release/tests/*)
                echo "$p"
                ;;
        esac
    done
}

checkout_if_in_commit() {
    local commit="$1"
    local path="$2"
    if git cat-file -e "$commit:$path" 2>/dev/null; then
        git show "$commit:$path" > "$path"
        git add -- "$path"
    fi
}

apply_descriptor_overlay() {
    local commit="$1"
    local marker="${DESCRIPTOR_OVERLAY_FROM:-ab37942c}"
    if [[ -z "$commit" ]] || ! git merge-base --is-ancestor "$marker" "$commit" 2>/dev/null; then
        return 0
    fi
    for f in src/uac2_usb_descriptors.h src/uac2_usb_descriptors.c; do
        checkout_if_in_commit "$commit" "$f"
    done
}

restore_build_anchors() {
    local base="${BUILD_ANCHOR_COMMIT:-HEAD}"
    for f in Makefile makefile.defs makefile.targets Release/src/subdir.mk; do
        if git cat-file -e "$base:$f" 2>/dev/null; then
            git checkout "$base" -- "$f"
        fi
    done
    if grep -q -- '-mpart=uc3a3256' makefile.defs 2>/dev/null; then
        perl -pi -e 's/\s+-mpart=uc3a3256//g' makefile.defs
    fi
}

remove_nul_artifact() {
    cmd.exe //c "del \\\\?\\$(cygpath -w "$ROOT")\\NUL" 2>/dev/null || rm -f NUL 2>/dev/null || true
}

experimental_keep=(
    src/uac2_image.c
    src/taskMoboCtrl.c
    src/archive_uac1
    Release/src/SOFTWARE_FRAMEWORK/BOARDS/SDRwdgtLite/subdir.mk
    makefile.defs
    makefile.targets
)

for p in "${experimental_keep[@]}"; do
    if git cat-file -e "$UPSTREAM:$p" 2>/dev/null; then
        git checkout "$UPSTREAM" -- "$p" 2>/dev/null || true
    fi
done

if [[ -n "$COMMIT" ]]; then
    while IFS= read -r p; do
        [[ -z "$p" ]] && continue
        checkout_if_in_commit "$COMMIT" "$p"
    done < <(loudness_paths_from_commit "$COMMIT" | sort -u)

    apply_descriptor_overlay "$COMMIT"
fi

perl -0777 -i -pe '
    s/void mobo_clear_adc_channel\(void\) \{\s*int i;.*?^}/void mobo_clear_adc_channel(void) {\n\tmemset((void *)audio_buffer_0, 0, sizeof(audio_buffer_0));\n\tmemset((void *)audio_buffer_1, 0, sizeof(audio_buffer_1));\n}/ms if $ARGV eq "src/Mobo_config.c";
' src/Mobo_config.c 2>/dev/null || true

perl -0777 -i -pe '
    s/void mobo_clear_dac_channel\(void\) \{\s*int i;.*?^}/void mobo_clear_dac_channel(void) {\n\tmemset((void *)spk_buffer_0, 0, sizeof(spk_buffer_0));\n\tmemset((void *)spk_buffer_1, 0, sizeof(spk_buffer_1));\n}/ms if $ARGV eq "src/Mobo_config.c";
' src/Mobo_config.c 2>/dev/null || true

if ! grep -q '#include <string.h>' src/Mobo_config.c; then
    perl -i -pe 'if ($.==14 && $_ =~ /avr32\/io.h/) { $_ .= "#include <string.h>\n" }' src/Mobo_config.c
fi

perl -pi -e 's/^\s*must_init_spk_index = TRUE;/\t\/\/ must_init_spk_index = TRUE;/' src/composite_widget.c

perl -pi -e 's/spk_current_freq/current_freq/g' src/device_mouse_hid_task.c

if ! grep -q 'FEATURE_ADC_NONE' src/features.h; then
    perl -0777 -i -pe 's/(#define FEATURE_OUT_SWAPPED.*\n)/$1\n#define FEATURE_ADC_NONE\t\t\t\t(features[feature_adc_index] == (uint8_t)feature_adc_none)\n#define FEATURE_ADC_AK5394A\t\t\t\t(features[feature_adc_index] == (uint8_t)feature_adc_ak5394a)\n/s' src/features.h
fi

if ! grep -q 'FEATURE_HDEAD_ON' src/features.h; then
    perl -0777 -i -pe 's/(#define FEATURE_NOSKIP_OFF.*\n)/$1#define FEATURE_HSTUPID_ON\t\t\t\t(features[feature_quirk_index] == (uint8_t)feature_quirk_fb_Hstupid)\n#define FEATURE_HSTUPID_OFF\t\t\t\t(features[feature_quirk_index] != (uint8_t)feature_quirk_fb_Hstupid)\n#define FEATURE_HDEAD_ON\t\t\t\t(features[feature_quirk_index] == (uint8_t)feature_quirk_fb_Hdead)\n#define FEATURE_HDEAD_OFF\t\t\t\t(features[feature_quirk_index] != (uint8_t)feature_quirk_fb_Hdead)\n/s' src/features.h
fi

if ! grep -q 'mutexSpkUSB' src/taskAK5394A.h; then
    perl -0777 -i -pe 's/(extern volatile U32 spk_usb_heart_beat.*\n)/$1extern volatile U32 spk_usb_sample_counter;\nextern xSemaphoreHandle mutexSpkUSB;\n/s' src/taskAK5394A.h
fi

if ! grep -q 'xSemaphoreHandle mutexSpkUSB' src/taskAK5394A.c; then
    perl -0777 -i -pe 's/(volatile U32 spk_usb_heart_beat = 0.*\n)/$1volatile U32 spk_usb_sample_counter = 0;\nxSemaphoreHandle mutexSpkUSB;\n/s' src/taskAK5394A.c
    perl -0777 -i -pe 's/(void AK5394A_task_init\(const Bool uac1\) \{\n)/$1\tmutexSpkUSB = xSemaphoreCreateMutex();\n/s' src/taskAK5394A.c
fi

if ! grep -q 'Mic_freq_valid' src/uac2_usb_specific_request.h; then
    perl -0777 -i -pe 's/(extern Bool uac2_user_read_request.*\n)/$1extern Bool Mic_freq_valid;\n/s' src/uac2_usb_specific_request.h
fi

sync_subdir_entry() {
    local srcfile="$1"
    local base
    base="$(basename "$srcfile" .c)"
    if [[ ! -f "$srcfile" ]]; then
        return 0
    fi
    if ! grep -q "../$srcfile" Release/src/subdir.mk; then
        perl -pi -e 's/\r$//' Release/src/subdir.mk
        perl -0777 -i -pe "s|(C_SRCS \+= \\\r?\n)|\$1../$srcfile \\\n|" Release/src/subdir.mk
        perl -0777 -i -pe "s|(OBJS \+= \\\r?\n)|\$1./src/${base}.o \\\n|" Release/src/subdir.mk
        perl -0777 -i -pe "s|(C_DEPS \+= \\\r?\n)|\$1./src/${base}.d \\\n|" Release/src/subdir.mk
    fi
}

restore_build_anchors

for f in src/loudness.c src/loudness_fast.c src/loudness_first_order.c src/loudness_inferred_gain.c src/loudness_internal.c src/usb_statistics.c src/usb_statistics_descriptors.c src/stats_telemetry.c src/usb_fifo_hw_lock.c src/usb_stats_hid_report_descriptor.c; do
    sync_subdir_entry "$f"
done

if ! grep -q 'WIDGET_CONTROL_CFLAGS' Makefile 2>/dev/null; then
    perl -0777 -i -pe 's/(WIDGET_DEFAULTS \?=.*\n)/$1# widget-control\/henryctl are native host builds: feature flags only, never AVR32 -mpart.\nWIDGET_CONTROL_CFLAGS = \$(AUDIO_WIDGET_FEATURE_FLAGS) \$(BUILD_INFO_DEFS) \$(WIDGET_LOUDNESS_FLAGS)\n/s' Makefile
    perl -pi -e 's/\$\(WIDGET_DEFAULTS\) \$\(CFLAGS_COMMON\)/\$(WIDGET_CONTROL_CFLAGS) \$(CFLAGS_COMMON)/' Makefile
fi

remove_nul_artifact
git add -A
remove_nul_artifact
