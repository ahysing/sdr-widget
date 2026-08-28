#ifndef SDR_WIDGET_STATS_TELEMETRY_H
#define SDR_WIDGET_STATS_TELEMETRY_H

#include "compiler.h"

typedef struct {
    U16 frequency_100hz;
    S8 gain_dbfs_left;
    S8 gain_dbfs_right;
    S8 db_spl_left;
    S8 db_spl_right;
    U8 equalizer_step_left;
    U8 equalizer_step_right;
    U8 source_has_volume_control;
    U8 bass_boost_enabled;
} stats_telemetry_snapshot_t;

void stats_telemetry_init(void);
void stats_telemetry_set_frequency_hz(U32 frequency_hz);
void stats_telemetry_set_gain_dbfs_stereo(
    S8 gain_dbfs_left, S8 gain_dbfs_right);
void stats_telemetry_set_equalizer_state_stereo(
    S8 db_spl_left, U8 equalizer_step_left,
    S8 db_spl_right, U8 equalizer_step_right);
void stats_telemetry_set_source_has_volume_control(U8 source_has_volume_control);
void stats_telemetry_set_bass_boost_enabled(U8 bass_boost_enabled);

stats_telemetry_snapshot_t stats_telemetry_read_best_effort(void);

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void);
#endif

#endif
