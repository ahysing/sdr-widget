#ifndef SDR_WIDGET_STATS_TELEMETRY_H
#define SDR_WIDGET_STATS_TELEMETRY_H

#include "compiler.h"

typedef struct {
    U16 frequency_100hz;
    S8 gain_dbfs;
    S8 db_spl;
    U8 equalizer_step;
    U8 source_volume_control;
} stats_telemetry_snapshot_t;

void stats_telemetry_init(void);
void stats_telemetry_set_frequency_hz(U32 frequency_hz);
void stats_telemetry_set_gain_dbfs(S8 gain_dbfs);
void stats_telemetry_set_equalizer_state(S8 db_spl, U8 equalizer_step);
void stats_telemetry_set_source_volume_control(U8 source_volume_control);

stats_telemetry_snapshot_t stats_telemetry_read_best_effort(void);

#ifdef UNIT_TEST
void stats_telemetry_test_reset(void);
#endif

#endif
