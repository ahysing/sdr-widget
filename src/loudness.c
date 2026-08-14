#include "loudness.h"
#if defined(BUILD_TESTING)
#include "../tests/pc/usb_specific_request.h"
extern S16 spk_vol_usb_L, spk_vol_usb_R;
#else
#include "usb_specific_request.h"
#include "device_audio_task.h"
#include "taskAK5394A.h"
#endif
#ifndef USBSTATISTICS_DISABLE
#include "usb_statistics.h"
#include "audio_stats_logic.h"
#endif
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#else
#define tskIDLE_PRIORITY 0
#define xTaskGetTickCount() 0
#define vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement)
#define xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask)
#define portTICK_RATE_MS 1
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

#if defined(__GNUC__) && defined(__AVR32__)
#include "SOFTWARE_FRAMEWORK/UTILS/DEBUG/print_funcs.h"
#define LOUDNESS_PRINT(x) print_dbg(x)
#else
#define LOUDNESS_PRINT(x) printf("%s", x)
#endif

/* Weighting for blending gain vs. measured track loudness when
 * selecting the equalizer step. Gain (host volume) dominates; the
 * running RMS provides a small correction. Weights in percent,
 * must sum to 100. */
#define LOUDNESS_GAIN_WEIGHT_PCT   90
#define LOUDNESS_TRACK_WEIGHT_PCT  10

/* Leaky integrator for the running mean-square of the input signal. */
U64 root_mean_square = 0;
static volatile U32 root_mean_square_seq = 0;
static inline void loudness_rms_write_begin(void) {
    root_mean_square_seq++;
}

static inline void loudness_rms_write_end(void) {
    root_mean_square_seq++;
}

static U64 loudness_rms_read(void) {
    U32 seq1;
    U32 seq2 = 0;
    U64 val = 0;

    do {
        seq1 = root_mean_square_seq;
        if (seq1 & 1U) {
            continue;
        }
        val = root_mean_square;
        seq2 = root_mean_square_seq;
    } while (seq1 != seq2);

    return val;
}

#define TO_Q30(X) (((S64)(X)) << 30)

static inline int64_t FROM_Q61(int64_t x) {
    // Extract the sign bit to handle rounding symmetrically
    int64_t sign = x >> 63;

    // Add 0.5 (1LL << 60) for positive numbers, or subtract 0.5 for negative numbers
    // This achieves perfect round-to-nearest behavior without overflowing int64_t
    int64_t round_offset = ((int64_t)1 << 60) ^ sign;
    round_offset -= sign;

    return (x + round_offset) >> 61;
}

#if !defined(_MSC_VER) && !defined(__AVR32__)
/* For generic GCC that might support __int128 */
#define HAS_INT128
#endif

#if !defined(HAS_INT128) && !defined(_MSC_VER)
/* Manual 64x64 -> 128 multiplication and shift for AVR32 or other compilers without __int128 */
static inline S64 mul_shift_q61(S64 a, S64 b) {
    U64 a_lo = (U32)a;
    S64 a_hi = a >> 32;
    U64 b_lo = (U32)b;
    S64 b_hi = b >> 32;

    U64 lo_lo = a_lo * b_lo;
    S64 hi_lo = a_hi * b_lo;
    S64 lo_hi = a_lo * b_hi;
    S64 hi_hi = a_hi * b_hi;

    U64 cross = (lo_lo >> 32) + (U32)hi_lo + (U32)lo_hi;
    S64 upper = hi_hi + (hi_lo >> 32) + (lo_hi >> 32) + (cross >> 32);
    S64 lower = (cross << 32) | (U32)lo_lo;

    /* Shift right by 61 */
    return (upper << 3) | (lower >> 61);
}
#endif

#define FROM_Q29(X) (((X) + (1LL << 28)) >> 29)
#define FROM_Q30(X) (((X) + (1LL << 29)) >> 30)
#define Q8_SHIFT   8
#define FROM_Q8(X) ROUND_SHIFT_S64(X, Q8_SHIFT)
/* Convert an offline-designed Q30 coefficient to the Q8 domain used by the
 * fast biquad path. Round-half-up. */
#define Q30_TO_Q8(X) ((S32)(((X) + (1LL << 21)) >> 22))

S32 saturate_24bit_s64_to_s32(S64 acc) {
    if (acc > INT24_MAX_S64) {
        return (S32)INT24_MAX;
    }
    if (acc < INT24_MIN_S64) {
        return (S32)INT24_MIN;
    }
    return (S32)acc;
}

S32 saturate_24bit_s32_to_s32(S32 acc) {
    if (acc > INT24_MAX) {
        return (S32)INT24_MAX;
    }
    if (acc < INT24_MIN) {
        return (S32)INT24_MIN;
    }
    return acc;
}

U32 saturate_24bit_s32_to_u32(S32 acc) {
    return (U32)saturate_24bit_s32_to_s32(acc);
}

S32 saturate_16bit_s32_to_s32(S32 acc) {
    if (acc > INT16_MAX) {
        return (S32)INT16_MAX;
    }
    if (acc < INT16_MIN) {
        return (S32)INT16_MIN;
    }
    return acc;
}

static void loudness_print_build_config(void) {
    LOUDNESS_PRINT("Audio firmware build options:\n");
#ifndef LOUDNESS_DISABLE
#ifdef FAST
    LOUDNESS_PRINT("  equalizer filter: FAST\n");
#else
    LOUDNESS_PRINT("  equalizer filter: PRECISE\n");
#endif
#else
    LOUDNESS_PRINT("  equalizer filter: disabled\n");
#endif
    LOUDNESS_PRINT("  volume control: enabled\n");
#ifndef USBSTATISTICS_DISABLE
    LOUDNESS_PRINT("  USB statistics: enabled (HID)\n");
#else
    LOUDNESS_PRINT("  USB statistics: disabled\n");
#endif
#if !defined(LOUDNESS_DISABLE) && !defined(USBSTATISTICS_DISABLE)
    LOUDNESS_PRINT("  loudness statistics events: enabled\n");
#elif !defined(USBSTATISTICS_DISABLE)
    LOUDNESS_PRINT("  loudness statistics events: disabled\n");
#endif
}

void loudness_usb_statistics_init(void) {
#ifndef USBSTATISTICS_DISABLE
#ifdef FREERTOS_USED
    statistics_init();
#endif
#endif
}

void loudness_volume_init(void) {
}

void loudness_init(void) {
    loudness_print_build_config();
    loudness_usb_statistics_init();
    loudness_filter_init();
}

