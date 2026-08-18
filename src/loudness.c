#include "loudness.h"
#include "track_dbfs.h"
#include "loudness_internal.h"
#ifdef PRECISE
#include "loudness_precise.h"
#endif
#ifdef FAST
#include "loudness_fast.h"
#endif
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
extern S16 spk_vol_usb_L, spk_vol_usb_R;
#else
#include "usb_specific_request.h"
#include "device_audio_task.h"
#include "taskAK5394A.h"
#endif
#ifndef LOUDNESS_DISABLE
#include "loudness_inferred_gain.h"
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

/* Weighting for blending gain vs. measured track loudness when
 * selecting the equalizer step. Gain (host volume) dominates; the
 * running RMS provides a small correction. Weights in percent,
 * must sum to 100. */
#define LOUDNESS_GAIN_WEIGHT_PCT   90
#define LOUDNESS_TRACK_WEIGHT_PCT  10

static void loudness_print_build_config(void) {
    LOUDNESS_PRINT("Audio firmware build options:\n");
#ifndef LOUDNESS_DISABLE
#ifdef FAST
    LOUDNESS_PRINT("  equalizer filter: FAST\n");
#else
    LOUDNESS_PRINT("  equalizer filter: PRECISE\n");
#endif
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

volatile S16 last_db_spl = LOUDNESS_DB_SPL_MAX;
#ifdef FREERTOS_USED
volatile S16 target_db_spl = LOUDNESS_DB_SPL_MAX;
#endif
volatile S16 target_gain_dbfs_q8 = 0;

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

static int32_t loudness_get_gain_dbfs_x10(void)
{
    if (loudness_inferred_gain_has_source_volume_control()) {
        return loudness_gain_dbfs_q8_to_x10(
            loudness_clamp_gain_dbfs_q8((int32_t)target_gain_dbfs_q8));
    }
    return loudness_get_gain_dbfs() * 10;
}

#if !defined(USBSTATISTICS_DISABLE)

static void loudness_record_event_tag(U8 tag, U8 arg0, U8 arg1, U8 arg2)
{
    audio_stats_record_event(get_usb_stats(), tag, arg0, arg1, arg2);
}

static void loudness_record_equalizer_step_switch_event(int32_t prev_db_spl, int32_t db_spl,
    int prev_step, int equalizer_step)
{
    if (prev_step != equalizer_step) {
        loudness_record_event_tag(USB_STATS_TAG_EQUALIZER_STEP_SWITCH,
            (U8)prev_db_spl, (U8)db_spl, (U8)equalizer_step);
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

void loudness_publish_equalizer_step(int32_t db_spl)
{
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
}

void loudness_report_equalizer_step_switch(int32_t prev_db_spl, int32_t db_spl,
    int prev_step, int equalizer_step)
{
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, prev_step, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}

int32_t loudness_internal_current_db_spl(void)
{
#ifdef FREERTOS_USED
    return (int32_t)target_db_spl;
#else
    return (int32_t)last_db_spl;
#endif
}

int loudness_get_equalizer_step(int32_t db_spl)
{
    if (db_spl >= LOUDNESS_REF_PHON) {
        return LOUDNESS_NEUTRAL_STEP;
    }
    if (db_spl < LOUDNESS_MIN_PHON) {
        return 0;
    }
    {
        int step = (db_spl - LOUDNESS_MIN_PHON) / LOUDNESS_PHON_STEP_DB;
        if (step >= LOUDNESS_CONTOUR_STEPS) {
            step = LOUDNESS_CONTOUR_STEPS - 1;
        }
        return step;
    }
}

#ifdef BUILD_TESTING
int loudness_test_get_equalizer_step(int32_t db_spl) {
    return loudness_get_equalizer_step(db_spl);
}
#endif

static int loudness_get_current_equalizer_step(void) {
    return loudness_get_equalizer_step((int32_t)last_db_spl);
}

static Bool loudness_should_change_equalizer_step(int32_t db_spl_x10) {
    int current = loudness_get_current_equalizer_step();

    if (current == LOUDNESS_NEUTRAL_STEP) {
        return db_spl_x10 < (LOUDNESS_REF_PHON * 10 - 5);
    }

    if (current == (LOUDNESS_CONTOUR_STEPS - 1)) {
        if (db_spl_x10 >= (LOUDNESS_REF_PHON * 10)) {
            return TRUE;
        }
        {
            int band_start = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
            int32_t lower_x10 = band_start * 10 - 5;
            return db_spl_x10 < lower_x10;
        }
    }

    {
        int band_start = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
        int32_t lower_x10 = band_start * 10 - 5;
        int next_band = band_start + LOUDNESS_PHON_STEP_DB;
        int32_t upper_x10 = next_band * 10;

        if (db_spl_x10 >= upper_x10) {
            return TRUE;
        }
        if (db_spl_x10 < lower_x10) {
            return TRUE;
        }
        return FALSE;
    }
}

#ifdef BUILD_TESTING
Bool loudness_test_should_change_equalizer_step(int32_t db_spl_x10) {
    return loudness_should_change_equalizer_step(db_spl_x10);
}
#endif

static void loudness_select_equalizer_step(int32_t db_spl) {
    int equalizer_step = loudness_get_equalizer_step(db_spl);
#ifdef PRECISE
    loudness_precise_select_equalizer_step(db_spl, equalizer_step);
#endif
#ifdef FAST
    loudness_fast_select_equalizer_step(db_spl, equalizer_step);
#endif
}

#ifdef FREERTOS_USED
xQueueHandle xLoudnessFreqQueue = NULL;

typedef enum {
    LOUDNESS_REQUEST_FREQUENCY
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
                    loudness_change_frequency(request.value);
                }
            }
            loudness_update_active_equalizer_step();
        } else {
            if (xQueueReceive(xLoudnessFreqQueue, &request, portMAX_DELAY) == pdPASS) {
                if (request.type == LOUDNESS_REQUEST_FREQUENCY) {
                    loudness_change_frequency(request.value);
                }
            }
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
                    (const signed char *)"LOUDNESS",
                    configMINIMAL_STACK_SIZE,
                    NULL,
                    (unsigned portBASE_TYPE)tskIDLE_PRIORITY + 1,
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

    if (xLoudnessFreqQueue == NULL) {
        return;
    }

    request.type = LOUDNESS_REQUEST_FREQUENCY;
    request.value = frequency;
    (void)xQueueSend(xLoudnessFreqQueue, &request, 0);
}
#endif

static int32_t loudness_calculate_db_spl_x10(void) {
    int32_t gain_dbfs_x10 = loudness_get_gain_dbfs_x10();
    int32_t db_spl_x10 = (LOUDNESS_DB_SPL_MAX * 10) + gain_dbfs_x10;

    if (db_spl_x10 < 0) {
        return 0;
    }
    if (db_spl_x10 > (LOUDNESS_DB_SPL_MAX * 10)) {
        return LOUDNESS_DB_SPL_MAX * 10;
    }
    return db_spl_x10;
}

static int32_t loudness_calculate_db_spl(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    return (db_spl_x10 + 5) / 10;
}

void loudness_update_active_equalizer_step(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    int32_t db_spl = (db_spl_x10 + 5) / 10;
    Bool db_spl_changed;

#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif

#ifdef BUILD_TESTING
    db_spl_changed = (db_spl != (int32_t)last_db_spl);
#else
    db_spl_changed = loudness_should_change_equalizer_step(db_spl_x10);
#endif

    if (db_spl_changed) {
        loudness_select_equalizer_step(db_spl);
    }
#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_gain_dbfs(
        loudness_clamp_s8(loudness_get_gain_dbfs()));
    stats_telemetry_set_source_volume_control(
        loudness_inferred_gain_has_source_volume_control() ? 1u : 0u);
    stats_telemetry_set_equalizer_state(
        loudness_clamp_s8(db_spl),
        (U8)loudness_get_equalizer_step((int32_t)last_db_spl)
    );
#endif
}

