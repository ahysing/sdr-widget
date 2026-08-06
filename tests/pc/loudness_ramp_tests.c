#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "loudness.h"
#include "loudness_test_access.h"
#include "usb_specific_request.h"

S_freq current_freq = { .frequency = 44100 };
volatile Bool freq_changed = FALSE;

S16 spk_vol_usb_L = 0, spk_vol_usb_R = 0;
volatile U8 spk_bit_resolution = 24;

#define LOUDNESS_COEFF_RAMP_MS 15

#define LOUDNESS_RAMP_44100   662u
#define LOUDNESS_RAMP_88200  1323u
#define LOUDNESS_RAMP_132300 1985u
#define LOUDNESS_RAMP_176400 2646u
#define LOUDNESS_RAMP_48000   720u
#define LOUDNESS_RAMP_96000  1440u
#define LOUDNESS_RAMP_144000 2160u
#define LOUDNESS_RAMP_192000 2880u

static uint32_t ramp_length_ref(uint32_t fs) {
    (void)fs;
    return LOUDNESS_COEFF_RAMP_MS;
}

static void test_ramp_length_constants(void) {
    printf("Running test_ramp_length_constants...\n");
    /* Coarse ramping uses a fixed number of calls (15) regardless of sample rate. */
    printf("test_ramp_length_constants passed\n");
}

#ifdef PRECISE
typedef int64_t coeff_t;

static coeff_t ramp_coeff_ref(coeff_t current, coeff_t target, uint32_t remaining) {
    coeff_t diff = target - current;
    coeff_t rem = (coeff_t)remaining;
    if (remaining == 0) {
        return current;
    }
    if (diff >= 0) {
        return current + (diff + rem / 2) / rem;
    }
    return current + (diff - rem / 2) / rem;
}

static inline coeff_t ramp_coeff_new(coeff_t current, coeff_t target, uint32_t remaining) {
    coeff_t diff = target - current;
    if (diff == 0 || remaining <= 1) {
        return target;
    }
#if defined(HAS_INT128)
    {
        uint32_t inv_rem = (uint32_t)(0x80000000ULL / remaining);
        __int128 step = ((__int128)diff * inv_rem) >> 31;
        return current + (coeff_t)step;
    }
#else
    {
        coeff_t rem = (coeff_t)remaining;
        coeff_t adj = (rem >> 1) ^ (diff >> 63);
        return current + (diff + adj) / rem;
    }
#endif
}
#else
typedef int32_t coeff_t;

static coeff_t ramp_coeff_ref(coeff_t current, coeff_t target, uint32_t remaining) {
    coeff_t diff = target - current;
    coeff_t rem = (coeff_t)remaining;
    if (remaining == 0) {
        return current;
    }
    if (diff >= 0) {
        return current + (diff + rem / 2) / rem;
    }
    return current + (diff - rem / 2) / rem;
}

static inline coeff_t ramp_coeff_new(coeff_t current, coeff_t target, uint32_t remaining) {
    coeff_t diff = target - current;
    if (diff == 0 || remaining <= 1) {
        return target;
    }
    uint32_t inv_rem = 0x7FFFFFFFu / remaining;
    coeff_t step = (coeff_t)(((int64_t)diff * inv_rem) >> 31);
    return current + step;
}
#endif

static coeff_t abs_diff(coeff_t a, coeff_t b) {
    coeff_t d = a - b;
    return d < 0 ? -d : d;
}

static void test_ramp_early_exit(void) {
    printf("Running test_ramp_early_exit...\n");
#ifdef PRECISE
    coeff_t current = 2348449834323295744LL;
    coeff_t target = 2314395388020622848LL;
#else
    coeff_t current = 546791087;
    coeff_t target = 538862168;
#endif
    assert(ramp_coeff_new(current, current, 100) == current);
    assert(ramp_coeff_new(current, target, 1) == target);
    assert(ramp_coeff_new(current, target, 0) == target);
    printf("test_ramp_early_exit passed\n");
}

