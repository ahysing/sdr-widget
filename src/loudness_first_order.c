#include "loudness_first_order.h"
#include "loudness_fast.h"
#include "loudness.h"
#include "loudness_internal.h"
#include "taskAK5394A.h"
#include <string.h>

/*
 * Canonical DF-II accumulates pole gain before zeros attenuate. At 50 Hz the
 * internal delay line can grow ~5000x larger than x[n] (~13 bits), overflowing
 * int32_t w1/w2 even when input and output stay in range.
 *
 * Fix: store w1/w2 right-shifted by M bits (w' = w >> M). Stored history
 * products are lifted by M before the Q4.28 pole and zero sums.
 */

const biquad_first_order_coefficients_t
highshelf_no_volume_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -182898219,   268435456,  -182898219 },  /* phon=25.0 volume=-60.0 dB fs=44100 Hz biquad */
    {   -45138489,   268435456,   -45138489 },  /* phon=25.5 volume=-59.5 dB fs=44100 Hz biquad */
    {  -200087642,   268435456,  -200087642 },  /* phon=26.0 volume=-59.0 dB fs=44100 Hz biquad */
    {  -201078655,   268435456,  -201078655 },  /* phon=26.5 volume=-58.5 dB fs=44100 Hz biquad */
    {  -201078496,   268435456,  -201078496 },  /* phon=27.0 volume=-58.0 dB fs=44100 Hz biquad */
    {   -82238573,   268435456,   -82238573 },  /* phon=27.5 volume=-57.5 dB fs=44100 Hz biquad */
    {   -39227550,   268435456,   -39227550 },  /* phon=28.0 volume=-57.0 dB fs=44100 Hz biquad */
    {  -200042780,   268435456,  -200042780 },  /* phon=28.5 volume=-56.5 dB fs=44100 Hz biquad */
    {  -201078714,   268435456,  -201078714 },  /* phon=29.0 volume=-56.0 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=29.5 volume=-55.5 dB fs=44100 Hz biquad */
    {  -152518178,   268435456,  -152518178 },  /* phon=30.0 volume=-55.0 dB fs=44100 Hz biquad */
    {  -201081572,   268435456,  -201081572 },  /* phon=30.5 volume=-54.5 dB fs=44100 Hz biquad */
    {  -201083918,   268435456,  -201083918 },  /* phon=31.0 volume=-54.0 dB fs=44100 Hz biquad */
    {  -158149569,   268435456,  -158149569 },  /* phon=31.5 volume=-53.5 dB fs=44100 Hz biquad */
    {  -201078701,   268435456,  -201078701 },  /* phon=32.0 volume=-53.0 dB fs=44100 Hz biquad */
    {   -39686646,   268435456,   -39686646 },  /* phon=32.5 volume=-52.5 dB fs=44100 Hz biquad */
    {  -201083915,   268435456,  -201083915 },  /* phon=33.0 volume=-52.0 dB fs=44100 Hz biquad */
    {   -38994595,   268435456,   -38994595 },  /* phon=33.5 volume=-51.5 dB fs=44100 Hz biquad */
    {  -201083928,   268435456,  -201083928 },  /* phon=34.0 volume=-51.0 dB fs=44100 Hz biquad */
    {  -201083918,   268435456,  -201083918 },  /* phon=34.5 volume=-50.5 dB fs=44100 Hz biquad */
    {  -201083929,   268435456,  -201083929 },  /* phon=35.0 volume=-50.0 dB fs=44100 Hz biquad */
    {   -38994777,   268435456,   -38994777 },  /* phon=35.5 volume=-49.5 dB fs=44100 Hz biquad */
    {   -74913870,   268435456,   -74913870 },  /* phon=36.0 volume=-49.0 dB fs=44100 Hz biquad */
    {  -200046185,   268435456,  -200046185 },  /* phon=36.5 volume=-48.5 dB fs=44100 Hz biquad */
    {   -39246034,   268435456,   -39246034 },  /* phon=37.0 volume=-48.0 dB fs=44100 Hz biquad */
    {   -38994552,   268435456,   -38994552 },  /* phon=37.5 volume=-47.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=38.0 volume=-47.0 dB fs=44100 Hz biquad */
    {  -200037405,   268435456,  -200037405 },  /* phon=38.5 volume=-46.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=39.0 volume=-46.0 dB fs=44100 Hz biquad */
    {   -39006556,   268435456,   -39006556 },  /* phon=39.5 volume=-45.5 dB fs=44100 Hz biquad */
    {   -38994514,   268435456,   -38994514 },  /* phon=40.0 volume=-45.0 dB fs=44100 Hz biquad */
    {   -38997927,   268435456,   -38997927 },  /* phon=40.5 volume=-44.5 dB fs=44100 Hz biquad */
    {   -81585129,   268435456,   -81585129 },  /* phon=41.0 volume=-44.0 dB fs=44100 Hz biquad */
    {  -201073819,   268435456,  -201073819 },  /* phon=41.5 volume=-43.5 dB fs=44100 Hz biquad */
    {  -201079667,   268435456,  -201079667 },  /* phon=42.0 volume=-43.0 dB fs=44100 Hz biquad */
    {  -198290999,   268435456,  -198290999 },  /* phon=42.5 volume=-42.5 dB fs=44100 Hz biquad */
    {   -39700897,   268435456,   -39700897 },  /* phon=43.0 volume=-42.0 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=43.5 volume=-41.5 dB fs=44100 Hz biquad */
    {   -69727036,   268435456,   -69727036 },  /* phon=44.0 volume=-41.0 dB fs=44100 Hz biquad */
    {   -38994512,   268435456,   -38994512 },  /* phon=44.5 volume=-40.5 dB fs=44100 Hz biquad */
    {   -38994511,   268435456,   -38994511 },  /* phon=45.0 volume=-40.0 dB fs=44100 Hz biquad */
    {   -39677774,   268435456,   -39677774 },  /* phon=45.5 volume=-39.5 dB fs=44100 Hz biquad */
    {  -200022106,   268435456,  -200022106 },  /* phon=46.0 volume=-39.0 dB fs=44100 Hz biquad */
    {   -74162051,   268435456,   -74162051 },  /* phon=46.5 volume=-38.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=47.0 volume=-38.0 dB fs=44100 Hz biquad */
    {  -201083928,   268435456,  -201083928 },  /* phon=47.5 volume=-37.5 dB fs=44100 Hz biquad */
    {   -38994527,   268435456,   -38994527 },  /* phon=48.0 volume=-37.0 dB fs=44100 Hz biquad */
    {  -200041005,   268435456,  -200041005 },  /* phon=48.5 volume=-36.5 dB fs=44100 Hz biquad */
    {   -38994517,   268435456,   -38994517 },  /* phon=49.0 volume=-36.0 dB fs=44100 Hz biquad */
    {   -39677892,   268435456,   -39677892 },  /* phon=49.5 volume=-35.5 dB fs=44100 Hz biquad */
    {  -201083929,   268435456,  -201083929 },  /* phon=50.0 volume=-35.0 dB fs=44100 Hz biquad */
    {  -201049275,   268435456,  -201049275 },  /* phon=50.5 volume=-34.5 dB fs=44100 Hz biquad */
    {  -201070269,   268435456,  -201070269 },  /* phon=51.0 volume=-34.0 dB fs=44100 Hz biquad */
    {   -38996662,   268435456,   -38996662 },  /* phon=51.5 volume=-33.5 dB fs=44100 Hz biquad */
    {   -39678150,   268435456,   -39678150 },  /* phon=52.0 volume=-33.0 dB fs=44100 Hz biquad */
    {  -201083929,   268435456,  -201083929 },  /* phon=52.5 volume=-32.5 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=53.0 volume=-32.0 dB fs=44100 Hz biquad */
    {   -93321661,   268435456,   -93321661 },  /* phon=53.5 volume=-31.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=54.0 volume=-31.0 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=54.5 volume=-30.5 dB fs=44100 Hz biquad */
    {   -38999709,   268435456,   -38999709 },  /* phon=55.0 volume=-30.0 dB fs=44100 Hz biquad */
    {   -38994522,   268435456,   -38994522 },  /* phon=55.5 volume=-29.5 dB fs=44100 Hz biquad */
    {   -39678163,   268435456,   -39678163 },  /* phon=56.0 volume=-29.0 dB fs=44100 Hz biquad */
    {   -38994512,   268435456,   -38994512 },  /* phon=56.5 volume=-28.5 dB fs=44100 Hz biquad */
    {   -38994553,   268435456,   -38994553 },  /* phon=57.0 volume=-28.0 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=57.5 volume=-27.5 dB fs=44100 Hz biquad */
    {   -38997910,   268435456,   -38997910 },  /* phon=58.0 volume=-27.0 dB fs=44100 Hz biquad */
    {   -39677886,   268435456,   -39677886 },  /* phon=58.5 volume=-26.5 dB fs=44100 Hz biquad */
    {  -200041038,   268435456,  -200041038 },  /* phon=59.0 volume=-26.0 dB fs=44100 Hz biquad */
    {  -200041585,   268435456,  -200041585 },  /* phon=59.5 volume=-25.5 dB fs=44100 Hz biquad */
    {   -38998779,   268435456,   -38998779 },  /* phon=60.0 volume=-25.0 dB fs=44100 Hz biquad */
    {  -200041435,   268435456,  -200041435 },  /* phon=60.5 volume=-24.5 dB fs=44100 Hz biquad */
    {  -201083911,   268435456,  -201083911 },  /* phon=61.0 volume=-24.0 dB fs=44100 Hz biquad */
    {   -39677785,   268435456,   -39677785 },  /* phon=61.5 volume=-23.5 dB fs=44100 Hz biquad */
    {  -201083929,   268435456,  -201083929 },  /* phon=62.0 volume=-23.0 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=62.5 volume=-22.5 dB fs=44100 Hz biquad */
    {  -201083922,   268435456,  -201083922 },  /* phon=63.0 volume=-22.0 dB fs=44100 Hz biquad */
    {  -200425148,   268435456,  -200425148 },  /* phon=63.5 volume=-21.5 dB fs=44100 Hz biquad */
    {  -201083902,   268435456,  -201083902 },  /* phon=64.0 volume=-21.0 dB fs=44100 Hz biquad */
    {   -38994594,   268435456,   -38994594 },  /* phon=64.5 volume=-20.5 dB fs=44100 Hz biquad */
    {   -39677512,   268435456,   -39677512 },  /* phon=65.0 volume=-20.0 dB fs=44100 Hz biquad */
    {   -39674760,   268435456,   -39674760 },  /* phon=65.5 volume=-19.5 dB fs=44100 Hz biquad */
    {  -200041005,   268435456,  -200041005 },  /* phon=66.0 volume=-19.0 dB fs=44100 Hz biquad */
    {  -201083800,   268435456,  -201083800 },  /* phon=66.5 volume=-18.5 dB fs=44100 Hz biquad */
    {   -71853207,   268435456,   -71853207 },  /* phon=67.0 volume=-18.0 dB fs=44100 Hz biquad */
    {   -38994555,   268435456,   -38994555 },  /* phon=67.5 volume=-17.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=68.0 volume=-17.0 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=68.5 volume=-16.5 dB fs=44100 Hz biquad */
    {  -141059032,   268435456,  -141059032 },  /* phon=69.0 volume=-16.0 dB fs=44100 Hz biquad */
    {   -38997953,   268435456,   -38997953 },  /* phon=69.5 volume=-15.5 dB fs=44100 Hz biquad */
    {   -38994513,   268435456,   -38994513 },  /* phon=70.0 volume=-15.0 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=70.5 volume=-14.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=71.0 volume=-14.0 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=71.5 volume=-13.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=72.0 volume=-13.0 dB fs=44100 Hz biquad */
    {   -38995209,   268435456,   -38995209 },  /* phon=72.5 volume=-12.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=73.0 volume=-12.0 dB fs=44100 Hz biquad */
    {  -200228534,   268435456,  -200228534 },  /* phon=73.5 volume=-11.5 dB fs=44100 Hz biquad */
    {  -201081424,   268435456,  -201081424 },  /* phon=74.0 volume=-11.0 dB fs=44100 Hz biquad */
    {   -69315036,   268435456,   -69315036 },  /* phon=74.5 volume=-10.5 dB fs=44100 Hz biquad */
    {   -38994510,   268435456,   -38994510 },  /* phon=75.0 volume=-10.0 dB fs=44100 Hz biquad */
    {  -201083929,   268435456,  -201083929 },  /* phon=75.5 volume=-9.5 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=76.0 volume=-9.0 dB fs=44100 Hz biquad */
    {  -200824711,   268435456,  -200824711 },  /* phon=76.5 volume=-8.5 dB fs=44100 Hz biquad */
    {   -38995471,   268435456,   -38995471 },  /* phon=77.0 volume=-8.0 dB fs=44100 Hz biquad */
    {   -68263746,   268435456,   -68263746 },  /* phon=77.5 volume=-7.5 dB fs=44100 Hz biquad */
    {  -201083930,   268435456,  -201083930 },  /* phon=78.0 volume=-7.0 dB fs=44100 Hz biquad */
    {  -201081225,   268435456,  -201081225 },  /* phon=78.5 volume=-6.5 dB fs=44100 Hz biquad */
    {  -201014593,   268435456,  -201014593 },  /* phon=79.0 volume=-6.0 dB fs=44100 Hz biquad */
    {  -145067094,   268435456,  -145067094 },  /* phon=79.5 volume=-5.5 dB fs=44100 Hz biquad */
    {   -60472975,   268435456,   -60472975 },  /* phon=80.0 volume=-5.0 dB fs=44100 Hz biquad */
    {   -40506436,   266666571,   -38737551 },  /* phon=80.5 volume=-4.5 dB fs=44100 Hz biquad */
    {   -41995566,   264924357,   -38484467 },  /* phon=81.0 volume=-4.0 dB fs=44100 Hz biquad */
    {   -43462432,   263208190,   -38235166 },  /* phon=81.5 volume=-3.5 dB fs=44100 Hz biquad */
    {   -44907553,   261517464,   -37989562 },  /* phon=82.0 volume=-3.0 dB fs=44100 Hz biquad */
    {   -46331429,   259851595,   -37747568 },  /* phon=82.5 volume=-2.5 dB fs=44100 Hz biquad */
    {   -47734547,   258210011,   -37509101 },  /* phon=83.0 volume=-2.0 dB fs=44100 Hz biquad */
    {   -49117373,   256592166,   -37274084 },  /* phon=83.5 volume=-1.5 dB fs=44100 Hz biquad */
    {   -50480367,   254997527,   -37042437 },  /* phon=84.0 volume=-1.0 dB fs=44100 Hz biquad */
    {   -51823970,   253425572,   -36814086 },  /* phon=84.5 volume=-0.5 dB fs=44100 Hz biquad */
    {   -53148613,   251875800,   -36588957 },  /* phon=85.0 volume=0.0 dB fs=44100 Hz biquad */
};

