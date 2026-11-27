/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: der Hall (Seiten VERB), ein Ambient-Hall nach dem Vorbild des Walrus Audio SLÖER (eigener Code). Fünf Modi:
 *   DREAM  üppig; X legt ein Vibrato über die Fläche, FREEZE (ARP) hält sie als Pad
 *   DARK   X mischt eine Oktave tiefer in den Hall
 *   RISE   der Hall schwillt nach jeder Note langsam an (X: wie lange)
 *   LIGHT  hell, X: Shimmer, eine Oktave höher, die sich im Hall immer weiter nach oben schraubt
 *   RAIN   wenig Diffusion: die Abgriffe werden als Echos hörbar (X: mehr Diffusion, weicher)
 * Der Aufbau: vier Allpässe je Kanal zerstreuen das Signal, dann acht Verzögerungsleitungen (int16), die über eine
 * Householder-Matrix ineinander zurückkoppeln; jede schwebt in ihrer Länge (DEPTH, RATE), jede hat einen Tiefpass
 * (FILTER). DECAY 0,8 s bis 30 s, ganz rechts INF. STRETCH: wie eine niedrigere Abtastrate, längere, dunklere Fahnen
 * mit Artefakten. Ein Tonhöhenverschieber (zwei Leseköpfe) macht die Oktaven für DARK und LIGHT. Teil von fikh_core.c. */

static const uint16_t RV_LEN[8] = {1153, 1361, 1597, 1847, 2083, 2399, 2711, 3023};
#define RV_PAD 48u
static const uint16_t RVA_LEN[8] = {223, 157, 431, 313, 211, 167, 449, 337};   /* die Allpässe davor, links und rechts */
_Static_assert(1153 + 1361 + 1597 + 1847 + 2083 + 2399 + 2711 + 3023 + 8 * RV_PAD <= FK_RV_N, "FK_RV_N: der Hall passt nicht");
_Static_assert(223 + 157 + 431 + 313 + 211 + 167 + 449 + 337 <= FK_RVA_N, "FK_RVA_N: die Allpässe passen nicht");

enum { VM_DREAM, VM_DARK, VM_RISE, VM_LIGHT, VM_RAIN, NVMODE };
const char *const FK_VMODE_NAME[NVMODE] = {"DREAM", "DARK", "RISE", "LIGHT", "RAIN"};
int fk_vmode_of(int v) { int m = v * NVMODE / 128; return m < 0 ? 0 : m >= NVMODE ? NVMODE - 1 : m; }

static struct {
    uint32_t off[8], n[8], w[8];
    uint32_t aoff[8], aw[8];
    float z[8], ph[8], mod[8];
    float vph, vib;                      /* DREAM: das Vibrato */
    float swell, fast, slow;             /* RISE */
    uint32_t sw;                         /* der Tonhöhenverschieber: sein Ring */
    float sp, sh_in, sh_out;
    float hold[2];                       /* STRETCH: gehaltene Werte (niedrigere Abtastrate) */
    float hcnt;
    float idle;
} RV;

static void verb_init(void)
{
    uint32_t i, o = 0, a = 0;
    for (i = 0; i < 8u; i++) {
        RV.off[i] = o;
        RV.n[i] = RV_LEN[i] + RV_PAD;
        o += RV.n[i];
        RV.w[i] = 0;
        RV.aoff[i] = a;
        a += RVA_LEN[i];
        RV.aw[i] = 0;
        RV.z[i] = 0.0f;
        RV.ph[i] = (float)i * 0.137f;
        RV.mod[i] = 0.0f;
    }
    RV.swell = 1.0f;
}

static inline float rv_read(int i, float d)
{
    const int16_t *b = E.m.rv + RV.off[i];
    float p = (float)RV.w[i] - d, fr;
    int k, k1;
    if (p < 0.0f)
        p += (float)RV.n[i];
    k = (int)p;
    if (k >= (int)RV.n[i])
        k -= (int)RV.n[i];
    fr = p - fm_floorf(p);
    k1 = k + 1 >= (int)RV.n[i] ? 0 : k + 1;
    return ((float)b[k] + (float)(b[k1] - b[k]) * fr) * (1.0f / 16000.0f);
}

