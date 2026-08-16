$ErrorActionPreference = 'Stop'
$root = if (Test-Path 'src/loudness.c') { (Get-Location).Path } else { Split-Path -Parent $PSScriptRoot }
$src = Join-Path $root 'src'
$lines = Get-Content -Path (Join-Path $src 'loudness.c')

function Slice($start, $end) {
    return ($lines[($start-1)..($end-1)] -join "`n")
}

# --- track_dbfs.c ---
$trackDbfs = @"
#include "track_dbfs.h"
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
#else
#include "usb_specific_request.h"
#endif
#include "loudness.h"
#include <stdint.h>

#ifndef LOUDNESS_DISABLE

"@ + (Slice 51 76) + "`n`n" + (Slice 625 631) + "`n`n" + (Slice 641 695) + "`n`n#ifdef PRECISE`n" + (Slice 700 720) + "`n#endif`n`n#ifdef FAST`n" + (Slice 725 752) + "`n#endif`n`n" + (Slice 1265 1278) + "`n`n" + (Slice 1416 1423) + "`n`n#endif /* LOUDNESS_DISABLE */`n"

Set-Content -Path (Join-Path $src 'track_dbfs.c') -Value $trackDbfs -NoNewline

# --- loudness_precise.c ---
$precise = @"
#ifdef PRECISE
#include "loudness_precise.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "track_dbfs.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#else
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

"@ + (Slice 80 117) + "`n`n#define SAMPLE_24BITS 24`n#define LOUDNESS_FILTERS 2`n#define LOUDNESS_Q61_ONE  ((int64_t)1 << 61)`n`n" + (Slice 269 418) + "`n`n" + (Slice 579 592) + "`n`n" + (Slice 763 771) + "`n`n" + (Slice 807 820) + "`n`n" + (Slice 825 857) + "`n`n#define SPEAKER_VOLUME_Q61  ((S64)1 << 61)`n`n" + (Slice 922 932) + "`n`n" + (Slice 1000 1051) + "`n`nvoid loudness_precise_reset_states(void)`n{`n    int i;`n    for (i = 0; i < LOUDNESS_FILTERS; i++) {`n        loudness_states[i].w1 = 0;`n        loudness_states[i].w2 = 0;`n    }`n}`n`n" + (Slice 1349 1358) + "`n`n" + (Slice 1425 1620) + "`n`n#endif /* PRECISE */`n"

Set-Content -Path (Join-Path $src 'loudness_precise.c') -Value $precise -NoNewline

# --- loudness_fast.c ---
$fast = @"
#ifdef FAST
#include "loudness_fast.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "track_dbfs.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#else
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

"@ + (Slice 215 248) + "`n`n#define SAMPLE_24BITS 24`n#define LOUDNESS_FILTERS 2`n#define LOUDNESS_Q29_ONE  ((int32_t)1 << 29)`n#define FROM_Q29(X) (((X) + (1LL << 28)) >> 29)`n`n" + (Slice 421 571) + "`n`n" + (Slice 594 607) + "`n`n" + (Slice 763 771) + "`n`n" + (Slice 792 805) + "`n`n" + (Slice 859 885) + "`n`n" + (Slice 1054 1113) + "`n`nvoid loudness_fast_reset_states(void)`n{`n    int i;`n    for (i = 0; i < LOUDNESS_FILTERS; i++) {`n        loudness_states[i].w1 = 0;`n        loudness_states[i].w2 = 0;`n    }`n}`n`n" + (Slice 1360 1385) + "`n`n" + (Slice 1502 1670) + "`n`n#endif /* FAST */`n"

Set-Content -Path (Join-Path $src 'loudness_fast.c') -Value $fast -NoNewline

Write-Host "Generated track_dbfs.c, loudness_precise.c, loudness_fast.c"
