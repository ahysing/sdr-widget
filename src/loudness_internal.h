/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness_internal.h — shared loudness internals (not for external callers)
 */

#ifndef LOUDNESS_INTERNAL_H_
#define LOUDNESS_INTERNAL_H_

#include "compiler.h"
#include "loudness.h"
#include "loudness_fast.h"

// TODO: find a way to get EP_OUT_LENGTH_2_HS from "uac2_usb_descriptors.h" without breaking PC tests
#ifndef EP_OUT_LENGTH_2_HS
#define EP_OUT_LENGTH_2_HS                392
#endif

#define UAC2_USB_OUT_MAX_STEREO_SAMPLES  (EP_OUT_LENGTH_2_HS / 8u)

#define BASS_PHON_55_IDX                 60

/* Coefficients and the DF-II accumulator use Q4.28 throughout. */
#define LOUDNESS_DF2_Q28_SHIFT          28
#define LOUDNESS_DF2_Q28_ROUND          (1LL << (LOUDNESS_DF2_Q28_SHIFT - 1))
#ifndef LOUDNESS_Q28_ONE
#define LOUDNESS_Q28_ONE                ((int32_t)1 << LOUDNESS_DF2_Q28_SHIFT)
#endif
#define LOUDNESS_DF2_STATE_HEADROOM_M   13

extern volatile S16 last_db_spl_left_x10;
extern volatile S16 last_db_spl_right_x10;
/* Per-channel host gain delta from VOL_MAX in USB Q8.8 dB. */
extern volatile S16 target_gain_dbfs_left_q8;
extern volatile S16 target_gain_dbfs_right_q8;

int32_t loudness_clamp_gain_dbfs_q8(int32_t gain_dbfs_q8);
int32_t loudness_clamp_gain_dbfs(int32_t gain_dbfs);
int32_t loudness_gain_dbfs_q8_to_x10(int32_t gain_dbfs_q8);
int loudness_get_equalizer_step(int32_t db_spl_x10);

void loudness_publish_equalizer_step(int32_t db_spl_left_x10, int32_t db_spl_right_x10);
void loudness_report_equalizer_step_switch(int32_t prev_db_spl_x10, int32_t db_spl_x10, int prev_step, int equalizer_step);
void loudness_equalizer_steps_for_mode(
    int32_t db_spl_left_x10, int32_t db_spl_right_x10,
    int *equalizer_step_left, int *equalizer_step_right);

void loudness_fast_select_equalizer_steps(int32_t db_spl_left_x10, int32_t db_spl_right_x10, int equalizer_step_left, int equalizer_step_right);
void loudness_fast_select_unity_passthrough(void);
void loudness_fast_reset_states(void);

const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_48000hz(void);
const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_44100hz(void);
void loudness_fast_refresh_quotient_table_pointers(void);
const biquad_quotients_fast_t *loudness_fast_no_volume_quotient_table_48000hz(void);
void loudness_fast_prepare_inactive_quotients(const biquad_quotients_fast_t *table, int equalizer_step_left, int equalizer_step_right);
void loudness_publish_quotients(void);
biquad_state_fast_t *loudness_fast_biquad_state(int channel);
void loudness_refresh_quotient_table_selection(void);
void loudness_set_source_has_volume_control(void);

#if defined(__GNUC__)
#define LOUDNESS_STATIC_INLINE static __attribute__((always_inline)) inline
#else
#define LOUDNESS_STATIC_INLINE static inline
#endif

int32_t loudness_saturate_s64_to_s32(int64_t value);

#endif /* LOUDNESS_INTERNAL_H_ */