const biquad_first_order_coefficients_t
highshelf_no_volume_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -192754160,   268435456,  -192754160 },  /* phon=25.0 volume=-60.0 dB fs=48000 Hz biquad */
    {  -205904895,   268435456,  -205904895 },  /* phon=25.5 volume=-59.5 dB fs=48000 Hz biquad */
    {  -190937001,   268435456,  -190937001 },  /* phon=26.0 volume=-59.0 dB fs=48000 Hz biquad */
    {   -53417599,   268435456,   -53417599 },  /* phon=26.5 volume=-58.5 dB fs=48000 Hz biquad */
    {   -53395416,   268435456,   -53395416 },  /* phon=27.0 volume=-58.0 dB fs=48000 Hz biquad */
    {   -53395133,   268435456,   -53395133 },  /* phon=27.5 volume=-57.5 dB fs=48000 Hz biquad */
    {  -205002508,   268435456,  -205002508 },  /* phon=28.0 volume=-57.0 dB fs=48000 Hz biquad */
    {   -93922214,   268435456,   -93922214 },  /* phon=28.5 volume=-56.5 dB fs=48000 Hz biquad */
    {   -53594503,   268435456,   -53594503 },  /* phon=29.0 volume=-56.0 dB fs=48000 Hz biquad */
    {  -205942866,   268435456,  -205942866 },  /* phon=29.5 volume=-55.5 dB fs=48000 Hz biquad */
    {  -205977591,   268435456,  -205977591 },  /* phon=30.0 volume=-55.0 dB fs=48000 Hz biquad */
    {   -53395466,   268435456,   -53395466 },  /* phon=30.5 volume=-54.5 dB fs=48000 Hz biquad */
    {  -205976988,   268435456,  -205976988 },  /* phon=31.0 volume=-54.0 dB fs=48000 Hz biquad */
    {  -205976769,   268435456,  -205976769 },  /* phon=31.5 volume=-53.5 dB fs=48000 Hz biquad */
    {   -53398365,   268435456,   -53398365 },  /* phon=32.0 volume=-53.0 dB fs=48000 Hz biquad */
    {  -205965889,   268435456,  -205965889 },  /* phon=32.5 volume=-52.5 dB fs=48000 Hz biquad */
    {   -53398328,   268435456,   -53398328 },  /* phon=33.0 volume=-52.0 dB fs=48000 Hz biquad */
    {  -205972901,   268435456,  -205972901 },  /* phon=33.5 volume=-51.5 dB fs=48000 Hz biquad */
    {  -205973565,   268435456,  -205973565 },  /* phon=34.0 volume=-51.0 dB fs=48000 Hz biquad */
    {   -53395155,   268435456,   -53395155 },  /* phon=34.5 volume=-50.5 dB fs=48000 Hz biquad */
    {  -205970931,   268435456,  -205970931 },  /* phon=35.0 volume=-50.0 dB fs=48000 Hz biquad */
    {  -205975996,   268435456,  -205975996 },  /* phon=35.5 volume=-49.5 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=36.0 volume=-49.0 dB fs=48000 Hz biquad */
    {  -205977769,   268435456,  -205977769 },  /* phon=36.5 volume=-48.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=37.0 volume=-48.0 dB fs=48000 Hz biquad */
    {  -205972910,   268435456,  -205972910 },  /* phon=37.5 volume=-47.5 dB fs=48000 Hz biquad */
    {  -205002879,   268435456,  -205002879 },  /* phon=38.0 volume=-47.0 dB fs=48000 Hz biquad */
    {   -81335001,   268435456,   -81335001 },  /* phon=38.5 volume=-46.5 dB fs=48000 Hz biquad */
    {   -53395133,   268435456,   -53395133 },  /* phon=39.0 volume=-46.0 dB fs=48000 Hz biquad */
    {   -54040887,   268435456,   -54040887 },  /* phon=39.5 volume=-45.5 dB fs=48000 Hz biquad */
    {  -205007352,   268435456,  -205007352 },  /* phon=40.0 volume=-45.0 dB fs=48000 Hz biquad */
    {  -205977637,   268435456,  -205977637 },  /* phon=40.5 volume=-44.5 dB fs=48000 Hz biquad */
    {   -53395721,   268435456,   -53395721 },  /* phon=41.0 volume=-44.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=41.5 volume=-43.5 dB fs=48000 Hz biquad */
    {  -205977094,   268435456,  -205977094 },  /* phon=42.0 volume=-43.0 dB fs=48000 Hz biquad */
    {  -204990507,   268435456,  -204990507 },  /* phon=42.5 volume=-42.5 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=43.0 volume=-42.0 dB fs=48000 Hz biquad */
    {  -205002508,   268435456,  -205002508 },  /* phon=43.5 volume=-41.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=44.0 volume=-41.0 dB fs=48000 Hz biquad */
    {  -205003405,   268435456,  -205003405 },  /* phon=44.5 volume=-40.5 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=45.0 volume=-40.0 dB fs=48000 Hz biquad */
    {   -53395142,   268435456,   -53395142 },  /* phon=45.5 volume=-39.5 dB fs=48000 Hz biquad */
    {  -205972148,   268435456,  -205972148 },  /* phon=46.0 volume=-39.0 dB fs=48000 Hz biquad */
    {  -205976700,   268435456,  -205976700 },  /* phon=46.5 volume=-38.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=47.0 volume=-38.0 dB fs=48000 Hz biquad */
    {   -53976907,   268435456,   -53976907 },  /* phon=47.5 volume=-37.5 dB fs=48000 Hz biquad */
    {  -205977275,   268435456,  -205977275 },  /* phon=48.0 volume=-37.0 dB fs=48000 Hz biquad */
    {   -53447714,   268435456,   -53447714 },  /* phon=48.5 volume=-36.5 dB fs=48000 Hz biquad */
    {   -53395134,   268435456,   -53395134 },  /* phon=49.0 volume=-36.0 dB fs=48000 Hz biquad */
    {  -205002525,   268435456,  -205002525 },  /* phon=49.5 volume=-35.5 dB fs=48000 Hz biquad */
    {   -53633360,   268435456,   -53633360 },  /* phon=50.0 volume=-35.0 dB fs=48000 Hz biquad */
    {   -54034656,   268435456,   -54034656 },  /* phon=50.5 volume=-34.5 dB fs=48000 Hz biquad */
    {  -205002508,   268435456,  -205002508 },  /* phon=51.0 volume=-34.0 dB fs=48000 Hz biquad */
    {  -103213689,   268435456,  -103213689 },  /* phon=51.5 volume=-33.5 dB fs=48000 Hz biquad */
    {  -205904989,   268435456,  -205904989 },  /* phon=52.0 volume=-33.0 dB fs=48000 Hz biquad */
    {   -53398328,   268435456,   -53398328 },  /* phon=52.5 volume=-32.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=53.0 volume=-32.0 dB fs=48000 Hz biquad */
    {  -205002532,   268435456,  -205002532 },  /* phon=53.5 volume=-31.5 dB fs=48000 Hz biquad */
    {  -205972914,   268435456,  -205972914 },  /* phon=54.0 volume=-31.0 dB fs=48000 Hz biquad */
    {  -104358522,   268435456,  -104358522 },  /* phon=54.5 volume=-30.5 dB fs=48000 Hz biquad */
    {   -53457645,   268435456,   -53457645 },  /* phon=55.0 volume=-30.0 dB fs=48000 Hz biquad */
    {  -204986076,   268435456,  -204986076 },  /* phon=55.5 volume=-29.5 dB fs=48000 Hz biquad */
    {  -205977712,   268435456,  -205977712 },  /* phon=56.0 volume=-29.0 dB fs=48000 Hz biquad */
    {  -205002988,   268435456,  -205002988 },  /* phon=56.5 volume=-28.5 dB fs=48000 Hz biquad */
    {  -205976970,   268435456,  -205976970 },  /* phon=57.0 volume=-28.0 dB fs=48000 Hz biquad */
    {   -53636337,   268435456,   -53636337 },  /* phon=57.5 volume=-27.5 dB fs=48000 Hz biquad */
    {   -53398312,   268435456,   -53398312 },  /* phon=58.0 volume=-27.0 dB fs=48000 Hz biquad */
    {   -53398328,   268435456,   -53398328 },  /* phon=58.5 volume=-26.5 dB fs=48000 Hz biquad */
    {  -205973569,   268435456,  -205973569 },  /* phon=59.0 volume=-26.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=59.5 volume=-25.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=60.0 volume=-25.0 dB fs=48000 Hz biquad */
    {   -53395354,   268435456,   -53395354 },  /* phon=60.5 volume=-24.5 dB fs=48000 Hz biquad */
    {   -53395143,   268435456,   -53395143 },  /* phon=61.0 volume=-24.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=61.5 volume=-23.5 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=62.0 volume=-23.0 dB fs=48000 Hz biquad */
    {  -205975324,   268435456,  -205975324 },  /* phon=62.5 volume=-22.5 dB fs=48000 Hz biquad */
    {   -53441415,   268435456,   -53441415 },  /* phon=63.0 volume=-22.0 dB fs=48000 Hz biquad */
    {   -53978376,   268435456,   -53978376 },  /* phon=63.5 volume=-21.5 dB fs=48000 Hz biquad */
    {   -53395136,   268435456,   -53395136 },  /* phon=64.0 volume=-21.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=64.5 volume=-20.5 dB fs=48000 Hz biquad */
    {   -53395134,   268435456,   -53395134 },  /* phon=65.0 volume=-20.0 dB fs=48000 Hz biquad */
    {  -205977739,   268435456,  -205977739 },  /* phon=65.5 volume=-19.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=66.0 volume=-19.0 dB fs=48000 Hz biquad */
    {   -53395148,   268435456,   -53395148 },  /* phon=66.5 volume=-18.5 dB fs=48000 Hz biquad */
    {  -205002508,   268435456,  -205002508 },  /* phon=67.0 volume=-18.0 dB fs=48000 Hz biquad */
    {   -53395142,   268435456,   -53395142 },  /* phon=67.5 volume=-17.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=68.0 volume=-17.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=68.5 volume=-16.5 dB fs=48000 Hz biquad */
    {  -205969105,   268435456,  -205969105 },  /* phon=69.0 volume=-16.0 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=69.5 volume=-15.5 dB fs=48000 Hz biquad */
    {  -205977767,   268435456,  -205977767 },  /* phon=70.0 volume=-15.0 dB fs=48000 Hz biquad */
    {  -123187393,   268435456,  -123187393 },  /* phon=70.5 volume=-14.5 dB fs=48000 Hz biquad */
    {  -205977738,   268435456,  -205977738 },  /* phon=71.0 volume=-14.0 dB fs=48000 Hz biquad */
    {  -205977661,   268435456,  -205977661 },  /* phon=71.5 volume=-13.5 dB fs=48000 Hz biquad */
    {  -205002508,   268435456,  -205002508 },  /* phon=72.0 volume=-13.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=72.5 volume=-12.5 dB fs=48000 Hz biquad */
    {  -205153777,   268435456,  -205153777 },  /* phon=73.0 volume=-12.0 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=73.5 volume=-11.5 dB fs=48000 Hz biquad */
    {   -81350660,   268435456,   -81350660 },  /* phon=74.0 volume=-11.0 dB fs=48000 Hz biquad */
    {  -205974351,   268435456,  -205974351 },  /* phon=74.5 volume=-10.5 dB fs=48000 Hz biquad */
    {  -205975672,   268435456,  -205975672 },  /* phon=75.0 volume=-10.0 dB fs=48000 Hz biquad */
    {  -205975288,   268435456,  -205975288 },  /* phon=75.5 volume=-9.5 dB fs=48000 Hz biquad */
    {  -205977768,   268435456,  -205977768 },  /* phon=76.0 volume=-9.0 dB fs=48000 Hz biquad */
    {  -205977769,   268435456,  -205977769 },  /* phon=76.5 volume=-8.5 dB fs=48000 Hz biquad */
    {   -95570255,   268435456,   -95570255 },  /* phon=77.0 volume=-8.0 dB fs=48000 Hz biquad */
    {   -54637857,   268435456,   -54637857 },  /* phon=77.5 volume=-7.5 dB fs=48000 Hz biquad */
    {   -53395135,   268435456,   -53395135 },  /* phon=78.0 volume=-7.0 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=78.5 volume=-6.5 dB fs=48000 Hz biquad */
    {  -205977770,   268435456,  -205977770 },  /* phon=79.0 volume=-6.0 dB fs=48000 Hz biquad */
    {   -53395132,   268435456,   -53395132 },  /* phon=79.5 volume=-5.5 dB fs=48000 Hz biquad */
    {   -53402549,   268435455,   -53402548 },  /* phon=80.0 volume=-5.0 dB fs=48000 Hz biquad */
    {   -54878665,   266583558,   -53026767 },  /* phon=80.5 volume=-4.5 dB fs=48000 Hz biquad */
    {   -56338283,   264761512,   -52664339 },  /* phon=81.0 volume=-4.0 dB fs=48000 Hz biquad */
    {   -57774603,   262968550,   -52307697 },  /* phon=81.5 volume=-3.5 dB fs=48000 Hz biquad */
    {   -59188219,   261203929,   -51956692 },  /* phon=82.0 volume=-3.0 dB fs=48000 Hz biquad */
    {   -60579700,   259466939,   -51611183 },  /* phon=82.5 volume=-2.5 dB fs=48000 Hz biquad */
    {   -61949600,   257756889,   -51271033 },  /* phon=83.0 volume=-2.0 dB fs=48000 Hz biquad */
    {   -63298453,   256073112,   -50936109 },  /* phon=83.5 volume=-1.5 dB fs=48000 Hz biquad */
    {   -64626772,   254414968,   -50606284 },  /* phon=84.0 volume=-1.0 dB fs=48000 Hz biquad */
    {   -65935057,   252781832,   -50281433 },  /* phon=84.5 volume=-0.5 dB fs=48000 Hz biquad */
    {   -67223785,   251173109,   -49961438 },  /* phon=85.0 volume=0.0 dB fs=48000 Hz biquad */
};

const biquad_first_order_coefficients_t *active_highshelf_LUT_for_frequency(uint32_t frequency)
{
    return (frequency == (uint32_t)FREQ_48) ? highshelf_no_volume_48000hz : highshelf_no_volume_44100hz;
}

biquad_first_order_state_t highshelf_states[LOUDNESS_CHANNELS];

Bool loudness_highshelf_biquad_is_idle(int channel)
{
    return highshelf_states[channel].w1 == 0;
}

void loudness_highshelf_reset_states(void)
{
    memset(highshelf_states, 0, sizeof(highshelf_states));
}



