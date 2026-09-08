#include "loudness.h"
#include "loudness_internal.h"
#include "loudness_fast.h"
#include "taskAK5394A.h"
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
#include "../tests/pc/device_audio_volume.h"
extern S16 spk_vol_usb_L, spk_vol_usb_R;
#else
#include "usb_specific_request.h"
#include "device_audio_task.h"
#endif
#ifndef USBSTATISTICS_DISABLE
#include "usb_statistics.h"
#include "audio_stats_logic.h"
#include "stats_telemetry.h"
#endif
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#else
#define tskIDLE_PRIORITY 0
#define xTaskGetTickCount() 0
#define vTaskDelay(x)
#define xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask) 0
#define portTICK_RATE_MS 1
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

#if defined(__GNUC__) && defined(__AVR32__)
#include "SOFTWARE_FRAMEWORK/UTILS/DEBUG/print_funcs.h"
#define LOUDNESS_PRINT(x) print_dbg(x)
#else
#define LOUDNESS_PRINT(x) printf("%s", x)
#endif

static void loudness_print_build_config(void) {
    LOUDNESS_PRINT("Audio firmware build options:\n");
#ifndef LOUDNESS_DISABLE
    LOUDNESS_PRINT("  equalizer filter: FAST\n");
#else
    LOUDNESS_PRINT("  equalizer filter: disabled\n");
#endif
    LOUDNESS_PRINT("  volume control: enabled\n");
#ifndef USBSTATISTICS_DISABLE
    LOUDNESS_PRINT("  USB statistics: enabled (HID)\n");
#else
    LOUDNESS_PRINT("  USB statistics: disabled\n");
#endif
#if !defined(LOUDNESS_DISABLE) && !defined(USBSTATISTICS_DISABLE)
    LOUDNESS_PRINT("  loudness statistics events: enabled\n");
#elif !defined(USBSTATISTICS_DISABLE)
    LOUDNESS_PRINT("  loudness statistics events: disabled\n");
#endif
}

void loudness_usb_statistics_init(void) {
#ifndef USBSTATISTICS_DISABLE
#ifdef FREERTOS_USED
    statistics_init();
#endif
#endif
}

void loudness_init(void) {
    loudness_print_build_config();
    loudness_usb_statistics_init();
    loudness_filter_init();
}

S32 saturate_24bit_s64_to_s32(S64 acc) {
    if (acc > INT24_MAX_S64) {
        return (S32)INT24_MAX;
    }
    if (acc < INT24_MIN_S64) {
        return (S32)INT24_MIN;
    }
    return (S32)acc;
}

S32 saturate_16bit_s32_to_s32(S32 acc) {
    if (acc > INT16_MAX) {
        return (S32)INT16_MAX;
    }
    if (acc < INT16_MIN) {
        return (S32)INT16_MIN;
    }
    return acc;
}

#ifndef LOUDNESS_DISABLE

#ifdef FREERTOS_USED
static Bool loudness_state_initialized = FALSE;
static Bool loudness_rtos_initialized = FALSE;
static volatile Bool loudness_task_ready = FALSE;
#endif

volatile S16 last_db_spl_left_x10 = LOUDNESS_DB_SPL_MAX * 10;
volatile S16 last_db_spl_right_x10 = LOUDNESS_DB_SPL_MAX * 10;
volatile S16 target_gain_dbfs_left_q8 = 0;
volatile S16 target_gain_dbfs_right_q8 = 0;
static volatile Bool loudness_bass_boost_enabled = FALSE;
static volatile Bool loudness_loudness_enabled = TRUE;
static volatile filter_mode_t last_filter_enabled = LOUDNESS_MODE;
static Bool loudness_external_volume_active = FALSE;

static void loudness_select_equalizer_steps(void);
static void loudness_publish_equalizer_telemetry(void);
static void loudness_update_filter_mode(void);
static void loudness_schedule_filter_update(void);

filter_mode_t loudness_active_filter()
{
    return last_filter_enabled;
}

