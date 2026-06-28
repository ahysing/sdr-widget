#include "loudness.h"
#include <stdint.h>
#include <limits.h>

typedef struct {
    U64 w0;
    U64 w1;
} biquad_state_t;

typedef struct {
    U64 b0;
    U64 b1;
    U64 b2;
} biquad_quotients_t;

#define SAMPLE_16BITS 16
#define SAMPLE_24BITS 24
#define LOUDNESS_FILTERS 4

biquad_quotients_t loudness_quotients[LOUDNESS_FILTERS] = {};
biquad_state_t loudness_states[LOUDNESS_FILTERS] = {};



// roughly equal to log10(2) * 20 = 6.020599....
#define SINGLE_BIT_AMPLITUDE 6

/* Use fixed point big-counting to calculate the Root mean square of the sample
 * This gives an estimate of the number of decibels the music plays at.
 */
U32 calculate_dB_24bit(U32 rms)
{
    if (rms == 0)
        return 0;
    
    U32 leading_zeros = __builtin_clz(rms);
    S32 bit_position = 32 - leading_zeros;
    S32 db_estimate = (bit_position - SAMPLE_24BITS) * SINGLE_BIT_AMPLITUDE;
    S32 db = db_estimate;
    if (db > 0)
        db = 0;
    return (U32)db;
}

/* Use an leaky integrator to aggragate the root means error */
uint64_t rms_square = 0;
U8 rms_counter = 0;
#define RMS_WINDOW 512

// En lynrask digital kvadratrot-algoritm (Bakhshali/Bitwise-metode) for heltall
uint32_t sqrt_i(uint64_t tall) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62; // Start med den høyeste biten en 64-bit kan ha

    // Finn starten på bit-stigen
    while (bit > tall) {
        bit >>= 2;
    }

    // Finn kvadratroten bit for bit
    while (bit != 0) {
        if (tall >= res + bit) {
            tall -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

U32 update_rms(U32 sample) {
    if (sample < 0)
        sample = -sample;

    uint64_t sample_u64 = (uint64_t)sample;
    sample_u64 *= sample_u64;
    if (rms_counter < RMS_WINDOW)
    {
        rms_counter ++;    

        rms_square = (((RMS_WINDOW - 1) * rms_square) + sample_u64) / RMS_WINDOW;
        return sqrt_i(rms_square);
    } else {
        rms_square = (((RMS_WINDOW - 1) * rms_square) + sample_u64) / RMS_WINDOW;
        return sqrt_i(rms_square);
    }
}


U64 process_biquad(U64 sample, biquad_quotients_t* quotients)
{
    return sample;
}

void loudness_init()
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i ++)
    {
        loudness_states[i].w0 = 0;
        loudness_states[i].w1 = 0;
    }
}

U64 loudness(U64 sample)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i ++)
    {
        sample = process_biquad(sample, &loudness_quotients[i]);
    }
    return sample;
}

void dsp_init()
{
    loudness_init();
    rms_square = 0;
    rms_counter = 0;
}

void dsp_24bit()
{
    /*
    U32 sample_raw = audio_buffer_0[i++];
    U64 sample = UPSAMPLE_24BIT(sample_raw);
    loudness(sample);
    sample_raw = DOWNSAMPLE_24BIT(sample);
    */
}
