#include "stats_telemetry.h"

#ifndef USBSTATISTICS_DISABLE

static volatile U8 stats_telemetry_generation;
static volatile U16 stats_telemetry_frequency_hz;
static volatile S8 stats_telemetry_track_dbfs;
static volatile S8 stats_telemetry_track_rms_dbfs;
static volatile S8 stats_telemetry_gain_dbfs;
static volatile S8 stats_telemetry_db_spl;
static volatile U8 stats_telemetry_equalizer_step;

void stats_telemetry_init(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_hz = 0;
    stats_telemetry_track_dbfs = 0;
    stats_telemetry_track_rms_dbfs = 0;
    stats_telemetry_gain_dbfs = 0;
    stats_telemetry_db_spl = 0;
    stats_telemetry_equalizer_step = 0;
}

void stats_telemetry_set_frequency_hz(U16 frequency_hz)
{
    stats_telemetry_generation++;
    stats_telemetry_frequency_hz = frequency_hz;
    stats_telemetry_generation++;
}

void stats_telemetry_set_gain_dbfs(S8 gain_dbfs)
{
    stats_telemetry_generation++;
    stats_telemetry_gain_dbfs = gain_dbfs;
    stats_telemetry_generation++;
}

void stats_telemetry_set_track_levels(S8 track_dbfs, S8 track_rms_dbfs)
{
    stats_telemetry_generation++;
    stats_telemetry_track_dbfs = track_dbfs;
    stats_telemetry_track_rms_dbfs = track_rms_dbfs;
    stats_telemetry_generation++;
}

void stats_telemetry_set_equalizer_state(S8 db_spl, U8 equalizer_step)
{
    stats_telemetry_generation++;
    stats_telemetry_db_spl = db_spl;
    stats_telemetry_equalizer_step = equalizer_step;
    stats_telemetry_generation++;
}

stats_telemetry_snapshot_t stats_telemetry_read_best_effort(void)
{
    stats_telemetry_snapshot_t snap;
    U8 g1;
    U8 g2;

    do {
        g1 = stats_telemetry_generation;
        snap.frequency_hz = stats_telemetry_frequency_hz;
        snap.track_dbfs = stats_telemetry_track_dbfs;
        snap.track_rms_dbfs = stats_telemetry_track_rms_dbfs;
        snap.gain_dbfs = stats_telemetry_gain_dbfs;
        snap.db_spl = stats_telemetry_db_spl;
        snap.equalizer_step = stats_telemetry_equalizer_step;
        g2 = stats_telemetry_generation;
    } while (g1 != g2 || (g1 & 1u));

    return snap;
}

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_hz = 0;
    stats_telemetry_track_dbfs = 0;
    stats_telemetry_track_rms_dbfs = 0;
    stats_telemetry_gain_dbfs = 0;
    stats_telemetry_db_spl = 0;
    stats_telemetry_equalizer_step = 0;
}
#endif

#endif
