#include "loudness_fast.h"
#include "loudness_highres.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "taskAK5394A.h"
#include "compiler.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <math.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#else
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
S64 macs_d(S64 d, S32 a, S32 b) {
    __asm__ ("macs.d %0, %1, %2" : "+r"(d) : "r"(a), "r"(b));
    return d;
}
#define FMA_24BIT(A, B, C) macs_d(A, B, C)
#define FMS_24BIT(A, B, C) macs_d(A, B, -C)
#else
#define FMA_24BIT(A, B, C) \
    ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) \
    ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))
#endif

#define LOUDNESS_FILTERS 1

/* Coefficients and the DF-II accumulator use Q4.28 throughout. */
#define LOUDNESS_DF2_Q28_SHIFT          28
#define LOUDNESS_DF2_Q28_ROUND          (1LL << (LOUDNESS_DF2_Q28_SHIFT - 1))
#define LOUDNESS_Q28_ONE                ((int32_t)1 << LOUDNESS_DF2_Q28_SHIFT)

/*
 * Canonical DF-II accumulates pole gain before zeros attenuate. At 50 Hz the
 * internal delay line can grow ~5000x larger than x[n] (~13 bits), overflowing
 * int32_t w1/w2 even when input and output stay in range.
 *
 * Fix: store w1/w2 right-shifted by M bits (w' = w >> M). Stored history
 * products are lifted by M before the Q4.28 pole and zero sums.
 */
#define LOUDNESS_DF2_STATE_HEADROOM_M   13
#define LOUDNESS_EQUALIZER_STEP_UNSET   UINT8_MAX

#if defined(__GNUC__)
#define LOUDNESS_FAST_INLINE static __attribute__((always_inline)) inline
#else
#define LOUDNESS_FAST_INLINE static inline
#endif

static inline void loudness_fast_memory_barrier(void)
{
#if defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
#endif
}

static uint8_t loudness_fast_committed_step[LOUDNESS_CHANNELS] = {
    LOUDNESS_EQUALIZER_STEP_UNSET, LOUDNESS_EQUALIZER_STEP_UNSET
};
static uint32_t loudness_fast_frequency_hz;
static Bool loudness_channel_idle_cached[LOUDNESS_CHANNELS] = { TRUE, TRUE };

