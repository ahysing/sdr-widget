#include "loudness_inferred_gain.h"

#ifndef LOUDNESS_DISABLE

#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
#include "../tests/pc/device_audio_volume.h"
#else
#include "usb_specific_request.h"
#include "device_audio_task.h"
#endif
#include "loudness.h"
#include "loudness_internal.h"

/*
 * Peak-tracking leaky-integrator time constants.
 *
 * The samples are processed per channel, so the effective update rate is
 * f = 2 * sample_rate (88 200 Hz at 44.1 kHz stereo, 96 000 Hz at 48 kHz).
 * For an integer leaky integrator the time constant is
 *
 *     tau ~= 2^shift / f
 *
 * The base shifts below are anchored at 44.1 kHz stereo (f = 88 200 Hz):
 *
 *     short attack   >> 12  -> 2^12 / 88200  ~= 46 ms   (catch peaks/volume-up)
 *     short release  >> 16  -> 2^16 / 88200  ~= 740 ms  (hold between beats)
 *     long slow      >> 20  -> 2^20 / 88200  ~= 11.8 s  (musical loudness memory)
 *     long fast      >> 12  -> 2^12 / 88200  ~= 46 ms   (collapse on volume-down)
 *
 * 16-bit and 24-bit sources share these SAME shifts (hence the same physical
 * delays). To keep the delays identical, both formats are normalised into a
 * common 24-bit magnitude domain before integration
 * (see loudness_inferred_gain_sample_to_24bit): 16-bit samples are scaled up
 * by << 8, 24-bit samples down by >> 8. The only per-format difference is that
 * input-normalisation shift; the integrator resolution (LSB = 2^shift in the
 * 24-bit domain) is then identical for both, so a 16-bit source does not stall
 * the 11.8 s decay the way a native 16-bit accumulator (max 2^15) would.
 */
#define LOUDNESS_GAIN_REF_HZ                   44100U
#define LOUDNESS_GAIN_SHORT_ATTACK_SHIFT_BASE  12
#define LOUDNESS_GAIN_SHORT_RELEASE_SHIFT_BASE 16
#define LOUDNESS_GAIN_LONG_FAST_SHIFT_BASE     12
#define LOUDNESS_GAIN_LONG_SLOW_SHIFT_BASE     20
#define LOUDNESS_GAIN_SHIFT_MAX                30
#define LOUDNESS_PEAK_DBFS_BITS                23
#define LOUDNESS_DBFS_MIN                      (-144)

static volatile Bool source_has_volume_control = FALSE;
static volatile uint32_t gain_short_memory[LOUDNESS_CHANNELS];
static volatile uint32_t gain_long_memory[LOUDNESS_CHANNELS];
static volatile uint32_t gain_track_frequency_hz = 0;
static volatile int gain_short_attack_shift = LOUDNESS_GAIN_SHORT_ATTACK_SHIFT_BASE;
static volatile int gain_short_release_shift = LOUDNESS_GAIN_SHORT_RELEASE_SHIFT_BASE;
static volatile int gain_long_fast_shift = LOUDNESS_GAIN_LONG_FAST_SHIFT_BASE;
static volatile int gain_long_slow_shift = LOUDNESS_GAIN_LONG_SLOW_SHIFT_BASE;

static int32_t loudness_peak_magnitude_to_dbfs(uint32_t magnitude)
{
    if (magnitude == 0)
        return LOUDNESS_DBFS_MIN; 
    
    uint32_t leading_zeros = CLZ(magnitude);
    int32_t bit_position = 32 - (int32_t)leading_zeros;
    uint32_t fraction = 0;
    uint32_t shift = leading_zeros + 1;
    if (bit_position < 32) {
        if (shift < 32)
            fraction = (magnitude << shift) >> 24;
        else
            fraction = 0;
    }

    int32_t db_base = (bit_position - LOUDNESS_PEAK_DBFS_BITS) * 6;
    int32_t db_fraction = (int32_t)((fraction * 6) >> 8);
    int32_t db = db_base + db_fraction;

    return (db > 0) ? 0 : db;
}

