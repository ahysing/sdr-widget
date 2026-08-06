#include "loudness.h"
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
extern S16 spk_vol_usb_L, spk_vol_usb_R;
#else
#include "usb_specific_request.h"
#include "device_audio_task.h"
#endif
#ifndef USBSTATISTICS_DISABLE
#include "usb_statistics.h"
#include "audio_stats_logic.h"
#endif
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#else
#define tskIDLE_PRIORITY 0
#define xTaskGetTickCount() 0
#define vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement)
#define xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask)
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
/* Leaky integrator for the running mean-square of the input signal. */
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

#define TO_Q30(X) (((S64)(X)) << 30)

static inline int64_t FROM_Q61(int64_t x) {
    // Extract the sign bit to handle rounding symmetrically
    int64_t sign = x >> 63;

    // Add 0.5 (1LL << 60) for positive numbers, or subtract 0.5 for negative numbers
    // This achieves perfect round-to-nearest behavior without overflowing int64_t
    int64_t round_offset = ((int64_t)1 << 60) ^ sign;
    round_offset -= sign;

    return (x + round_offset) >> 61;
}

#if !defined(_MSC_VER) && !defined(__AVR32__)
/* For generic GCC that might support __int128 */
#define HAS_INT128
#endif

#if !defined(HAS_INT128) && !defined(_MSC_VER)
/* Manual 64x64 -> 128 multiplication and shift for AVR32 or other compilers without __int128 */
static inline S64 mul_shift_q61(S64 a, S64 b) {
    U64 a_lo = (U32)a;
    S64 a_hi = a >> 32;
    U64 b_lo = (U32)b;
    S64 b_hi = b >> 32;

    U64 lo_lo = a_lo * b_lo;
    S64 hi_lo = a_hi * b_lo;
    S64 lo_hi = a_lo * b_hi;
    S64 hi_hi = a_hi * b_hi;

    U64 cross = (lo_lo >> 32) + (U32)hi_lo + (U32)lo_hi;
    S64 upper = hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (cross >> 32);
    S64 lower = (cross << 32) | (U32)lo_lo;

    /* Shift right by 61 */
    return (upper << 3) | (lower >> 61);
}
#endif

#define FROM_Q29(X) (((X) + (1LL << 28)) >> 29)
#define FROM_Q30(X) (((X) + (1LL << 29)) >> 30)
#define Q8_SHIFT   8
#define FROM_Q8(X) ROUND_SHIFT_S64(X, Q8_SHIFT)
/* Convert an offline-designed Q30 coefficient to the Q8 domain used by the
 * fast biquad path. Round-half-up. */
#define Q30_TO_Q8(X) ((S32)(((X) + (1LL << 21)) >> 22))

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

void loudness_volume_init(void) {
}

void loudness_init(void) {
    loudness_print_build_config();
    loudness_usb_statistics_init();
    loudness_filter_init();
}

#ifndef LOUDNESS_DISABLE
/* The macs.d is only available when compiling
 * with avr32-gcc for a target that has the AVR32 DSP extension (e.g. the
 * Atmel AT32UC3A3). When building with any other toolchain -- notably
 * the Visual Studio (MSVC) toolchain used for host-side unit tests --
 * fall back to a portable C expression with equivalent semantics.
 *
 * We therefore require BOTH:
 *   - __GNUC__            : the compiler is GCC (or a GCC-compatible one
 *                           that defines this macro; MSVC does not).
 *   - __AVR32_HAS_DSP__   : the target actually has the DSP extension
 *                           that provides the mach_d instruction.
 */
#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
/*   macs.d: signed 32x32 -> 64 multiply-accumulate.
 *   acc64 += (S32)b * (S32)c
 */
S64 macs_d(S64 d, S32 a, S32 b) {
    __asm__ ("macs.d %0, %1, %2" : "+r"(d) : "r"(a), "r"(b));
    return d;
}
#define FMA_24BIT(A, B, C) macs_d(A, B, C)
#define FMS_24BIT(A, B, C) macs_d(A, B, -C)

/*   mulu.d: Unsigned 32x32 -> 64-bit widening multiplication.
 */
static inline uint64_t mulu_d(uint32_t a, uint32_t b) {
    uint64_t res;
    __asm__ ("mulu.d %0, %1, %2" : "=r"(res) : "r"(a), "r"(b));
    return res;
}
#else
/* Portable fallback for non-GCC toolchains (e.g. MSVC on Windows used
 * for host-side testing). Matches the semantics of the AVR32 builtin:
 *
 *   FMA: A += B * C
 *   FMS: A -= B * C
 */
#define FMA_24BIT(A, B, C) \
    ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) \
    ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))

static inline uint64_t mulu_d(uint32_t a, uint32_t b) {
    return (uint64_t)a * (uint64_t)b;
}
#endif

#define SAMPLE_24BITS 24
#define LOUDNESS_FILTERS 2
/* Unity (1.0) in Q61 fixed-point; PRECISE biquad coefficients use this scale. */
#define LOUDNESS_Q61_ONE  ((int64_t)1 << 61)
#define LOUDNESS_Q29_ONE  ((int32_t)1 << 29)
/* Coefficient de-zip ramp duration (ms). ~15 ms => 662 samples @ 44.1 kHz, 2880 @ 192 kHz. */
#define LOUDNESS_COEFF_RAMP_MS 15

#define Q30_ONE             (1LL << 30)
/* Speaker volume factor in Q30. Set to 1.0 (unity); change to e.g.
 * ((S64)((1LL<<30) * 4 / 5)) for 0.8. Applied only to the feed-forward
 * taps (b0,b1,b2). NEVER scale a1,a2 -- it would move the poles. */
#define SPEAKER_VOLUME_Q30  Q30_ONE

