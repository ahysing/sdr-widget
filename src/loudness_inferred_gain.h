/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_inferred_gain.h
 *
 * Peak-based host gain inference when the source downscales PCM instead of
 * using USB volume SET_CUR/GET_CUR.
 */

#ifndef LOUDNESS_INFERRED_GAIN_H_
#define LOUDNESS_INFERRED_GAIN_H_

#include <stdint.h>
#include "compiler.h"

#ifndef LOUDNESS_DISABLE

void loudness_inferred_gain_reset(void);
void loudness_inferred_gain_set_rate(uint32_t frequency_hz);
Bool loudness_inferred_gain_has_source_volume_control(void);
void loudness_set_source_has_volume_control(void);
void loudness_envelope_follower_update_stereo(int32_t sample_L, int32_t sample_R);
int32_t loudness_inferred_gain_dbfs(void);
int32_t loudness_inferred_gain_dbfs_channel(int channel);

#ifdef BUILD_TESTING
void loudness_test_reset_inferred_gain(void);
void loudness_test_set_short_memory(uint32_t value);
void loudness_test_set_long_memory(uint32_t value);
uint32_t loudness_test_get_short_memory(void);
uint32_t loudness_test_get_long_memory(void);
void loudness_test_combined_context_loop(uint32_t instant_sample_peak);
uint32_t loudness_test_get_active_loudness_level(void);
int32_t loudness_inferred_gain_dbfs_from_magnitude(uint32_t mag);
#endif

#else /* LOUDNESS_DISABLE */

void loudness_set_source_has_volume_control(void);
void loudness_envelope_follower_update_stereo(int32_t sample_L, int32_t sample_R);

#endif /* LOUDNESS_DISABLE */

#endif /* LOUDNESS_INFERRED_GAIN_H_ */
