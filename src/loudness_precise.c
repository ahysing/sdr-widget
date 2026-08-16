#ifdef PRECISE
#include "loudness_precise.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "track_dbfs.h"
#include "compiler.h"
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
static inline int64_t FROM_Q61(int64_t x) {
    int64_t sign = x >> 63;
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

#define SAMPLE_24BITS 24
#define LOUDNESS_FILTERS 2
#define LOUDNESS_Q61_ONE  ((int64_t)1 << 61)
#define LOUDNESS_SCALE_Q15_SHIFT       15
#define LOUDNESS_SCALE_Q15_UNITY       (1 << LOUDNESS_SCALE_Q15_SHIFT)

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

static biquad_quotients_precise_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];
/* Pointer to the currently active equalizer-step table */
static const biquad_quotients_precise_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;
/* Active coefficients used by the real-time biquad path. */
static biquad_quotients_precise_t active_quotients[LOUDNESS_FILTERS];
static biquad_quotients_precise_t staging_quotients[LOUDNESS_FILTERS];
static biquad_state_precise_t     loudness_states[LOUDNESS_FILTERS];
static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type);

static int64_t loudness_scale_w_q15_s64(int64_t w, int32_t factor_q15)
{
    return (int64_t)((int64_t)w * (int64_t)factor_q15 >> LOUDNESS_SCALE_Q15_SHIFT);
}

static void loudness_apply_df2_state_scale_precise(int32_t factor_q15)
{
    int i;
    if (factor_q15 == LOUDNESS_SCALE_Q15_UNITY) {
        return;
    }
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].w1 = loudness_scale_w_q15_s64(loudness_states[i].w1, factor_q15);
        loudness_states[i].w2 = loudness_scale_w_q15_s64(loudness_states[i].w2, factor_q15);
    }
}

int64_t biquad_step_precise_24bit(int64_t sample, biquad_state_precise_t* biquad_states,
    const biquad_quotients_precise_t* q)
{
    int64_t x_n = sample;
    int64_t w_n;
    int64_t y_n_scaled;
#if defined(HAS_INT128)
    __int128 acc = (__int128)q->a1 * biquad_states->w1 + (__int128)q->a2 * biquad_states->w2;
    w_n = x_n - FROM_Q61(acc);
    acc = (__int128)q->b0 * w_n + (__int128)q->b1 * biquad_states->w1 + (__int128)q->b2 * biquad_states->w2;
    y_n_scaled = FROM_Q61(acc);
#else
    w_n = x_n - mul_shift_q61(q->a1, biquad_states->w1) - mul_shift_q61(q->a2, biquad_states->w2);
    y_n_scaled = mul_shift_q61(q->b0, w_n)
               + mul_shift_q61(q->b1, biquad_states->w1)
               + mul_shift_q61(q->b2, biquad_states->w2);
#endif
    biquad_states->w2 = biquad_states->w1;
    biquad_states->w1 = (int64_t)saturate_24bit_s64_to_s32(w_n);
    return y_n_scaled;
}

#define SPEAKER_VOLUME_Q61  ((S64)1 << 61)

/* Unity gain on b* taps; host volume is applied per-sample in the audio task.
 * Reported gain_dbfs uses loudness_usb_volume_q8_to_gain_dbfs(): Windows
 * volume 0 -> LOUDNESS_GAIN_DBFS_MIN (-60), volume 100 -> 0 dBFS. */
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

static void loudness_fill_staging_quotients_precise(int equalizer_step)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_precise_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q61(src->b0);
        int64_t b1 = apply_volume_q61(src->b1);
        int64_t b2 = apply_volume_q61(src->b2);
        staging_quotients[i].b0 = b0;
        staging_quotients[i].b1 = b1;
        staging_quotients[i].b2 = b2;
        staging_quotients[i].a1 = src->a1;
        staging_quotients[i].a2 = src->a2;
    }
}
static void loudness_commit_staging_quotients_precise(void)
{
    memcpy(active_quotients, staging_quotients, sizeof(staging_quotients));
}
static void loudness_load_active_quotients_precise(int equalizer_step)
{
    loudness_fill_staging_quotients_precise(equalizer_step);
    loudness_commit_staging_quotients_precise();
}
void loudness_precise_select_equalizer_step(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    int prev_step = loudness_get_equalizer_step(prev_db_spl);
    int32_t factor_q15;
    target_gain_dbfs_q8 = (S16)loudness_clamp_gain_dbfs_q8((int32_t)target_gain_dbfs_q8);
    loudness_fill_staging_quotients_precise(equalizer_step);
    factor_q15 = loudness_combined_step_scale_q15(prev_step, equalizer_step);
    taskENTER_CRITICAL();
    loudness_apply_df2_state_scale_precise(factor_q15);
    loudness_commit_staging_quotients_precise();
    loudness_publish_equalizer_step(db_spl);
    taskEXIT_CRITICAL();
    loudness_report_equalizer_step_switch(prev_db_spl, db_spl, prev_step, equalizer_step);
}

void loudness_precise_reset_states(void)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].w1 = 0;
        loudness_states[i].w2 = 0;
    }
}

int64_t loudness_precise_24bit(int64_t sample)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_precise_24bit(sample, &loudness_states[i], &active_quotients[i]);
    }
    return sample;
}

static biquad_quotients_precise_t loudness_scale_quotients_precise(biquad_quotients_precise_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type; // Agnostic approach doesn't need filter_type
    if (n <= 1 || n > 4) {
        return base;
    }
    double b0 = (double)base.b0 / (double)LOUDNESS_Q61_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q61_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q61_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q61_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q61_ONE;
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }
    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }
    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;
    biquad_quotients_precise_t result;
    result.b0 = (int64_t)round(b0_n * (double)LOUDNESS_Q61_ONE);
    result.b1 = (int64_t)round(b1_n * (double)LOUDNESS_Q61_ONE);
    result.b2 = (int64_t)round(b2_n * (double)LOUDNESS_Q61_ONE);
    result.a1 = (int64_t)round(a1_n * (double)LOUDNESS_Q61_ONE);
    result.a2 = (int64_t)round(a2_n * (double)LOUDNESS_Q61_ONE);
    return result;
}

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
    int equalizer_step = loudness_get_equalizer_step(loudness_internal_current_db_spl());
    active_equalizer_step_table = base_table;
    loudness_fill_staging_quotients_precise(equalizer_step);
    taskENTER_CRITICAL();
    loudness_commit_staging_quotients_precise();
    taskEXIT_CRITICAL();
    loudness_inferred_gain_set_rate(frequency);
}

#endif /* PRECISE */