/* Equalizer-step coefficient table. Indexed [equalizer_step][filter].
 * Layout per filter row: { b0, b1, b2, a1, a2 } in Q30 for PRECISE,
 * and Q2.30 for FAST (32-bit signed).
 * Designed offline (RBJ cookbook, fs=44100 Hz):
 *   filter 0: low-shelf
 *   filter 1: high-shelf
 *
 * Bands (phon labels; assuming 0 dBFS == LOUDNESS_REF_DB_SPL = 80 dB SPL)
 */
#ifdef PRECISE
static const biquad_quotients_precise_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        {  2348449834323295744LL, -4541805258938481152LL,  2194782095529568256LL,  LOUDNESS_Q61_ONE, -4542374673978030080LL,  2236819505599620096LL },
        {  3715021076448667136LL,   158237355048826912LL,   431624141683216128LL,  LOUDNESS_Q61_ONE,  1529017445594673408LL,   470022118372342080LL },
    },
    /* 65 phon */
    {
        {  2331457254426197504LL, -4527686235807184896LL,  2197327148297707264LL,  LOUDNESS_Q61_ONE, -4528023852580137472LL,  2222603776737257984LL },
        {  3078756756369804800LL,   493916885933517312LL,   336448630401514112LL,  LOUDNESS_Q61_ONE,  1229518856989233152LL,   373760406501908864LL },
    },
    /* 75 phon */
    {
        {  2314395388020622848LL, -4512055621803879424LL,  2198486932718281216LL,  LOUDNESS_Q61_ONE, -4512167933961389568LL,  2206926999367699456LL },
        {  2540423121366800896LL,   710477326520095232LL,   291992445812729152LL,  LOUDNESS_Q61_ONE,   931626744154566656LL,   305423140331365120LL },
    },
    /* 80 phon */
    {
        {  LOUDNESS_Q61_ONE, 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
        {  LOUDNESS_Q61_ONE, 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
    },
};

static const biquad_quotients_precise_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        {  2345086951461462016LL, -4547376282380467712LL,  2203495877909601536LL,  LOUDNESS_Q61_ONE, -4547857895502438400LL,  2242258207035398656LL },
        {  3956091077457189376LL,  -496402134921083648LL,   564147130472615680LL,  LOUDNESS_Q61_ONE,  1264149353203375616LL,   453843710591652096LL },
    },
    /* 65 phon */
    {
        {  2329436069369872896LL, -4534344608244988416LL,  2205837698611165696LL,  LOUDNESS_Q61_ONE, -4534630291457484288LL,  2229145075554848256LL },
        {  3194123349551754752LL,      832382984908222LL,   404153643991243904LL,  LOUDNESS_Q61_ONE,   928706015203938048LL,   364560352110274624LL },
    },
    /* 75 phon */
    {
        {  2313722089908387840LL, -4519914297063417856LL,  2206891915723137536LL,  LOUDNESS_Q61_ONE, -4520009365427488256LL,  2214675928053760768LL },
        {  2571359374490702336LL,   328712754173955904LL,   318592361904885248LL,  LOUDNESS_Q61_ONE,   603488473017925376LL,   309333008337924480LL },
    },
    /* 80 phon */
    {
        {  LOUDNESS_Q61_ONE, 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
        {  LOUDNESS_Q61_ONE, 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
    },
};
#endif

#ifdef FAST
static const biquad_quotients_fast_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        {   546791087, -1057471442,   511012528,   536870912, -1057604019,   520800125 },
        {   864970748,    36842505,   100495327,   536870912,   356002116,   109435552 },
    },
    /* 65 phon */
    {
        {   542834693, -1054184101,   511605094,   536870912, -1054262708,   517490268 },
        {   716828917,   114998986,    78335551,   536870912,   286269667,    87022876 },
    },
    /* 75 phon */
    {
        {   538862168, -1050544815,   511875128,   536870912, -1050570964,   513840234 },
        {   591488351,   165420893,    67984789,   536870912,   216911254,    71111866 },
    },
     /* 80 phon */
    {
        {  LOUDNESS_Q29_ONE, 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        {  LOUDNESS_Q29_ONE, 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },
};

static const biquad_quotients_fast_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        {   546008104, -1058768547,   513041364,   536870912, -1058880681,   522066422 },
        {   921099232,  -115577629,   131350740,   536870912,   294332708,   105668723 },
    },
    /* 65 phon */
    {
        {   542364099, -1055734374,   513586611,   536870912, -1055800889,   519013283 },
        {   743689795,      193804,    94099353,   536870912,   216231219,    84880821 },
    },
    /* 75 phon */
    {
        {   538705403, -1052374555,   513832065,   536870912, -1052396690,   515644422 },
        {   598691258,    76534402,    74178065,   536870912,   140510610,    72022203 },
    },
     /* 80 phon */
    {
        {  LOUDNESS_Q29_ONE, 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        {  LOUDNESS_Q29_ONE, 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },
};
#endif

#ifdef FREERTOS_USED
static Bool loudness_state_initialized = FALSE;
static Bool loudness_rtos_initialized = FALSE;
static volatile Bool loudness_task_ready = FALSE;
#endif

#ifdef BUILD_TESTING
/* Samples remaining in the coefficient de-zip ramp (audio path decrements). */
volatile uint32_t coeff_ramp_remaining = 0;
#else
/* Samples remaining in the coefficient de-zip ramp (audio path decrements). */
static volatile uint32_t coeff_ramp_remaining = 0;
#endif

#ifdef PRECISE
static biquad_quotients_precise_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];

/* Pointer to the currently active equalizer-step table */
static const biquad_quotients_precise_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