static const biquad_quotients_fast_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -531749993,   263329846,      276562,     -531625,      255328 },  /* phon=35.0 volume=-60.0 dB */
    {  -531689456,   263269645,      292872,     -563063,      270469 },  /* phon=35.5 volume=-59.5 dB */
    {  -531628309,   263208843,      310143,     -596360,      286508 },  /* phon=36.0 volume=-59.0 dB */
    {  -531566900,   263147783,      328431,     -631625,      303500 },  /* phon=36.5 volume=-58.5 dB */
    {  -531505266,   263086504,      347796,     -668976,      321500 },  /* phon=37.0 volume=-58.0 dB */
    {  -531443352,   263024949,      368302,     -708534,      340568 },  /* phon=37.5 volume=-57.5 dB */
    {  -531381186,   262963148,      390016,     -750432,      360768 },  /* phon=38.0 volume=-57.0 dB */
    {  -531318525,   262900860,      413009,     -794806,      382167 },  /* phon=38.5 volume=-56.5 dB */
    {  -531256003,   262838713,      437356,     -841805,      404836 },  /* phon=39.0 volume=-56.0 dB */
    {  -531192791,   262775885,      463137,     -891581,      428850 },  /* phon=39.5 volume=-55.5 dB */
    {  -531129442,   262712924,      490436,     -944301,      454290 },  /* phon=40.0 volume=-55.0 dB */
    {  -531065825,   262649702,      519343,    -1000137,      481239 },  /* phon=40.5 volume=-54.5 dB */
    {  -531001887,   262586164,      549953,    -1059275,      509788 },  /* phon=41.0 volume=-54.0 dB */
    {  -530937623,   262522307,      582366,    -1121908,      540031 },  /* phon=41.5 volume=-53.5 dB */
    {  -530873041,   262458137,      616687,    -1188244,      572069 },  /* phon=42.0 volume=-53.0 dB */
    {  -530808093,   262393610,      653030,    -1258501,      606008 },  /* phon=42.5 volume=-52.5 dB */
    {  -530742845,   262328788,      691512,    -1332912,      641961 },  /* phon=43.0 volume=-52.0 dB */
    {  -530677259,   262263635,      732262,    -1411722,      680047 },  /* phon=43.5 volume=-51.5 dB */
    {  -530611261,   262198078,      775411,    -1495190,      720394 },  /* phon=44.0 volume=-51.0 dB */
    {  -530544934,   262132198,      821101,    -1583592,      763135 },  /* phon=44.5 volume=-50.5 dB */
    {  -530478307,   262066026,      869481,    -1677219,      808412 },  /* phon=45.0 volume=-50.0 dB */
    {  -530411301,   261999482,      920711,    -1776382,      856376 },  /* phon=45.5 volume=-49.5 dB */
    {  -530343908,   261932558,      974957,    -1881405,      907186 },  /* phon=46.0 volume=-49.0 dB */
    {  -530276090,   261865217,     1032398,    -1992636,      961010 },  /* phon=46.5 volume=-48.5 dB */
    {  -530207970,   261797583,     1093222,    -2110442,     1018028 },  /* phon=47.0 volume=-48.0 dB */
    {  -530139450,   261729556,     1157627,    -2235211,     1078429 },  /* phon=47.5 volume=-47.5 dB */
    {  -530070555,   261661161,     1225825,    -2367354,     1142414 },  /* phon=48.0 volume=-47.0 dB */
    {  -530001256,   261592371,     1298039,    -2507308,     1210195 },  /* phon=48.5 volume=-46.5 dB */
    {  -529931502,   261523135,     1374505,    -2655533,     1281996 },  /* phon=49.0 volume=-46.0 dB */
    {  -529861428,   261453587,     1455473,    -2812519,     1358058 },  /* phon=49.5 volume=-45.5 dB */
    {  -529790943,   261383637,     1541210,    -2978783,     1438632 },  /* phon=50.0 volume=-45.0 dB */
    {  -529720119,   261313355,     1631995,    -3154874,     1523986 },  /* phon=50.5 volume=-44.5 dB */
    {  -529648873,   261242660,     1728126,    -3341371,     1614404 },  /* phon=51.0 volume=-44.0 dB */
    {  -529577201,   261171549,     1829918,    -3538891,     1710185 },  /* phon=51.5 volume=-43.5 dB */
    {  -529505050,   261099970,     1937703,    -3748083,     1811647 },  /* phon=52.0 volume=-43.0 dB */
    {  -529432600,   261028098,     2051835,    -3969638,     1919128 },  /* phon=52.5 volume=-42.5 dB */
    {  -529359715,   260955801,     2172688,    -4204286,     2032984 },  /* phon=53.0 volume=-42.0 dB */
    {  -529286466,   260883149,     2300657,    -4452802,     2153593 },  /* phon=53.5 volume=-41.5 dB */
    {  -529212667,   260809959,     2436161,    -4716002,     2281355 },  /* phon=54.0 volume=-41.0 dB */
    {  -529138484,   260736393,     2579644,    -4994755,     2416695 },  /* phon=54.5 volume=-40.5 dB */
    {  -529064000,   260662535,     2731575,    -5289982,     2560062 },  /* phon=55.0 volume=-40.0 dB */
    {  -528988916,   260588090,     2892452,    -5602653,     2711931 },  /* phon=55.5 volume=-39.5 dB */
    {  -528913576,   260513397,     3062803,    -5933802,     2872807 },  /* phon=56.0 volume=-39.0 dB */
    {  -528837721,   260438199,     3243183,    -6284517,     3043223 },  /* phon=56.5 volume=-38.5 dB */
    {  -528761414,   260362562,     3434185,    -6655954,     3223745 },  /* phon=57.0 volume=-38.0 dB */
    {  -528684778,   260286603,     3636433,    -7049341,     3414972 },  /* phon=57.5 volume=-37.5 dB */
    {  -528607598,   260210113,     3850590,    -7465970,     3617538 },  /* phon=58.0 volume=-37.0 dB */
    {  -528530031,   260133247,     4077357,    -7907216,     3832115 },  /* phon=58.5 volume=-36.5 dB */
    {  -528452069,   260055995,     4317476,    -8374535,     4059416 },  /* phon=59.0 volume=-36.0 dB */
    {  -528373618,   259978268,     4571733,    -8869463,     4300193 },  /* phon=59.5 volume=-35.5 dB */
    {  -528294698,   259900082,     4840961,    -9393632,     4555245 },  /* phon=60.0 volume=-35.0 dB */
    {  -528215433,   259821561,     5126041,    -9948772,     4825420 },  /* phon=60.5 volume=-34.5 dB */
    {  -528135640,   259742527,     5427907,   -10536709,     5111612 },  /* phon=61.0 volume=-34.0 dB */
    {  -528055562,   259663215,     5747546,   -11159384,     5414773 },  /* phon=61.5 volume=-33.5 dB */
    {  -527974899,   259583333,     6086006,   -11818844,     5735904 },  /* phon=62.0 volume=-33.0 dB */
    {  -527893742,   259502970,     6444395,   -12517261,     6076070 },  /* phon=62.5 volume=-32.5 dB */
    {  -527812167,   259422200,     6823885,   -13256940,     6436402 },  /* phon=63.0 volume=-32.0 dB */
    {  -527730286,   259341134,     7225719,   -14040320,     6818097 },  /* phon=63.5 volume=-31.5 dB */
    {  -527647715,   259259396,     7651214,   -14869971,     7222410 },  /* phon=64.0 volume=-31.0 dB */
    {  -527564954,   259177475,     8101760,   -15748641,     7650696 },  /* phon=64.5 volume=-30.5 dB */
    {  -527481526,   259094904,     8578835,   -16679210,     8104360 },  /* phon=65.0 volume=-30.0 dB */
    {  -527397615,   259011863,     9083999,   -17664749,     8584912 },  /* phon=65.5 volume=-29.5 dB */
    {  -527313546,   258928671,     9618907,   -18708515,     9093955 },  /* phon=66.0 volume=-29.0 dB */
    {  -527228757,   258844779,    10185309,   -19813926,     9633157 },  /* phon=66.5 volume=-28.5 dB */
    {  -527143436,   258760369,    10785059,   -20984630,    10204312 },  /* phon=67.0 volume=-28.0 dB */
    {  -527057659,   258675517,    11420123,   -22224485,    10809315 },  /* phon=67.5 volume=-27.5 dB */
    {  -526971542,   258590336,    12092577,   -23537580,    11450174 },  /* phon=68.0 volume=-27.0 dB */
    {  -526884898,   258504642,    12804624,   -24928231,    12129007 },  /* phon=68.5 volume=-26.5 dB */
    {  -526797934,   258418641,    13558595,   -26401028,    12848072 },  /* phon=69.0 volume=-26.0 dB */
    {  -526710351,   258332038,    14356957,   -27960806,    13609736 },  /* phon=69.5 volume=-25.5 dB */
    {  -526622159,   258244843,    15202326,   -29612701,    14416523 },  /* phon=70.0 volume=-25.0 dB */
    {  -526533778,   258157467,    16097467,   -31362176,    15271128 },  /* phon=70.5 volume=-24.5 dB */
    {  -526444866,   258069578,    17045311,   -33214973,    16176363 },  /* phon=71.0 volume=-24.0 dB */
    {  -526355237,   257980991,    18048961,   -35177179,    17135215 },  /* phon=71.5 volume=-23.5 dB */
    {  -526265335,   257892142,    19111702,   -37255284,    18150886 },  /* phon=72.0 volume=-23.0 dB */
    {  -526174879,   257802757,    20237014,   -39456112,    19226723 },  /* phon=72.5 volume=-22.5 dB */
    {  -526083894,   257712858,    21428580,   -41786908,    20366288 },  /* phon=73.0 volume=-22.0 dB */
    {  -525992576,   257622641,    22690300,   -44255362,    21573371 },  /* phon=73.5 volume=-21.5 dB */
    {  -525900518,   257531704,    24026305,   -46869566,    22851935 },  /* phon=74.0 volume=-21.0 dB */
    {  -525808259,   257440576,    25440968,   -49638173,    24206259 },  /* phon=74.5 volume=-20.5 dB */
    {  -525714997,   257348475,    26938919,   -52570221,    25640753 },  /* phon=75.0 volume=-20.0 dB */
    {  -525621299,   257255950,    28525063,   -55675413,    27160215 },  /* phon=75.5 volume=-19.5 dB */
    {  -525527838,   257163666,    30204589,   -58964045,    28769751 },  /* phon=76.0 volume=-19.0 dB */
    {  -525432960,   257070003,    31982997,   -62446760,    30474508 },  /* phon=76.5 volume=-18.5 dB */
    {  -525338324,   256976579,    33866108,   -66135210,    32280315 },  /* phon=77.0 volume=-18.0 dB */
    {  -525242931,   256882423,    35860086,   -70041417,    34193033 },  /* phon=77.5 volume=-17.5 dB */
    {  -525146891,   256787643,    37971455,   -74178246,    36219004 },  /* phon=78.0 volume=-17.0 dB */
    {  -525049693,   256691742,    40207128,   -78559232,    38364851 },  /* phon=78.5 volume=-16.5 dB */
    {  -524952988,   256596323,    42574423,   -83199035,    40637913 },  /* phon=79.0 volume=-16.0 dB */
    {  -524854492,   256499169,    45081087,   -88112568,    43045365 },  /* phon=79.5 volume=-15.5 dB */
    {  -524761105,   256406279,    47735324,   -93317187,    45596201 },  /* phon=80.0 volume=-15.0 dB */
    {  -524658206,   256305556,    50545823,   -98827437,    48296728 },  /* phon=80.5 volume=-14.5 dB */
    {  -524558400,   256207144,    53521782,  -104663674,    51157666 },  /* phon=81.0 volume=-14.0 dB */
    {  -524459500,   256109615,    56672940,  -110844757,    54188270 },  /* phon=81.5 volume=-13.5 dB */
    {  -524359102,   256010637,    60009611,  -117390535,    57398090 },  /* phon=82.0 volume=-13.0 dB */
    {  -524258189,   255911165,    63542716,  -124322738,    60797933 },  /* phon=82.5 volume=-12.5 dB */
    {  -524157147,   255811572,    67283817,  -131664269,    64399137 },  /* phon=83.0 volume=-12.0 dB */
    {  -524054652,   255710572,    71245156,  -139438943,    68213283 },  /* phon=83.5 volume=-11.5 dB */
    {  -523951729,   255609168,    75439703,  -147672580,    72253221 },  /* phon=84.0 volume=-11.0 dB */
    {  -523849285,   255508231,    79881180,  -156392538,    76532575 },  /* phon=84.5 volume=-10.5 dB */
    {  -523745845,   255406334,    84584125,  -165627082,    81065090 },  /* phon=85.0 volume=-10.0 dB */
    {  -523641665,   255303726,    89563928,  -175406648,    85865809 },  /* phon=85.5 volume=-9.5 dB */
    {  -523536661,   255200325,    94836884,  -185763358,    90950562 },  /* phon=86.0 volume=-9.0 dB */
    {  -523431087,   255096377,   100420247,  -196731349,    96336230 },  /* phon=86.5 volume=-8.5 dB */
    {  -523325445,   254992370,   106332295,  -208346887,   102040801 },  /* phon=87.0 volume=-8.0 dB */
    {  -523218918,   254887513,   112592369,  -220647858,   108082827 },  /* phon=87.5 volume=-7.5 dB */
    {  -523111933,   254782219,   119220957,  -233674877,   114482434 },  /* phon=88.0 volume=-7.0 dB */
    {  -523004115,   254676125,   126239746,  -247470608,   121260605 },  /* phon=88.5 volume=-6.5 dB */
    {  -522895957,   254569710,   133671708,  -262080634,   128439948 },  /* phon=89.0 volume=-6.0 dB */
    {  -522787109,   254462636,   141541160,  -277552825,   136044022 },  /* phon=89.5 volume=-5.5 dB */
    {  -522677755,   254355077,   149873850,  -293938138,   144098034 },  /* phon=90.0 volume=-5.0 dB */
    {  -522567972,   254247116,   158697067,  -311290492,   152628620 },  /* phon=90.5 volume=-4.5 dB */
    {  -522458032,   254139014,   168039688,  -329667116,   161664128 },  /* phon=91.0 volume=-4.0 dB */
    {  -522347399,   254030254,   177932283,  -349128106,   171234097 },  /* phon=91.5 volume=-3.5 dB */
    {  -522236546,   253921290,   188407223,  -369737754,   181370440 },  /* phon=92.0 volume=-3.0 dB */
    {  -522125184,   253811845,   199498781,  -391563633,   192106465 },  /* phon=92.5 volume=-2.5 dB */
    {  -522013341,   253701946,   211243261,  -414677514,   203477641 },  /* phon=93.0 volume=-2.0 dB */
    {  -521900895,   253591476,   223679085,  -439155281,   215521437 },  /* phon=93.5 volume=-1.5 dB */
    {  -521788162,   253480738,   236846954,  -465077660,   228277872 },  /* phon=94.0 volume=-1.0 dB */
    {  -521674809,   253369414,   250789949,  -492529583,   241788808 },  /* phon=94.5 volume=-0.5 dB */
    {  -521560664,   253257335,   265553687,  -521601091,   256098677 },  /* phon=95.0 volume=0.0 dB */
};

