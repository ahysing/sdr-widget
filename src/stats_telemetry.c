#include "stats_telemetry.h"

#ifndef USBSTATISTICS_DISABLE

static volatile U8 stats_telemetry_generation;
static volatile U16 stats_telemetry_frequency_100hz;
static volatile S8 stats_telemetry_gain_dbfs;
static volatile S8 stats_telemetry_db_spl;
static volatile U8 stats_telemetry_equalizer_step;
static volatile U8 stats_telemetry_source_has_volume_control;

void stats_telemetry_init(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_100hz = 0;
    stats_telemetry_gain_dbfs = 0;
    stats_telemetry_db_spl = 0;
    stats_telemetry_equalizer_step = 0;
    stats_telemetry_source_has_volume_control = 0;
}

void stats_telemetry_set_frequency_hz(U32 frequency_hz)
{
    stats_telemetry_generation++;
    stats_telemetry_frequency_100hz = (U16)(frequency_hz / 100u);
    stats_telemetry_generation++;
}

void stats_telemetry_set_gain_dbfs(S8 gain_dbfs)
{
    stats_telemetry_generation++;
    stats_telemetry_gain_dbfs = gain_dbfs;
    stats_telemetry_generation++;
}

void stats_telemetry_set_equalizer_state(S8 db_spl, U8 equalizer_step)
{
    stats_telemetry_generation++;
    stats_telemetry_db_spl = db_spl;
    stats_telemetry_equalizer_step = equalizer_step;
    stats_telemetry_generation++;
}

void stats_telemetry_set_source_has_volume_control(U8 source_has_volume_control)
{
    stats_telemetry_generation++;
    stats_telemetry_source_has_volume_control = source_has_volume_control ? 1u : 0u;
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
        snap.gain_dbfs = stats_telemetry_gain_dbfs;
        snap.db_spl = stats_telemetry_db_spl;
        snap.equalizer_step = stats_telemetry_equalizer_step;
        snap.source_has_volume_control = stats_telemetry_source_has_volume_control;
        g2 = stats_telemetry_generation;
    } while (g1 != g2 || (g1 & 1u));

    return snap;
}

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void)
{
    stats_telemetry_generation = 0;
    stats_telemetry_frequency_100hz = 0;
    stats_telemetry_gain_dbfs = 0;
    stats_telemetry_db_spl = 0;
    stats_telemetry_equalizer_step = 0;
    stats_telemetry_source_has_volume_control = 0;
}
#endif

#endif