#if defined(BUILD_TESTING)
Bool loudness_test_volume_in_biquad(void);
#endif

static void loudness_set_volume_in_biquad(Bool source_has_volume_control, Bool active_filter_enabled)
{
#ifdef FEATURE_VOLUME_CTRL
	if (active_filter_enabled && last_filter_enabled == LOUDNESS_MODE) {
		/* Volume is baked into lowshelf rows; biquad saturates internally. */
		device_audio_volume_apply_fn = keep_volume;
		return;
	}
	device_audio_set_volume_in_biquad(source_has_volume_control, active_filter_enabled);
#else
	(void)source_has_volume_control;
	(void)active_filter_enabled;
#endif
}

void loudness_refresh_volume_apply_fn(void)
{
#ifndef LOUDNESS_DISABLE
	loudness_set_volume_in_biquad(
		loudness_inferred_gain_has_source_volume_control(),
		loudness_active_filter() != FILTER_OFF_MODE);
#endif
}

static int32_t loudness_get_gain_dbfs_left_x10()
{
    int32_t gain_q8 = (int32_t)target_gain_dbfs_left_q8;
    return loudness_gain_dbfs_q8_to_x10(
        loudness_clamp_gain_dbfs_q8(gain_q8));
}

static int32_t loudness_get_gain_dbfs_right_x10()
{
    int32_t gain_q8 = (int32_t)target_gain_dbfs_right_q8;
    return loudness_gain_dbfs_q8_to_x10(
        loudness_clamp_gain_dbfs_q8(gain_q8));
}

int32_t loudness_get_db_spl_left_x10(void)
{
        return (LOUDNESS_DB_SPL_MAX * 10) + loudness_get_gain_dbfs_left_x10();
}

int32_t loudness_get_db_spl_right_x10(void)
{
    return (LOUDNESS_DB_SPL_MAX * 10) + loudness_get_gain_dbfs_right_x10();
}

static void loudness_update_filter_mode(void)
{
	if (last_filter_enabled == FILTER_OFF_MODE) {
		int32_t db_spl_left_x10 = loudness_get_db_spl_left_x10();
		int32_t db_spl_right_x10 = loudness_get_db_spl_right_x10();

		loudness_fast_select_unity_passthrough();
		loudness_set_volume_in_biquad(
			loudness_inferred_gain_has_source_volume_control(),
			FALSE);
		loudness_external_volume_active = TRUE;
		loudness_publish_equalizer_step(db_spl_left_x10, db_spl_right_x10);
		loudness_publish_equalizer_telemetry();
		return;
	}

	/*
	 * LOUDNESS_MODE <-> BASS_BOOST_MODE changes the high-shelf from an active
	 * treble contour to identity (or the reverse). Residual delay-line energy
	 * in highshelf_states[] may cause a short pop if coeffs flip without a
	 * state reset. loudness_fast_reset_states() is intentionally not called
	 * here until empirical HW listening confirms whether it is needed.
	 */

	if (loudness_external_volume_active) {
		loudness_fast_reset_states();
		loudness_external_volume_active = FALSE;
	}

	loudness_set_volume_in_biquad(
		loudness_inferred_gain_has_source_volume_control(),
		last_filter_enabled != FILTER_OFF_MODE);

	loudness_fast_refresh_quotient_table_pointers();
	loudness_publish_equalizer_telemetry();
	loudness_select_equalizer_steps();
}

#if defined(BUILD_TESTING)
Bool loudness_test_volume_in_biquad(void)
{
	return last_filter_enabled == LOUDNESS_MODE;
}
#endif

int32_t loudness_clamp_gain_dbfs_q8(int32_t gain_dbfs_q8)
{
    if (gain_dbfs_q8 > (int32_t)VOL_MAX) {
        return (int32_t)VOL_MAX;
    }
    if (gain_dbfs_q8 < (int32_t)VOL_MIN) {
        return (int32_t)VOL_MIN;
    }
    return gain_dbfs_q8;
}