#ifndef LOUDNESS_DISABLE
/* The macs.d is only available when compiling
 * with avr32-gcc for a target that has the AVR32 DSP extension (e.g. the
 * Atmel AT32UC3A3). When building with any other toolchain -- notably
 * the Visual Studio (MSVC) toolchain used for host-side unit tests --
 * fall back to a portable C expression with equivalent semantics.
 *
 * We therefore require BOTH:
 *   - __GNUC__            : the compiler is GCC (or a GCC-compatible one
 *                           that defines this macro; MSVC does not).
 *   - __AVR32_HAS_DSP__   : the target actually has the DSP extension
 *                           that provides the mach_d instruction.
 */
#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
/*   macs.d: signed 32x32 -> 64 multiply-accumulate.
 *   acc64 += (S32)b * (S32)c
 */
S64 macs_d(S64 d, S32 a, S32 b) {
    __asm__ ("macs.d %0, %1, %2" : "+r"(d) : "r"(a), "r"(b));
    return d;
}
#define FMA_24BIT(A, B, C) macs_d(A, B, C)
#define FMS_24BIT(A, B, C) macs_d(A, B, -C)

/*   mulu.d: Unsigned 32x32 -> 64-bit widening multiplication.
 */
static inline uint64_t mulu_d(uint32_t a, uint32_t b) {
    uint64_t res;
    __asm__ ("mulu.d %0, %1, %2" : "=r"(res) : "r"(a), "r"(b));
    return res;
}
#else
/* Portable fallback for non-GCC toolchains (e.g. MSVC on Windows used
 * for host-side testing). Matches the semantics of the AVR32 builtin:
 *
 *   FMA: A += B * C
 *   FMS: A -= B * C
 */
#define FMA_24BIT(A, B, C) \
    ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) \
    ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))

static inline uint64_t mulu_d(uint32_t a, uint32_t b) {
    return (uint64_t)a * (uint64_t)b;
}
#endif

#define SAMPLE_24BITS 24
#define LOUDNESS_FILTERS 2
/* Unity (1.0) in Q61 fixed-point; PRECISE biquad coefficients use this scale. */
#define LOUDNESS_Q61_ONE  ((int64_t)1 << 61)
#define LOUDNESS_Q29_ONE  ((int32_t)1 << 29)
#define Q30_ONE             (1LL << 30)
/* Speaker volume factor in Q30. Set to 1.0 (unity); change to e.g.
 * ((S64)((1LL<<30) * 4 / 5)) for 0.8. Applied only to the feed-forward
 * taps (b0,b1,b2). NEVER scale a1,a2 -- it would move the poles. */
#define SPEAKER_VOLUME_Q30  Q30_ONE

/* Equalizer-step coefficient table. Indexed [equalizer_step][filter].
 * Layout per filter row: { a1, a2, b0, b1, b2 } (a0 normalized to 1, not stored).
 * Designed offline (RBJ cookbook, fs=44100 Hz):
 *   filter 0: low-shelf
 *   filter 1: high-shelf
 *
 * Bands (phon labels; assuming 0 dBFS == LOUDNESS_REF_DB_SPL = 80 dB SPL)
 */
#ifdef PRECISE
static const biquad_quotients_precise_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -4544594964962185216LL,  2239043835069701376LL,  2346434730249055744LL, -4544030131664932864LL,  2199016947331592192LL },
        {  1082560395763763712LL,   661268705817253248LL,  3434008590088733184LL,  -258364970506451648LL,   874028491212429312LL },
    },
    /* 57 phon */
    {
        { -4541995269362138624LL,  2236466595495678464LL,  2343248147601199104LL, -4541478420180965376LL,  2199578306289345792LL },
        {  1027618965036025984LL,   647958575822061952LL,  3328269619333880832LL,  -183902238188864672LL,   837053168926765312LL },
    },
    /* 59 phon */
    {
        { -4539336313756435456LL,  2233831557928468736LL,  2340042694246722048LL, -4538866735204360704LL,  2200101451447515136LL },
        {   973164440619111680LL,   635538229899741952LL,  3225339101669282304LL,  -113313669654873440LL,   802520247718138752LL },
    },
    /* 61 phon */
    {
        { -4536618107120222208LL,  2231138787148195584LL,  2336822257747526656LL, -4536195143259076608LL,  2200582502475508736LL },
        {   919182763010035968LL,   623993490520784384LL,  3125207602411523584LL,   -46479149678316232LL,   770290810011307264LL },
    },
    /* 63 phon */
    {
        { -4533838294316304384LL,  2228386039191132160LL,  2333589574931922944LL, -4533461318943725568LL,  2201016448845482496LL },
        {   865600393636043648LL,   613299414752994048LL,  3027849339827409920LL,    16669731454998906LL,   740223746320323200LL },
    },
    /* 65 phon */
    {
        { -4530997758508082176LL,  2225574245287986944LL,  2330347125107535872LL, -4530666232897932288LL,  2201401655004295168LL },
        {   812377803749021824LL,   603451939735328768LL,  2933226924565017088LL,    76243198267788560LL,   712202629865239424LL },
    },
    /* 67 phon */
    {
        { -4528094105972418560LL,  2222701127352272384LL,  2327096719355233280LL, -4527807546271639552LL,  2201733976911512064LL },
        {   759471493723527808LL,   594441926638050432LL,  2841298349180923904LL,   132350825634692544LL,   686107254759655424LL },
    },
    /* 69 phon */
    {
        { -4525125038186378240LL,  2219764509413321728LL,  2323839817467394560LL, -4524883030920565248LL,  2202009708425434880LL },
        {   706841569276866304LL,   586261496915460608LL,  2752016493159427584LL,   185108002433245920LL,   661821579813347328LL },
    },
    /* 71 phon */
    {
        { -4522090351544483328LL,  2216764265924469248LL,  2320577535500458496LL, -4521892582210855936LL,  2202227508971332096LL },
        {   654486203185031808LL,   578901536085074688LL,  2665332152379604480LL,   234669590548644256LL,   639229005555551232LL },
    },
    /* 73 phon */
    {
        { -4518990940801205248LL,  2213701353656891648LL,  2317310534975097856LL, -4518837196511904768LL,  2202387572184788992LL },
        {   602312110870820096LL,   572361455427760640LL,  2581186382350857728LL,   281097190765210976LL,   618233002396205696LL },
    },
    /* 75 phon */
    {
        { -4515821764084009472LL,  2210570941594736128LL,  2314039193856607232LL, -4515711907671009280LL,  2202484613364823040LL },
        {   550278219221001280LL,   566640732497040320LL,  2499522764500645376LL,   324504398268713856LL,   598734798162376448LL },
    },
    /* 77 phon */
    {
        { -4512583558079280128LL,  2207373834636114432LL,  2310763798611542528LL, -4512517567521567232LL,  2202519035795978496LL },
        {   498442609824688000LL,   561718805167060288LL,  2420285976317830144LL,   365102996095559936LL,   580615451792051712LL },
    },
    /* 79 phon */
    {
        { -4509272346573570048LL,  2204106247004356864LL,  2307484334153939456LL, -4509250304339496960LL,  2202486964298184960LL },
        {   446732285439977344LL,   557601166357438080LL,  2343412635966013952LL,   402969402806003584LL,   563794422239092288LL },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
    },

};

