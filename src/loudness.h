/* -*- mode: c; tab-width: 4; c-basic-offset: 4 -*- */
/*
 * loudness.h
 *
 *  Created on: 2026-06-22
 *      Author: Andreas Dreyer Hysing
 */

#ifndef LOUDNESS_H_
#define LOUDNESS_H_
#include "compiler.h"


void loudness_init();
U64 loudness(U64 sample);

void dsp_init();
void dsp_24bit();

#define UPSAMPLE_24BIT(sample) (((U64)(sample)) << 15)
#define UPSAMPLE_16BIT(sample) (((U64)(sample)) << 8)
#define DOWNSAMPLE_24BIT(sample) ((U32)((sample) >> 15))
#define DOWNSAMPLE_16BIT(sample) ((U16)((sample) >> 8))

#endif