/* Active coefficients (audio path); target coefficients (background task). */
static biquad_quotients_precise_t coeff_buf_endpoint_ping[LOUDNESS_FILTERS];
static biquad_quotients_precise_t coeff_buf_endpoint_pong[LOUDNESS_FILTERS];
static biquad_quotients_precise_t coeff_buf_ramp_blend[LOUDNESS_FILTERS];

static biquad_quotients_precise_t *current_quotients = coeff_buf_endpoint_ping;
static biquad_quotients_precise_t *target_quotients = coeff_buf_endpoint_pong;
static biquad_quotients_precise_t *active_quotients = coeff_buf_endpoint_ping;

/* Pointer used by real-time biquad steps. */
biquad_quotients_precise_t * volatile loudness_quotients = coeff_buf_endpoint_ping;

static biquad_state_precise_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type);
#endif

#ifdef FAST
static biquad_quotients_fast_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];

/* Pointer to the currently active equalizer-step table */
static const biquad_quotients_fast_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

/* Active coefficients (audio path); target coefficients (background task). */
static biquad_quotients_fast_t coeff_buf_endpoint_ping[LOUDNESS_FILTERS];
static biquad_quotients_fast_t coeff_buf_endpoint_pong[LOUDNESS_FILTERS];
static biquad_quotients_fast_t coeff_buf_ramp_blend[LOUDNESS_FILTERS];

static biquad_quotients_fast_t *current_quotients = coeff_buf_endpoint_ping;
static biquad_quotients_fast_t *target_quotients = coeff_buf_endpoint_pong;
static biquad_quotients_fast_t *active_quotients = coeff_buf_endpoint_ping;

/* Pointer used by real-time biquad steps. */
biquad_quotients_fast_t * volatile loudness_quotients = coeff_buf_endpoint_ping;

static biquad_state_fast_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type);
#endif


#ifdef BUILD_TESTING
/* Current phon estimate (dB SPL); updated by background task, read on audio hot path for bypass. */
volatile S16 last_db_spl = LOUDNESS_REF_PHON;
#else
/* Current phon estimate (dB SPL); updated by background task, read on audio hot path for bypass. */
static volatile S16 last_db_spl = LOUDNESS_REF_PHON;
#endif
#ifdef FREERTOS_USED
static volatile S16 target_db_spl = LOUDNESS_REF_PHON;
static volatile S16 target_db_fs = 0;
#else
static volatile S16 target_db_fs = 0;
#endif


#define ROOT_MEAN_SQUARE_WINDOW_SHIFT 21
#define ROOT_MEAN_SQUARE_WINDOW_SIZE (1UL << ROOT_MEAN_SQUARE_WINDOW_SHIFT)

/* Stereo feeds left then right through loudness_update_track_level_*(), so the
 * leaky window counts 2*fs samples per second. A shift of 21 corresponds to
 * 2,097,152 samples (~22 seconds at 48 kHz stereo, ~5.5 seconds at 192 kHz).
 */
#define NUM_CHANNELS 2

/* ---------------------------------------------------------------------------
 * Internal DSP Helpers
 * -------------------------------------------------------------------------*/

/**
 * @brief Bakhshali / bitwise integer square root.
 */
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


#ifdef PRECISE
void loudness_update_track_level_precise(int64_t sample) {
    uint32_t fs = current_freq.frequency;
    if (fs == 0) {
        return;
    }

    if (sample < 0) {
        sample = -sample;
    }
    uint64_t sample_sq = (uint64_t)sample * (uint64_t)sample;
    /* One call per channel; L and R form a single interleaved series whose
     * mean square matches stereo RMS^2 = (L^2 + R^2) / (2N). */
    loudness_rms_write_begin();
    if (sample_sq > root_mean_square) {
        root_mean_square += (sample_sq - root_mean_square) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    } else {
        root_mean_square -= (root_mean_square - sample_sq) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    }

    loudness_rms_write_end();
}
#endif

#ifdef FAST
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
    uint64_t sample_sq = mulu_d(s, s);
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
#endif

/* ---------------------------------------------------------------------------
 * Biquad steps
 * -------------------------------------------------------------------------*/
#ifdef PRECISE
int64_t biquad_step_precise_24bit(int64_t sample, biquad_state_precise_t* biquad_states)
{
    int64_t x_n = sample;
    int filter_idx = (int)(biquad_states - loudness_states);
    const biquad_quotients_precise_t* q = &loudness_quotients[filter_idx];

#if defined(_MSC_VER)
    /* MSVC doesn't support __int128. Use double for intermediate calculation in tests. */
    double y_n = (double)q->b0 * x_n;
    y_n += (double)q->b1 * biquad_states->xn_1;
    y_n += (double)q->b2 * biquad_states->xn_2;
    y_n -= (double)q->a1 * biquad_states->yn_1;
    y_n -= (double)q->a2 * biquad_states->yn_2;

    int64_t y_n_scaled = (int64_t)round(y_n / (double)LOUDNESS_Q61_ONE);
#elif defined(HAS_INT128)
    __int128 y_n = (__int128)q->b0 * x_n;
    y_n += (__int128)q->b1 * biquad_states->xn_1;
    y_n += (__int128)q->b2 * biquad_states->xn_2;
    y_n -= (__int128)q->a1 * biquad_states->yn_1;
    y_n -= (__int128)q->a2 * biquad_states->yn_2;

    int64_t y_n_scaled = FROM_Q61(y_n);
#else
    /* Manual 128-bit accumulation and shift for compilers without __int128 */
    int64_t y_n_scaled = mul_shift_q61(q->b0, x_n);
    y_n_scaled += mul_shift_q61(q->b1, biquad_states->xn_1);
    y_n_scaled += mul_shift_q61(q->b2, biquad_states->xn_2);
    y_n_scaled -= mul_shift_q61(q->a1, biquad_states->yn_1);
    y_n_scaled -= mul_shift_q61(q->a2, biquad_states->yn_2);
#endif

    biquad_states->xn_2 = biquad_states->xn_1;
    biquad_states->yn_2 = biquad_states->yn_1;
    biquad_states->xn_1 = x_n;
    biquad_states->yn_1 = y_n_scaled;

    return y_n_scaled;
}
#endif

