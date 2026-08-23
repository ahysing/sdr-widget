#include "track_dbfs.h"
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
#else
#include "usb_specific_request.h"
#endif
#include "loudness.h"
#include "compiler.h"
#include <stdint.h>

#ifndef LOUDNESS_DISABLE
U64 root_mean_square = 0;
static volatile U32 root_mean_square_seq = 0;
static inline void loudness_rms_write_begin(void) {
    root_mean_square_seq++;
}

static inline void loudness_rms_write_end(void) {
    root_mean_square_seq++;
}

static U64 loudness_rms_read(void) {
    U32 seq1;
    U32 seq2 = 0;
    U64 val = 0;

    do {
        seq1 = root_mean_square_seq;
        if (seq1 & 1U) {
            continue;
        }
        val = root_mean_square;
        seq2 = root_mean_square_seq;
    } while (seq1 != seq2);

    return val;
}

#define ROOT_MEAN_SQUARE_WINDOW_SHIFT 21
#define SAMPLE_24BITS 24

static uint32_t sqrt_i(uint64_t number) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > number) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (number >= res + bit) {
            number -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

/**
 * @brief Estimate dB FS from an RMS magnitude in the 24-bit sample domain.
 *
 * Converts the RMS magnitude (post-sqrt_i, in the 24-bit sample domain) to dBFS
 * for equalizer-step blending via loudness_get_track_dbfs().
 *
 * Silence (rms == 0) returns -144 dBFS, the 24-bit dynamic-range floor, not 0 dBFS.
 *
 * Integer log2 via CLZ: bit_position = 32 - CLZ(rms) locates the MSB. An 8-bit
 * fraction below the MSB approximates the sub-bit position. Each bit is mapped to
 * 6 dB (log2(10) ~ 6.02); fractional correction is (fraction * 6) >> 8 for ~1 dB
 * resolution without floating point. Result is clamped to 0 dBFS maximum.
 * SAMPLE_24BITS (24) is the full-scale reference.
 *
 * @return A non-positive signed value in the range -144 .. 0 dBFS.
 */
static int32_t calculate_dB_24bit(uint32_t rms) {
    if (rms == 0) {
        return -144;
    }

    uint32_t leading_zeros = CLZ(rms);
    int32_t bit_position = 32 - (int32_t)leading_zeros;

    uint32_t fraction = 0;
    if (bit_position < 32) {
        fraction = (rms << (leading_zeros + 1)) >> 24;
    }

    int32_t db_base = (bit_position - SAMPLE_24BITS) * 6;
    int32_t db_fraction = (int32_t)((fraction * 6) >> 8);

    int32_t db = db_base + db_fraction;

    return (db > 0) ? 0 : db;
}

void loudness_update_track_level_fast(int32_t sample) {
    uint32_t fs = current_freq.frequency;
    if (fs == 0) {
        return;
    }

    if (sample < 0) {
        sample = -sample;
    }

    // 1. SKALER NED FØRST: 24-bit -> 15-bit (Mister kun uaudbar bunnstøy)
    // Dette garanterer at kvadratet ALDRI fyller mer enn 30 bits.
    // uint32_t s = (uint32_t)(sample >> 9); 
    // uint32_t sample_sq = s * s; // 100 % ren og lynrask 32-bit multiplikasjon!
    int32_t s = sample;
    uint64_t sample_sq = (uint64_t)(int64_t)s * (uint64_t)(int64_t)s;
    /* One call per channel; L and R form a single interleaved series whose
     * mean square matches stereo RMS^2 = (L^2 + R^2) / (2N). */

    loudness_rms_write_begin();
    /* Power-of-two leaky integrator avoids expensive 64-bit division in audio path. */
    if (sample_sq > root_mean_square) {
        root_mean_square += (sample_sq - root_mean_square) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    } else {
        root_mean_square -= (root_mean_square - sample_sq) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    }
    loudness_rms_write_end();
}

int32_t loudness_get_track_rms_dbfs(void) {
    uint32_t rms = sqrt_i(loudness_rms_read());
    return calculate_dB_24bit(rms);
}

int32_t loudness_get_track_dbfs(void) {
    int32_t track_dbfs = loudness_get_track_rms_dbfs();
    if (track_dbfs < LOUDNESS_TRACK_DBFS_MIN) {
        track_dbfs = LOUDNESS_TRACK_DBFS_MIN;
    }
    if (track_dbfs > LOUDNESS_TRACK_DBFS_MAX) {
        track_dbfs = LOUDNESS_TRACK_DBFS_MAX;
    }
    return track_dbfs;
}

void loudness_reset_rms(void)
{
    // seeding the integrator to a typical master signal (-6 dBFS)
    // a rms value of -6 dBFS in 24-bit is 4194394
    loudness_rms_write_begin();
    root_mean_square = 17592186044416ULL;
    loudness_rms_write_end();
}

#if 0 /* reserved: compression compensation via track RMS in equalizer-step selection */
/* Weighting for blending gain vs. measured track loudness. Must sum to 100. */
#define LOUDNESS_GAIN_WEIGHT_PCT   90
#define LOUDNESS_TRACK_WEIGHT_PCT  10

static int32_t loudness_calculate_db_spl_x10_blended(void) {
    int32_t track_dbfs = loudness_get_track_dbfs();
    int32_t track_normalized = track_dbfs - LOUDNESS_TRACK_DBFS_MIN;
    int32_t host_gain_dbfs = loudness_get_gain_dbfs();
    int32_t host_gain_scaled = host_gain_dbfs * 10;
    int32_t track_scaled = track_normalized * 10;

    int32_t blended_scaled_x100 = (host_gain_scaled * LOUDNESS_GAIN_WEIGHT_PCT) +
                                  (track_scaled * LOUDNESS_TRACK_WEIGHT_PCT);

    int32_t blended_dbfs_x10 = (blended_scaled_x100 - 50) / 100;
    return blended_dbfs_x10 + (LOUDNESS_DB_SPL_MAX * 10);
}
#endif

#else /* LOUDNESS_DISABLE */

void loudness_reset_rms(void) {}

int32_t loudness_get_track_rms_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

int32_t loudness_get_track_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

#endif /* LOUDNESS_DISABLE */