static const biquad_quotients_fast_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -532163589,   263741067,      275903,     -532058,      256379 },  /* phon=35.0 volume=-60.0 dB */
    {  -532107899,   263685660,      292180,     -563527,      271581 },  /* phon=35.5 volume=-59.5 dB */
    {  -532051663,   263629715,      309417,     -596856,      287686 },  /* phon=36.0 volume=-59.0 dB */
    {  -531995201,   263573548,      327669,     -632157,      304746 },  /* phon=36.5 volume=-58.5 dB */
    {  -531938499,   263517146,      346997,     -669544,      322818 },  /* phon=37.0 volume=-58.0 dB */
    {  -531881575,   263460526,      367464,     -709143,      341963 },  /* phon=37.5 volume=-57.5 dB */
    {  -531824387,   263403647,      389137,     -751084,      362244 },  /* phon=38.0 volume=-57.0 dB */
    {  -531766758,   263346333,      412087,     -795504,      383728 },  /* phon=38.5 volume=-56.5 dB */
    {  -531709039,   263288932,      436390,     -842551,      406487 },  /* phon=39.0 volume=-56.0 dB */
    {  -531651077,   263231293,      462125,     -892380,      430597 },  /* phon=39.5 volume=-55.5 dB */
    {  -531592814,   263173358,      489376,     -945155,      456138 },  /* phon=40.0 volume=-55.0 dB */
    {  -531534288,   263115166,      518233,    -1001051,      483194 },  /* phon=40.5 volume=-54.5 dB */
    {  -531475466,   263056681,      548790,    -1060253,      511856 },  /* phon=41.0 volume=-54.0 dB */
    {  -531416342,   262997901,      581148,    -1122954,      542219 },  /* phon=41.5 volume=-53.5 dB */
    {  -531356953,   262938862,      615413,    -1189363,      574383 },  /* phon=42.0 volume=-53.0 dB */
    {  -531297236,   262879499,      651696,    -1259699,      608456 },  /* phon=42.5 volume=-52.5 dB */
    {  -531237196,   262819820,      690117,    -1334193,      644550 },  /* phon=43.0 volume=-52.0 dB */
    {  -531176861,   262759851,      730802,    -1413092,      682786 },  /* phon=43.5 volume=-51.5 dB */
    {  -531116143,   262699506,      773884,    -1496655,      723291 },  /* phon=44.0 volume=-51.0 dB */
    {  -531055143,   262638884,      819505,    -1585160,      766199 },  /* phon=44.5 volume=-50.5 dB */
    {  -530993809,   262577934,      867813,    -1678896,      811652 },  /* phon=45.0 volume=-50.0 dB */
    {  -530932126,   262516641,      918968,    -1778175,      859803 },  /* phon=45.5 volume=-49.5 dB */
    {  -530870134,   262455046,      973136,    -1883323,      910810 },  /* phon=46.0 volume=-49.0 dB */
    {  -530807797,   262393113,     1030496,    -1994687,      964844 },  /* phon=46.5 volume=-48.5 dB */
    {  -530745127,   262330852,     1091235,    -2112635,     1022083 },  /* phon=47.0 volume=-48.0 dB */
    {  -530682079,   262268221,     1155553,    -2237556,     1082717 },  /* phon=47.5 volume=-47.5 dB */
    {  -530618676,   262205241,     1223660,    -2369862,     1146949 },  /* phon=48.0 volume=-47.0 dB */
    {  -530554871,   262141867,     1295780,    -2509989,     1214991 },  /* phon=48.5 volume=-46.5 dB */
    {  -530490757,   262078191,     1372149,    -2658400,     1287069 },  /* phon=49.0 volume=-46.0 dB */
    {  -530426279,   262014157,     1453017,    -2815585,     1363423 },  /* phon=49.5 volume=-45.5 dB */
    {  -530361419,   261949750,     1538649,    -2982061,     1444307 },  /* phon=50.0 volume=-45.0 dB */
    {  -530296213,   261885003,     1629326,    -3158378,     1529988 },  /* phon=50.5 volume=-44.5 dB */
    {  -530230555,   261819812,     1725346,    -3345117,     1620750 },  /* phon=51.0 volume=-44.0 dB */
    {  -530164702,   261754432,     1827022,    -3542896,     1716898 },  /* phon=51.5 volume=-43.5 dB */
    {  -530098423,   261688634,     1934688,    -3752365,     1818747 },  /* phon=52.0 volume=-43.0 dB */
    {  -530031733,   261622433,     2048698,    -3974216,     1926638 },  /* phon=52.5 volume=-42.5 dB */
    {  -529964554,   261555754,     2169424,    -4209179,     2040926 },  /* phon=53.0 volume=-42.0 dB */
    {  -529897048,   261488753,     2297262,    -4458030,     2161993 },  /* phon=53.5 volume=-41.5 dB */
    {  -529829255,   261421474,     2432631,    -4721592,     2290240 },  /* phon=54.0 volume=-41.0 dB */
    {  -529760979,   261353720,     2575975,    -5000730,     2426093 },  /* phon=54.5 volume=-40.5 dB */
    {  -529692399,   261285671,     2727764,    -5296368,     2570003 },  /* phon=55.0 volume=-40.0 dB */
    {  -529623421,   261217230,     2888495,    -5609480,     2722446 },  /* phon=55.5 volume=-39.5 dB */
    {  -529554029,   261148386,     3058696,    -5941097,     2883930 },  /* phon=56.0 volume=-39.0 dB */
    {  -529484215,   261079128,     3238922,    -6292313,     3054988 },  /* phon=56.5 volume=-38.5 dB */
    {  -529413969,   261009448,     3429767,    -6664287,     3236190 },  /* phon=57.0 volume=-38.0 dB */
    {  -529343302,   260939356,     3631854,    -7058244,     3428135 },  /* phon=57.5 volume=-37.5 dB */
    {  -529272375,   260869012,     3845846,    -7475486,     3631464 },  /* phon=58.0 volume=-37.0 dB */
    {  -529201004,   260798232,     4072445,    -7917386,     3846847 },  /* phon=58.5 volume=-36.5 dB */
    {  -529129283,   260727112,     4312392,    -8385402,     4075001 },  /* phon=59.0 volume=-36.0 dB */
    {  -529057039,   260655480,     4566476,    -8881075,     4316680 },  /* phon=59.5 volume=-35.5 dB */
    {  -528984430,   260583493,     4835527,    -9406041,     4572688 },  /* phon=60.0 volume=-35.0 dB */
    {  -528911470,   260511162,     5120428,    -9962031,     4843875 },  /* phon=60.5 volume=-34.5 dB */
    {  -528838123,   260438454,     5422113,   -10550878,     5131139 },  /* phon=61.0 volume=-34.0 dB */
    {  -528764288,   260365270,     5741570,   -11174520,     5435430 },  /* phon=61.5 volume=-33.5 dB */
    {  -528690052,   260291694,     6079847,   -11835015,     5757761 },  /* phon=62.0 volume=-33.0 dB */
    {  -528615467,   260217779,     6438051,   -12534542,     6099199 },  /* phon=62.5 volume=-32.5 dB */
    {  -528540312,   260143305,     6817356,   -13275401,     6460873 },  /* phon=63.0 volume=-32.0 dB */
    {  -528464827,   260068512,     7219007,   -14060039,     6843988 },  /* phon=63.5 volume=-31.5 dB */
    {  -528389056,   259993441,     7644318,   -14891045,     7249814 },  /* phon=64.0 volume=-31.0 dB */
    {  -528312721,   259917819,     8094684,   -15771149,     7679690 },  /* phon=64.5 volume=-30.5 dB */
    {  -528235924,   259841747,     8571580,   -16703255,     8135043 },  /* phon=65.0 volume=-30.0 dB */
    {  -528158630,   259765190,     9076570,   -17690433,     8617382 },  /* phon=65.5 volume=-29.5 dB */
    {  -528081203,   259688505,     9611309,   -18735949,     9128315 },  /* phon=66.0 volume=-29.0 dB */
    {  -528002897,   259610962,    10177547,   -19843223,     9669513 },  /* phon=66.5 volume=-28.5 dB */
    {  -527924496,   259533330,    10777142,   -21015930,    10242796 },  /* phon=67.0 volume=-28.0 dB */
    {  -527845683,   259455296,    11412059,   -22257925,    10850052 },  /* phon=67.5 volume=-27.5 dB */
    {  -527766361,   259376767,    12084377,   -23573296,    11493290 },  /* phon=68.0 volume=-27.0 dB */
    {  -527686641,   259297850,    12796300,   -24966381,    12174647 },  /* phon=68.5 volume=-26.5 dB */
    {  -527606390,   259218417,    13550160,   -26441765,    12896372 },  /* phon=69.0 volume=-26.0 dB */
    {  -527525826,   259138680,    14348429,   -28004319,    13660867 },  /* phon=69.5 volume=-25.5 dB */
    {  -527444655,   259058352,    15193722,   -29659176,    14470651 },  /* phon=70.0 volume=-25.0 dB */
    {  -527363283,   258977832,    16088809,   -31411811,    15328428 },  /* phon=70.5 volume=-24.5 dB */
    {  -527281412,   258896826,    17036622,   -33267980,    16237023 },  /* phon=71.0 volume=-24.0 dB */
    {  -527198973,   258815267,    18040269,   -35233795,    17199441 },  /* phon=71.5 volume=-23.5 dB */
    {  -527116006,   258733194,    19103037,   -37315731,    18218870 },  /* phon=72.0 volume=-23.0 dB */
    {  -527032930,   258651019,    20228409,   -39520679,    19298716 },  /* phon=72.5 volume=-22.5 dB */
    {  -526949115,   258568125,    21420072,   -41855854,    20442512 },  /* phon=73.0 volume=-22.0 dB */
    {  -526864757,   258484702,    22681933,   -44328963,    21654056 },  /* phon=73.5 volume=-21.5 dB */
    {  -526780185,   258401076,    24018123,   -46948178,    22937388 },  /* phon=74.0 volume=-21.0 dB */
    {  -526695018,   258316870,    25433024,   -49722094,    24296725 },  /* phon=74.5 volume=-20.5 dB */
    {  -526609679,   258232500,    26931269,   -52659887,    25736607 },  /* phon=75.0 volume=-20.0 dB */
    {  -526523421,   258147236,    28517770,   -55771159,    27261729 },  /* phon=75.5 volume=-19.5 dB */
    {  -526437439,   258062246,    30197722,   -59066281,    28877263 },  /* phon=76.0 volume=-19.0 dB */
    {  -526350046,   257975882,    31976633,   -62555920,    30588372 },  /* phon=76.5 volume=-18.5 dB */
    {  -526262476,   257889348,    33860330,   -66251703,    32400855 },  /* phon=77.0 volume=-18.0 dB */
    {  -526173914,   257801848,    35854985,   -70165697,    34320609 },  /* phon=77.5 volume=-17.5 dB */
    {  -526085973,   257714960,    37967133,   -74311007,    36354200 },  /* phon=78.0 volume=-17.0 dB */
    {  -525996961,   257627035,    40203694,   -78701054,    38508138 },  /* phon=78.5 volume=-16.5 dB */
    {  -525907224,   257538403,    42571998,   -83350334,    40789585 },  /* phon=79.0 volume=-16.0 dB */
    {  -525817334,   257449622,    45079803,   -88274243,    43206178 },  /* phon=79.5 volume=-15.5 dB */
    {  -525742953,   257375564,    47735324,   -93491787,    45768567 },  /* phon=80.0 volume=-15.0 dB */
    {  -525634493,   257269105,    50547264,   -99011298,    48476823 },  /* phon=80.5 volume=-14.5 dB */
    {  -525544665,   257180400,    53524834,  -104860381,    51348883 },  /* phon=81.0 volume=-14.0 dB */
    {  -525452508,   257089430,    56677790,  -111054502,    54390629 },  /* phon=81.5 volume=-13.5 dB */
    {  -525359923,   256998051,    60016461,  -117614413,    57612473 },  /* phon=82.0 volume=-13.0 dB */
    {  -525267275,   256906615,    63551786,  -124561794,    61025158 },  /* phon=82.5 volume=-12.5 dB */
    {  -525174109,   256814678,    67295346,  -131919418,    64639876 },  /* phon=83.0 volume=-12.0 dB */
    {  -525080392,   256722209,    71259406,  -139711492,    68468574 },  /* phon=83.5 volume=-11.5 dB */
    {  -524985713,   256628810,    75456952,  -147963546,    72523796 },  /* phon=84.0 volume=-11.0 dB */
    {  -524890712,   256535102,    79901735,  -156702906,    76819118 },  /* phon=84.5 volume=-10.5 dB */
    {  -524795403,   256441100,    84608317,  -165958349,    81368753 },  /* phon=85.0 volume=-10.0 dB */
    {  -524699293,   256346323,    89592114,  -175760179,    86187595 },  /* phon=85.5 volume=-9.5 dB */
    {  -524602792,   256251173,    94869456,  -186140781,    91291699 },  /* phon=86.0 volume=-9.0 dB */
    {  -524505804,   256155554,   100457626,  -197134286,    96697911 },  /* phon=86.5 volume=-8.5 dB */
    {  -524408311,   256059450,   106374936,  -208776862,   102424094 },  /* phon=87.0 volume=-8.0 dB */
    {  -524310247,   255962798,   112640766,  -221106793,   108489150 },  /* phon=87.5 volume=-7.5 dB */
    {  -524211715,   255865697,   119275640,  -234164688,   114913166 },  /* phon=88.0 volume=-7.0 dB */
    {  -524112234,   255767680,   126301289,  -247993289,   121717158 },  /* phon=88.5 volume=-6.5 dB */
    {  -524012446,   255669371,   133740736,  -262638377,   128923883 },  /* phon=89.0 volume=-6.0 dB */
    {  -523912430,   255570848,   141618344,  -278148193,   136557217 },  /* phon=89.5 volume=-5.5 dB */
    {  -523811526,   255471470,   149959916,  -294573418,   144642048 },  /* phon=90.0 volume=-5.0 dB */
    {  -523710445,   255371929,   158792795,  -311968472,   153205448 },  /* phon=90.5 volume=-4.5 dB */
    {  -523609037,   255272079,   168145913,  -330390519,   162275652 },  /* phon=91.0 volume=-4.0 dB */
    {  -523507121,   255171749,   178049919,  -349900057,   171882515 },  /* phon=91.5 volume=-3.5 dB */
    {  -523404990,   255071216,   188537243,  -370561469,   182057986 },  /* phon=92.0 volume=-3.0 dB */
    {  -523302291,   254970144,   199642245,  -392442491,   192835450 },  /* phon=92.5 volume=-2.5 dB */
    {  -523199103,   254868606,   211401297,  -415615152,   204250563 },  /* phon=93.0 volume=-2.0 dB */
    {  -523095679,   254766848,   223852923,  -440155883,   216341232 },  /* phon=93.5 volume=-1.5 dB */
    {  -522991544,   254664412,   237037909,  -466145023,   229147021 },  /* phon=94.0 volume=-1.0 dB */
    {  -522887151,   254561733,   250999432,  -493668441,   242710614 },  /* phon=94.5 volume=-0.5 dB */
    {  -522782041,   254458371,   265783229,  -522816245,   257076394 },  /* phon=95.0 volume=0.0 dB */
};