#ifdef FAST
int32_t biquad_step_fast_32bit(int32_t x_n, biquad_state_fast_t* biquad_states)
{
    int filter_idx = (int)(biquad_states - loudness_states);
    const biquad_quotients_fast_t* q = &loudness_quotients[filter_idx];

    int64_t y_n = (int64_t)q->b0 * x_n;
    y_n = FMA_24BIT(y_n, q->b1, biquad_states->xn_1);
    y_n = FMA_24BIT(y_n, q->b2, biquad_states->xn_2);
    y_n = FMS_24BIT(y_n, q->a1, biquad_states->yn_1);
    y_n = FMS_24BIT(y_n, q->a2, biquad_states->yn_2);

    int64_t y_n_scaled = FROM_Q29(y_n);
    S32 y_n_safe = saturate_24bit_s64_to_s32(y_n_scaled);

    biquad_states->xn_2 = biquad_states->xn_1;
    biquad_states->yn_2 = biquad_states->yn_1;
    biquad_states->xn_1 = x_n;
    biquad_states->yn_1 = y_n_safe;

    return y_n_safe;
}
#endif

/* ---------------------------------------------------------------------------
 * Equalizer-step selection + coefficient de-zipping (interpolation)
 * -------------------------------------------------------------------------*/

#define SPEAKER_VOLUME_Q61  ((S64)1 << 61)

#define LOUDNESS_RAMP_44100   662u
#define LOUDNESS_RAMP_88200  1323u
#define LOUDNESS_RAMP_132300 1985u
#define LOUDNESS_RAMP_176400 2646u
#define LOUDNESS_RAMP_48000   720u
#define LOUDNESS_RAMP_96000  1440u
#define LOUDNESS_RAMP_144000 2160u
#define LOUDNESS_RAMP_192000 2880u

static inline uint32_t loudness_coeff_ramp_length(void) {
    /* Coarse ramping: called once per 1 ms packet.
     * We ramp over LOUDNESS_COEFF_RAMP_MS packets. */
    return LOUDNESS_COEFF_RAMP_MS;
}

#ifdef PRECISE
static inline int64_t ramp_coeff_precise(int64_t current, int64_t target, uint32_t remaining) {
    int64_t diff = target - current;
    if (diff == 0 || remaining <= 1) {
        return target;
    }
#if defined(HAS_INT128)
    {
        uint32_t inv_rem = (uint32_t)(0x80000000ULL / remaining);
        __int128 step = ((__int128)diff * inv_rem) >> 31;
        return current + (int64_t)step;
    }
#else
    {
        int64_t rem = (int64_t)remaining;
        int64_t adj = (rem >> 1) ^ (diff >> 63);
        return current + (diff + adj) / rem;
    }
#endif
}
#endif

#ifdef FAST


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
#ifdef BUILD_TESTING
int32_t ramp_coeff_fast(int32_t current, int32_t target, uint32_t remaining) {
#else
static inline int32_t ramp_coeff_fast(int32_t current, int32_t target, uint32_t remaining) {
#endif
    int32_t diff = target - current;
    if (diff == 0 || remaining <= 1) {
        return target;
    }
    uint32_t inv_rem = 0x7FFFFFFFu / remaining;
//#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
//    int32_t step = __builtin_mfrsrd(diff, inv_rem);
// #else
    int32_t step = (int32_t)(((int64_t)diff * inv_rem) >> 31);
// #endif
    return current + step;
}
#endif

#ifdef PRECISE
static Bool loudness_coeffs_converged_precise(void) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        if (active_quotients[i].b0 != target_quotients[i].b0 ||
            active_quotients[i].b1 != target_quotients[i].b1 ||
            active_quotients[i].b2 != target_quotients[i].b2 ||
            active_quotients[i].a1 != target_quotients[i].a1 ||
            active_quotients[i].a2 != target_quotients[i].a2) {
            return FALSE;
        }
    }
    return TRUE;
}

static void loudness_ramp_blend_step_precise(uint32_t remaining) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        active_quotients[i].b0 = ramp_coeff_precise(active_quotients[i].b0, target_quotients[i].b0, remaining);
        active_quotients[i].b1 = ramp_coeff_precise(active_quotients[i].b1, target_quotients[i].b1, remaining);
        active_quotients[i].b2 = ramp_coeff_precise(active_quotients[i].b2, target_quotients[i].b2, remaining);
        active_quotients[i].a1 = ramp_coeff_precise(active_quotients[i].a1, target_quotients[i].a1, remaining);
        active_quotients[i].a2 = ramp_coeff_precise(active_quotients[i].a2, target_quotients[i].a2, remaining);
    }
}
#endif

#ifdef FAST
static Bool loudness_coeffs_converged_fast(void) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        if (active_quotients[i].b0 != target_quotients[i].b0 ||
            active_quotients[i].b1 != target_quotients[i].b1 ||
            active_quotients[i].b2 != target_quotients[i].b2 ||
            active_quotients[i].a1 != target_quotients[i].a1 ||
            active_quotients[i].a2 != target_quotients[i].a2) {
            return FALSE;
        }
    }
    return TRUE;
}

