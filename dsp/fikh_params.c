/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die Parameter (Namen wie auf dem Panel), die neun Algorithmen der ersten Bank und wie ihre Werte
 * angezeigt werden. Teil von fikh_core.c. */

const fk_param_info FK_PARAMS[FK_NP] = {
    {"WARP", 1}, {"SPAN", 1}, {"MORPH", 0}, {"FUSE", 1},
    {"MIX", 0}, {"FEED", 1}, {"FORM", 0}, {"TIME", 0},
    {"FIELD", 1}, {"CELL", 1}, {"ATTACK", 0}, {"RELEASE", 0},
    {"GLIDE", 0}, {"DRIFT", 0}, {"WIDTH", 0}, {"LEVEL", 0},
    {"SEED", 0}, {"SCAN", 0},
    {"MIX", 0}, {"DECAY", 0}, {"X", 0}, {"FILTER", 0},
    {"MODE", 0}, {"DEPTH", 0}, {"RATE", 0}, {"STRETCH", 0},
};

/* der Hall, wie er anfängt: DREAM, halb nass, lang, weich */
const uint8_t FK_VERB_INIT[FK_NPV] = {44, 88, 40, 70, 12, 46, 40, 16};

/*                                    WARP SPAN MORPH FUSE  MIX FEED FORM TIME  FIELD CELL ATK REL  GLD DRF WID LVL  SEED SCAN */
const fk_algo_info FK_ALGOS[FK_NALGO] = {
    {"METAL PAIR", "DUAL OSC / RING", PROC_DUNGEON,
     {50, 88, 70, 40, 60, 86, 58, 66, 50, 64, 24, 70, 18, 46, 92, 96, 11, 40}},
    {"SHAPER PAIR", "WAVESHAPER / PM", PROC_REFLEKTA,
     {40, 76, 44, 52, 66, 90, 84, 78, 54, 64, 30, 78, 18, 50, 96, 92, 23, 30}},
    {"SINE CLUSTER", "MULTI-SINE BANK", PROC_DUNGEON,
     {58, 92, 50, 30, 62, 84, 70, 74, 50, 64, 40, 84, 20, 56, 100, 100, 37, 24}},
    {"WAVE QUAD", "WAVETABLE / CRUSH", PROC_DUNGEON,
     {64, 80, 40, 80, 58, 82, 62, 70, 48, 64, 28, 74, 18, 50, 96, 96, 5, 40}},
    {"DIATONIC", "QUANTISED CLUSTER", PROC_REFLEKTA,
     {64, 92, 54, 34, 70, 92, 92, 80, 46, 64, 52, 90, 26, 60, 104, 98, 42, 36}},
    {"CHORD SCAN", "CHORD SCANNER", PROC_DUNGEON,
     {58, 74, 30, 30, 64, 84, 74, 76, 50, 64, 46, 86, 30, 52, 100, 96, 17, 40}},
    {"PARTIALS", "HARMONIC / INHARM.", PROC_DUNGEON,
     {64, 52, 52, 64, 60, 86, 66, 72, 58, 64, 36, 82, 20, 48, 92, 98, 29, 30}},
    {"COMB SWARM", "FILTERBANK COMBS", PROC_REFLEKTA,
     {78, 96, 50, 40, 60, 88, 80, 74, 56, 64, 30, 80, 18, 50, 96, 94, 7, 34}},
    {"GRAIN CLOUD", "GRANULAR / PITCH", PROC_REFLEKTA,
     {64, 88, 46, 36, 72, 92, 76, 84, 52, 88, 48, 92, 26, 58, 104, 96, 51, 40}},
    /* BANK 2 */
    {"ZERO CARRIER", "0 HZ WAVETABLE", PROC_REFLEKTA,
     {56, 76, 40, 50, 64, 88, 80, 76, 52, 64, 34, 82, 20, 48, 96, 96, 13, 30}},
    {"DISPERSOR", "COUPLED SINES", PROC_DISPERSOR,
     {44, 84, 50, 40, 62, 90, 86, 70, 54, 64, 36, 84, 20, 52, 96, 98, 21, 40}},
    {"NOTCH STACK", "SINE DRIVE / NOTCH", PROC_DUNGEON,
     {64, 70, 50, 84, 56, 84, 60, 70, 56, 64, 24, 76, 18, 46, 92, 92, 9, 20}},
    {"SINE SWARM", "EIGHT LOOSE SINES", PROC_DUNGEON,
     {58, 90, 50, 34, 62, 86, 72, 76, 52, 64, 46, 88, 22, 56, 100, 98, 33, 40}},
    {"RADIO", "CHANNEL SURFING", PROC_REFLEKTA,
     {80, 74, 40, 70, 60, 84, 70, 72, 64, 64, 20, 80, 0, 30, 90, 94, 45, 40}},
    {"WEATHER", "WIND / RAIN / INSECTS", PROC_REFLEKTA,
     {74, 64, 20, 30, 62, 82, 84, 80, 54, 64, 60, 90, 0, 40, 104, 96, 27, 50}},
    {"ARCHITECT", "MORPHING BODY", PROC_DUNGEON,
     {54, 92, 30, 50, 60, 84, 70, 74, 60, 64, 4, 90, 0, 30, 96, 100, 15, 30}},
    {"FOLD BASS", "SAWS / SUB / FOLDER", PROC_DUNGEON,
     {64, 66, 40, 30, 30, 70, 40, 60, 44, 64, 4, 50, 14, 30, 70, 96, 3, 64}},
    {"SUPERSAW", "SEVEN SAWS", PROC_REFLEKTA,
     {64, 84, 10, 34, 58, 82, 80, 80, 50, 64, 30, 82, 18, 40, 100, 92, 19, 50}},
    /* BANK 3: die übrigen Algorithmen der ersten Bank des Moduls (5, 6, 8, 9) */
    {"WAVE PM", "WAVETABLE / PM", PROC_DUNGEON,
     {48, 80, 44, 40, 60, 84, 66, 72, 50, 64, 30, 76, 18, 50, 96, 96, 25, 36}},
    {"WAVE FOLD", "WAVETABLE / FOLDER", PROC_REFLEKTA,
     {56, 80, 36, 82, 64, 88, 82, 78, 52, 64, 32, 82, 18, 50, 96, 94, 35, 40}},
    {"METAL CLUSTER", "CLUSTER SCAN", PROC_DUNGEON,
     {64, 90, 50, 34, 62, 86, 70, 74, 50, 64, 44, 86, 22, 56, 100, 98, 39, 34}},
    {"DIFFUSE CLUSTER", "CLUSTER SCAN", PROC_REFLEKTA,
     {64, 90, 50, 34, 70, 92, 90, 82, 48, 64, 50, 90, 24, 58, 104, 98, 47, 34}},
};