static const biquad_quotients_precise_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -4549984990155025920LL,  2244388596002164480LL,  2343170573435714560LL, -4549507664721170432LL,  2207538357213998336LL },
        {   715730939798030464LL,   652304573352203904LL,  3589717238135678976LL,  -915009802436835968LL,   999171086665085056LL },
    },
    /* 57 phon */
    {
        { -4547591078082438656LL,  2242013689312390144LL,  2340241598440835072LL, -4547154267007306752LL,  2208051911160380416LL },
        {   657091860763613952LL,   641096680271087232LL,  3465884636911263744LL,  -812201433117264128LL,   950348346454395648LL },
    },
    /* 59 phon */
    {
        { -4545142767135882240LL,  2239585615425062144LL,  2337295246005512192LL, -4544745879049984000LL,  2208530266719141120LL },
        {   599253296632956416LL,   630863775778640128LL,  3346129493171741184LL,  -714929926419497088LL,   904760514873046144LL },
    },
    /* 61 phon */
    {
        { -4542640796944227328LL,  2237105156772112384LL,  2334334676389210112LL, -4542283292244551680LL,  2208970994296271104LL },
        {   542156596902418752LL,   621581462644094208LL,  3230338417459191296LL,  -622966241248392704LL,   862208892549409024LL },
    },
    /* 63 phon */
    {
        { -4540081414579860992LL,  2234568669826904320LL,  2331362748504799744LL, -4539762763705006592LL,  2209367581410653184LL },
        {   485719797385116096LL,   613229496157792256LL,  3118390752947205632LL,  -536108665352953472LL,   822510215162349952LL },
    },
    /* 65 phon */
    {
        { -4537465327589775360LL,  2231976909327117824LL,  2328381589455526912LL, -4537185073644899840LL,  2209718583030159872LL },
        {   429894023513245120LL,   605789959016356992LL,  3010171348812148736LL,  -454132209614052928LL,   785487852545200640LL },
    },
    /* 67 phon */
    {
        { -4534790815937808384LL,  2229328249068541952LL,  2325392829996227584LL, -4534548552858652160LL,  2210020691365164288LL },
        {   374574763958811520LL,   599243984956872704LL,  2905558954690131968LL,  -376872747873006400LL,   750975551312252288LL },
    },
    /* 69 phon */
    {
        { -4532057362127641600LL,  2226622236102765824LL,  2322397895561377280LL, -4531852758749741056LL,  2210271953132983808LL },
        {   319718876736702784LL,   593580454592561280LL,  2804440870209056256LL,  -304116152496551168LL,   718817622830452480LL },
    },
    /* 71 phon */
    {
        { -4529262615289694208LL,  2223856631084658176LL,  2319397707738387968LL, -4529095403984148992LL,  2210469143865508608LL },
        {   265274269837931392LL,   588787208720238336LL,  2706704988542221824LL,  -235664902127274432LL,   688864401356916096LL },
    },
    /* 73 phon */
    {
        { -4526407785935123456LL,  2221032701539209728LL,  2316392775251419136LL, -4526277787528607232LL,  2210612933908001536LL },
        {   211199551497707936LL,   584856152625080704LL,  2612241116309018624LL,  -171319098046413120LL,   660976695073877120LL },
    },
    /* 75 phon */
    {
        { -4523488985334932480LL,  2218146698746236672LL,  2313383671261484032LL, -4523396092019512320LL,  2210698930013865984LL },
        {   157437848176375168LL,   581773308460217984LL,  2520941099633763328LL,  -110902591969354176LL,   635015658185878144LL },
    },
    /* 77 phon */
    {
        { -4520507651477720064LL,  2215200103186459136LL,  2310370513824201216LL, -4520451850852480000LL,  2210728399201191680LL },
        {   103912672824484496LL,   579543794145904896LL,  2432697599983670272LL,   -54266868621410952LL,   610868744821824256LL },
    },
    /* 79 phon */
    {
        { -4517452902898677760LL,  2212182379534243584LL,  2307353250617307648LL, -4517434260588528128LL,  2210690780440779776LL },
        {    50728125275401992LL,   578119439999038720LL,  2347411236157759488LL,    -1091948743144148LL,   588371287073518976LL },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q61_ONE, 0, 0 },
    },

};
#endif

#ifdef FAST
static const biquad_quotients_fast_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -1058120971,   521318017,   546321909, -1057989460,   511998531 },
        {   252053234,   153963618,   799542430,   -60155282,   203500616 },
    },
    /* 57 phon */
    {
        { -1057515682,   520717957,   545579974, -1057395344,   512129233 },
        {   239261185,   150864612,   774923158,   -42818076,   194891628 },
    },
    /* 59 phon */
    {
        { -1056896596,   520104439,   544833647, -1056787263,   512251037 },
        {   226582503,   147972775,   750957779,   -26382895,   186851306 },
    },
    /* 61 phon */
    {
        { -1056263714,   519477480,   544083830, -1056165235,   512363040 },
        {   214013914,   145284806,   727644098,   -10821770,   179347305 },
    },
    /* 63 phon */
    {
        { -1055616488,   518836556,   543331163, -1055528717,   512464076 },
        {   201538297,   142794897,   704976111,     3881224,   172346771 },
    },
    /* 65 phon */
    {
        { -1054955125,   518181884,   542576221, -1054877935,   512553764 },
        {   189146447,   140502104,   682945113,    17751753,   165822597 },
    },
    /* 67 phon */
    {
        { -1054279065,   517512934,   541819427, -1054212345,   512631139 },
        {   176828237,   138404296,   661541323,    30815328,   159746794 },
    },
    /* 69 phon */
    {
        { -1053587775,   516829199,   541061120, -1053531429,   512695338 },
        {   164574378,   136499642,   640753771,    43098815,   154092344 },
    },
    /* 71 phon */
    {
        { -1052881207,   516130651,   540301561, -1052835160,   512746048 },
        {   152384444,   134786017,   620571001,    54638272,   148832101 },
    },
    /* 73 phon */
    {
        { -1052159569,   515417511,   539540903, -1052123773,   512783316 },
        {   140236716,   133263286,   600979287,    65448040,   143943588 },
    },
    /* 75 phon */
    {
        { -1051421688,   514688655,   538779235, -1051396110,   512805910 },
        {   128121632,   131931326,   581965494,    75554568,   139403808 },
    },
    /* 77 phon */
    {
        { -1050667734,   513944271,   538016623, -1050652370,   512813925 },
        {   116052714,   130785351,   563516742,    85007166,   135185069 },
    },
    /* 79 phon */
    {
        { -1049896783,   513183476,   537253063, -1049891651,   512806458 },
        {   104012966,   129826638,   545618272,    93823625,   131268618 },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },

};

