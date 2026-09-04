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

S32 saturate_24bit_s32_to_s32(S32 acc) {
    if (acc > INT24_MAX) {
        return (S32)INT24_MAX;
    }
    if (acc < INT24_MIN) {
        return (S32)INT24_MIN;
    }
    return acc;
}

U32 saturate_24bit_s32_to_u32(S32 acc) {
    return (U32)saturate_24bit_s32_to_s32(acc);
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

static Bool loudness_filter_contour_is_enabled(void)
{
    return last_filter_enabled != FILTER_OFF_MODE;
}

filter_mode_t loudness_active_filter()
{
    return last_filter_enabled;
}

#if defined(BUILD_TESTING)
Bool loudness_test_volume_in_biquad(void);
#endif

static void loudness_set_volume_in_biquad(Bool volume_in_biquad)
{
#ifdef FEATURE_VOLUME_CTRL
	device_audio_set_volume_in_biquad(volume_in_biquad);
#else
	(void)volume_in_biquad;
#endif
}

static void loudness_update_filter_mode(void)
{
	if (!loudness_filter_contour_is_enabled()) {
		int32_t db_spl_left_x10;
		int32_t db_spl_right_x10;

		loudness_fast_select_unity_passthrough();
		loudness_set_volume_in_biquad(FALSE);
		loudness_external_volume_active = TRUE;
		loudness_internal_current_stereo_db_spl_x10(
			&db_spl_left_x10, &db_spl_right_x10);
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

	if (last_filter_enabled == LOUDNESS_MODE) {
		loudness_set_volume_in_biquad(TRUE);
	} else if (last_filter_enabled == BASS_BOOST_MODE) {
		loudness_set_volume_in_biquad(FALSE);
	}

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

int32_t loudness_usb_volume_q8_to_gain_dbfs(S16 volume_q8)
{
    int32_t gain_q8 = loudness_clamp_gain_dbfs_q8(
        (int32_t)volume_q8 - (int32_t)VOL_MAX);
    return loudness_clamp_gain_dbfs(gain_q8 / 256);
}

static S16 loudness_target_gain_dbfs_q8(int channel)
{
    return (channel == 1)
        ? target_gain_dbfs_right_q8
        : target_gain_dbfs_left_q8;
}

static int32_t loudness_get_gain_dbfs_x10_for_channel(int channel)
{
    int32_t gain_q8 = (int32_t)loudness_target_gain_dbfs_q8(channel);
    return loudness_gain_dbfs_q8_to_x10(
        loudness_clamp_gain_dbfs_q8(gain_q8));
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

static int8_t loudness_clamp_s8(int32_t value)
{
    if (value < -128) {
        return (int8_t)-128;
    }
    if (value > 127) {
        return (int8_t)127;
    }
    return (int8_t)value;
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

static int32_t loudness_get_db_spl_x10_for_channel(int channel)
{
    return (LOUDNESS_DB_SPL_MAX * 10)
        + loudness_get_gain_dbfs_x10_for_channel(channel);
}

void loudness_internal_current_stereo_db_spl_x10(
    int32_t *db_spl_left_x10, int32_t *db_spl_right_x10)
{
    *db_spl_left_x10 = loudness_get_db_spl_x10_for_channel(0);
    *db_spl_right_x10 = loudness_get_db_spl_x10_for_channel(1);
}

int loudness_get_equalizer_step(int32_t db_spl_x10)
{
    return (int)((db_spl_x10 - LOUDNESS_MIN_PHON_X10)
        / LOUDNESS_EQUALIZER_STEP_X10);
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
    int32_t db_spl_left_x10 = 0;
    int32_t db_spl_right_x10 = 0;
    int equalizer_step_left = 0;
    int equalizer_step_right = 0;

    loudness_internal_current_stereo_db_spl_x10(
        &db_spl_left_x10, &db_spl_right_x10);

    switch (last_filter_enabled)
    {
        case BASS_BOOST_MODE:
            equalizer_step_left = BASSS_PHON_55_IDX;
            equalizer_step_right = BASSS_PHON_55_IDX;
            break;
        case LOUDNESS_MODE:
            equalizer_step_left = loudness_get_equalizer_step(db_spl_left_x10);
            equalizer_step_right = loudness_get_equalizer_step(db_spl_right_x10);
            break;
        default:
            break;
    }

    loudness_fast_select_equalizer_steps(db_spl_left_x10, db_spl_right_x10,
        equalizer_step_left, equalizer_step_right);
}

#ifdef FREERTOS_USED
xQueueHandle xLoudnessFreqQueue = NULL;

typedef enum {
    LOUDNESS_REQUEST_FREQUENCY,
    LOUDNESS_REQUEST_VOLUME
} loudness_request_type_t;

typedef struct {
    loudness_request_type_t type;
    uint32_t value;
} loudness_request_t;

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
    loudness_request_t request;

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
                if (request.type == LOUDNESS_REQUEST_FREQUENCY) {
                    if (request.value != 0) {
                        loudness_change_frequency(request.value);
                    }
                }
            }
            loudness_update_filter_mode();
        } else {
            if (xQueueReceive(xLoudnessFreqQueue, &request, portMAX_DELAY) == pdPASS) {
                if (request.type == LOUDNESS_REQUEST_FREQUENCY) {
                    if (request.value != 0) {
                        loudness_change_frequency(request.value);
                    }
                }
            }
            loudness_set_volume_in_biquad(FALSE);
        }
    }
}

void loudness_rtos_init(void)
{
    if (loudness_rtos_initialized) {
        return;
    }

    if (xLoudnessFreqQueue == NULL) {
        xLoudnessFreqQueue = xQueueCreate(2, sizeof(loudness_request_t));
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
    loudness_request_t request;

    if (frequency == 0 || xLoudnessFreqQueue == NULL) {
        return;
    }

    request.type = LOUDNESS_REQUEST_FREQUENCY;
    request.value = frequency;
    (void)xQueueSend(xLoudnessFreqQueue, &request, 0);
}

static void loudness_request_volume_apply(void)
{
    loudness_request_t request;

    if (xLoudnessFreqQueue == NULL) {
        return;
    }

    request.type = LOUDNESS_REQUEST_VOLUME;
    request.value = 0;
    if (xQueueSend(xLoudnessFreqQueue, &request, 0) != pdPASS) {
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
        loudness_change_frequency(frequency);
    }
}

#endif /* FREERTOS_USED */

static void loudness_calculate_db_spl_stereo_x10(
    int32_t *db_spl_left_x10, int32_t *db_spl_right_x10)
{
    loudness_internal_current_stereo_db_spl_x10(
        db_spl_left_x10, db_spl_right_x10);
}

static void loudness_publish_equalizer_telemetry(void)
{
#if !defined(USBSTATISTICS_DISABLE)
    int32_t db_spl_left_x10;
    int32_t db_spl_right_x10;
    int32_t db_spl_left;
    int32_t db_spl_right;
    U8 equalizer_step_left;
    U8 equalizer_step_right;

    loudness_calculate_db_spl_stereo_x10(
        &db_spl_left_x10, &db_spl_right_x10);
    db_spl_left = (db_spl_left_x10 + 5) / 10;
    db_spl_right = (db_spl_right_x10 + 5) / 10;
    if (last_filter_enabled == BASS_BOOST_MODE) {
        equalizer_step_left = (U8)BASSS_PHON_55_IDX;
        equalizer_step_right = (U8)BASSS_PHON_55_IDX;
    } else {
        equalizer_step_left = (U8)loudness_get_equalizer_step(db_spl_left_x10);
        equalizer_step_right = (U8)loudness_get_equalizer_step(db_spl_right_x10);
    }
    stats_telemetry_set_gain_dbfs_stereo(
        loudness_clamp_s8(db_spl_left - LOUDNESS_DB_SPL_MAX),
        loudness_clamp_s8(db_spl_right - LOUDNESS_DB_SPL_MAX));
    stats_telemetry_set_source_has_volume_control(
        loudness_inferred_gain_has_source_volume_control() ? 1u : 0u);
    stats_telemetry_set_equalizer_state_stereo(
        loudness_clamp_s8(db_spl_left),
        equalizer_step_left,
        loudness_clamp_s8(db_spl_right),
        equalizer_step_right);
#endif
}

void loudness_apply_equalizer_step_if_needed(void)
{
    loudness_update_filter_mode();
}

void loudness_update_active_equalizer_step(void)
{
    loudness_update_filter_mode();
}
static void stats_emit_bass_boost()
{
#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_loudness_enabled(0u);
    stats_telemetry_set_bass_boost_enabled(1u);
#endif
}

static void stats_emit_loudness()
{
#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_loudness_enabled(1u);
    stats_telemetry_set_bass_boost_enabled(0u);
#endif
}

static void stats_emit_filter_off()
{
#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_loudness_enabled(0u);
    stats_telemetry_set_bass_boost_enabled(0u);
#endif
}

void loudness_bass_boost_set(Bool enabled)
{
    loudness_bass_boost_enabled = enabled;
    if (enabled) {
        last_filter_enabled = BASS_BOOST_MODE;
        stats_emit_bass_boost();
    } else if (last_filter_enabled == BASS_BOOST_MODE) {
        if (loudness_loudness_enabled) {
            last_filter_enabled = LOUDNESS_MODE;
            stats_emit_loudness();
        } else {
            last_filter_enabled = FILTER_OFF_MODE;
            stats_emit_filter_off();
        }
    }

    if (loudness_rtos_is_ready()) {
#ifdef FREERTOS_USED
        loudness_request_volume_apply();
#endif
        return;
    }
    loudness_update_filter_mode();
}

Bool loudness_bass_boost_is_enabled(void)
{
    return loudness_bass_boost_enabled;
}

void loudness_loudness_set(Bool enabled)
{
    loudness_loudness_enabled = enabled;
    if (enabled) {
        last_filter_enabled = LOUDNESS_MODE;
        stats_emit_loudness();
    } else if (last_filter_enabled == LOUDNESS_MODE) {
        if (loudness_bass_boost_enabled) {
            last_filter_enabled = BASS_BOOST_MODE;
            stats_emit_bass_boost();
        } else {
            last_filter_enabled = FILTER_OFF_MODE;
            stats_emit_filter_off();
        }
    }

    if (loudness_rtos_is_ready()) {
#ifdef FREERTOS_USED
        loudness_request_volume_apply();
#endif
        return;
    }
    loudness_update_filter_mode();
}

Bool loudness_loudness_is_enabled(void)
{
    return loudness_loudness_enabled;
}

void loudness_usb_volume_changed_left(S16 volume_q8)
{
    int32_t gain_q8 = loudness_clamp_gain_dbfs_q8(
        (int32_t)volume_q8 - (int32_t)VOL_MAX);

    target_gain_dbfs_left_q8 = (int16_t)gain_q8;
    loudness_set_source_has_volume_control();

#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_source_has_volume_control(1u);
#endif

    if (loudness_rtos_is_ready()) {
        loudness_publish_equalizer_telemetry();
#ifdef FREERTOS_USED
        loudness_request_volume_apply();
#endif
        return;
    }

    loudness_update_active_equalizer_step();
}

void loudness_usb_volume_changed_right(S16 volume_q8)
{
    int32_t gain_q8 = loudness_clamp_gain_dbfs_q8(
        (int32_t)volume_q8 - (int32_t)VOL_MAX);

    target_gain_dbfs_right_q8 = (int16_t)gain_q8;
    loudness_set_source_has_volume_control();

    if (loudness_rtos_is_ready()) {
        loudness_publish_equalizer_telemetry();
#ifdef FREERTOS_USED
        loudness_request_volume_apply();
#endif
        return;
    }
    loudness_update_active_equalizer_step();
}

int32_t loudness_get_gain_dbfs_channel(int channel)
{
    int32_t gain_dbfs_q8 = (int32_t)loudness_target_gain_dbfs_q8(channel);
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

    loudness_update_active_equalizer_step();
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
        loudness_change_frequency(current_freq.frequency);
    }
    loudness_update_filter_mode();
    stats_emit_loudness();

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

int32_t loudness_apply_noise_shaper_to_output(int32_t sample_32bit, int32_t* noise_shaper_error) {
    int32_t error = *noise_shaper_error;
    int64_t accumulated_sample = (int64_t)sample_32bit - (int64_t)error;
    int32_t sample_24bit = DOWNSAMPLE_24BIT_ROUND(accumulated_sample);
    int32_t reconstructed_32bit = sample_24bit << 8;
    *noise_shaper_error = reconstructed_32bit - sample_32bit;
    if (sample_24bit > INT24_MAX) {
        return (int32_t)INT24_MAX;
    }
    if (sample_24bit < INT24_MIN) {
        return (int32_t)INT24_MIN;
    }
    return sample_24bit;
}

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
    return LOUDNESS_DB_SPL_MAX * 10;
}

int16_t loudness_get_last_db_spl_right_x10(void) {
    return LOUDNESS_DB_SPL_MAX * 10;
}

int32_t loudness_get_gain_dbfs_channel(int channel) {
    int32_t gain_dbfs_q8 = (channel == 1)
        ? (int32_t)spk_vol_usb_R - (int32_t)VOL_MAX
        : (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t gain_dbfs = gain_dbfs_q8 / 256;
    if (gain_dbfs > 0) {
        return 0;
    }
    return gain_dbfs;
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
