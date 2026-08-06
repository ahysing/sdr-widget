#ifndef LOUDNESS_TEST_ACCESS_H
#define LOUDNESS_TEST_ACCESS_H

#include <stdint.h>

/* PC unit tests link loudness.c directly and seed the RMS integrator. */
extern uint64_t root_mean_square;
extern uint32_t root_mean_square_counter;

#ifdef FAST
void loudness_update_track_level_fast(int32_t sample);
#define loudness_update_track_level(sample) loudness_update_track_level_fast(sample)
#elif defined(PRECISE)
void loudness_update_track_level_precise(int64_t sample);
#define loudness_update_track_level(sample) loudness_update_track_level_precise(sample)
#endif

#ifdef BUILD_TESTING
#include "compiler.h"

/* Exposed from loudness.c when built with -DBUILD_TESTING. */
extern volatile uint32_t coeff_ramp_remaining;
extern volatile S16 last_db_spl;
#endif

#endif