static const biquad_quotients_fast_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -1059375934,   522562441,   545561913, -1059264798,   513982577 },
        {   166644095,   151876494,   835796175,  -213042321,   232637647 },
    },
    /* 57 phon */
    {
        { -1058818558,   522009490,   544879958, -1058716855,   514102148 },
        {   152991121,   149266953,   806964151,  -189105382,   221270217 },
    },
    /* 59 phon */
    {
        { -1058248516,   521444160,   544193957, -1058156108,   514213524 },
        {   139524531,   146884419,   779081484,  -166457595,   210655973 },
    },
    /* 61 phon */
    {
        { -1057665981,   520866634,   543504645, -1057582743,   514316138 },
        {   126230669,   144723212,   752121773,  -145045631,   200748651 },
    },
    /* 63 phon */
    {
        { -1057070078,   520276062,   542812689, -1056995886,   514408476 },
        {   113090453,   142778618,   726056926,  -124822526,   191505583 },
    },
    /* 65 phon */
    {
        { -1056460973,   519672620,   542118584, -1056395721,   514490200 },
        {   100092502,   141046466,   700860133,  -105735895,   182885642 },
    },
    /* 67 phon */
    {
        { -1055838265,   519055931,   541422709, -1055781858,   514560540 },
        {    87212483,   139522363,   676503162,   -87747524,   174850121 },
    },
    /* 69 phon */
    {
        { -1055201833,   518425888,   540725397, -1055154195,   514619041 },
        {    74440352,   138203719,   652959773,   -70807560,   167362770 },
    },
    /* 71 phon */
    {
        { -1054551130,   517781971,   540026861, -1054512198,   514664954 },
        {    61763979,   137087705,   630203865,   -54870011,   160388742 },
    },
    /* 73 phon */
    {
        { -1053886438,   517124473,   539327221, -1053856171,   514698432 },
        {    49173728,   136172434,   608209780,   -39888336,   153895629 },
    },
    /* 75 phon */
    {
        { -1053206852,   516452524,   538626609, -1053185224,   514718455 },
        {    36656356,   135454654,   586952339,   -25821522,   147851104 },
    },
    /* 77 phon */
    {
        { -1052512706,   515766466,   537925054, -1052499714,   514725316 },
        {    24194054,   134935555,   566406548,   -12634990,   142228963 },
    },
    /* 79 phon */
    {
        { -1051801467,   515063847,   537222542, -1051797127,   514716557 },
        {    11811062,   134603921,   546549269,     -254239,   136990865 },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },

};
#endif

#ifdef FREERTOS_USED
static Bool loudness_state_initialized = FALSE;
static Bool loudness_rtos_initialized = FALSE;
static volatile Bool loudness_task_ready = FALSE;
#endif

#ifdef PRECISE
static biquad_quotients_precise_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];

/* Pointer to the currently active equalizer-step table */
static const biquad_quotients_precise_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

/* Active coefficients used by the real-time biquad path. */
static biquad_quotients_precise_t active_quotients[LOUDNESS_FILTERS];

static biquad_state_precise_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type);
#endif

#ifdef FAST
static biquad_quotients_fast_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];

/* Pointer to the currently active equalizer-step table */
static const biquad_quotients_fast_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

/* Active coefficients used by the real-time biquad path. */
static biquad_quotients_fast_t active_quotients[LOUDNESS_FILTERS];

static biquad_state_fast_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type);
#endif


#ifdef BUILD_TESTING
/* Current phon estimate (dB SPL); updated by background task, read on audio hot path for bypass. */
volatile S16 last_db_spl = LOUDNESS_REF_PHON;
#else
/* Current phon estimate (dB SPL); updated by background task, read on audio hot path for bypass. */
static volatile S16 last_db_spl = LOUDNESS_REF_PHON;
#endif
#ifdef FREERTOS_USED
static volatile S16 target_db_spl = LOUDNESS_REF_PHON;
static volatile S16 target_db_fs = 0;
#else
static volatile S16 target_db_fs = 0;
#endif


#define ROOT_MEAN_SQUARE_WINDOW_SHIFT 21
#define ROOT_MEAN_SQUARE_WINDOW_SIZE (1UL << ROOT_MEAN_SQUARE_WINDOW_SHIFT)


Bool source_has_volume_control = FALSE;

/* Stereo feeds left then right through loudness_update_track_level_*(), so the
 * leaky window counts 2*fs samples per second. A shift of 21 corresponds to
 * 2,097,152 samples (~22 seconds at 48 kHz stereo, ~5.5 seconds at 192 kHz).
 */
#define NUM_CHANNELS 2

/* ---------------------------------------------------------------------------
 * Internal DSP Helpers
 * -------------------------------------------------------------------------*/

/**
 * @brief Bakhshali / bitwise integer square root.
 */
