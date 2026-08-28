#ifndef LOUDNESS_TEST_ACCESS_H
#define LOUDNESS_TEST_ACCESS_H

#include <stdint.h>
#include "loudness_inferred_gain.h"

#ifdef BUILD_TESTING
#include "compiler.h"

extern volatile S16 last_db_spl;
extern volatile S16 last_db_spl_x10;

int loudness_test_get_equalizer_step(int32_t db_spl_x10);
Bool loudness_test_should_change_equalizer_step(int32_t db_spl_x10);
#endif

#endif