int32_t loudness_clamp_gain_dbfs(int32_t gain_dbfs)
{
    if (gain_dbfs > LOUDNESS_GAIN_DBFS_MAX) {
        return LOUDNESS_GAIN_DBFS_MAX;
    }
    if (gain_dbfs < LOUDNESS_GAIN_DBFS_MIN) {
        return LOUDNESS_GAIN_DBFS_MIN;
    }
    return gain_dbfs;
}

/* Q8 (1/256 dB) -> Q1 (0.1 dB), round-half-away-from-zero. */
int32_t loudness_gain_dbfs_q8_to_x10(int32_t gain_dbfs_q8)
{
    int64_t scaled = (int64_t)gain_dbfs_q8 * 10;
    if (scaled >= 0) {
        return (int32_t)((scaled + 128) / 256);
    }
    return (int32_t)((scaled - 128) / 256);
}

#if !defined(USBSTATISTICS_DISABLE)

static void loudness_record_event_tag(U8 tag, U8 arg0, U8 arg1, U8 arg2)
{
    audio_stats_record_event(get_usb_stats(), tag, arg0, arg1, arg2);
}

static void loudness_record_equalizer_step_switch_event(
    int32_t prev_db_spl_x10, int32_t db_spl_x10,
    int prev_step, int equalizer_step)
{
    if (prev_step != equalizer_step) {
        loudness_record_event_tag(USB_STATS_TAG_EQUALIZER_STEP_SWITCH,
            (U8)((prev_db_spl_x10 + 5) / 10),
            (U8)((db_spl_x10 + 5) / 10),
            (U8)equalizer_step);
    }
}
#endif

void loudness_publish_equalizer_step(int32_t db_spl_left_x10, int32_t db_spl_right_x10)
{
    last_db_spl_left_x10 = (int16_t)db_spl_left_x10;
    last_db_spl_right_x10 = (int16_t)db_spl_right_x10;
}

void loudness_report_equalizer_step_switch(int32_t prev_db_spl_x10,
    int32_t db_spl_x10, int prev_step, int equalizer_step)
{
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl_x10,
        db_spl_x10, prev_step, equalizer_step);
#endif
}

int loudness_get_equalizer_step(int32_t db_spl_x10)
{
    return (int)((db_spl_x10 - LOUDNESS_MIN_PHON_X10)
        / LOUDNESS_EQUALIZER_STEP_X10);
}

void loudness_equalizer_steps_for_mode(
    int32_t db_spl_left_x10, int32_t db_spl_right_x10,
    int *equalizer_step_left, int *equalizer_step_right)
{
    filter_mode_t mode = loudness_active_filter();

    if (mode == BASS_BOOST_MODE) {
        *equalizer_step_left = BASS_PHON_55_IDX;
        *equalizer_step_right = BASS_PHON_55_IDX;
    } else if (mode == LOUDNESS_MODE) {
        *equalizer_step_left = loudness_get_equalizer_step(db_spl_left_x10);
        *equalizer_step_right = loudness_get_equalizer_step(db_spl_right_x10);
    } else {
        *equalizer_step_left = 0;
        *equalizer_step_right = 0;
    }
}

#ifdef BUILD_TESTING
int loudness_test_get_equalizer_step(int32_t db_spl_x10) {
    return loudness_get_equalizer_step(db_spl_x10);
}
#endif

#ifdef BUILD_TESTING
static Bool loudness_should_change_equalizer_step(int32_t db_spl_left_x10, int32_t db_spl_right_x10)
{
    int step_left = loudness_get_equalizer_step(db_spl_left_x10);
    int step_right = loudness_get_equalizer_step(db_spl_right_x10);
    int curr_step_left = loudness_get_equalizer_step((int32_t)last_db_spl_left_x10);
    int curr_step_right = loudness_get_equalizer_step((int32_t)last_db_spl_right_x10);

    return (step_left != curr_step_left) || (step_right != curr_step_right);
}

Bool loudness_test_should_change_equalizer_step(int32_t db_spl_left_x10, int32_t db_spl_right_x10) {
    return loudness_should_change_equalizer_step(db_spl_left_x10, db_spl_right_x10);
}
#endif