static uint32_t sqrt_i(uint64_t number) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > number) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (number >= res + bit) {
            number -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

/**
 * @brief Estimate dB FS from an RMS magnitude in the 24-bit sample domain.
 *
 * Converts the RMS magnitude (post-sqrt_i, in the 24-bit sample domain) to dBFS
 * for equalizer-step blending via loudness_get_track_dbfs().
 *
 * Silence (rms == 0) returns -144 dBFS, the 24-bit dynamic-range floor, not 0 dBFS.
 *
 * Integer log2 via CLZ: bit_position = 32 - CLZ(rms) locates the MSB. An 8-bit
 * fraction below the MSB approximates the sub-bit position. Each bit is mapped to
 * 6 dB (log2(10) ~ 6.02); fractional correction is (fraction * 6) >> 8 for ~1 dB
 * resolution without floating point. Result is clamped to 0 dBFS maximum.
 * SAMPLE_24BITS (24) is the full-scale reference.
 *
 * @return A non-positive signed value in the range -144 .. 0 dBFS.
 */
static int32_t calculate_dB_24bit(uint32_t rms) {
    if (rms == 0) {
        return -144;
    }

    uint32_t leading_zeros = CLZ(rms);
    int32_t bit_position = 32 - (int32_t)leading_zeros;

    uint32_t fraction = 0;
    if (bit_position < 32) {
        fraction = (rms << (leading_zeros + 1)) >> 24;
    }

    int32_t db_base = (bit_position - SAMPLE_24BITS) * 6;
    int32_t db_fraction = (int32_t)((fraction * 6) >> 8);

    int32_t db = db_base + db_fraction;

    return (db > 0) ? 0 : db;
}


#ifdef PRECISE
/* dead code: per-sample RMS integrator (not used in gain-only equalizer path) */
void loudness_update_track_level_precise(int64_t sample) {
    uint32_t fs = current_freq.frequency;
    if (fs == 0) {
        return;
    }

    if (sample < 0) {
        sample = -sample;
    }
    uint64_t sample_sq = (uint64_t)sample * (uint64_t)sample;
    /* One call per channel; L and R form a single interleaved series whose
     * mean square matches stereo RMS^2 = (L^2 + R^2) / (2N). */
    loudness_rms_write_begin();
    if (sample_sq > root_mean_square) {
        root_mean_square += (sample_sq - root_mean_square) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    } else {
        root_mean_square -= (root_mean_square - sample_sq) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    }

    loudness_rms_write_end();
}
#endif

#ifdef FAST
/* dead code: per-sample RMS integrator (not used in gain-only equalizer path) */
void loudness_update_track_level_fast(int32_t sample) {
    uint32_t fs = current_freq.frequency;
    if (fs == 0) {
        return;
    }

    if (sample < 0) {
        sample = -sample;
    }

    // 1. SKALER NED FØRST: 24-bit -> 15-bit (Mister kun uaudbar bunnstøy)
    // Dette garanterer at kvadratet ALDRI fyller mer enn 30 bits.
    // uint32_t s = (uint32_t)(sample >> 9); 
    // uint32_t sample_sq = s * s; // 100 % ren og lynrask 32-bit multiplikasjon!
    int32_t s = sample;
    uint64_t sample_sq = mulu_d(s, s);
    /* One call per channel; L and R form a single interleaved series whose
     * mean square matches stereo RMS^2 = (L^2 + R^2) / (2N). */

    loudness_rms_write_begin();
    /* Power-of-two leaky integrator avoids expensive 64-bit division in audio path. */
    if (sample_sq > root_mean_square) {
        root_mean_square += (sample_sq - root_mean_square) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    } else {
        root_mean_square -= (root_mean_square - sample_sq) >> ROOT_MEAN_SQUARE_WINDOW_SHIFT;
    }
    loudness_rms_write_end();
}
#endif

/* ---------------------------------------------------------------------------
 * Biquad steps
 * -------------------------------------------------------------------------*/
#ifdef PRECISE
int64_t biquad_step_precise_24bit(int64_t sample, biquad_state_precise_t* biquad_states)
{
    int64_t x_n = sample;
    int filter_idx = (int)(biquad_states - loudness_states);
    const biquad_quotients_precise_t* q = &active_quotients[filter_idx];

    /* a0 is normalized to 1 (Q61/Q29 unity); not stored in biquad_quotients_*_t. */
    /* y_n += q->a0 * x_n; */

#if defined(_MSC_VER)
    /* MSVC doesn't support __int128. Use double for intermediate calculation in tests. */
    double y_n = (double)q->b0 * x_n;
    y_n += (double)q->b1 * biquad_states->xn_1;
    y_n += (double)q->b2 * biquad_states->xn_2;
    y_n -= (double)q->a1 * biquad_states->yn_1;
    y_n -= (double)q->a2 * biquad_states->yn_2;

    int64_t y_n_scaled = (int64_t)round(y_n / (double)LOUDNESS_Q61_ONE);
#elif defined(HAS_INT128)
    __int128 y_n = (__int128)q->b0 * x_n;
    y_n += (__int128)q->b1 * biquad_states->xn_1;
    y_n += (__int128)q->b2 * biquad_states->xn_2;
    y_n -= (__int128)q->a1 * biquad_states->yn_1;
    y_n -= (__int128)q->a2 * biquad_states->yn_2;

    int64_t y_n_scaled = FROM_Q61(y_n);
#else
    /* Manual 128-bit accumulation and shift for compilers without __int128 */
    int64_t y_n_scaled = mul_shift_q61(q->b0, x_n);
    y_n_scaled += mul_shift_q61(q->b1, biquad_states->xn_1);
    y_n_scaled += mul_shift_q61(q->b2, biquad_states->xn_2);
    y_n_scaled -= mul_shift_q61(q->a1, biquad_states->yn_1);
    y_n_scaled -= mul_shift_q61(q->a2, biquad_states->yn_2);
#endif

    biquad_states->xn_2 = biquad_states->xn_1;
    biquad_states->yn_2 = biquad_states->yn_1;
    biquad_states->xn_1 = x_n;
    biquad_states->yn_1 = y_n_scaled;

    return y_n_scaled;
}
#endif

#ifdef FAST
int32_t biquad_step_fast_32bit(int32_t x_n, biquad_state_fast_t* biquad_states)
{
    int filter_idx = (int)(biquad_states - loudness_states);
    const biquad_quotients_fast_t* q = &active_quotients[filter_idx];

    /* a0 is normalized to 1 (Q61/Q29 unity); not stored in biquad_quotients_*_t. */
    /* y_n += q->a0 * x_n; */

    int64_t y_n = (int64_t)q->b0 * x_n;
    y_n = FMA_24BIT(y_n, q->b1, biquad_states->xn_1);
    y_n = FMA_24BIT(y_n, q->b2, biquad_states->xn_2);
    y_n = FMS_24BIT(y_n, q->a1, biquad_states->yn_1);
    y_n = FMS_24BIT(y_n, q->a2, biquad_states->yn_2);

    int64_t y_n_scaled = FROM_Q29(y_n);
    S32 y_n_safe = saturate_24bit_s64_to_s32(y_n_scaled);

    biquad_states->xn_2 = biquad_states->xn_1;
    biquad_states->yn_2 = biquad_states->yn_1;
    biquad_states->xn_1 = x_n;
    biquad_states->yn_1 = y_n_safe;

    return y_n_safe;
}
#endif

/* ---------------------------------------------------------------------------
 * Equalizer-step selection (immediate coefficient commit)
 * -------------------------------------------------------------------------*/

#if !defined(USBSTATISTICS_DISABLE)
#include "stats_telemetry.h"

static void loudness_record_event_tag(U8 tag, U8 arg0, U8 arg1, U8 arg2)
{
    audio_stats_record_event(get_usb_stats(), tag, arg0, arg1, arg2);
}

static void loudness_record_equalizer_step_switch_event(int32_t prev_db_spl, int32_t db_spl, int equalizer_step)
{
    if (prev_db_spl != db_spl) {
        loudness_record_event_tag(USB_STATS_TAG_EQUALIZER_STEP_SWITCH,
            (U8)prev_db_spl, (U8)db_spl, (U8)equalizer_step);
    }
}

static int8_t loudness_clamp_s8(int32_t value)
{
    if (value < -128) {
        return (int8_t)-128;
    }
    if (value > 127) {
        return (int8_t)127;
    }
    return (int8_t)value;
}
#endif

#define SPEAKER_VOLUME_Q61  ((S64)1 << 61)

/* Apply the speaker volume to the feed-forward (b*) coefficients in Q61. */
static inline S64 apply_volume_q61(S64 coeff)
{
#if defined(_MSC_VER)
    return (S64)round((double)coeff * (SPEAKER_VOLUME_Q61 / (double)LOUDNESS_Q61_ONE));
#elif defined(HAS_INT128)
    __int128 result = (__int128)coeff * SPEAKER_VOLUME_Q61;
    return (S64)(result >> 61);
#else
    return mul_shift_q61(coeff, SPEAKER_VOLUME_Q61);
#endif
}

static int loudness_get_equalizer_step(int32_t db_spl) {
    if (db_spl >= LOUDNESS_REF_DB_SPL) {
        return LOUDNESS_NEUTRAL_STEP;
    }
    if (db_spl < LOUDNESS_MIN_PHON) {
        return 0;
    }
    {
        int step = (db_spl - LOUDNESS_MIN_PHON) / LOUDNESS_PHON_STEP_DB;
        if (step >= LOUDNESS_CONTOUR_STEPS) {
            step = LOUDNESS_CONTOUR_STEPS - 1;
        }
        return step;
    }
}

#ifdef BUILD_TESTING
int loudness_test_get_equalizer_step(int32_t db_spl) {
    return loudness_get_equalizer_step(db_spl);
}
#endif

static int loudness_get_current_equalizer_step(void) {
    return loudness_get_equalizer_step((int32_t)last_db_spl);
}

static Bool loudness_should_change_equalizer_step(int32_t db_spl_x10) {
    int current = loudness_get_current_equalizer_step();

    if (current == LOUDNESS_NEUTRAL_STEP) {
        return db_spl_x10 < 795;
    }

    if (current == (LOUDNESS_CONTOUR_STEPS - 1)) {
        if (db_spl_x10 >= 800) {
            return TRUE;
        }
        {
            int band_start_phon = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
            int32_t lower_x10 = band_start_phon * 10 - 5;
            return db_spl_x10 < lower_x10;
        }
    }

    {
        int band_start_phon = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
        int32_t lower_x10 = band_start_phon * 10 - 5;
        int next_phon = band_start_phon + LOUDNESS_PHON_STEP_DB;
        int32_t upper_x10 = next_phon * 10;

        if (db_spl_x10 >= upper_x10) {
            return TRUE;
        }
        if (db_spl_x10 < lower_x10) {
            return TRUE;
        }
        return FALSE;
    }
}

#ifdef BUILD_TESTING
Bool loudness_test_should_change_equalizer_step(int32_t db_spl_x10) {
    return loudness_should_change_equalizer_step(db_spl_x10);
}
#endif

#ifdef PRECISE
static void loudness_load_active_quotients_precise(int equalizer_step) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_precise_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q61(src->b0);
        int64_t b1 = apply_volume_q61(src->b1);
        int64_t b2 = apply_volume_q61(src->b2);

        active_quotients[i].b0 = b0;
        active_quotients[i].b1 = b1;
        active_quotients[i].b2 = b2;
        active_quotients[i].a1 = src->a1;
        active_quotients[i].a2 = src->a2;
    }
}