static biquad_quotients_fast_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS];
static const biquad_quotients_fast_t *active_equalizer_step_table =
    loudness_quotients_44100hz;
static biquad_quotients_fast_t staging_quotients[LOUDNESS_CHANNELS];
static biquad_runtime_fast_t staging_runtime[LOUDNESS_CHANNELS];
static biquad_quotients_fast_t loudness_quotients_bank[2][LOUDNESS_CHANNELS];
static biquad_runtime_fast_t loudness_runtime_bank[2][LOUDNESS_CHANNELS];
static volatile uint8_t loudness_active_quotients_bank;
static biquad_state_fast_t     loudness_states[LOUDNESS_CHANNELS];

static biquad_quotients_fast_t loudness_scale_quotients_fast(
    biquad_quotients_fast_t base, uint32_t n);

static const biquad_runtime_fast_t *loudness_fast_active_runtime(void)
{
    return loudness_runtime_bank[loudness_active_quotients_bank & 1u];
}

static void loudness_runtime_from_quotients(const biquad_quotients_fast_t *src,
    biquad_runtime_fast_t *dst)
{
    dst->b0 = src->b0;
    dst->b1 = src->b1;
    dst->b2 = src->b2;
    dst->a1 = src->a1;
    dst->a2 = src->a2;
}