static void loudness_select_equalizer_steps(void)
{
    int32_t db_spl_left_x10 = loudness_get_db_spl_left_x10();
    int32_t db_spl_right_x10 = loudness_get_db_spl_right_x10();
    int equalizer_step_left;
    int equalizer_step_right;

    loudness_equalizer_steps_for_mode(
        db_spl_left_x10, db_spl_right_x10,
        &equalizer_step_left, &equalizer_step_right);

    loudness_fast_select_equalizer_steps(db_spl_left_x10, db_spl_right_x10,
        equalizer_step_left, equalizer_step_right);
}

#ifdef FREERTOS_USED
xQueueHandle xLoudnessFreqQueue = NULL;

static portTickType loudness_task_delay_20ms(void)
{
    portTickType delay =
        (portTickType)((20u * (uint32_t)configTICK_RATE_HZ + 999u) / 1000u);
    return (delay == 0) ? 1 : delay;
}

static void loudness_update_filter_by_volume_or_frequency(void *pvParameters)
{
    (void)pvParameters;

    portTickType xDelay20ms;
    uint32_t request;

    xDelay20ms = loudness_task_delay_20ms();
    loudness_task_ready = TRUE;

    while (TRUE)
    {
        if (xLoudnessFreqQueue == NULL) {
            vTaskDelay(xDelay20ms);
            continue;
        }

        if (current_freq.frequency == FREQ_44 || current_freq.frequency == FREQ_48) {
            if (xQueueReceive(xLoudnessFreqQueue, &request, xDelay20ms) == pdPASS) {
                if (request != 0) {
                    loudness_change_frequency_fast(request);
                }
            }
            loudness_update_filter_mode();
        } else {
            if (xQueueReceive(xLoudnessFreqQueue, &request, portMAX_DELAY) == pdPASS) {
                if (request != 0) {
                    loudness_change_frequency_fast(request);
                }
            }
            loudness_set_volume_in_biquad(
                loudness_inferred_gain_has_source_volume_control(),
                FALSE);
        }
    }
}

void loudness_rtos_init(void)
{
    if (loudness_rtos_initialized) {
        return;
    }

    if (xLoudnessFreqQueue == NULL) {
        xLoudnessFreqQueue = xQueueCreate(2, sizeof(uint32_t));
        if (xLoudnessFreqQueue == NULL) {
            return;
        }
    }
    if (xTaskCreate(loudness_update_filter_by_volume_or_frequency,
                    configTSK_LOUDNESS_NAME,
                    configTSK_LOUDNESS_STACK_SIZE,
                    NULL,
                    configTSK_LOUDNESS_PRIORITY,
                    NULL) != pdPASS) {
        return;
    }

    loudness_rtos_initialized = TRUE;
}

Bool loudness_rtos_is_ready(void)
{
    return loudness_rtos_initialized && (xLoudnessFreqQueue != NULL) && loudness_task_ready;
}

void loudness_request_frequency_change(uint32_t frequency)
{
    if (frequency == 0 || xLoudnessFreqQueue == NULL) {
        return;
    }

    (void)xQueueSend(xLoudnessFreqQueue, &frequency, 0);
}

static void loudness_request_filter_apply(void)
{
    uint32_t wake = 0;

    if (xLoudnessFreqQueue == NULL) {
        return;
    }

    if (xQueueSend(xLoudnessFreqQueue, &wake, 0) != pdPASS) {
        loudness_update_filter_mode();
    }
}
#else /* FREERTOS_USED */

void loudness_rtos_init(void)
{
}

Bool loudness_rtos_is_ready(void)
{
    return FALSE;
}

void loudness_request_frequency_change(uint32_t frequency)
{
    if (frequency != 0) {
        loudness_change_frequency_fast(frequency);
    }
}

#endif /* FREERTOS_USED */

