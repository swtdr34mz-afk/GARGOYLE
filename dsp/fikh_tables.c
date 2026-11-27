/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die Wavetables (beim Start additiv berechnet, bandbegrenzt auf 32 Obertöne) und der Wurf von SEED.
 * Teil von fikh_core.c.
 *   Set 0 KLASSIK   Sinus, Dreieck, Säge, Rechteck, Puls, Formant, Glocke, Summen
 *   Set 1 ORGEL     Zugriegel-Mischungen, von rund bis hohl
 *   Set 2 GLAS      dünne, hohe Obertöne in festen Zufallsmustern
 *   Set 3 GRIT      gefaltete und gesättigte Sinus (aus ihrer Form zerlegt) */

#define WT_H 32

static float wt_shape_eval(int set, int w, float x)   /* Set 3: die Form, aus der zerlegt wird (x in 0..1) */
{
    float s = fk_sin(x), k = 1.0f + (float)w * 0.9f;
    (void)set;
    switch (w & 3) {
    case 0: return fk_fold(s * k);
    case 1: return fk_sat(s * k * 1.4f);
    case 2: return fk_fold(s * k * 0.7f + fk_sin(x * 2.0f) * 0.4f);
    default: return fk_sat(fk_fold(s * k) * 2.0f + 0.3f * s);
    }
}

static float wt_amp(int set, int w, int h, uint32_t *rs)   /* die Stärke des Obertons h (1..WT_H) */
{
    float fh = (float)h;
    switch (set) {
    case 0:
        switch (w) {
        case 0: return h == 1 ? 1.0f : 0.0f;
        case 1: return (h & 1) ? ((h >> 1) & 1 ? -1.0f : 1.0f) / (fh * fh) : 0.0f;
        case 2: return 1.0f / fh;
        case 3: return (h & 1) ? 1.0f / fh : 0.0f;
        case 4: return fk_sin(fh * 0.125f) / fh;                            /* Puls 25 % */
        case 5: {                                                           /* zwei Formanten */
            float a = (fh - 4.0f) / 1.6f, b = (fh - 9.0f) / 2.2f;
            return 0.15f / fh + fm_expf(-a * a) + 0.6f * fm_expf(-b * b);
        }
        case 6: return (h == 1 || h == 4 || h == 7 || h == 11 || h == 17) ? 1.0f / fm_sqrtf(fh) : 0.0f;
        default: return 1.0f / fm_sqrtf(fh);                                /* hell, summend */
        }
    case 1: {
        static const uint8_t BAR[8][6] = {{1, 0, 0, 0, 0, 0}, {1, 2, 0, 0, 0, 0}, {1, 3, 0, 0, 0, 0}, {1, 2, 4, 8, 0, 0},
                                          {1, 3, 5, 0, 0, 0}, {2, 3, 0, 0, 0, 0}, {1, 2, 3, 4, 6, 8}, {1, 5, 6, 8, 10, 16}};
        int i;
        for (i = 0; i < 6; i++)
            if (BAR[w][i] == h)
                return 1.0f / (1.0f + 0.25f * (float)i);
        return 0.0f;
    }
    case 2:
        if (h == 1)
            return 0.7f;
        return (fk_rand01(rs) < 0.18f + 0.04f * (float)w) ? fk_rand01(rs) * 1.2f / fm_sqrtf(fh) : 0.0f;
    default:
        return 0.0f;
    }
}

static void wt_build(float *wt)
{
    static float tmp[512];
    int set, w, h, i;
    uint32_t rs = 0x5A1A55u;
    for (set = 0; set < (int)FK_WT_SETS; set++)
        for (w = 0; w < (int)FK_WT_WAVES; w++) {
            float *d = wt + ((uint32_t)set * FK_WT_WAVES + (uint32_t)w) * FK_WT_LEN, pk = 0.0f;
            for (i = 0; i < (int)FK_WT_LEN; i++)
                d[i] = 0.0f;
            if (set == 3) {                         /* zerlegen: die Form, dann ihre Obertöne */
                for (i = 0; i < 512; i++)
                    tmp[i] = wt_shape_eval(set, w, (float)i / 512.0f);
                for (h = 1; h <= WT_H; h++) {
                    float re = 0.0f, im = 0.0f, fh = (float)h;
                    for (i = 0; i < 512; i++) {
                        float x = fh * (float)i / 512.0f;
                        re += tmp[i] * fk_sin(x + 0.25f);
                        im += tmp[i] * fk_sin(x);
                    }
                    re *= 2.0f / 512.0f;
                    im *= 2.0f / 512.0f;
                    re *= 1.0f - 0.5f * fh / (float)WT_H;   /* weich ausblenden: kein Gibbs-Klirren */
                    im *= 1.0f - 0.5f * fh / (float)WT_H;
                    for (i = 0; i < (int)FK_WT_LEN; i++) {
                        float x = fh * (float)i / (float)FK_WT_LEN;
                        d[i] += re * fk_sin(x + 0.25f) + im * fk_sin(x);
                    }
                }
            } else {
                for (h = 1; h <= WT_H; h++) {
                    float a = wt_amp(set, w, h, &rs), ph = set == 2 ? fk_rand01(&rs) : 0.0f, fh = (float)h;
                    if (a == 0.0f)
                        continue;
                    a *= 1.0f - 0.35f * fh / (float)WT_H;
                    for (i = 0; i < (int)FK_WT_LEN; i++)
                        d[i] += a * fk_sin(fh * (float)i / (float)FK_WT_LEN + ph);
                }
            }
            for (i = 0; i < (int)FK_WT_LEN; i++)
                pk = fk_abs(d[i]) > pk ? fk_abs(d[i]) : pk;
            if (pk > 1e-6f)
                for (i = 0; i < (int)FK_WT_LEN; i++)
                    d[i] /= pk;
        }
}

/* ein Wert aus Set set: pos 0..1 über die acht Wellen, ph 0..1 */
static inline float wt_read(const float *wt, int set, float pos, float ph)
{
    float fp = fk_clamp(pos, 0.0f, 1.0f) * (float)(FK_WT_WAVES - 1u), fi = ph * (float)FK_WT_LEN, fr, a, b;
    int i0 = (int)fp, j0;
    const float *p;
    if (i0 > (int)FK_WT_WAVES - 2)
        i0 = (int)FK_WT_WAVES - 2;
    fr = fp - (float)i0;
    j0 = (int)fi;
    fi -= (float)j0;
    j0 &= (int)FK_WT_LEN - 1;
    p = wt + ((uint32_t)set * FK_WT_WAVES + (uint32_t)i0) * FK_WT_LEN;
    a = p[j0] + (p[(j0 + 1) & ((int)FK_WT_LEN - 1)] - p[j0]) * fi;
    p += FK_WT_LEN;
    b = p[j0] + (p[(j0 + 1) & ((int)FK_WT_LEN - 1)] - p[j0]) * fi;
    return a + (b - a) * fr;
}