/*
 * Preserve tau = 2^shift / f across sample rates by adding one shift bit per
 * octave above the anchor band. The anchor band [REF, 2*REF) spans both 44.1
 * and 48 kHz (and their 2x/4x multiples), so those neighbouring rates share the
 * same tier instead of being split a full bit apart by a plain doubling test.
 */
static int loudness_gain_shift_for_rate(int base_shift, uint32_t frequency)
{
    uint32_t fs = frequency;
    int shift = base_shift;

    if (fs == 0) {
        return base_shift;
    }
    while (fs >= (LOUDNESS_GAIN_REF_HZ * 2U)) {
        fs >>= 1;
        shift++;
    }
    while (fs < LOUDNESS_GAIN_REF_HZ) {
        fs <<= 1;
        shift--;
    }
    if (shift < 1) {
        shift = 1;
    }
    if (shift > LOUDNESS_GAIN_SHIFT_MAX) {
        shift = LOUDNESS_GAIN_SHIFT_MAX;
    }
    return shift;
}

void loudness_inferred_gain_set_rate(uint32_t frequency_hz)
{
    gain_track_frequency_hz = frequency_hz;
    if (frequency_hz == 0) {
        gain_short_attack_shift = LOUDNESS_GAIN_SHORT_ATTACK_SHIFT_BASE;
        gain_short_release_shift = LOUDNESS_GAIN_SHORT_RELEASE_SHIFT_BASE;
        gain_long_fast_shift = LOUDNESS_GAIN_LONG_FAST_SHIFT_BASE;
        gain_long_slow_shift = LOUDNESS_GAIN_LONG_SLOW_SHIFT_BASE;
        return;
    }

    gain_short_attack_shift = loudness_gain_shift_for_rate(
        LOUDNESS_GAIN_SHORT_ATTACK_SHIFT_BASE, frequency_hz);
    gain_short_release_shift = loudness_gain_shift_for_rate(
        LOUDNESS_GAIN_SHORT_RELEASE_SHIFT_BASE, frequency_hz);
    gain_long_fast_shift = loudness_gain_shift_for_rate(
        LOUDNESS_GAIN_LONG_FAST_SHIFT_BASE, frequency_hz);
    gain_long_slow_shift = loudness_gain_shift_for_rate(
        LOUDNESS_GAIN_LONG_SLOW_SHIFT_BASE, frequency_hz);
}

void loudness_inferred_gain_reset(void)
{
    int channel;

    for (channel = 0; channel < LOUDNESS_CHANNELS; channel++) {
        gain_short_memory[channel] = 0;
        gain_long_memory[channel] = 0;
    }
    loudness_inferred_gain_set_rate(current_freq.frequency);
}

/*
 * Normalise a raw container sample into the common 24-bit magnitude domain used
 * by the peak integrators. This is the only place the 16-bit and 24-bit paths
 * differ: the 16-bit source is left-shifted (<< 8) so it occupies the same range
 * as the 24-bit source (>> 8 from the 32-bit container). Both then feed the same
 * shift constants, giving identical time constants for both formats.
 */
static int32_t loudness_inferred_gain_sample_to_24bit(int32_t sample, Bool is_16bit_container)
{
    if (is_16bit_container) {
        /* 16-bit path: extract the 16-bit sample, scale up into 24-bit range. */
        sample = (int32_t)(int16_t)(sample >> 16);
        sample <<= 8;
    } else {
        /* 24-bit path: 24-bit sample sits in the top of the 32-bit container. */
        sample >>= 8;
    }
    return sample;
}

static uint32_t loudness_inferred_gain_magnitude(int32_t sample)
{
    if (sample < 0) {
        sample = -sample;
    }
    return (uint32_t)sample;
}

static uint32_t loudness_gain_leak_down(uint32_t high, uint32_t low, int shift)
{
    uint32_t delta = (high - low) >> shift;
    return (high > delta) ? (high - delta) : low;
}