static inline int32_t loudness_saturate_s64_to_s32(int64_t value)
{
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
LOUDNESS_FAST_INLINE int32_t loudness_biquad1_step_runtime_inline(int32_t x_n,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int64_t acc;
    int64_t fb;
    int64_t w_unscaled;
    int32_t w0;
    int32_t y_n;

    fb = FMS_24BIT(FMS_24BIT(0, rt->a1, st->w1), rt->a2, st->w2);
    acc = ((int64_t)x_n << LOUDNESS_DF2_Q28_SHIFT)
        + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    w_unscaled = acc >> LOUDNESS_DF2_Q28_SHIFT;

    fb = FMA_24BIT(FMA_24BIT(0, rt->b1, st->w1), rt->b2, st->w2);
    acc = (int64_t)rt->b0 * w_unscaled
        + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    y_n = saturate_24bit_s64_to_s32(
        (acc + LOUDNESS_DF2_Q28_ROUND) >> LOUDNESS_DF2_Q28_SHIFT);

    w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w2 = st->w1;
    st->w1 = w0;
    return y_n;
}

int32_t loudness_fast_biquad1_step_runtime(int32_t x_n, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *rt)
{
    return loudness_biquad1_step_runtime_inline(x_n, st, rt);
}

#define LOUDNESS_BIQUAD1_STEP loudness_biquad1_step_runtime_inline
#else
int32_t loudness_fast_biquad1_step_runtime(int32_t x_n,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int64_t acc;
    int64_t fb;
    int64_t w_unscaled;
    int32_t w0;
    int32_t y_n;

    fb = FMA_24BIT(FMA_24BIT(0, rt->a1, st->w1), rt->a2, st->w2);
    acc = ((int64_t)x_n << LOUDNESS_DF2_Q28_SHIFT)
        - (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    w_unscaled = acc >> LOUDNESS_DF2_Q28_SHIFT;

    fb = FMA_24BIT(FMA_24BIT(0, rt->b1, st->w1), rt->b2, st->w2);
    acc = (int64_t)rt->b0 * w_unscaled
        + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    y_n = saturate_24bit_s64_to_s32(
        (acc + LOUDNESS_DF2_Q28_ROUND) >> LOUDNESS_DF2_Q28_SHIFT);

    w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w2 = st->w1;
    st->w1 = w0;
    return y_n;
}

#define LOUDNESS_BIQUAD1_STEP loudness_fast_biquad1_step_runtime
#endif

#define LOUDNESS_FAST_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(fn_name, shift_plus_one, counter_mask) \
int32_t fn_name(int32_t x_24, biquad_state_fast_t *st,                                      \
    loudness_highres_channel_state_t *ch, const biquad_runtime_fast_t *rt)                  \
{                                                                                             \
    if (ch->sample_counter == 0) {                                                            \
        int32_t y_true = loudness_fast_biquad1_step_runtime(x_24, st, rt);                  \
        ch->y_derivative = (y_true - ch->y_prev_biquad) >> ((shift_plus_one) - 1);           \
        ch->y_current_est = y_true;                                                           \
        ch->y_prev_biquad = y_true;                                                           \
        ch->sample_counter = 1;                                                               \
        return y_true;                                                                        \
    }                                                                                         \
    ch->y_current_est += ch->y_derivative;                                                    \
    ch->sample_counter++;                                                                     \
    ch->sample_counter &= (counter_mask);                                                     \
    return ch->y_current_est;                                                                 \
}

LOUDNESS_FAST_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(
    loudness_fast_biquad1_step_runtime_stride4, 3, 0x03)
LOUDNESS_FAST_DEFINE_BIQUAD1_STEP_RUNTIME_STRIDE(
    loudness_fast_biquad1_step_runtime_stride2, 2, 0x01)

static inline int32_t loudness_downsample_filter_to_16bit_container(int32_t y_24)
{
    int32_t s;

    s = y_24 >> 8;
    if (s > INT16_MAX) {
        s = INT16_MAX;
    } else if (s < INT16_MIN) {
        s = INT16_MIN;
    }
    return s << 16;
}

LOUDNESS_FAST_INLINE int32_t loudness_biquad1_16bit_container_step(int32_t x_container,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int32_t x_24;
    int32_t y_24;

    x_24 = (int32_t)(int16_t)(x_container >> 16) << 8;
    y_24 = LOUDNESS_BIQUAD1_STEP(x_24, st, rt);
    return loudness_downsample_filter_to_16bit_container(y_24);
}

#ifdef BUILD_TESTING
void loudness_test_filter_16bit_stereo_packet_hires_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples)
{
    loudness_highres_test_filter_16bit_stereo_packet_fullrate(sample_L, sample_R,
        &loudness_states[0], &loudness_states[1],
        loudness_fast_active_runtime(), num_samples);
}
#endif

int32_t biquad_step_fast_24bit(int32_t x_n, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q)
{
    biquad_runtime_fast_t rt;

    loudness_runtime_from_quotients(q, &rt);
    return LOUDNESS_BIQUAD1_STEP(x_n, biquad_states, &rt);
}

int32_t biquad_step_fast_32bit(int32_t x_n, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q)
{
    return biquad_step_fast_24bit(x_n, biquad_states, q);
}

static Bool loudness_fast_share_master_row(void)
{
    return loudness_highres_applies(loudness_fast_frequency_hz);
}

static int loudness_fast_runtime_channel(int channel)
{
    return loudness_fast_share_master_row() ? 0 : channel;
}

static void loudness_fill_staging_quotients_fast(int equalizer_step_left,
    int equalizer_step_right)
{
    int channel;
    const biquad_quotients_fast_t *hires_src_left;
    const biquad_quotients_fast_t *hires_src_right;

    if (loudness_fast_share_master_row()) {
        equalizer_step_right = equalizer_step_left;
    }
    hires_src_left = loudness_highres_halfrate_quotients(
        loudness_fast_frequency_hz, equalizer_step_left,
        loudness_quotients_44100hz, loudness_quotients_48000hz);
    if (loudness_fast_share_master_row()) {
        staging_quotients[0] =
            active_equalizer_step_table[equalizer_step_left];
        loudness_runtime_from_quotients(&staging_quotients[0],
            &staging_runtime[0]);
        loudness_highres_staging_fill_halfrate_shared(hires_src_left,
            staging_runtime);
        return;
    }

    hires_src_right = loudness_highres_halfrate_quotients(
        loudness_fast_frequency_hz, equalizer_step_right,
        loudness_quotients_44100hz, loudness_quotients_48000hz);
    for (channel = 0; channel < LOUDNESS_CHANNELS; channel++) {
        int step = (channel == 0) ? equalizer_step_left : equalizer_step_right;
        staging_quotients[channel] = active_equalizer_step_table[step];
        loudness_runtime_from_quotients(&staging_quotients[channel],
            &staging_runtime[channel]);
    }
    loudness_highres_staging_fill_halfrate_independent(
        hires_src_left, hires_src_right, staging_runtime);
}

static void loudness_commit_staging_quotients_fast(void)
{
    uint8_t inactive = (uint8_t)(loudness_active_quotients_bank ^ 1u);

    memcpy(loudness_quotients_bank[inactive], staging_quotients,
        sizeof(staging_quotients));
    memcpy(loudness_runtime_bank[inactive], staging_runtime,
        sizeof(staging_runtime));
    loudness_highres_staging_commit();
    loudness_fast_memory_barrier();
    loudness_active_quotients_bank = inactive;
}

#ifdef BUILD_TESTING
static const biquad_quotients_fast_t *loudness_fast_active_quotients(void)
{
    return loudness_quotients_bank[loudness_active_quotients_bank & 1u];
}

void loudness_test_load_active_quotients_fast(int equalizer_step)
{
    loudness_fill_staging_quotients_fast(equalizer_step, equalizer_step);
    taskENTER_CRITICAL();
    loudness_commit_staging_quotients_fast();
    taskEXIT_CRITICAL();
    loudness_fast_committed_step[0] = (uint8_t)equalizer_step;
    loudness_fast_committed_step[1] = (uint8_t)equalizer_step;
}

void loudness_test_get_fast_channel(int channel,
    biquad_state_fast_t *state, biquad_quotients_fast_t *quotients)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return;
    }
    taskENTER_CRITICAL();
    *state = loudness_states[channel];
    if (quotients != NULL) {
        int quotient_channel = loudness_fast_share_master_row() ? 0 : channel;
        *quotients = loudness_fast_active_quotients()[quotient_channel];
    }
    taskEXIT_CRITICAL();
}

