#ifndef DEVICE_AUDIO_VOLUME_H
#define DEVICE_AUDIO_VOLUME_H

#include "compiler.h"

typedef void (*device_audio_volume_apply_fn_t)(S32 *sample_L, S32 *sample_R);

void adjust_volume(S32 *sample_L, S32 *sample_R);
void keep_volume(S32 *sample_L, S32 *sample_R);
void device_audio_set_volume_in_biquad(Bool volume_in_biquad);

extern device_audio_volume_apply_fn_t device_audio_volume_apply_fn;

#endif