static void loudness_select_equalizer_step_precise(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;

    taskENTER_CRITICAL();
    loudness_load_active_quotients_precise(equalizer_step);
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
    taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}
#endif


#ifdef FAST
/* Unity gain on b* taps; host volume is applied per-sample via loudness_set_volume(). */
static inline int64_t apply_volume_q29(int64_t coeff) {
    (void)SPEAKER_VOLUME_Q30;
    return coeff;
}
#endif

#ifdef FAST
static void loudness_load_active_quotients_fast(int equalizer_step) {
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_fast_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q29(src->b0);
        int64_t b1 = apply_volume_q29(src->b1);
        int64_t b2 = apply_volume_q29(src->b2);

        active_quotients[i].b0 = (int32_t)b0;
        active_quotients[i].b1 = (int32_t)b1;
        active_quotients[i].b2 = (int32_t)b2;
        active_quotients[i].a1 = src->a1;
        active_quotients[i].a2 = src->a2;
    }
}

static void loudness_select_equalizer_step_fast(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;

    taskENTER_CRITICAL();
    loudness_load_active_quotients_fast(equalizer_step);
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
    taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}
#endif

static void loudness_select_equalizer_step(int32_t db_spl) {
    int equalizer_step = loudness_get_equalizer_step(db_spl);
#ifdef PRECISE
    loudness_select_equalizer_step_precise(db_spl, equalizer_step);
#endif
#ifdef FAST
    loudness_select_equalizer_step_fast(db_spl, equalizer_step);
#endif
}


#ifdef FREERTOS_USED
xQueueHandle xLoudnessFreqQueue = NULL;

static portTickType loudness_task_delay_20ms(void)
{
    if (portTICK_RATE_MS == 0) {
        return 1;
    }
    {
        portTickType delay = (portTickType)(20 / (portTICK_RATE_MS ? portTICK_RATE_MS : 1));
        return (delay == 0) ? 1 : delay;
    }
}

/* Background task to handle equalizer-step selection */
static void loudness_update_filter_by_volume_or_frequency(void *pvParameters)
{
    (void)pvParameters;

    portTickType xLastWakeTime;
    portTickType xDelay20ms;
    U32 target_frequency;

    xDelay20ms = loudness_task_delay_20ms();
    xLastWakeTime = xTaskGetTickCount();
    loudness_task_ready = TRUE;

    while (TRUE)
    {
        if (xLoudnessFreqQueue == NULL) {
            vTaskDelay(xDelay20ms);
            continue;
        }

        if (current_freq.frequency == FREQ_44 || current_freq.frequency == FREQ_48) {
            if (xQueueReceive(xLoudnessFreqQueue, &target_frequency, xDelay20ms) == pdPASS) {
                loudness_change_frequency(target_frequency);
            }
            loudness_update_active_equalizer_step();
        } else {
            (void)xQueueReceive(xLoudnessFreqQueue, &target_frequency, portMAX_DELAY);
        }
    }
}

void loudness_rtos_init(void)
{
    if (loudness_rtos_initialized) {
        return;
    }

    if (xLoudnessFreqQueue == NULL) {
        xLoudnessFreqQueue = xQueueCreate(2, sizeof(uint32_t));
        if (xLoudnessFreqQueue == NULL) {
            return;
        }
    }

    if (xTaskCreate(loudness_update_filter_by_volume_or_frequency,
                    (const signed char *)"LOUDNESS",
                    configMINIMAL_STACK_SIZE,
                    NULL,
                    (unsigned portBASE_TYPE)tskIDLE_PRIORITY + 1,
                    NULL) != pdPASS) {
        return;
    }

    loudness_rtos_initialized = TRUE;
}

Bool loudness_rtos_is_ready(void)
{
    return loudness_rtos_initialized && (xLoudnessFreqQueue != NULL) && loudness_task_ready;
}

void loudness_request_frequency_change(uint32_t frequency)
{
    if (xLoudnessFreqQueue == NULL) {
        return;
    }

    (void)xQueueSend(xLoudnessFreqQueue, &frequency, 0);
}
#endif

