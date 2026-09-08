#ifndef DEVICE_AUDIO_VOLUME_H
#define DEVICE_AUDIO_VOLUME_H

#include "compiler.h"

typedef void (*device_audio_volume_apply_fn_t)(S32 *sample_L, S32 *sample_R);

void adjust_volume(S32 *sample_L, S32 *sample_R);
void adjust_volume_hard_clip(S32 *sample_L, S32 *sample_R);
void hard_clip(S32 *sample_L, S32 *sample_R);
void keep_volume(S32 *sample_L, S32 *sample_R);
void device_audio_set_volume_in_biquad(Bool source_has_volume_control, Bool active_filter_enabled);
void device_audio_volume_update_mult_left(void);
void device_audio_volume_update_mult_right(void);
void device_audio_volume_refresh_mult(void);

extern device_audio_volume_apply_fn_t device_audio_volume_apply_fn;
extern S32 spk_vol_mult_L;
extern S32 spk_vol_mult_R;

#endif