/* Update short long peak time from instant_sample_peak (positive magnitude). */
static void loudness_combined_context_loop(uint32_t instant_sample_peak, int channel)
{
    int attack_shift = gain_short_attack_shift;
    int release_shift = gain_short_release_shift;
    int fast_shift = gain_long_fast_shift;
    int slow_shift = gain_long_slow_shift;
    uint32_t short_mem = gain_short_memory[channel];
    uint32_t long_mem = gain_long_memory[channel];

    if (instant_sample_peak > short_mem) {
        short_mem += (instant_sample_peak - short_mem) >> attack_shift;
    } else {
        uint32_t delta = (short_mem - instant_sample_peak) >> release_shift;
        short_mem = (short_mem > delta) ? (short_mem - delta) : instant_sample_peak;
    }

    if (short_mem > long_mem) {
        long_mem = short_mem;
    } else if (short_mem < (long_mem >> 2)) {
        long_mem = loudness_gain_leak_down(long_mem, short_mem, fast_shift);
    } else {
        long_mem = loudness_gain_leak_down(long_mem, short_mem, slow_shift);
    }

    gain_short_memory[channel] = short_mem;
    gain_long_memory[channel] = long_mem;
}

static uint32_t loudness_get_active_loudness_level(int channel)
{
    return gain_long_memory[channel];
}

int32_t loudness_inferred_gain_dbfs_from_magnitude(uint32_t mag)
{
    if (mag > (uint32_t)INT24_MAX) {
        mag = (uint32_t)INT24_MAX;
    }
    int32_t inferred_dbfs = loudness_peak_magnitude_to_dbfs(mag);
    if (inferred_dbfs < -60)
    {
        return -60;
    }
    return inferred_dbfs;
}

static void envelope_follower_update_sample(int32_t sample, Bool is_16bit_container,
    int channel)
{
    uint32_t mag;

    if (gain_track_frequency_hz == 0 || channel < 0
        || channel >= LOUDNESS_CHANNELS) {
        return;
    }

    sample = loudness_inferred_gain_sample_to_24bit(sample, is_16bit_container);
    mag = loudness_inferred_gain_magnitude(sample);
    loudness_combined_context_loop(mag, channel);
}

Bool loudness_inferred_gain_has_source_volume_control(void)
{
    return source_has_volume_control;
}

void loudness_set_source_has_volume_control(void)
{
    if (!source_has_volume_control) {
        source_has_volume_control = TRUE;
        loudness_inferred_gain_reset();
        loudness_refresh_quotient_table_selection();
#ifdef FEATURE_VOLUME_CTRL
        device_audio_set_volume_in_biquad(
            TRUE,
            loudness_active_filter() != FILTER_OFF_MODE);
#endif
    }
}

void loudness_envelope_follower_update_stereo(int32_t sample_L, int32_t sample_R)
{
    Bool is_16bit_container = (usb_alternate_setting_out == 0x02);

    envelope_follower_update_sample(sample_L, is_16bit_container, 0);
    envelope_follower_update_sample(sample_R, is_16bit_container, 1);
}

#ifdef BUILD_TESTING
void loudness_test_reset_inferred_gain(void)
{
    source_has_volume_control = FALSE;
    loudness_inferred_gain_reset();
#ifdef FEATURE_VOLUME_CTRL
    device_audio_set_volume_in_biquad(
        FALSE,
        loudness_active_filter() != FILTER_OFF_MODE);
#endif
}

void loudness_test_set_short_memory(uint32_t value)
{
    gain_short_memory[0] = value;
    gain_short_memory[1] = value;
}

void loudness_test_set_long_memory(uint32_t value)
{
    gain_long_memory[0] = value;
    gain_long_memory[1] = value;
}

uint32_t loudness_test_get_short_memory(void)
{
    return gain_short_memory[0];
}

uint32_t loudness_test_get_long_memory(void)
{
    return gain_long_memory[0];
}

void loudness_test_combined_context_loop(uint32_t instant_sample_peak)
{
    loudness_combined_context_loop(instant_sample_peak, 0);
}

uint32_t loudness_test_get_active_loudness_level(void)
{
    return loudness_get_active_loudness_level(0);
}
#endif

#else /* LOUDNESS_DISABLE */

void loudness_set_source_has_volume_control(void) {}

void loudness_envelope_follower_update_stereo(int32_t sample_L, int32_t sample_R)
{
    (void)sample_L;
    (void)sample_R;
}

#endif /* LOUDNESS_DISABLE */