static void loudness_ramp_blend_step_fast(uint32_t remaining) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        active_quotients[i].b0 = ramp_coeff_fast(active_quotients[i].b0, target_quotients[i].b0, remaining);
        active_quotients[i].b1 = ramp_coeff_fast(active_quotients[i].b1, target_quotients[i].b1, remaining);
        active_quotients[i].b2 = ramp_coeff_fast(active_quotients[i].b2, target_quotients[i].b2, remaining);
        active_quotients[i].a1 = ramp_coeff_fast(active_quotients[i].a1, target_quotients[i].a1, remaining);
        active_quotients[i].a2 = ramp_coeff_fast(active_quotients[i].a2, target_quotients[i].a2, remaining);
    }
}
#endif

#if !defined(USBSTATISTICS_DISABLE)
#include "stats_telemetry.h"

static void loudness_record_event_tag(U8 tag, U8 arg0, U8 arg1, U8 arg2)
{
    audio_stats_record_event(get_usb_stats(), tag, arg0, arg1, arg2);
}

static void loudness_record_equalizer_step_switch_event(int32_t prev_db_spl, int32_t db_spl, int equalizer_step)
{
    if (prev_db_spl != db_spl) {
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

void loudness_coeff_ramp_step(void) {
    uint32_t remaining = coeff_ramp_remaining;
    if (remaining == 0) {
        return;
    }

    if (active_quotients == current_quotients) {
        int i;
        for (i = 0; i < LOUDNESS_FILTERS; i++) {
            coeff_buf_ramp_blend[i] = current_quotients[i];
        }
        active_quotients = coeff_buf_ramp_blend;
    }

#ifdef PRECISE
    loudness_ramp_blend_step_precise(remaining);
#else
    loudness_ramp_blend_step_fast(remaining);
#endif

    loudness_quotients = active_quotients;
    remaining--;

    if (remaining == 0) {
#ifdef PRECISE
        Bool converged = loudness_coeffs_converged_precise();
#else
        Bool converged = loudness_coeffs_converged_fast();
#endif
        if (!converged) {
            remaining = 1;
        } else {
            taskENTER_CRITICAL();
#ifdef PRECISE
            {
                biquad_quotients_precise_t *swap_tmp = current_quotients;
                current_quotients = target_quotients;
                target_quotients = swap_tmp;
                active_quotients = current_quotients;
                loudness_quotients = current_quotients;
            }
#else
            {
                biquad_quotients_fast_t *swap_tmp = current_quotients;
                current_quotients = target_quotients;
                target_quotients = swap_tmp;
                active_quotients = current_quotients;
                loudness_quotients = current_quotients;
            }
#endif
            remaining = 0;
            taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
            loudness_record_event_tag(USB_STATS_TAG_RAMP_COMPLETE, 0, 0, 0);
#endif
        }
    }

    coeff_ramp_remaining = remaining;
}

/* Apply the speaker volume to the feed-forward (b*) coefficients in Q61. */
static inline S64 apply_volume_q61(S64 coeff)
{
#if defined(_MSC_VER)
    return (S64)round((double)coeff * (SPEAKER_VOLUME_Q61 / (double)LOUDNESS_Q61_ONE));
#elif defined(HAS_INT128)
    __int128 result = (__int128)coeff * SPEAKER_VOLUME_Q61;
    return (S64)(result >> 61);
#else
    return mul_shift_q61(coeff, SPEAKER_VOLUME_Q61);
#endif
}

static int loudness_get_equalizer_step(int32_t db_spl) {
    if (db_spl < 55) {
        return 0;
    } else if (db_spl < 65) {
        return 1;
    } else if (db_spl < 75) {
        return 2;
    } else {
        return 3;
    }
}

#ifdef PRECISE
static void loudness_set_target_from_equalizer_step_precise(int equalizer_step) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_precise_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q61(src->b0);
        int64_t b1 = apply_volume_q61(src->b1);
        int64_t b2 = apply_volume_q61(src->b2);

        target_quotients[i].b0 = b0;
        target_quotients[i].b1 = b1;
        target_quotients[i].b2 = b2;
        target_quotients[i].a0 = src->a0;
        target_quotients[i].a1 = src->a1;
        target_quotients[i].a2 = src->a2;
    }
}

static void loudness_commit_coefficients_precise(void) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        current_quotients[i] = target_quotients[i];
    }
    active_quotients = current_quotients;
    loudness_quotients = current_quotients;
}

static void loudness_select_equalizer_step_precise(int32_t db_spl, int equalizer_step, Bool immediate) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    loudness_set_target_from_equalizer_step_precise(equalizer_step);

    taskENTER_CRITICAL();
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
    if (immediate) {
        loudness_commit_coefficients_precise();
        coeff_ramp_remaining = 0;
    } else {
        uint32_t ramp = loudness_coeff_ramp_length();
        if (ramp == 0) {
            loudness_commit_coefficients_precise();
            coeff_ramp_remaining = 0;
        } else {
            coeff_ramp_remaining = ramp;
        }
    }
    taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}
#endif


#ifdef FAST
/* Unity gain on b* taps; host volume is applied per-sample via loudness_set_volume(). */
static inline int64_t apply_volume_q29(int64_t coeff) {
    (void)SPEAKER_VOLUME_Q30;
    return coeff;
}
#endif

#ifdef FAST
static void loudness_set_target_from_equalizer_step_fast(int equalizer_step) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_fast_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q29(src->b0);
        int64_t b1 = apply_volume_q29(src->b1);
        int64_t b2 = apply_volume_q29(src->b2);

        target_quotients[i].b0 = (int32_t)b0;
        target_quotients[i].b1 = (int32_t)b1;
        target_quotients[i].b2 = (int32_t)b2;
        target_quotients[i].a0 = src->a0;
        target_quotients[i].a1 = src->a1;
        target_quotients[i].a2 = src->a2;
    }
}

static void loudness_commit_coefficients_fast(void) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        current_quotients[i] = target_quotients[i];
    }
    active_quotients = current_quotients;
    loudness_quotients = current_quotients;
}