#if 0 /* dead code: blended track RMS + host gain SPL estimate */
static int32_t loudness_calculate_db_spl_x10_blended(void) {
    int32_t track_dbfs = loudness_get_track_dbfs();
    int32_t track_normalized = track_dbfs - LOUDNESS_TRACK_DBFS_MIN;
    int32_t host_gain_dbfs = loudness_get_gain_dbfs();
    int32_t host_gain_scaled = host_gain_dbfs * 10;
    int32_t track_scaled = track_normalized * 10;

    int32_t blended_scaled_x100 = (host_gain_scaled * LOUDNESS_GAIN_WEIGHT_PCT) +
                                  (track_scaled * LOUDNESS_TRACK_WEIGHT_PCT);

    int32_t blended_dbfs_x10 = (blended_scaled_x100 - 50) / 100;
    return blended_dbfs_x10 + (LOUDNESS_REF_DB_SPL * 10);
}
#endif

static int32_t loudness_calculate_db_spl_x10(void) {
    int32_t host_gain_dbfs = loudness_get_gain_dbfs();
    return (host_gain_dbfs * 10) + (LOUDNESS_REF_DB_SPL * 10);
}

static int32_t loudness_calculate_db_spl(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    return (db_spl_x10 + 5) / 10;
}

void loudness_update_active_equalizer_step(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    int32_t db_spl = (db_spl_x10 + 5) / 10;
    Bool db_spl_changed;

#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif

#ifdef BUILD_TESTING
    db_spl_changed = (db_spl != (int32_t)last_db_spl);
#else
    db_spl_changed = loudness_should_change_equalizer_step(db_spl_x10);
#endif

    if (db_spl_changed) {
        loudness_select_equalizer_step(db_spl);
    }
#if !defined(USBSTATISTICS_DISABLE)
    stats_telemetry_set_gain_dbfs(
        loudness_clamp_s8(loudness_get_gain_dbfs()));
#endif
}

/**
 * @brief Return the current dBFS estimate derived from the running RMS.
 * @return A non-positive signed value.
 */
int32_t loudness_get_track_rms_dbfs(void) {
    uint32_t rms = sqrt_i(loudness_rms_read());
    return calculate_dB_24bit(rms);
}

int32_t loudness_get_track_dbfs(void) {
    int32_t track_dbfs = loudness_get_track_rms_dbfs();
    if (track_dbfs < LOUDNESS_TRACK_DBFS_MIN) {
        track_dbfs = LOUDNESS_TRACK_DBFS_MIN;
    }
    if (track_dbfs > LOUDNESS_TRACK_DBFS_MAX) {
        track_dbfs = LOUDNESS_TRACK_DBFS_MAX;
    }
    return track_dbfs;
}

int32_t loudness_get_db_spl(void) {
    return loudness_calculate_db_spl();
}

/**
 * @brief Compute the current host-gain expressed in dBFS.
 * spk_vol_usb_L and VOL_MAX are 16-bit signed values in 1/256 dB units.
 */
int32_t loudness_get_gain_dbfs(void) {
    int32_t delta_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t db_fs = delta_q8 / 256;
    if (db_fs > 0) {
        db_fs = 0;
    }
    return db_fs;
}

int16_t loudness_get_last_db_spl(void) {
    return last_db_spl;
}

/* Select the active equalizer step based on a weighted blend of:
 *   - the host gain (95%), taken from spk_vol_usb_L relative to VOL_MAX
 *   - the measured track dBFS from the RMS estimator (5%)
 * The db_fs argument is treated as an externally supplied gain value; if
 * caller-supplied is > 0 it is clamped to 0. */
void loudness_set_level_dbfs(int32_t db_fs) {
    if (db_fs > 0) {
        db_fs = 0;
    }

    target_db_fs = (int16_t)db_fs;

    /* Perform the update using the shared logic. */
    loudness_update_active_equalizer_step();
}

void loudness_filter_init(void) {
#ifdef FREERTOS_USED
    if (loudness_state_initialized) {
        return;
    }
#endif

    loudness_reset_rms();

    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].xn_1 = 0;
        loudness_states[i].xn_2 = 0;
        loudness_states[i].yn_1 = 0;
        loudness_states[i].yn_2 = 0;
    }

    /* Start at the reference equalizer step (unity biquads at 80 phon). */
    loudness_select_equalizer_step((int32_t)LOUDNESS_REF_PHON);

#ifdef FREERTOS_USED
    loudness_state_initialized = TRUE;
#endif
}

/* ---------------------------------------------------------------------------
 * Public per-sample entry points
 * -------------------------------------------------------------------------*/
#ifdef PRECISE
int64_t loudness_precise_24bit(int64_t sample)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_precise_24bit(sample, &loudness_states[i]);
    }
    return sample;
}
#endif

#ifdef FAST
int64_t loudness_fast_24bit(int32_t sample)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_fast_32bit(sample, &loudness_states[i]);
    }
    return (S64)saturate_24bit_s32_to_s32(sample);
}

S32 loudness_filter_16bit_container(S32 sample)
{
    S32 x = UPSAMPLE_16BIT_TO_FILTER_32(sample);
    S32 y = (S32)loudness_fast_24bit(x);
    return DOWNSAMPLE_FILTER_TO_16BIT_CONTAINER(y);
}
#endif

#endif /* LOUDNESS_DISABLE */

/**
 * @brief First-order error-feedback quantizer for 24-bit DAC output.
 *
 * Shapes truncation noise when converting the internal high-resolution sample
 * (post-filter/volume, 32-bit with 8 fractional bits) to the 24-bit word sent
 * to the DAC. Subtracts the stored per-channel error in 64-bit, quantizes with
 * DOWNSAMPLE_24BIT_ROUND (round-half-up >> 8), reconstructs the truncated
 * value, and stores the quantization error for the next sample. Final output is
 * clamped to [INT24_MIN, INT24_MAX].
 */
int32_t loudness_apply_noise_shaper_to_output(int32_t sample_32bit, int32_t* noise_shaper_error) {
    int32_t error = *noise_shaper_error;
    int64_t accumulated_sample = (int64_t)sample_32bit - (int64_t)error;
    int32_t sample_24bit = DOWNSAMPLE_24BIT_ROUND(accumulated_sample);
    int32_t reconstructed_32bit = sample_24bit << 8;
    *noise_shaper_error = reconstructed_32bit - sample_32bit;
    if (sample_24bit > INT24_MAX) {
        return (int32_t)INT24_MAX;
    }
    if (sample_24bit < INT24_MIN) {
        return (int32_t)INT24_MIN;
    }
    return sample_24bit;
}

#ifndef LOUDNESS_DISABLE

