/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness.h
 *
 *  Created on: 2026-06-22
 *      Author: Andreas Dreyer Hysing
 */

#ifndef LOUDNESS_H_
#define LOUDNESS_H_

#include <stdint.h>
#include "compiler.h"

#ifdef PRECISE
typedef struct {
    int64_t xn_1;
    int64_t xn_2;
    int64_t yn_1;
    int64_t yn_2;
} biquad_state_precise_t;

typedef struct {
    int64_t a1;
    int64_t a2;
    int64_t b0;
    int64_t b1;
    int64_t b2;
} biquad_quotients_precise_t;
#endif

#ifdef FAST
typedef struct {
    int32_t xn_1;
    int32_t xn_2;
    int32_t yn_1;
    int32_t yn_2;
} biquad_state_fast_t;

typedef struct {
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
} biquad_quotients_fast_t;
#endif

typedef enum {
    LOUDNESS_FILTER_LOW_SHELF = 0,
    LOUDNESS_FILTER_HIGH_SHELF = 1
} biquad_type_t;

/* Equal-loudness equalizer steps (assumes 0 dBFS == 80 dB SPL):
 *   index  0 -> 55 phon, index 1 -> 57 phon, ... index 12 -> 79 phon
 *   index 13 -> 80 phon (neutral unity biquads)
 */
#define LOUDNESS_NUM_EQUALIZER_STEPS    14
#define LOUDNESS_MIN_PHON               55
#define LOUDNESS_PHON_STEP_DB           2
#define LOUDNESS_NEUTRAL_STEP           (LOUDNESS_NUM_EQUALIZER_STEPS - 1)
#define LOUDNESS_CONTOUR_STEPS          (LOUDNESS_NEUTRAL_STEP)
#define LOUDNESS_REF_DB_SPL   80   /* 100% (0 dBFS) corresponds to 80 dB SPL */
#define LOUDNESS_REF_PHON     80
#define LOUDNESS_EQUALIZER_STEP_DB LOUDNESS_PHON_STEP_DB

/* --- Public API --- */

void loudness_init(void);
void loudness_usb_statistics_init(void);
void loudness_filter_init(void);

#ifndef LOUDNESS_DISABLE
#ifdef FREERTOS_USED
void loudness_rtos_init(void);
Bool loudness_rtos_is_ready(void);
void loudness_request_frequency_change(uint32_t frequency);
#endif
#ifdef PRECISE
void loudness_change_frequency_precise(uint32_t frequency);
#define loudness_change_frequency loudness_change_frequency_precise
#endif
#ifdef FAST
void loudness_change_frequency_fast(uint32_t frequency);
#define loudness_change_frequency loudness_change_frequency_fast
#endif
void loudness_reset_rms(void);

#ifdef PRECISE
int64_t loudness_precise_24bit(int64_t sample);
int64_t biquad_step_precise_24bit(int64_t sample, biquad_state_precise_t* biquad_states);
#define LOUDNESS_FILTER_PRECISE_24(sample_64) loudness_precise_24bit(sample_64)
#else
#define LOUDNESS_FILTER_PRECISE_24(sample_64) (sample_64)
#endif

#ifdef FAST
int64_t loudness_fast_24bit(int32_t sample);
int32_t biquad_step_fast_32bit(int32_t sample, biquad_state_fast_t* biquad_states);
S32 loudness_filter_16bit_container(S32 sample);
#define LOUDNESS_FILTER_FAST_32(sample_32) ((S32)loudness_fast_24bit(sample_32))
#define LOUDNESS_FILTER_16BIT_CONTAINER(sample_32) loudness_filter_16bit_container(sample_32)
#else
#define LOUDNESS_FILTER_FAST_32(sample_32) (sample_32)
#define LOUDNESS_FILTER_16BIT_CONTAINER(sample_32) (sample_32)
#endif

/* Force the active loudness band from an external dBFS estimate (<= 0). */
void loudness_set_level_dbfs(int32_t db_fs);

/* Update the active equalizer step based on current track and host gain levels. */
void loudness_update_active_equalizer_step(void);

/* Return the current dBFS estimate derived from the running RMS. Non-positive. */
int32_t loudness_get_track_rms_dbfs(void);