static void loudness_select_equalizer_step_fast(int32_t db_spl, int equalizer_step, Bool immediate) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    loudness_set_target_from_equalizer_step_fast(equalizer_step);

    taskENTER_CRITICAL();
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
    if (immediate) {
        loudness_commit_coefficients_fast();
        coeff_ramp_remaining = 0;
    } else {
        uint32_t ramp = loudness_coeff_ramp_length();
        if (ramp == 0) {
            loudness_commit_coefficients_fast();
            coeff_ramp_remaining = 0;
        } else {
            coeff_ramp_remaining = ramp;
        }
    }
    taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}
#endif

static void loudness_select_equalizer_step(int32_t db_spl, Bool immediate) {
    int equalizer_step = loudness_get_equalizer_step(db_spl);
#ifdef PRECISE
    loudness_select_equalizer_step_precise(db_spl, equalizer_step, immediate);
#endif
#ifdef FAST
    loudness_select_equalizer_step_fast(db_spl, equalizer_step, immediate);
#endif
}

#ifdef FREERTOS_USED
xQueueHandle xLoudnessFreqQueue = NULL;

static portTickType loudness_task_delay_20ms(void)
{
    if (portTICK_RATE_MS == 0) {
        return 1;
    }
    {
        portTickType delay = (portTickType)(20 / (portTICK_RATE_MS ? portTICK_RATE_MS : 1));
        return (delay == 0) ? 1 : delay;
    }
}

/* Background task to handle equalizer-step selection */
static void loudness_update_filter_by_volume_or_frequency(void *pvParameters)
{
    (void)pvParameters;

    portTickType xLastWakeTime;
    portTickType xDelay20ms;
    U32 target_frequency;

    xDelay20ms = loudness_task_delay_20ms();
    xLastWakeTime = xTaskGetTickCount();
    loudness_task_ready = TRUE;

    while (TRUE)
    {
        if (xLoudnessFreqQueue != NULL) {
            if (xQueueReceive(xLoudnessFreqQueue, &target_frequency, xDelay20ms) == pdPASS) {
                loudness_change_frequency(target_frequency);
            }
        } else {
            vTaskDelay(xDelay20ms);
            continue;
        }

        loudness_update_active_equalizer_step();
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
    if (xLoudnessFreqQueue == NULL) {
        return;
    }

    (void)xQueueSend(xLoudnessFreqQueue, &frequency, 0);
}
#endif

/**
 * @brief Compute blended listening level in dB SPL (phon estimate).
 *
 * Clamps measured track dBFS to [LOUDNESS_TRACK_DBFS_MIN, LOUDNESS_TRACK_DBFS_MAX].
 * Normalizes track level relative to -18 dBFS (uncompressed master reference): a
 * brickwalled -6 dBFS track yields +12 dB offset. Blends host gain (90%) with
 * normalized track level (10%) at 0.1 dB resolution. Symmetric round-half-up is
 * applied for negative blends: (blended_scaled_x100 - 50) / 100. Adds
 * LOUDNESS_REF_DB_SPL (80) and rounds to integer dB SPL.
 *
 * @return Blended dB SPL (~53..81 for typical volume/RMS combinations).
 */
static int32_t loudness_calculate_db_spl(void) {
    int32_t track_dbfs = loudness_get_track_dbfs();
    int32_t track_normalized = track_dbfs - LOUDNESS_TRACK_DBFS_MIN;
    int32_t host_gain_dbfs = loudness_get_gain_dbfs();
    int32_t host_gain_scaled = host_gain_dbfs * 10;
    int32_t track_scaled = track_normalized * 10;

    int32_t blended_scaled_x100 = (host_gain_scaled * LOUDNESS_GAIN_WEIGHT_PCT) +
                                  (track_scaled * LOUDNESS_TRACK_WEIGHT_PCT);

    int32_t blended_dbfs_x10 = (blended_scaled_x100 - 50) / 100;
    int32_t db_spl_x10 = blended_dbfs_x10 + (LOUDNESS_REF_DB_SPL * 10);
    return (db_spl_x10 + 5) / 10;
}

void loudness_update_active_equalizer_step(void) {
    int32_t db_spl = loudness_calculate_db_spl();
    int32_t current_last = (int32_t)last_db_spl;
    Bool db_spl_changed;

#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif

    /* Add 2 dB hysteresis to avoid frequent step switching and coefficient ramping
     * when phon level flutters due to small track RMS or volume changes.
     * Hysteresis is disabled in unit tests for precise validation. */
#ifndef BUILD_TESTING
    int32_t diff = db_spl - current_last;
    if (diff >= 2 || diff <= -2) {
        db_spl_changed = TRUE;
    } else {
        db_spl_changed = FALSE;
    }
#else
    db_spl_changed = (db_spl != current_last);
#endif

    if (db_spl_changed) {
        loudness_select_equalizer_step(db_spl, FALSE);
    }
#if !defined(USBSTATISTICS_DISABLE)
    {
        int32_t track_rms_dbfs = loudness_get_track_rms_dbfs();
        int32_t track_dbfs = loudness_get_track_dbfs();

        stats_telemetry_set_track_levels(
            loudness_clamp_s8(track_dbfs),
            loudness_clamp_s8(track_rms_dbfs));
        stats_telemetry_set_gain_dbfs(
            loudness_clamp_s8(loudness_get_gain_dbfs()));
    }
#endif
}

/**
 * @brief Return the current dBFS estimate derived from the running RMS.
 * @return A non-positive signed value.
 */
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

int32_t loudness_get_db_spl(void) {
    return loudness_calculate_db_spl();
}

/**
 * @brief Compute the current host-gain expressed in dBFS.
 * spk_vol_usb_L and VOL_MAX are 16-bit signed values in 1/256 dB units.
 */
int32_t loudness_get_gain_dbfs(void) {
    int32_t delta_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t db_fs = delta_q8 / 256;
    if (db_fs > 0) {
        db_fs = 0;
    }
    return db_fs;
}

int16_t loudness_get_last_db_spl(void) {
    return last_db_spl;
}

/* Select the active equalizer step based on a weighted blend of:
 *   - the host gain (95%), taken from spk_vol_usb_L relative to VOL_MAX
 *   - the measured track dBFS from the RMS estimator (5%)
 * The db_fs argument is treated as an externally supplied gain value; if
 * caller-supplied is > 0 it is clamped to 0. */
void loudness_set_level_dbfs(int32_t db_fs) {
    if (db_fs > 0) {
        db_fs = 0;
    }

    target_db_fs = (int16_t)db_fs;

    /* Perform the update using the shared logic. */
    loudness_update_active_equalizer_step();
}

void loudness_filter_init(void) {
#ifdef FREERTOS_USED
    if (loudness_state_initialized) {
        return;
    }
#endif

    loudness_reset_rms();

    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].xn_1 = 0;
        loudness_states[i].xn_2 = 0;
        loudness_states[i].yn_1 = 0;
        loudness_states[i].yn_2 = 0;

        coeff_buf_endpoint_ping[i].b0 = 0;
        coeff_buf_endpoint_ping[i].b1 = 0;
        coeff_buf_endpoint_ping[i].b2 = 0;
        coeff_buf_endpoint_ping[i].a0 = 0;
        coeff_buf_endpoint_ping[i].a1 = 0;
        coeff_buf_endpoint_ping[i].a2 = 0;

        coeff_buf_endpoint_pong[i].b0 = 0;
        coeff_buf_endpoint_pong[i].b1 = 0;
        coeff_buf_endpoint_pong[i].b2 = 0;
        coeff_buf_endpoint_pong[i].a0 = 0;
        coeff_buf_endpoint_pong[i].a1 = 0;
        coeff_buf_endpoint_pong[i].a2 = 0;

        coeff_buf_ramp_blend[i].b0 = 0;
        coeff_buf_ramp_blend[i].b1 = 0;
        coeff_buf_ramp_blend[i].b2 = 0;
        coeff_buf_ramp_blend[i].a0 = 0;
        coeff_buf_ramp_blend[i].a1 = 0;
        coeff_buf_ramp_blend[i].a2 = 0;
    }

    current_quotients = coeff_buf_endpoint_ping;
    target_quotients = coeff_buf_endpoint_pong;
    active_quotients = coeff_buf_endpoint_ping;
    loudness_quotients = coeff_buf_endpoint_ping;

    /* Start at the reference equalizer step (flat) -- snap immediately, no ramp from zero. */
    loudness_select_equalizer_step((int32_t)LOUDNESS_REF_PHON, TRUE);