/* der Tonhöhenverschieber: x hinein, die Oktave (ratio 0,5 oder 2) heraus; zwei Köpfe mit Sinus²-Fenstern */
#define SH_W 1700.0f                             /* (+ 2 Samples, < FK_SH_N) */
_Static_assert(FK_SH_N >= 1712u, "FK_SH_N: das Fenster passt nicht");
static inline float shift_run(float x, float step)
{
    int16_t *b = E.m.sh;
    float p0 = RV.sp, p1 = p0 + 0.5f, w0, r0, r1;
    b[RV.sw] = (int16_t)(fk_clamp(x, -1.9f, 1.9f) * 16000.0f);
    if (p1 >= 1.0f)
        p1 -= 1.0f;
    w0 = fk_sin(p0 * 0.5f);
    w0 *= w0;
    {
        float d0 = 2.0f + p0 * SH_W, d1 = 2.0f + p1 * SH_W, q0 = (float)RV.sw - d0, q1 = (float)RV.sw - d1, f0, f1;
        int i0, i1;
        if (q0 < 0.0f)
            q0 += (float)FK_SH_N;
        if (q1 < 0.0f)
            q1 += (float)FK_SH_N;
        i0 = (int)q0;
        i1 = (int)q1;
        f0 = q0 - (float)i0;
        f1 = q1 - (float)i1;
        r0 = (float)b[i0] + (float)(b[(i0 + 1) & (FK_SH_N - 1)] - b[i0]) * f0;
        r1 = (float)b[i1] + (float)(b[(i1 + 1) & (FK_SH_N - 1)] - b[i1]) * f1;
    }
    RV.sw = (RV.sw + 1u) & (FK_SH_N - 1u);
    RV.sp += step;
    RV.sp -= fm_floorf(RV.sp);
    return (r0 * w0 + r1 * (1.0f - w0)) * (1.0f / 16000.0f);
}

