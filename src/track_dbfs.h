/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * track_dbfs.h — running RMS track level for equalizer-step blending
 */

#ifndef TRACK_DBFS_H_
#define TRACK_DBFS_H_

#include <stdint.h>
#include "compiler.h"

void loudness_reset_rms(void);
int32_t loudness_get_track_rms_dbfs(void);
int32_t loudness_get_track_dbfs(void);

#ifndef LOUDNESS_DISABLE

extern uint64_t root_mean_square;

void loudness_update_track_level_fast(int32_t sample);

#endif /* LOUDNESS_DISABLE */

#endif /* TRACK_DBFS_H_ */