static char *vt_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static void vt_int(char *d, int v)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    while (*d)
        d++;
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *d++ = '-';
    while (n)
        *d++ = t[--n];
    *d = 0;
}
static void vt_hz(char *t, float hz)
{
    if (hz >= 1000.0f) {
        int k10 = (int)(hz / 100.0f + 0.5f);
        vt_int(t, k10 / 10);
        if (k10 < 100) {
            vt_cat(t, ".");
            vt_int(t, k10 % 10);
        }
        vt_cat(t, "K");
    } else if (hz >= 10.0f) {
        vt_int(t, (int)(hz + 0.5f));
    } else {
        int d = (int)(hz * 10.0f + 0.5f);
        vt_int(t, d / 10);
        vt_cat(t, ".");
        vt_int(t, d % 10);
    }
}
static void vt_ms(char *t, float ms)
{
    if (ms >= 1000.0f) {
        int d = (int)(ms / 100.0f + 0.5f);
        vt_int(t, d / 10);
        vt_cat(t, ".");
        vt_int(t, d % 10);
        vt_cat(t, "S");
    } else {
        vt_int(t, (int)(ms + 0.5f));
        vt_cat(t, "MS");
    }
}

/* die Kurven, an einer Stelle (Engine und Anzeige benutzen dieselben) */
static float fk_bip(int v)                /* -1..1, mit einer kleinen Ruhezone in der Mitte */
{
    float b = (float)(v - 64) / 63.0f;
    if (b > -0.025f && b < 0.025f)
        return 0.0f;
    b = b > 0.0f ? (b - 0.025f) / 0.975f : (b + 0.025f) / 0.975f;
    return fk_clamp(b, -1.0f, 1.0f);
}
static float fk_time_ms(float u) { return 5.0f * fm_exp2f(u * 7.2095f); }       /* 5 .. 740 ms */
static float fk_atk_ms(float u) { return 1.0f * fm_exp2f(u * 13.0f); }          /* 1 ms .. 8 s */
static float fk_rel_ms(float u) { return 5.0f * fm_exp2f(u * 11.5f); }          /* 5 ms .. 14 s */
static float fk_glide_ms(float u) { return u < 0.01f ? 0.0f : 2.0f * fm_exp2f(u * 10.5f); }   /* aus, 2 ms .. 3 s */
static float fk_level_db(float u) { return -30.0f + 36.0f * u; }
static float fk_field_hz(float b)        /* links: Tiefpass zu, rechts: Hochpass auf */
{
    return b < 0.0f ? 20000.0f * fm_exp2f(b * 9.5f) : b > 0.0f ? 20.0f * fm_exp2f(b * 9.0f) : 20000.0f;
}
static float fk_cell_hz(float b)         /* die Verschiebung: fein um die Mitte, außen bis 600 Hz */
{
    float a = fk_abs(b);
    a = a * a * fm_sqrtf(a);
    return (b < 0.0f ? -600.0f : 600.0f) * a;
}

