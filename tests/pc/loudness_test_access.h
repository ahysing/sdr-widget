#ifndef LOUDNESS_TEST_ACCESS_H
#define LOUDNESS_TEST_ACCESS_H

#include <stdint.h>
#include "loudness_inferred_gain.h"

#include "track_dbfs.h"

#ifdef FAST
void loudness_update_track_level_fast(int32_t sample);
#define loudness_update_track_level(sample) loudness_update_track_level_fast(sample)
#elif defined(PRECISE)
void loudness_update_track_level_precise(int64_t sample);
#define loudness_update_track_level(sample) loudness_update_track_level_precise(sample)
#endif

#ifdef BUILD_TESTING
#include "compiler.h"

extern volatile S16 last_db_spl;

int loudness_test_get_equalizer_step(int32_t db_spl);
Bool loudness_test_should_change_equalizer_step(int32_t db_spl_x10);
#endif

#endif