int32_t loudness_get_gain_dbfs(void) {
    if (loudness_inferred_gain_has_source_volume_control()) {
        int32_t gain_q8 = loudness_clamp_gain_dbfs_q8((int32_t)target_gain_dbfs_q8);
        return loudness_clamp_gain_dbfs(gain_q8 / 256);
    }
    return loudness_inferred_gain_dbfs();
}

void loudness_usb_volume_changed(S16 volume_q8)
{
    int32_t gain_q8 = loudness_clamp_gain_dbfs_q8(
        (int32_t)volume_q8 - (int32_t)VOL_MAX);
    int32_t gain_dbfs = loudness_clamp_gain_dbfs(gain_q8 / 256);

    target_gain_dbfs_q8 = (int16_t)gain_q8;
    loudness_set_source_has_volume_control();

#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_gain_dbfs(loudness_clamp_s8(gain_dbfs));
    stats_telemetry_set_source_volume_control(1u);
#endif

    loudness_update_active_equalizer_step();
}

int32_t loudness_get_db_spl(void) {
    return loudness_calculate_db_spl();
}

int16_t loudness_get_last_db_spl(void) {
    return last_db_spl;
}

void loudness_set_level_dbfs(int32_t db_fs) {
    int32_t gain_q8;

    db_fs = loudness_clamp_gain_dbfs(db_fs);
    gain_q8 = loudness_clamp_gain_dbfs_q8(db_fs * 256);

    target_gain_dbfs_q8 = (int16_t)gain_q8;

    loudness_update_active_equalizer_step();
}

void loudness_filter_init(void) {
#ifdef FREERTOS_USED
    if (loudness_state_initialized) {
        return;
    }
#endif

    loudness_reset_rms();
    loudness_inferred_gain_reset();

#ifdef PRECISE
    loudness_precise_reset_states();
#endif
#ifdef FAST
    loudness_fast_reset_states();
#endif

    loudness_select_equalizer_step(LOUDNESS_DB_SPL_MAX);
    loudness_change_frequency(current_freq.frequency);

#ifdef FREERTOS_USED
    loudness_state_initialized = TRUE;
#endif
}

#endif /* LOUDNESS_DISABLE */

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

void loudness_set_level_dbfs(int32_t db_fs) {
    (void)db_fs;
}

void loudness_update_active_equalizer_step(void) {}

int16_t loudness_get_last_db_spl(void) {
    return LOUDNESS_DB_SPL_MAX;
}

int32_t loudness_get_db_spl(void) {
    return LOUDNESS_DB_SPL_MAX;
}

int32_t loudness_get_gain_dbfs(void) {
    int32_t gain_dbfs_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
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
