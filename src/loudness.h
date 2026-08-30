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

#include "loudness_fast.h"

/* One baked loudness+volume row every 0.5 dB from 35 to 95 phon. */
#define LOUDNESS_MIN_PHON_X10           350
#define LOUDNESS_MAX_PHON_X10           950
#define LOUDNESS_EQUALIZER_STEP_X10       5
#define LOUDNESS_NUM_EQUALIZER_STEPS    121
#define LOUDNESS_REF_PHON     80   /* ISO contour shape reference */
#ifndef LOUDNESS_DB_SPL_MAX
/* A dB SPL max decides where on the volume know the loudness filters starts.
 * 95 db SPL max is common for sonos and other consumer devices.
 * 105 dB SPL max is the default peak level in THX and
 * film industry reference level for standard home theaters and small mixing spaces. */
#define LOUDNESS_DB_SPL_MAX   95
#endif
#if LOUDNESS_DB_SPL_MAX <= LOUDNESS_REF_PHON
#error LOUDNESS_DB_SPL_MAX must be greater than LOUDNESS_REF_PHON (80)
#endif
#define LOUDNESS_GAIN_DBFS_MIN  (-60) /* AK5394A / USB volume floor (dBFS) */
#define LOUDNESS_GAIN_DBFS_MAX  0     /* Windows volume 100; matches VOL_MAX */
/* Reported dB SPL equals phon for equalizer contour selection. */

/* --- Public API --- */

void loudness_init(void);
void loudness_usb_statistics_init(void);
void loudness_filter_init(void);

#ifndef LOUDNESS_DISABLE
void loudness_rtos_init(void);
Bool loudness_rtos_is_ready(void);
void loudness_request_frequency_change(uint32_t frequency);
void loudness_change_frequency_fast(uint32_t frequency);
#define loudness_change_frequency loudness_change_frequency_fast

#define LOUDNESS_FILTER_FAST_32(ch, sample_32) ((S32)loudness_fast_24bit((ch), (sample_32)))
#define LOUDNESS_FILTER_16BIT_CONTAINER(ch, sample_32) \
    loudness_filter_16bit_container((ch), (sample_32))
#define LOUDNESS_FILTER_16BIT_STEREO_PACKET(L, R, N) \
    loudness_filter_16bit_stereo_packet((L), (R), (N))
#define LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample_32) \
    loudness_filter_24bit_container((ch), (sample_32))

Bool loudness_filter_is_active(void);
Bool loudness_channel_filter_is_idle(int channel);

/* Force the active loudness band from an external dBFS estimate (<= 0). */
void loudness_set_level_dbfs(int32_t db_fs);

/* Publish per-channel host USB volume changes (signed Q8.8 dB). */
void loudness_usb_volume_changed_left(S16 volume_q8);
void loudness_usb_volume_changed_right(S16 volume_q8);

/* Windows Bass Boost preference mirror (UAC Feature Unit CS 0x09). */
void loudness_bass_boost_set(Bool enabled);
Bool loudness_bass_boost_is_enabled(void);

/* Update the active equalizer step based on current host gain level. */
void loudness_update_active_equalizer_step(void);

/* Return per-channel host gain in dBFS (channel 0 = L, 1 = R). Non-positive. */
int32_t loudness_get_gain_dbfs_channel(int channel);

#include "loudness_inferred_gain.h"


/* Current left/master level in 0.1 dB SPL units. */
int16_t loudness_get_last_db_spl_x10(void);

#else /* LOUDNESS_DISABLE */
#define LOUDNESS_FILTER_FAST_32(ch, sample_32) \
    ((void)(ch), (S32)(sample_32))
#define LOUDNESS_FILTER_16BIT_CONTAINER(ch, sample_32) \
    ((void)(ch), (sample_32))
#define LOUDNESS_FILTER_16BIT_STEREO_PACKET(L, R, N) \
    do { (void)(L); (void)(R); (void)(N); } while (0)
#define LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample_32) \
    ((void)(ch), (sample_32))
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
#define DOWNSAMPLE_FILTER_TO_16BIT_CONTAINER(sample) \
    ((int32_t)(((int64_t)saturate_16bit_s32_to_s32((sample) >> 8) << 16)))
#define DOWNSAMPLE_24BIT(sample) ((int32_t)((sample) >> 8))
#define DOWNSAMPLE_16BIT(sample) ((int16_t)((sample) >> 16))

/* Round-half-up right-shift for signed 64-bit values. */
#define ROUND_SHIFT_S64(x, n) \
    (((int64_t)(x) + (1LL << ((n) - 1))) >> (n))

/* Quantize internal 32.8 fixed sample to 24-bit DAC word (round, not truncate). */
#define DOWNSAMPLE_24BIT_ROUND(sample) \
    ((int32_t)ROUND_SHIFT_S64((sample), 8))

#endif