static void loudness_publish_equalizer_telemetry(void)
{
#if !defined(USBSTATISTICS_DISABLE)
    int32_t db_spl_left_x10 = loudness_get_db_spl_left_x10();
    int32_t db_spl_right_x10 = loudness_get_db_spl_right_x10();
    int32_t gain_dbfs_left_x10 = loudness_get_gain_dbfs_left_x10();
    int32_t gain_dbfs_right_x10 = loudness_get_gain_dbfs_right_x10();
    int equalizer_step_left;
    int equalizer_step_right;

    loudness_equalizer_steps_for_mode(
        db_spl_left_x10, db_spl_right_x10,
        &equalizer_step_left, &equalizer_step_right);
    stats_telemetry_set_gain_dbfs_stereo(
        (S16)gain_dbfs_left_x10,
        (S16)gain_dbfs_right_x10);
    stats_telemetry_set_source_has_volume_control(
        loudness_inferred_gain_has_source_volume_control() ? 1u : 0u);
    stats_telemetry_set_equalizer_state_stereo(
        (S16)db_spl_left_x10,
        (U8)equalizer_step_left,
        (S16)db_spl_right_x10,
        (U8)equalizer_step_right);
#endif
}

void loudness_update_active_equalizer_step(void)
{
    loudness_update_filter_mode();
}

#if !defined(USBSTATISTICS_DISABLE)
static void stats_emit_filter_mode(filter_mode_t mode)
{
    stats_telemetry_set_loudness_enabled(mode == LOUDNESS_MODE ? 1u : 0u);
    stats_telemetry_set_bass_boost_enabled(mode == BASS_BOOST_MODE ? 1u : 0u);
}
#endif

static void loudness_schedule_filter_update(void)
{
    if (loudness_rtos_is_ready()) {
#ifdef FREERTOS_USED
        loudness_request_filter_apply();
#endif
    } else {
        loudness_update_filter_mode();
    }
}

static filter_mode_t loudness_fallback_from_bass_boost(void)
{
    return loudness_loudness_enabled ? LOUDNESS_MODE : FILTER_OFF_MODE;
}

static filter_mode_t loudness_fallback_from_loudness(void)
{
    return loudness_bass_boost_enabled ? BASS_BOOST_MODE : FILTER_OFF_MODE;
}

static void loudness_apply_filter_flag(
    volatile Bool *flag, Bool enabled, filter_mode_t active_mode,
    filter_mode_t (*fallback)(void))
{
    *flag = enabled;
    if (enabled) {
        last_filter_enabled = active_mode;
    } else if (last_filter_enabled == active_mode) {
        last_filter_enabled = fallback();
    }
#if !defined(USBSTATISTICS_DISABLE)
    stats_emit_filter_mode(last_filter_enabled);
#endif
    loudness_schedule_filter_update();
}

void loudness_bass_boost_set(Bool enabled)
{
    loudness_apply_filter_flag(
        &loudness_bass_boost_enabled, enabled,
        BASS_BOOST_MODE, loudness_fallback_from_bass_boost);
}

Bool loudness_bass_boost_is_enabled(void)
{
    return loudness_bass_boost_enabled;
}

void loudness_loudness_set(Bool enabled)
{
    loudness_apply_filter_flag(
        &loudness_loudness_enabled, enabled,
        LOUDNESS_MODE, loudness_fallback_from_loudness);
}

Bool loudness_loudness_is_enabled(void)
{
    return loudness_loudness_enabled;
}

static void loudness_usb_volume_changed_channel(int channel, S16 volume_q8)
{
    int32_t gain_q8 = loudness_clamp_gain_dbfs_q8(
        (int32_t)volume_q8 - (int32_t)VOL_MAX);

    if (channel == 0) {
        spk_vol_usb_L = volume_q8;
        target_gain_dbfs_left_q8 = (int16_t)gain_q8;
    } else {
        spk_vol_usb_R = volume_q8;
        target_gain_dbfs_right_q8 = (int16_t)gain_q8;
    }
    loudness_set_source_has_volume_control();

#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_source_has_volume_control(1u);
#endif

    if (loudness_rtos_is_ready()) {
        loudness_publish_equalizer_telemetry();
#ifdef FREERTOS_USED
        loudness_request_filter_apply();
#endif
        return;
    }

    loudness_update_filter_mode();
}