void loudness_reset_rms(void)
{
    // seeding the integrator to a typical master signal (-6 dBFS)
    // a rms value of -6 dBFS in 24-bit is 4194394
    loudness_rms_write_begin();
    root_mean_square = 17592186044416ULL;
    loudness_rms_write_end();
}

#if defined(PRECISE)
/**
 * @brief Scale a biquad coefficient set designed for fs to a new set for fs' = n * fs.
 *
 * This approach is called an Agnostic Bilinear Frequency Scaling. It converts the digital coefficients into the analog s-domain, scales the frequency axis, and maps them back to the new z-domain.
 */
static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type; // Agnostic approach doesn't need filter_type
    if (n <= 1 || n > 4) {
        return base;
    }

    // 1. Unpack og normaliser til double
    double b0 = (double)base.b0 / (double)LOUDNESS_Q61_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q61_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q61_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q61_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q61_ONE;

    // 2. Finn den opprinnelige digitale vinkelfrekvensen w0 fra a1 og a2
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);

    // 3. Beregn det eksakte forholdet for den bilinære pre-warping-skaleringen
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);

    // 4. Invers Bilinær Transformasjon (Hent ut analoge koeffisienter)
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }

    // Analoge teller-koeffisienter
    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;

    // Analoge nevner-koeffisienter
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;

    // 5. Skaler de analoge koeffisientene direkte med k
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;

    // 6. Bilinær transformasjon tilbake til det nye z-domenet
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }

    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;

    // 7. Pakk tilbake til Q61 fixed-point
    biquad_quotients_precise_t result;
    result.b0 = (int64_t)round(b0_n * (double)LOUDNESS_Q61_ONE);
    result.b1 = (int64_t)round(b1_n * (double)LOUDNESS_Q61_ONE);
    result.b2 = (int64_t)round(b2_n * (double)LOUDNESS_Q61_ONE);
    result.a1 = (int64_t)round(a1_n * (double)LOUDNESS_Q61_ONE);
    result.a2 = (int64_t)round(a2_n * (double)LOUDNESS_Q61_ONE);

    return result;
}
#endif

#if defined(FAST)
/**
 * @brief Scale a biquad coefficient set designed for fs to a new set for fs' = n * fs.
 */
static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type; // Agnostic approach doesn't need filter_type
    if (n <= 1 || n > 4) {
        return base;
    }

    // 1. Unpack og normaliser til double
    double b0 = (double)base.b0 / (double)LOUDNESS_Q29_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q29_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q29_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q29_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q29_ONE;

    // 2. Finn den opprinnelige digitale vinkelfrekvensen w0 fra a1 og a2
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);

    // 3. Beregn det eksakte forholdet for den bilinære pre-warping-skaleringen
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);

    // 4. Invers Bilinær Transformasjon
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }

    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;

    // 5. Skaler de analoge koeffisientene
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;

    // 6. Bilinær transformasjon tilbake
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }

    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;

    // 7. Pakk tilbake til Q29
    biquad_quotients_fast_t result;
    result.b0 = (int32_t)round(b0_n * (double)LOUDNESS_Q29_ONE);
    result.b1 = (int32_t)round(b1_n * (double)LOUDNESS_Q29_ONE);
    result.b2 = (int32_t)round(b2_n * (double)LOUDNESS_Q29_ONE);
    result.a1 = (int32_t)round(a1_n * (double)LOUDNESS_Q29_ONE);
    result.a2 = (int32_t)round(a2_n * (double)LOUDNESS_Q29_ONE);

    return result;
}
#endif

#if defined(PRECISE)
void loudness_change_frequency_precise(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    uint32_t n = 1;
    const biquad_quotients_precise_t (*base_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

    if (frequency % 48000 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / 48000;
    } else if (frequency % 44100 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / 44100;
    }

    if (n > 4) {
        n = 4;
    }

    if (n > 1) {
        int b, f;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            for (f = 0; f < LOUDNESS_FILTERS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_precise(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        base_table = (const biquad_quotients_precise_t (*)[LOUDNESS_FILTERS])(void*)loudness_quotients_scaled;
    }

    // Recalculate coefficients for the current equalizer step
#ifdef FREERTOS_USED
    int32_t db_spl_val = (int32_t)target_db_spl;
#else
    int32_t db_spl_val = (int32_t)last_db_spl;
#endif
    int equalizer_step = loudness_get_equalizer_step(db_spl_val);

    taskENTER_CRITICAL();
    active_equalizer_step_table = base_table;
    loudness_load_active_quotients_precise(equalizer_step);
    taskEXIT_CRITICAL();

}
#endif

#if defined(FAST)
void loudness_change_frequency_fast(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    uint32_t n = 1;
    const biquad_quotients_fast_t (*base_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;

    if (frequency % 48000 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / 48000;
    } else if (frequency % 44100 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / 44100;
    }

    if (n > 4) {
        n = 4;
    }

    if (n > 1) {
        int b, f;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            for (f = 0; f < LOUDNESS_FILTERS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_fast(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        base_table = (const biquad_quotients_fast_t (*)[LOUDNESS_FILTERS])(void*)loudness_quotients_scaled;
    }

    // Recalculate coefficients for the current equalizer step
#ifdef FREERTOS_USED
    int32_t db_spl_val = (int32_t)target_db_spl;
#else
    int32_t db_spl_val = (int32_t)last_db_spl;
#endif
    int equalizer_step = loudness_get_equalizer_step(db_spl_val);

    taskENTER_CRITICAL();
    active_equalizer_step_table = base_table;
    loudness_load_active_quotients_fast(equalizer_step);
    taskEXIT_CRITICAL();
}
#endif

#else /* LOUDNESS_DISABLE */

void loudness_filter_init(void) {}

void loudness_reset_rms(void) {}

void loudness_set_level_dbfs(int32_t db_fs) {
    (void)db_fs;
}

void loudness_update_active_equalizer_step(void) {}

int16_t loudness_get_last_db_spl(void) {
    return LOUDNESS_REF_PHON;
}

int32_t loudness_get_track_rms_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

int32_t loudness_get_track_dbfs(void) {
    return LOUDNESS_TRACK_DBFS_MAX;
}

int32_t loudness_get_db_spl(void) {
    return LOUDNESS_REF_PHON;
}

int32_t loudness_get_gain_dbfs(void) {
    int32_t delta_q8 = (int32_t)spk_vol_usb_L - (int32_t)VOL_MAX;
    int32_t db_fs = delta_q8 / 256;
    if (db_fs > 0) {
        db_fs = 0;
    }
    return db_fs;
}

#ifdef FREERTOS_USED
void loudness_rtos_init(void) {}

Bool loudness_rtos_is_ready(void) {
    return TRUE;
}

void loudness_request_frequency_change(uint32_t frequency) {
    (void)frequency;
}
#endif

#endif /* LOUDNESS_DISABLE */