void loudness_test_set_fast_channel(int channel,
    const biquad_state_fast_t *state)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return;
    }
    taskENTER_CRITICAL();
    loudness_states[channel] = *state;
    taskEXIT_CRITICAL();
}

#endif

void loudness_fast_select_equalizer_steps(int32_t db_spl_x10,
    int equalizer_step_left, int equalizer_step_right)
{
    int32_t prev_db_spl_x10 = (int32_t)last_db_spl_x10;
    int prev_step = loudness_get_equalizer_step((int32_t)last_db_spl_x10);

    if (loudness_fast_share_master_row()) {
        equalizer_step_right = equalizer_step_left;
    }
    if (equalizer_step_left == (int)loudness_fast_committed_step[0]
        && equalizer_step_right == (int)loudness_fast_committed_step[1]) {
        loudness_publish_equalizer_step(db_spl_x10);
        return;
    }

    loudness_fill_staging_quotients_fast(equalizer_step_left,
        equalizer_step_right);
    loudness_commit_staging_quotients_fast();
    loudness_fast_committed_step[0] = (uint8_t)equalizer_step_left;
    loudness_fast_committed_step[1] = (uint8_t)equalizer_step_right;
    loudness_publish_equalizer_step(db_spl_x10);

    loudness_report_equalizer_step_switch(prev_db_spl_x10, db_spl_x10, prev_step,
        equalizer_step_left);
}