/* ein Block: l/r werden mit dem Hall gemischt */
static void verb_block(float *l, float *r, int n)
{
    int mode = fk_vmode_of(E.raw[P_VMODE]), i, k, hstep;
    float mix = E.v.vmix, X = E.v.vx, st = E.v.vstretch, dec = E.v.vdecay, t60, inf = dec > 0.985f || E.freeze;
    float size = 0.58f + 0.42f * st, tone, d[8], dm[8], g[8], gi, dry, wet, ag, depth, rate, vib0, vibs;
    float shstep = 0.0f, shamt = 0.0f, rise = 0.0f;
    float xs = X_l ? X_send : 0.0f;              /* GARGOYLE: der GROOVE, mit FERNE in den Hall */
    if (mix <= 0.002f && xs <= 0.002f && RV.idle > 4.0f * FK_SR) /* ausgeschaltet und verklungen: nichts zu tun */
        return;
    RV.idle = mix <= 0.002f && xs <= 0.002f ? RV.idle + (float)n : 0.0f;
    t60 = 0.8f * fm_exp2f(dec * 5.2f) * (1.0f + 0.8f * st);
    tone = fk_lp_coef(900.0f * fm_exp2f(E.v.vfilt * 4.4f) * (1.0f - 0.45f * st) * (mode == VM_LIGHT ? 1.6f : 1.0f));
    if (inf)                                     /* INF: kaum Verlust in der Schleife, die Fläche bleibt */
        tone = fk_lp_coef(15000.0f);
    depth = 1.0f + E.v.vdepth * 18.0f;
    rate = 0.04f * fm_exp2f(E.v.vrate * 5.5f);
    ag = mode == VM_RAIN ? 0.08f + 0.62f * X : 0.64f;
    for (k = 0; k < 8; k++) {                    /* die Längen, ihr Schweben, die Dämpfung je Leitung */
        float m0 = RV.mod[k], m1;
        RV.ph[k] += rate * (0.7f + 0.13f * (float)k) * (float)n / FK_SR;
        RV.ph[k] -= fm_floorf(RV.ph[k]);
        m1 = fk_sin(RV.ph[k]) * depth * (0.6f + 0.1f * (float)(k & 3));
        RV.mod[k] = m1;
        d[k] = (float)RV_LEN[k] * size + m0 + 24.0f;
        dm[k] = (m1 - m0) / (float)n;
        g[k] = inf ? 1.0f : fm_exp2f(-9.9658f * d[k] / (t60 * FK_SR));
    }
    /* DREAM: ein gemeinsames Vibrato über der Fläche */
    vib0 = RV.vib;
    if (mode == VM_DREAM) {
        RV.vph += 4.6f * (float)n / FK_SR;
        RV.vph -= fm_floorf(RV.vph);
        RV.vib = fk_sin(RV.vph) * X * 14.0f;
    } else {
        RV.vib = 0.0f;
    }
    vibs = (RV.vib - vib0) / (float)n;
    if (mode == VM_DARK) {
        shstep = 0.5f / SH_W;                    /* eine Oktave tiefer */
        shamt = X * 1.1f;
    } else if (mode == VM_LIGHT) {
        shstep = -1.0f / SH_W;                   /* eine Oktave höher, im Kreis: Shimmer */
        shamt = X * 0.55f;
    }
    if (mode == VM_RISE) {                       /* RISE: eine neue Note lässt den Hall von vorn anschwellen */
        rise = 1.0f / ((0.15f * fm_exp2f(X * 5.3f)) * FK_SR);
        if (E.rv_trig)
            RV.swell = 0.0f;
    } else {
        RV.swell = 1.0f;
    }
    E.rv_trig = 0;
    gi = E.freeze ? 0.0f : inf ? 0.12f : 0.30f;  /* FREEZE: nichts Neues hinein, der Hall hält; INF: wenig */
    dry = 1.0f - 0.5f * mix;
    wet = mix * 0.85f;
    if (wet < xs * 0.6f)                         /* GARGOYLE: weit weg ist immer im Raum */
        wet = xs * 0.6f;
    hstep = st < 0.15f ? 1 : st < 0.45f ? 2 : st < 0.75f ? 3 : 4;
    for (i = 0; i < n; i++) {
        float x[2], y[8], s = 0.0f, ol, orr, inj = 0.0f, sw, dith = fk_rand11(&E.noise) * 0.5f;
        RV.swell += (1.0f - RV.swell) * rise * 3.0f;
        sw = RV.swell * RV.swell;
        x[0] = l[i] * sw;
        x[1] = r[i] * sw;
        if (xs > 0.0f) {
            x[0] += X_l[i] * xs;
            x[1] += X_r[i] * xs;
        }
        if (shamt > 0.0f) {
            float src = mode == VM_DARK ? (l[i] + r[i]) * 0.5f : RV.sh_out;
            inj = shift_run(src, shstep) * shamt;
            if (mode == VM_LIGHT)
                inj = fk_lp1(&RV.sh_in, inj, 0.35f);
        }
        for (k = 0; k < 2; k++) {               /* zerstreuen */
            int b = k * 4, j;
            x[k] += inj;
            for (j = 0; j < 4; j++)
                x[k] = fk_ap(E.m.rva + RV.aoff[b + j], RVA_LEN[b + j], &RV.aw[b + j], x[k], ag);
        }
        if (hstep > 1) {                         /* STRETCH: nur jeder n-te Wert neu: wie eine niedrigere Abtastrate */
            RV.hcnt += 1.0f;
            if (RV.hcnt >= (float)hstep) {
                RV.hcnt = 0.0f;
                RV.hold[0] = x[0];
                RV.hold[1] = x[1];
            }
            x[0] = RV.hold[0];
            x[1] = RV.hold[1];
        }
        for (k = 0; k < 8; k++) {
            float v = rv_read(k, d[k] + vib0);
            d[k] += dm[k];
            RV.z[k] += (v - RV.z[k]) * tone;
            y[k] = RV.z[k];
            s += y[k];
        }
        vib0 += vibs;
        s *= 0.25f;                              /* Householder: y - 2/N * Summe */
        for (k = 0; k < 8; k++) {
            float o = (y[k] - s) * g[k] + x[k & 1] * gi;
            if (o > 1.0f)                        /* erst oberhalb von 1 weich begrenzt: INF und Shimmer laufen nicht über */
                o = 1.0f + fk_sat((o - 1.0f) * 1.5f) * 0.6f;
            else if (o < -1.0f)
                o = -1.0f - fk_sat((-o - 1.0f) * 1.5f) * 0.6f;
            E.m.rv[RV.off[k] + RV.w[k]] = (int16_t)(o * 16000.0f + dith);   /* (Dither: leise Fahnen reißen nicht ab) */
            if (++RV.w[k] >= RV.n[k])
                RV.w[k] = 0;
        }
        ol = (y[0] - y[2] + y[4] - y[6]) * 0.55f;
        orr = (y[1] - y[3] + y[5] - y[7]) * 0.55f;
        RV.sh_out = (ol + orr) * 0.5f;
        l[i] = l[i] * dry + ol * wet;
        r[i] = r[i] * dry + orr * wet;
    }
    for (k = 0; k < 8; k++)
        RV.z[k] = fm_flush(RV.z[k]);
    RV.sh_in = fm_flush(RV.sh_in);
}
