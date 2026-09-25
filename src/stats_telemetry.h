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
    U8 loudness_enabled;
    U8 sample_bits;
    U8 num_samples;
    S8 gain_inferred_dbfs_left;
    S8 gain_inferred_dbfs_right;
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
void stats_telemetry_set_loudness_enabled(U8 loudness_enabled);
void stats_telemetry_set_sample_bits(U8 sample_bits);
void stats_telemetry_set_num_samples(U8 num_samples);
void stats_telemetry_set_gain_inferred_dbfs_stereo(
    S8 gain_inferred_dbfs_left, S8 gain_inferred_dbfs_right);

stats_telemetry_snapshot_t stats_telemetry_read_best_effort(void);

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void);
#endif

#endif