static Bool loudness_fast_channel_biquad_is_idle(int channel)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return TRUE;
    }
    return loudness_states[channel].w1 == 0
        && loudness_states[channel].w2 == 0;
}

static void loudness_filter_refresh_channel_idle_cache(int channel)
{
    if (!loudness_fast_channel_biquad_is_idle(channel)) {
        loudness_channel_idle_cached[channel] = FALSE;
        return;
    }
    if (loudness_highres_applies(loudness_fast_frequency_hz)) {
        loudness_channel_idle_cached[channel] =
            loudness_highres_channel_is_idle(channel);
        return;
    }
    loudness_channel_idle_cached[channel] = TRUE;
}

static void loudness_filter_refresh_idle_cache(void)
{
    loudness_filter_refresh_channel_idle_cache(0);
    loudness_filter_refresh_channel_idle_cache(1);
}

static Bool loudness_stereo_packet_all_zero(const S32 *sample_L,
    const S32 *sample_R, U16 num_samples)
{
    U16 i;

    for (i = 0; i < num_samples; i++) {
        if (sample_L[i] != 0 || sample_R[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

void loudness_fast_reset_states(void)
{
    taskENTER_CRITICAL();
    memset(loudness_states, 0, sizeof(loudness_states));
    taskEXIT_CRITICAL();
    loudness_highres_reset_states();
    loudness_filter_refresh_idle_cache();
}

int32_t loudness_fast_24bit(int channel, int32_t sample)
{
    const biquad_runtime_fast_t *runtime;
    biquad_state_fast_t *st;

    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        channel = 0;
    }
    st = &loudness_states[channel];

    if (sample == 0 && loudness_channel_idle_cached[channel]) {
        return saturate_24bit_s32_to_s32(0);
    }

    runtime = loudness_fast_active_runtime();
    sample = LOUDNESS_BIQUAD1_STEP(sample, st,
        &runtime[loudness_fast_runtime_channel(channel)]);
    loudness_filter_refresh_channel_idle_cache(channel);
    return saturate_24bit_s32_to_s32(sample);
}

S32 loudness_filter_16bit_container(int channel, S32 sample)
{
    biquad_state_fast_t *st;

    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        channel = 0;
    }
    st = &loudness_states[channel];

    if (sample == 0 && loudness_channel_idle_cached[channel]) {
        return 0;
    }

    sample = loudness_biquad1_16bit_container_step(sample, st,
        &loudness_fast_active_runtime()[loudness_fast_runtime_channel(channel)]);
    loudness_filter_refresh_channel_idle_cache(channel);
    return sample;
}

void loudness_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R, U16 num_samples)
{
    const biquad_runtime_fast_t *runtime;
    biquad_state_fast_t *stL;
    biquad_state_fast_t *stR;
    Bool idle_L;
    Bool idle_R;
    int i;

    if (loudness_channel_idle_cached[0] && loudness_channel_idle_cached[1]
            && loudness_stereo_packet_all_zero(sample_L, sample_R, num_samples)) {
        return;
    }

    stL = &loudness_states[0];
    stR = &loudness_states[1];

    runtime = loudness_fast_active_runtime();

    if (loudness_highres_applies(loudness_fast_frequency_hz)) {
        if (loudness_fast_share_master_row()) {
            loudness_highres_filter_16bit_stereo_packet_shared(
                sample_L, sample_R, stL, stR, num_samples);
        } else {
            loudness_highres_filter_16bit_stereo_packet_independent(
                sample_L, sample_R, stL, stR, num_samples);
        }
        loudness_filter_refresh_idle_cache();
        return;
    }

    idle_L = loudness_channel_idle_cached[0];
    idle_R = loudness_channel_idle_cached[1];
    for (i = 0; i < num_samples; i++) {
        if (sample_L[i] == 0 && idle_L) {
            ;
        } else {
            sample_L[i] = loudness_biquad1_16bit_container_step(sample_L[i], stL,
                &runtime[0]);
            idle_L = loudness_fast_channel_biquad_is_idle(0);
        }
        if (sample_R[i] == 0 && idle_R) {
            ;
        } else {
            sample_R[i] = loudness_biquad1_16bit_container_step(sample_R[i], stR,
                &runtime[1]);
            idle_R = loudness_fast_channel_biquad_is_idle(1);
        }
    }
    loudness_channel_idle_cached[0] = idle_L;
    loudness_channel_idle_cached[1] = idle_R;
}

S32 loudness_filter_24bit_container(int channel, S32 sample)
{
    /*
     * USB 24-bit samples occupy bits 31:8 of the S32 container.  The FAST
     * biquad itself uses signed 24-bit sample units, so shift down before
     * filtering and restore the container alignment afterwards.
     */
    S32 x = sample >> 8;
    S32 y = loudness_fast_24bit(channel, x);
    return y << 8;
}

static biquad_quotients_fast_t loudness_scale_quotients_fast(
    biquad_quotients_fast_t base, uint32_t n)
{
    if (n <= 1 || n > 4) {
        return base;
    }
    double b0 = (double)base.b0 / (double)LOUDNESS_Q28_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q28_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q28_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q28_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q28_ONE;
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
    biquad_quotients_fast_t result;
    result.b0 = (int32_t)round(b0_n * (double)LOUDNESS_Q28_ONE);
    result.b1 = (int32_t)round(b1_n * (double)LOUDNESS_Q28_ONE);
    result.b2 = (int32_t)round(b2_n * (double)LOUDNESS_Q28_ONE);
    result.a1 = (int32_t)round(a1_n * (double)LOUDNESS_Q28_ONE);
    result.a2 = (int32_t)round(a2_n * (double)LOUDNESS_Q28_ONE);
    return result;
}

void loudness_change_frequency_fast(uint32_t frequency) {
    loudness_fast_frequency_hz = frequency;
    if (loudness_highres_applies(frequency)) {
        loudness_highres_set_stride(frequency);
    }
    uint32_t n = 1;
    const biquad_quotients_fast_t *base_table = loudness_quotients_44100hz;
    if (frequency % FREQ_48 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / FREQ_48;
    } else if (frequency % FREQ_44 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / FREQ_44;
    }
    if (n > 4) {
        n = 4;
    }
    if (n > 1) {
        int b;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            loudness_quotients_scaled[b] = loudness_scale_quotients_fast(
                base_table[b], n);
        }
        base_table = loudness_quotients_scaled;
    }
    {
        int32_t db_spl_left_x10;
        int32_t db_spl_right_x10;
        int equalizer_step_left;
        int equalizer_step_right;

        loudness_internal_current_stereo_db_spl_x10(
            &db_spl_left_x10, &db_spl_right_x10);
        equalizer_step_left = loudness_get_equalizer_step(db_spl_left_x10);
        equalizer_step_right = loudness_get_equalizer_step(db_spl_right_x10);

        active_equalizer_step_table = base_table;
        loudness_fill_staging_quotients_fast(equalizer_step_left,
            equalizer_step_right);
        loudness_commit_staging_quotients_fast();
        loudness_fast_committed_step[0] = (uint8_t)equalizer_step_left;
        loudness_fast_committed_step[1] = (uint8_t)(
            loudness_fast_share_master_row()
                ? equalizer_step_left : equalizer_step_right);
    }
    loudness_filter_refresh_idle_cache();
    loudness_inferred_gain_set_rate(frequency);
}

Bool loudness_channel_biquad_is_idle(int channel)
{
    return loudness_fast_channel_biquad_is_idle(channel);
}

Bool loudness_channel_filter_idle_cached(int channel)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return TRUE;
    }
    return loudness_channel_idle_cached[channel];
}

Bool loudness_channel_filter_is_idle(int channel)
{
    return loudness_channel_filter_idle_cached(channel);
}

Bool loudness_filter_is_active(void)
{
    return !loudness_channel_idle_cached[0]
        || !loudness_channel_idle_cached[1];
}