#ifdef FREERTOS_USED
    loudness_state_initialized = TRUE;
#endif
}

/* ---------------------------------------------------------------------------
 * Public per-sample entry points
 * -------------------------------------------------------------------------*/
#ifdef PRECISE
int64_t loudness_precise_24bit(int64_t sample)
{
    loudness_update_track_level_precise(sample);
    for (int i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_precise_24bit(sample, &loudness_states[i]);
    }
    return sample;
}
#endif

#ifdef FAST
int64_t loudness_fast_24bit(int32_t sample)
{
    loudness_update_track_level_fast(sample);
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_fast_32bit(sample, &loudness_states[i]);
    }
    return (S64)saturate_24bit_s32_to_s32(sample);
}
#endif

#endif /* LOUDNESS_DISABLE */

/**
 * @brief First-order error-feedback quantizer for 24-bit DAC output.
 *
 * Shapes truncation noise when converting the internal high-resolution sample
 * (post-filter/volume, 32-bit with 8 fractional bits) to the 24-bit word sent
 * to the DAC. Subtracts the stored per-channel error in 64-bit, quantizes with
 * DOWNSAMPLE_24BIT_ROUND (round-half-up >> 8), reconstructs the truncated
 * value, and stores the quantization error for the next sample. Final output is
 * clamped to [INT24_MIN, INT24_MAX].
 */
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

#ifndef LOUDNESS_DISABLE

void loudness_reset_rms(void)
{
    // seeding the integrator to a typical master signal (-6 dBFS)
    // a rms value of -6 dBFS in 24-bit is 4194394
    loudness_rms_write_begin();
    root_mean_square = 17592186044416ULL;
    loudness_rms_write_end();
}

#if defined(PRECISE)
/**
 * @brief Scale a biquad coefficient set designed for fs to a new set for fs' = n * fs.
 *
 * This approach is called an Agnostic Bilinear Frequency Scaling. It converts the digital coefficients into the analog s-domain, scales the frequency axis, and maps them back to the new z-domain.
 */
static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type; // Agnostic approach doesn't need filter_type
    if (n <= 1 || n > 4) {
        return base;
    }

    // 1. Unpack og normaliser til double
    double b0 = (double)base.b0 / (double)LOUDNESS_Q61_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q61_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q61_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q61_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q61_ONE;

    // 2. Finn den opprinnelige digitale vinkelfrekvensen w0 fra a1 og a2
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);

    // 3. Beregn det eksakte forholdet for den bilinære pre-warping-skaleringen
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);

    // 4. Invers Bilinær Transformasjon (Hent ut analoge koeffisienter)
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }

    // Analoge teller-koeffisienter
    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;

    // Analoge nevner-koeffisienter
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;

    // 5. Skaler de analoge koeffisientene direkte med k
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;

    // 6. Bilinær transformasjon tilbake til det nye z-domenet
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }

    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;

    // 7. Pakk tilbake til Q61 fixed-point
    biquad_quotients_precise_t result;
    result.b0 = (int64_t)round(b0_n * (double)LOUDNESS_Q61_ONE);
    result.b1 = (int64_t)round(b1_n * (double)LOUDNESS_Q61_ONE);
    result.b2 = (int64_t)round(b2_n * (double)LOUDNESS_Q61_ONE);
    result.a0 = (int64_t)LOUDNESS_Q61_ONE;
    result.a1 = (int64_t)round(a1_n * (double)LOUDNESS_Q61_ONE);
    result.a2 = (int64_t)round(a2_n * (double)LOUDNESS_Q61_ONE);

    return result;
}
#endif

