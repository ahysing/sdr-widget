/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_internal.h — shared loudness internals (not for external callers)
 */

#ifndef LOUDNESS_INTERNAL_H_
#define LOUDNESS_INTERNAL_H_

#include "compiler.h"
#include "loudness.h"
#include "loudness_fast.h"

extern volatile S16 last_db_spl_x10;
/* Per-channel host gain delta from VOL_MAX in USB Q8.8 dB. */
extern volatile S16 target_gain_dbfs_left_q8;
extern volatile S16 target_gain_dbfs_right_q8;

int32_t loudness_usb_volume_q8_to_gain_dbfs(S16 volume_q8);
int32_t loudness_clamp_gain_dbfs_q8(int32_t gain_dbfs_q8);
int32_t loudness_clamp_gain_dbfs(int32_t gain_dbfs);
int32_t loudness_gain_dbfs_q8_to_x10(int32_t gain_dbfs_q8);
int loudness_get_equalizer_step(int32_t db_spl_x10);
void loudness_internal_current_stereo_db_spl_x10(
    int32_t *db_spl_left_x10, int32_t *db_spl_right_x10);

void loudness_publish_equalizer_step(int32_t db_spl_x10);
void loudness_report_equalizer_step_switch(int32_t prev_db_spl_x10,
    int32_t db_spl_x10,
    int prev_step, int equalizer_step);
void loudness_apply_equalizer_step_if_needed(void);

void loudness_fast_select_equalizer_steps(int32_t db_spl_x10,
    int equalizer_step_left, int equalizer_step_right);
void loudness_fast_select_unity_passthrough(void);
void loudness_fast_reset_states(void);
void loudness_fast_refresh_idle_cache(void);

const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_44100hz(void);
const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_48000hz(void);
void loudness_fast_set_active_equalizer_step_table(
    const biquad_quotients_fast_t *table);
const biquad_quotients_fast_t *loudness_fast_active_equalizer_step_table(void);
void loudness_fast_stage_equalizer_steps(
    const biquad_quotients_fast_t *table,
    int equalizer_step_left, int equalizer_step_right);
void loudness_fast_stage_shared_equalizer_step(
    const biquad_quotients_fast_t *table, int equalizer_step);
void loudness_fast_commit_staged_quotients(void);
biquad_state_fast_t *loudness_fast_biquad_state(int channel);
const biquad_runtime_fast_t *loudness_fast_channel_runtime(int channel);

void loudness_refresh_quotient_table_selection(void);
void loudness_set_source_has_volume_control(void);

#endif /* LOUDNESS_INTERNAL_H_ */