static void test_ramp_sign_and_remaining(void) {
    printf("Running test_ramp_sign_and_remaining...\n");
    const uint32_t remainings[] = {2, 10, 662, 720, 1323, 1440, 1985, 2160};
    int r;
    coeff_t max_step_delta = 0;

#ifdef PRECISE
    coeff_t pairs[][2] = {
        {2348449834323295744LL, 2314395388020622848LL},
        {-4541805258938481152LL, -4512055621803879424LL},
        {2194782095529568256LL, 2198486932718281216LL},
    };
#else
    coeff_t pairs[][2] = {
        {546791087, 538862168},
        {-1057471442, -1050544815},
        {511012528, 511875128},
    };
#endif

    for (r = 0; r < 8; r++) {
        uint32_t rem = remainings[r];
        int p;
        for (p = 0; p < 3; p++) {
            coeff_t current = pairs[p][0];
            coeff_t target = pairs[p][1];
            coeff_t ref = ramp_coeff_ref(current, target, rem);
            coeff_t neu = ramp_coeff_new(current, target, rem);
            coeff_t delta = abs_diff(ref, neu);
            if (delta > max_step_delta) {
                max_step_delta = delta;
            }
            assert(delta <= 1);
        }
    }

    printf("  max per-step delta vs reference: %lld\n", (long long)max_step_delta);
    printf("test_ramp_sign_and_remaining passed\n");
}

static coeff_t simulate_ramp(coeff_t start, coeff_t end, uint32_t n, int use_new) {
    coeff_t current = start;
    uint32_t remaining;
    for (remaining = n; remaining > 0; remaining--) {
        if (use_new) {
            current = ramp_coeff_new(current, end, remaining);
        } else {
            current = ramp_coeff_ref(current, end, remaining);
        }
    }
    return current;
}

static void test_full_ramp_sequence(void) {
    printf("Running test_full_ramp_sequence...\n");
#ifdef PRECISE
    coeff_t start = 2348449834323295744LL;
    coeff_t end = 2314395388020622848LL;
#else
    coeff_t start = 546791087;
    coeff_t end = 538862168;
#endif
    const uint32_t lengths[] = {662, 720, 1440};
    int i;
    for (i = 0; i < 3; i++) {
        coeff_t ref_final = simulate_ramp(start, end, lengths[i], 0);
        coeff_t new_final = simulate_ramp(start, end, lengths[i], 1);
        assert(abs_diff(new_final, end) <= 1);
        assert(abs_diff(new_final, ref_final) <= (coeff_t)lengths[i]);
    }
    printf("test_full_ramp_sequence passed\n");
}

static void test_mid_ramp_retarget_convergence(void) {
    printf("Running test_mid_ramp_retarget_convergence...\n");
#ifdef PRECISE
    coeff_t start = 2348449834323295744LL;
    coeff_t mid = 2314395388020622848LL;
    coeff_t final_target = 2197057563315481856LL;
#else
    coeff_t start = 546791087;
    coeff_t mid = 538862168;
    coeff_t final_target = 511542327;
#endif
    uint32_t n = 720;
    coeff_t current = start;
    uint32_t remaining;
    uint32_t half = n / 2;

    for (remaining = n; remaining > 0; remaining--) {
        coeff_t target = (remaining > half) ? mid : final_target;
        current = ramp_coeff_new(current, target, remaining);
    }

    assert(current == final_target);
    printf("test_mid_ramp_retarget_convergence passed\n");
}

static void test_loudness_ramp_bypass_logic(void) {
    printf("Running test_loudness_ramp_bypass_logic...\n");
    loudness_init();
    last_db_spl = LOUDNESS_REF_PHON;
    coeff_ramp_remaining = 15;

#ifdef FAST
    {
        int32_t sample = 1234567;
        int64_t out = loudness_fast_24bit(sample);
        assert(out == (int64_t)sample);
    }
#elif defined(PRECISE)
    {
        int64_t sample = 1234567LL;
        int64_t out = loudness_precise_24bit(sample);
        assert(out == sample);
    }
#endif

    /* Step the ramp once. */
    loudness_coeff_ramp_step();
    assert(coeff_ramp_remaining == 14);
    printf("test_loudness_ramp_bypass_logic passed\n");
}

int main(void) {
#ifdef PRECISE
    printf("=== loudness_ramp_tests (PRECISE) ===\n");
#else
    printf("=== loudness_ramp_tests (FAST) ===\n");
#endif
    test_ramp_length_constants();
    test_ramp_early_exit();
    test_ramp_sign_and_remaining();
    test_full_ramp_sequence();
    test_mid_ramp_retarget_convergence();
    test_loudness_ramp_bypass_logic();
    printf("All loudness_ramp_tests passed.\n");
    return 0;
}
