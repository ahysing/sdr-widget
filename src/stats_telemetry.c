#include "stats_telemetry.h"

#ifndef USBSTATISTICS_DISABLE

static volatile U8 stats_telemetry_generation;
static volatile U16 stats_telemetry_frequency_100hz;
static volatile S8 stats_telemetry_gain_dbfs_left;
static volatile S8 stats_telemetry_gain_dbfs_right;
static volatile S8 stats_telemetry_db_spl_left;
static volatile S8 stats_telemetry_db_spl_right;
static volatile U8 stats_telemetry_equalizer_step_left;
static volatile U8 stats_telemetry_equalizer_step_right;
static volatile U8 stats_telemetry_source_has_volume_control;
static volatile U8 stats_telemetry_bass_boost_enabled;
static volatile U8 stats_telemetry_loudness_enabled;
static volatile U8 stats_telemetry_sample_bits;
static volatile U8 stats_telemetry_num_samples;
static volatile S8 stats_telemetry_gain_inferred_dbfs_left;
static volatile S8 stats_telemetry_gain_inferred_dbfs_right;

void stats_telemetry_init(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_100hz = 0;
    stats_telemetry_gain_dbfs_left = 0;
    stats_telemetry_gain_dbfs_right = 0;
    stats_telemetry_db_spl_left = 0;
    stats_telemetry_db_spl_right = 0;
    stats_telemetry_equalizer_step_left = 0;
    stats_telemetry_equalizer_step_right = 0;
    stats_telemetry_source_has_volume_control = 0;
    stats_telemetry_bass_boost_enabled = 0;
    stats_telemetry_loudness_enabled = 0;
    stats_telemetry_sample_bits = 0;
    stats_telemetry_num_samples = 0;
    stats_telemetry_gain_inferred_dbfs_left = 0;
    stats_telemetry_gain_inferred_dbfs_right = 0;
}

void stats_telemetry_set_frequency_hz(U32 frequency_hz)
{
    stats_telemetry_generation++;
    stats_telemetry_frequency_100hz = (U16)(frequency_hz / 100u);
    stats_telemetry_generation++;
}

void stats_telemetry_set_gain_dbfs_stereo(
    S8 gain_dbfs_left, S8 gain_dbfs_right)
{
    stats_telemetry_generation++;
    stats_telemetry_gain_dbfs_left = gain_dbfs_left;
    stats_telemetry_gain_dbfs_right = gain_dbfs_right;
    stats_telemetry_generation++;
}

void stats_telemetry_set_equalizer_state_stereo(
    S8 db_spl_left, U8 equalizer_step_left,
    S8 db_spl_right, U8 equalizer_step_right)
{
    stats_telemetry_generation++;
    stats_telemetry_db_spl_left = db_spl_left;
    stats_telemetry_db_spl_right = db_spl_right;
    stats_telemetry_equalizer_step_left = equalizer_step_left;
    stats_telemetry_equalizer_step_right = equalizer_step_right;
    stats_telemetry_generation++;
}

void stats_telemetry_set_source_has_volume_control(U8 source_has_volume_control)
{
    stats_telemetry_generation++;
    stats_telemetry_source_has_volume_control = source_has_volume_control ? 1u : 0u;
    stats_telemetry_generation++;
}

void stats_telemetry_set_bass_boost_enabled(U8 bass_boost_enabled)
{
    stats_telemetry_generation++;
    stats_telemetry_bass_boost_enabled = bass_boost_enabled ? 1u : 0u;
    stats_telemetry_generation++;
}

void stats_telemetry_set_loudness_enabled(U8 loudness_enabled)
{
    stats_telemetry_generation++;
    stats_telemetry_loudness_enabled = loudness_enabled ? 1u : 0u;
    stats_telemetry_generation++;
}

void stats_telemetry_set_sample_bits(U8 sample_bits)
{
    stats_telemetry_generation++;
    stats_telemetry_sample_bits = sample_bits;
    stats_telemetry_generation++;
}

void stats_telemetry_set_num_samples(U8 num_samples)
{
    stats_telemetry_generation++;
    stats_telemetry_num_samples = num_samples;
    stats_telemetry_generation++;
}
void stats_telemetry_set_gain_inferred_dbfs_stereo(
    S8 gain_inferred_dbfs_left, S8 gain_inferred_dbfs_right)
{
    stats_telemetry_generation++;
    stats_telemetry_gain_inferred_dbfs_left = gain_inferred_dbfs_left;
    stats_telemetry_gain_inferred_dbfs_right = gain_inferred_dbfs_right;
    stats_telemetry_generation++;
}

stats_telemetry_snapshot_t stats_telemetry_read_best_effort(void)
{
    stats_telemetry_snapshot_t snap;
    U8 g1;
    U8 g2;

    do {
        g1 = stats_telemetry_generation;
        snap.frequency_100hz = stats_telemetry_frequency_100hz;
        snap.gain_dbfs_left = stats_telemetry_gain_dbfs_left;
        snap.gain_dbfs_right = stats_telemetry_gain_dbfs_right;
        snap.db_spl_left = stats_telemetry_db_spl_left;
        snap.db_spl_right = stats_telemetry_db_spl_right;
        snap.equalizer_step_left = stats_telemetry_equalizer_step_left;
        snap.equalizer_step_right = stats_telemetry_equalizer_step_right;
        snap.source_has_volume_control = stats_telemetry_source_has_volume_control;
        snap.bass_boost_enabled = stats_telemetry_bass_boost_enabled;
        snap.loudness_enabled = stats_telemetry_loudness_enabled;
        snap.sample_bits = stats_telemetry_sample_bits;
        snap.num_samples = stats_telemetry_num_samples;
        snap.gain_inferred_dbfs_left = stats_telemetry_gain_inferred_dbfs_left;
        snap.gain_inferred_dbfs_right = stats_telemetry_gain_inferred_dbfs_right;
        g2 = stats_telemetry_generation;
    } while (g1 != g2 || (g1 & 1u));

    return snap;
}

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_100hz = 0;
    stats_telemetry_gain_dbfs_left = 0;
    stats_telemetry_gain_dbfs_right = 0;
    stats_telemetry_db_spl_left = 0;
    stats_telemetry_db_spl_right = 0;
    stats_telemetry_equalizer_step_left = 0;
    stats_telemetry_equalizer_step_right = 0;
    stats_telemetry_source_has_volume_control = 0;
    stats_telemetry_bass_boost_enabled = 1;
    stats_telemetry_loudness_enabled = 0;
    stats_telemetry_sample_bits = 0;
    stats_telemetry_num_samples = 0;
    stats_telemetry_gain_inferred_dbfs_left = 0;
    stats_telemetry_gain_inferred_dbfs_right = 0;
}
#endif

#endif
