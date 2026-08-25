/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_internal.h — shared loudness internals (not for external callers)
 */

#ifndef LOUDNESS_INTERNAL_H_
#define LOUDNESS_INTERNAL_H_

#include "compiler.h"
#include "loudness.h"

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
void loudness_fast_reset_states(void);

#endif /* LOUDNESS_INTERNAL_H_ */