void loudness_usb_volume_changed_left(S16 volume_q8)
{
    loudness_usb_volume_changed_channel(0, volume_q8);
}

void loudness_usb_volume_changed_right(S16 volume_q8)
{
    loudness_usb_volume_changed_channel(1, volume_q8);
}

int32_t loudness_get_gain_dbfs_left() {
    int32_t gain_dbfs_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t gain_dbfs = gain_dbfs_q8 / 256;
    if (gain_dbfs > 0) {
        return 0;
    }
    return gain_dbfs;
}

int32_t loudness_get_gain_dbfs_right() {
    int32_t gain_dbfs_q8 = (int32_t)spk_vol_usb_R - (int32_t)VOL_MAX;
    int32_t gain_dbfs = gain_dbfs_q8 / 256;
    if (gain_dbfs > 0) {
        return 0;
    }
    return gain_dbfs;
}

int16_t loudness_get_last_db_spl_left_x10(void) {
    return last_db_spl_left_x10;
}

int16_t loudness_get_last_db_spl_right_x10(void) {
    return last_db_spl_right_x10;
}

void loudness_set_level_dbfs(int32_t db_fs) {
    int32_t gain_q8;

    db_fs = loudness_clamp_gain_dbfs(db_fs);
    gain_q8 = loudness_clamp_gain_dbfs_q8(db_fs * 256);

    target_gain_dbfs_left_q8 = (int16_t)gain_q8;
    target_gain_dbfs_right_q8 = (int16_t)gain_q8;

    loudness_update_filter_mode();
}

void loudness_filter_init(void) {
#ifdef FREERTOS_USED
    if (loudness_state_initialized) {
        return;
    }
#endif

    loudness_fast_reset_states();
    target_gain_dbfs_left_q8 = (S16)loudness_clamp_gain_dbfs_q8(
        (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX);
    target_gain_dbfs_right_q8 = (S16)loudness_clamp_gain_dbfs_q8(
        (int32_t)spk_vol_usb_R - (int32_t)VOL_MAX);

    if (current_freq.frequency != 0) {
        loudness_change_frequency_fast(current_freq.frequency);
    }
    loudness_update_filter_mode();
#if !defined(USBSTATISTICS_DISABLE)
    stats_emit_filter_mode(last_filter_enabled);
#endif

#ifdef FREERTOS_USED
    loudness_state_initialized = TRUE;
#endif
}

#endif /* LOUDNESS_DISABLE */

#ifndef LOUDNESS_DISABLE
Bool loudness_uac2_packet_filter_enabled(Bool not_muted, uint32_t freq_hz)
{
    return not_muted
        && (freq_hz == (uint32_t)FREQ_44 || freq_hz == (uint32_t)FREQ_48)
        && loudness_active_filter() != FILTER_OFF_MODE;
}
#endif

#ifdef LOUDNESS_DISABLE

void loudness_filter_init(void) {}

void loudness_bass_boost_set(Bool enabled) {
    (void)enabled;
}

Bool loudness_bass_boost_is_enabled(void) {
    return TRUE;
}

void loudness_set_level_dbfs(int32_t db_fs) {
    (void)db_fs;
}

void loudness_update_active_equalizer_step(void) {}

int16_t loudness_get_last_db_spl_left_x10(void) {
    return LOUDNESS_DB_SPL_MAX * 10 + loudness_get_gain_dbfs_left();
}

int16_t loudness_get_last_db_spl_right_x10(void) {
    return LOUDNESS_DB_SPL_MAX * 10 + loudness_get_gain_dbfs_right();
}

#ifdef FREERTOS_USED
void loudness_rtos_init(void) {}

Bool loudness_rtos_is_ready(void) {
    return TRUE;
}

void loudness_request_frequency_change(uint32_t frequency) {
    (void)frequency;
}
#endif

#endif /* LOUDNESS_DISABLE */