const char *fk_value_text(int p, int v, char *t)
{
    float u = (float)v / 127.0f, b = fk_bip(v);
    t[0] = 0;
    switch (p) {
    case P_TIME: vt_ms(t, fk_time_ms(u)); break;
    case P_ATK: vt_ms(t, fk_atk_ms(u)); break;
    case P_REL: vt_ms(t, fk_rel_ms(u)); break;
    case P_GLIDE:
        if (fk_glide_ms(u) <= 0.0f)
            vt_cat(t, "OFF");
        else
            vt_ms(t, fk_glide_ms(u));
        break;
    case P_LEVEL: {
        float db = fk_level_db(u);
        int d = (int)(db < 0.0f ? db - 0.5f : db + 0.5f);
        if (d > 0)
            vt_cat(t, "+");
        vt_int(t, d);
        vt_cat(t, "DB");
        break;
    }
    case P_FIELD:
        if (b == 0.0f) {
            vt_cat(t, "OPEN");
        } else {
            vt_cat(t, b < 0.0f ? "LP " : "HP ");
            vt_hz(t, fk_field_hz(b));
        }
        break;
    case P_CELL:
        if (b == 0.0f) {
            vt_cat(t, "OFF");
        } else if (fk_cur_algo() == 8) {   /* GRAIN CLOUD: Halbtöne */
            int st = (int)(b * 12.0f + (b < 0.0f ? -0.5f : 0.5f));
            if (st > 0)
                vt_cat(t, "+");
            vt_int(t, st);
            vt_cat(t, " ST");
        } else {
            vt_cat(t, b < 0.0f ? "-" : "+");
            vt_hz(t, fk_abs(fk_cell_hz(b)));
            vt_cat(t, "HZ");
        }
        break;
    case P_VMODE: vt_cat(t, FK_VMODE_NAME[fk_vmode_of(v)]); break;
    case P_VDECAY:
        if (u > 0.985f) {
            vt_cat(t, "INF");
        } else {
            float sec = 0.8f * fm_exp2f(u * 5.2f);
            int d = (int)(sec * 10.0f + 0.5f);
            vt_int(t, d / 10);
            if (d < 100) {
                vt_cat(t, ".");
                vt_int(t, d % 10);
            }
            vt_cat(t, "S");
        }
        break;
    case P_VRATE: {
        float hz = 0.04f * fm_exp2f(u * 5.5f);
        int c = (int)(hz * 100.0f + 0.5f);
        vt_int(t, c / 100);
        vt_cat(t, ".");
        if (c % 100 < 10)
            vt_cat(t, "0");
        vt_int(t, c % 100);
        vt_cat(t, "HZ");
        break;
    }
    case P_SEED:
        vt_cat(t, "#");
        if (v < 100)
            vt_cat(t, "0");
        if (v < 10)
            vt_cat(t, "0");
        vt_int(t, v);
        break;
    default:
        if (FK_PARAMS[p].bip) {
            int s = (int)(b * 100.0f + (b < 0.0f ? -0.5f : 0.5f));
            if (s > 0)
                vt_cat(t, "+");
            vt_int(t, s);
        } else {
            vt_int(t, (int)(u * 100.0f + 0.5f));
        }
        break;
    }
    return t;
}