/* Return track dBFS clamped to [LOUDNESS_TRACK_DBFS_MIN, LOUDNESS_TRACK_DBFS_MAX]. */
int32_t loudness_get_track_dbfs(void);

/* Return the current host-gain expressed in dBFS. Non-positive. */
int32_t loudness_get_gain_dbfs(void);

/* Blended listening level in dB SPL (same value as used for equalizer step selection). */
int32_t loudness_get_db_spl(void);

/* Current blended phon estimate (dB SPL) used for bypass and equalizer step selection. */
int16_t loudness_get_last_db_spl(void);

#else /* LOUDNESS_DISABLE */
#define LOUDNESS_FILTER_PRECISE_24(sample_64) (sample_64)
#define LOUDNESS_FILTER_FAST_32(sample_32) (sample_32)
#define LOUDNESS_FILTER_16BIT_CONTAINER(sample_32) (sample_32)
#endif /* LOUDNESS_DISABLE */

int32_t loudness_apply_noise_shaper_to_output(int32_t sample_32bit, int32_t* noise_shaper_error);

/* --- Helpers --- */

#if defined(__GNUC__)
#define CLZ(x) __builtin_clz(x)
#elif defined(_MSC_VER)
#include <intrin.h>
#define CLZ(x) _lzcnt_u32(x)
#else
static inline int CLZ(uint32_t x) {
    int n = 0;
    if (x == 0) return 32;
    if (x <= 0x0000FFFF) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFF) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFF) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFF) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFF) { n += 1; }
    return n;
}
#endif

#define INT24_MAX  8388607
#define INT24_MIN -8388608
#define INT24_MAX_S64  8388607LL
#define INT24_MIN_S64 -8388608LL

/* Clamp wide accumulators/samples to the 24-bit DAC range. */
S32 saturate_24bit_s64_to_s32(S64 acc);
S32 saturate_24bit_s32_to_s32(S32 acc);
U32 saturate_24bit_s32_to_u32(S32 acc);
S32 saturate_16bit_s32_to_s32(S32 acc);

/* Correct macros for upsampling/downsampling */
#define UPSAMPLE_16BIT_64(sample) (((int64_t)(int16_t)(sample)) << 16)
#define UPSAMPLE_24BIT_64(sample) (((int64_t)(((int32_t)(sample) << 8) >> 8)) << 8)
#define UPSAMPLE_16BIT_32(sample) (((int32_t)(int16_t)(sample)) << 16)
#define UPSAMPLE_24BIT_32(sample) ((((int32_t)(sample) << 8) >> 8) << 8)
#define UPSAMPLE_16BIT_TO_FILTER_32(sample) (((int32_t)(int16_t)((sample) >> 16)) << 8)
#define DOWNSAMPLE_FILTER_TO_16BIT_CONTAINER(sample) (((int32_t)saturate_16bit_s32_to_s32((sample) >> 8)) << 16)
#define DOWNSAMPLE_24BIT(sample) ((int32_t)((sample) >> 8))
#define DOWNSAMPLE_16BIT(sample) ((int16_t)((sample) >> 16))

/* Round-half-up right-shift for signed 64-bit values. */
#define ROUND_SHIFT_S64(x, n) \
    (((int64_t)(x) + (1LL << ((n) - 1))) >> (n))

/* Quantize internal 32.8 fixed sample to 24-bit DAC word (round, not truncate). */
#define DOWNSAMPLE_24BIT_ROUND(sample) \
    ((int32_t)ROUND_SHIFT_S64((sample), 8))



/* Empirically measured RMS dBFS range for typical program material,
 * derived from the rock genre's "Loudness War" span:
 *   - 1980s pre-war masters:   ~ -18 dBFS RMS   (quiet, dynamic)
 *   - late-1990s/2000s masters:~  -6 dBFS RMS   (brickwalled, loud)
 * The measured track dBFS is clamped to this window before it
 * contributes to the phon-band selection so that outliers (silence,
 * unusually hot material) don't drag the blended level to extremes.
 */
#define LOUDNESS_TRACK_DBFS_MIN   (-18)
#define LOUDNESS_TRACK_DBFS_MAX    (-6)

#endif