#if defined(FAST)
/**
 * @brief Scale a biquad coefficient set designed for fs to a new set for fs' = n * fs.
 */
static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type; // Agnostic approach doesn't need filter_type
    if (n <= 1 || n > 4) {
        return base;
    }

    // 1. Unpack og normaliser til double
    double b0 = (double)base.b0 / (double)LOUDNESS_Q29_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q29_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q29_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q29_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q29_ONE;

    // 2. Finn den opprinnelige digitale vinkelfrekvensen w0 fra a1 og a2
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);

    // 3. Beregn det eksakte forholdet for den bilinære pre-warping-skaleringen
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);

    // 4. Invers Bilinær Transformasjon
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }

    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;

    // 5. Skaler de analoge koeffisientene
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;

    // 6. Bilinær transformasjon tilbake
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }

    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;

    // 7. Pakk tilbake til Q29
    biquad_quotients_fast_t result;
    result.b0 = (int32_t)round(b0_n * (double)LOUDNESS_Q29_ONE);
    result.b1 = (int32_t)round(b1_n * (double)LOUDNESS_Q29_ONE);
    result.b2 = (int32_t)round(b2_n * (double)LOUDNESS_Q29_ONE);
    result.a0 = (int32_t)536870912LL;
    result.a1 = (int32_t)round(a1_n * (double)LOUDNESS_Q29_ONE);
    result.a2 = (int32_t)round(a2_n * (double)LOUDNESS_Q29_ONE);

    return result;
}
#endif

#if defined(PRECISE)
void loudness_change_frequency_precise(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    uint32_t n = 1;
    const biquad_quotients_precise_t (*base_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

    if (frequency % 48000 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / 48000;
    } else if (frequency % 44100 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / 44100;
    }

    if (n > 4) {
        n = 4;
    }

    if (n > 1) {
        int b, f;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            for (f = 0; f < LOUDNESS_FILTERS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_precise(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        active_equalizer_step_table = (const biquad_quotients_precise_t (*)[LOUDNESS_FILTERS])(void*)loudness_quotients_scaled;
    } else {
        active_equalizer_step_table = base_table;
    }

    // Recalculate target quotients for the current equalizer step and ramp
#ifdef FREERTOS_USED
    int32_t db_spl_val = (int32_t)target_db_spl;
#else
    int32_t db_spl_val = (int32_t)last_db_spl;
#endif
    int equalizer_step = loudness_get_equalizer_step(db_spl_val);
    loudness_set_target_from_equalizer_step_precise(equalizer_step);

    taskENTER_CRITICAL();
    {
        uint32_t ramp = loudness_coeff_ramp_length();
        if (ramp == 0) {
            loudness_commit_coefficients_precise();
            coeff_ramp_remaining = 0;
        } else {
            coeff_ramp_remaining = ramp;
        }
    }
    taskEXIT_CRITICAL();
}
#endif

#if defined(FAST)
void loudness_change_frequency_fast(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    uint32_t n = 1;
    const biquad_quotients_fast_t (*base_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

    if (frequency % 48000 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / 48000;
    } else if (frequency % 44100 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / 44100;
    }

    if (n > 4) {
        n = 4;
    }

    if (n > 1) {
        int b, f;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            for (f = 0; f < LOUDNESS_FILTERS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_fast(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        active_equalizer_step_table = (const biquad_quotients_fast_t (*)[LOUDNESS_FILTERS])(void*)loudness_quotients_scaled;
    } else {
        active_equalizer_step_table = base_table;
    }

    // Recalculate target quotients for the current equalizer step and ramp
#ifdef FREERTOS_USED
    int32_t db_spl_val = (int32_t)target_db_spl;
#else
    int32_t db_spl_val = (int32_t)last_db_spl;
#endif
    int equalizer_step = loudness_get_equalizer_step(db_spl_val);
    loudness_set_target_from_equalizer_step_fast(equalizer_step);

    taskENTER_CRITICAL();
    {
        uint32_t ramp = loudness_coeff_ramp_length();
        if (ramp == 0) {
            loudness_commit_coefficients_fast();
            coeff_ramp_remaining = 0;
        } else {
            coeff_ramp_remaining = ramp;
        }
    }
    taskEXIT_CRITICAL();
}
#endif

#else /* LOUDNESS_DISABLE */

void loudness_filter_init(void) {}

void loudness_reset_rms(void) {}

void loudness_set_level_dbfs(int32_t db_fs) {
    (void)db_fs;
}

void loudness_update_active_equalizer_step(void) {}

int16_t loudness_get_last_db_spl(void) {
    return LOUDNESS_REF_PHON;
}

int32_t loudness_get_track_rms_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

int32_t loudness_get_track_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

int32_t loudness_get_db_spl(void) {
    return LOUDNESS_REF_PHON;
}

int32_t loudness_get_gain_dbfs(void) {
    int32_t delta_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t db_fs = delta_q8 / 256;
    if (db_fs > 0) {
        db_fs = 0;
    }
    return db_fs;
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
