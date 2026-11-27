/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: das Cluster-Scanning, wie es das Handbuch des VHIKK X beschreibt: MORPH fährt weich überblendend durch
 * eine Reihe von Paaren aus Frequenz und Welle, wie eine Wavetable, bei der jede Position auch einen eigenen
 * Frequenz-Versatz trägt, und das für jeden Oszillator anders. Hier: acht Punkte je Stimme (SEED würfelt sie, SPAN
 * skaliert ihre Versätze), zwischen zwei Punkten klingen beide Unterstimmen, gleich laut überblendet. Beim
 * Überschreiten eines Punktes übernimmt die eine Unterstimme die Phase der anderen: kein Sprung.
 * Die Generatoren mit Cluster-Scanning: SINE CLUSTER, DIATONIC, GRAIN CLOUD, DISPERSOR, METAL CLUSTER, DIFFUSE CLUSTER.
 * Dazu die Wavetable-Familie (WAVE QUAD, WAVE PM, WAVE FOLD). Teil von fikh_core.c (nach fikh_gen.c). */

#define CS_K 8

typedef struct {
    int i0;                              /* der Punkt links der Position */
    float ga, gb;                        /* die Überblendung (gleiche Leistung) */
    int a_on, b_on;
} cs_pos;

/* die Position auf der Punktreihe aus MORPH; wandert sie über einen Punkt, wechseln die Unterstimmen die Rollen */
static cs_pos cs_scan(int nv, int subfb)   /* subfb: jede Unterstimme hat ihre eigene Rückkopplung (fb, fb2) */
{
    cs_pos s;
    float pos = E.v.morph * (float)(CS_K - 1), fr;
    int c, k;
    s.i0 = (int)pos;
    if (s.i0 > CS_K - 2)
        s.i0 = CS_K - 2;
    fr = pos - (float)s.i0;
    if (E.cs_i0 >= 0 && s.i0 != E.cs_i0) {
        int up = s.i0 > E.cs_i0;
        for (c = 0; c < 2; c++)
            for (k = 0; k < nv; k++) {
                if (up) {                    /* B wird A (es klang schon), B setzt mit seiner Phase fort */
                    E.ph[c][k] = E.ph2[c][k];
                    if (subfb)
                        E.fb[c][k] = E.fb2[c][k];
                } else {
                    E.ph2[c][k] = E.ph[c][k];
                    if (subfb)
                        E.fb2[c][k] = E.fb[c][k];
                }
            }
        for (k = 0; k < nv; k++) {
            if (up)
                E.sa[k] = E.sb[k];
            else
                E.sb[k] = E.sa[k];
        }
    }
    E.cs_i0 = s.i0;
    s.ga = fm_cosf(fr * 1.5707963f);
    s.gb = fm_sinf(fr * 1.5707963f);
    s.a_on = s.ga > 0.002f;
    s.b_on = s.gb > 0.002f;
    return s;
}

/* der Versatz einer Unterstimme in Halbtönen: frei (range) oder in Moll quantisiert */
static float cs_semi(int k, int pt, float sp, float range, int quant)
{
    float o = E.cs_f[k][pt] * sp;
    if (quant)
        return minor_semi((int)(o * 14.0f + (o < 0.0f ? -0.5f : 0.5f)));
    return o * range;
}

/* ---- 3 SINE CLUSTER: sechs Sinus, jeder mit seiner Punktreihe; MORPH scannt sie; WARP links Phasen-Rückkopplung,
 *      rechts Kreuz-FM; SCAN Ringmodulation der Nachbarn; FUSE links Chorus, rechts Rauschmodulation */
static void gen_sines(float *gl, float *gr, int n)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), u = E.v.fuse > 0.0f ? E.v.fuse : 0.0f, sc = E.v.scan;
    float fbk = w < 0.0f ? -w * 0.16f : 0.0f, xm = w > 0.0f ? w * 1.1f : 0.0f, g, aa[6], ab[6];
    int c, i, k, N = E.eco ? 4 : 6;
    cs_pos s = cs_scan(N, 1);
    g = 0.55f / fm_sqrtf((float)N * 0.55f);
    for (k = 0; k < N; k++) {
        E.sa[k] += (cs_semi(k, s.i0, sp, 24.0f, 0) - E.sa[k]) * 0.25f;
        E.sb[k] += (cs_semi(k, s.i0 + 1, sp, 24.0f, 0) - E.sb[k]) * 0.25f;
        aa[k] = E.cs_a[k][s.i0] * s.ga;
        ab[k] = E.cs_a[k][s.i0 + 1] * s.gb;
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, da[6], db[6], pa[6], pb[6], ya[6], yb[6], y[6], nz = E.nz[c];
        for (k = 0; k < N; k++) {
            float st = c && E.v.span > 0.0f ? E.r_off2[k] * E.v.span * 0.35f : 0.0f;
            da[k] = osc_inc(c, k, E.sa[k] + st);
            db[k] = osc_inc(c, k, E.sb[k] + st);
            pa[k] = E.ph[c][k];
            pb[k] = E.ph2[c][k];
            ya[k] = E.fb[c][k];
            yb[k] = E.fb2[c][k];
            y[k] = ya[k] * aa[k] + yb[k] * ab[k];
        }
        for (i = 0; i < n; i++) {
            float sum = 0.0f, prev = y[N - 1], m;
            nz += (fk_rand11(&E.noise) - nz) * 0.003f;
            for (k = 0; k < N; k++) {
                m = 1.0f;
                if (xm > 0.0f)
                    m *= fk_exp2s(prev * xm);
                if (u > 0.0f)
                    m *= 1.0f + nz * u * 0.6f;
                y[k] = 0.0f;
                if (s.a_on) {
                    pa[k] = ph_step(pa[k], da[k] * m);
                    ya[k] = fk_sin(pa[k] + fbk * ya[k]);
                    y[k] += ya[k] * aa[k];
                }
                if (s.b_on) {
                    pb[k] = ph_step(pb[k], db[k] * m);
                    yb[k] = fk_sin(pb[k] + fbk * yb[k]);
                    y[k] += yb[k] * ab[k];
                }
                prev = y[k];
            }
            for (k = 0; k < N; k++)
                sum += y[k] * (1.0f - sc) + y[k] * y[k + 1 < N ? k + 1 : 0] * sc * 1.6f;
            o[i] = sum * g * 0.8f * (1.0f + nz * u * 6.0f);
        }
        for (k = 0; k < N; k++) {
            E.ph[c][k] = pa[k];
            E.ph2[c][k] = pb[k];
            E.fb[c][k] = ya[k];
            E.fb2[c][k] = yb[k];
        }
        E.nz[c] = nz;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
}

/* vier Wavetable-Stimmen mit Cluster-Scanning (DIATONIC, GRAIN CLOUD, METAL / DIFFUSE CLUSTER): jede Position hat
 * ihren Frequenz-Versatz und ihre Welle; SCAN verschiebt die Wellen jeder Stimme zufällig (die Basis-Wavetable);
 * WARP links Phasenmodulation unter den Stimmen, rechts Kreuz-FM; SPAN rechts dazu Stereo */
static void gen_cluster4(float *gl, float *gr, int n, int set, int quant, float gain)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), pm = w < 0.0f ? -w * 0.30f : 0.0f, xm = w > 0.0f ? w : 0.0f;
    float wa[4], wb[4], aa[4], ab[4];
    int c, i, k;
    cs_pos s = cs_scan(4, 0);
    for (k = 0; k < 4; k++) {
        float sh = E.v.scan * E.r_wave[k] * 1.3f;
        E.sa[k] += (cs_semi(k, s.i0, sp, 12.0f, quant) - E.sa[k]) * 0.12f;
        E.sb[k] += (cs_semi(k, s.i0 + 1, sp, 12.0f, quant) - E.sb[k]) * 0.12f;
        wa[k] = 0.08f + 0.86f * fk_wrap(E.cs_w[k][s.i0] + sh);
        wb[k] = 0.08f + 0.86f * fk_wrap(E.cs_w[k][s.i0 + 1] + sh);
        aa[k] = E.cs_a[k][s.i0] * s.ga;
        ab[k] = E.cs_a[k][s.i0 + 1] * s.gb;
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, da[4], db[4], pa[4], pb[4], y[4], pan[4];
        for (k = 0; k < 4; k++) {
            float pn = E.v.span > 0.0f ? E.r_pan[k] * E.v.span * 0.7f : 0.0f;
            da[k] = osc_inc(c, k, E.sa[k]);
            db[k] = osc_inc(c, k, E.sb[k]);
            pa[k] = E.ph[c][k];
            pb[k] = E.ph2[c][k];
            y[k] = E.fb[c][k];
            pan[k] = c ? 1.0f + pn : 1.0f - pn;
        }
        for (i = 0; i < n; i++) {
            float sum = 0.0f;
            for (k = 0; k < 4; k++) {
                float prev = y[(k + 3) & 3], m = xm > 0.0f ? fk_exp2s(prev * xm) : 1.0f, v = 0.0f;
                if (s.a_on) {
                    pa[k] = ph_step(pa[k], da[k] * m);
                    v += aa[k] * wt_read(E.m.wt, set, wa[k], fk_wrap(pa[k] + pm * prev));
                }
                if (s.b_on) {
                    pb[k] = ph_step(pb[k], db[k] * m);
                    v += ab[k] * wt_read(E.m.wt, set, wb[k], fk_wrap(pb[k] + pm * prev));
                }
                y[k] = v;
                sum += v * pan[k];
            }
            o[i] = sum * gain;
        }
        for (k = 0; k < 4; k++) {
            E.ph[c][k] = pa[k];
            E.ph2[c][k] = pb[k];
            E.fb[c][k] = y[k];
        }
    }
}

/* ---- 11 DISPERSOR: vier gekoppelte Sinus (jeder zieht an der Phase seiner Nachbarn), mit Cluster-Scanning; der
 *      Prozessor ist eine Kette von Allpässen in der Schleife (FORM, SEED). WARP links Kopplung, rechts chaotische
 *      Kreuz-FM; FUSE links Diffusion, rechts Sättigung */
static void gen_disp(float *gl, float *gr, int n)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), cpl = 0.04f + (w < 0.0f ? -w * 0.35f : 0.0f), xm = w > 0.0f ? w * 1.4f : 0.0f;
    float aa[4], ab[4];
    int c, i, k;
    cs_pos s = cs_scan(4, 0);
    for (k = 0; k < 4; k++) {
        E.sa[k] += (cs_semi(k, s.i0, sp, 12.0f, 0) - E.sa[k]) * 0.2f;
        E.sb[k] += (cs_semi(k, s.i0 + 1, sp, 12.0f, 0) - E.sb[k]) * 0.2f;
        aa[k] = E.cs_a[k][s.i0] * s.ga;
        ab[k] = E.cs_a[k][s.i0 + 1] * s.gb;
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, da[4], db[4], pa[4], pb[4], y[4], ys[4];
        for (k = 0; k < 4; k++) {
            float st = c && E.v.span > 0.0f ? E.r_off2[k] * E.v.span * 0.4f : 0.0f;
            da[k] = osc_inc(c, k, E.sa[k] + st);
            db[k] = osc_inc(c, k, E.sb[k] + st);
            pa[k] = E.ph[c][k];
            pb[k] = E.ph2[c][k];
            y[k] = E.fb[c][k];
            ys[k] = E.fb2[c][k];             /* (die Kopplung geglättet: kein Kreischen bei Nyquist) */
        }
        for (i = 0; i < n; i++) {
            float sum = 0.0f, ny[4];
            for (k = 0; k < 4; k++) {
                float nb = ys[(k + 1) & 3] + ys[(k + 3) & 3], m = xm > 0.0f ? fk_exp2s(ys[(k + 2) & 3] * xm) : 1.0f, v = 0.0f;
                if (s.a_on) {
                    pa[k] = ph_step(pa[k], da[k] * m);
                    v += aa[k] * fk_sin(pa[k] + cpl * nb);
                }
                if (s.b_on) {
                    pb[k] = ph_step(pb[k], db[k] * m);
                    v += ab[k] * fk_sin(pb[k] + cpl * nb);
                }
                ny[k] = v;
                sum += v;
            }
            for (k = 0; k < 4; k++) {
                y[k] = ny[k];
                ys[k] += (ny[k] - ys[k]) * 0.3f;
            }
            o[i] = sum * 0.30f;
        }
        for (k = 0; k < 4; k++) {
            E.ph[c][k] = pa[k];
            E.ph2[c][k] = pb[k];
            E.fb[c][k] = y[k];
            E.fb2[c][k] = ys[k];
        }
    }
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- die Wavetable-Familie: vier Oszillatoren; MORPH durch die Tabelle, SCAN versetzt jeden zufällig (die
 *      Basis-Wavetable); SPAN links Frequenz- und Wellen-Spreizung, rechts Frequenz und Stereo.
 *      4  WAVE QUAD (DUNGEON):  WARP links Bitcrush, rechts Kreuz-FM; FUSE links Bandpass mit Sinus, rechts Chorus + Resonanz
 *      19 WAVE PM   (DUNGEON):  WARP links Phasenmodulation, rechts Kreuz-FM; FUSE links Chorus, rechts Chorus + Resonanz
 *      20 WAVE FOLD (REFLEKTA): WARP wie WAVE PM; FUSE links Modulation von Allpässen und Delay mit Audiorate, rechts
 *                               Wavefolder je Oszillator */
enum { WQ_QUAD, WQ_PM, WQ_FOLD };
static void gen_quad(float *gl, float *gr, int n, int v)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), pos[4], xm = w > 0.0f ? w * 1.2f : 0.0f;
    float pm = v != WQ_QUAD && w < 0.0f ? -w * 0.35f : 0.0f, fold = v == WQ_FOLD && E.v.fuse > 0.0f ? E.v.fuse * 4.0f : 0.0f;
    int c, i, k, set = v == WQ_FOLD ? (E.r_set + 2) % 4 : v == WQ_PM ? (E.r_set + 1) % 4 : E.r_set;
    for (k = 0; k < 4; k++) {
        pos[k] = E.v.morph + E.v.scan * (E.r_wave[k] - 0.5f) * 0.9f;
        if (E.v.span < 0.0f)
            pos[k] += sp * (E.r_wave2[k] - 0.5f) * 0.6f;
        pos[k] = fk_clamp(pos[k], 0.0f, 1.0f);
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d[4], p[4], y[4];
        for (k = 0; k < 4; k++) {
            float st = E.r_off[k] * sp * 7.0f;
            if (c && E.v.span > 0.0f)
                st += E.r_off2[k] * E.v.span * 0.6f;
            d[k] = osc_inc(c, k, st);
            p[k] = E.ph[c][k];
            y[k] = E.fb[c][k];
        }
        for (i = 0; i < n; i++) {
            float sum = 0.0f;
            for (k = 0; k < 4; k++) {
                float dk = d[k], prev = y[(k + 3) & 3];
                if (xm > 0.0f)
                    dk *= fk_exp2s(y[(k + 1) & 3] * xm);
                p[k] = ph_step(p[k], dk);
                y[k] = wt_read(E.m.wt, set, pos[k], pm > 0.0f ? fk_wrap(p[k] + pm * prev) : p[k]);
                if (fold > 0.0f)
                    y[k] = fk_fold(y[k] * (1.0f + fold));
                sum += y[k];
            }
            o[i] = sum * 0.27f;
        }
        for (k = 0; k < 4; k++) {
            E.ph[c][k] = p[k];
            E.fb[c][k] = y[k];
        }
    }
    if (v == WQ_QUAD) {
        if (w < 0.0f)
            E.cfg.crush = -w;
        if (E.v.fuse < 0.0f) {
            E.cfg.bpm = -E.v.fuse;
            E.cfg.psine = -E.v.fuse * 0.6f;
        } else {
            E.cfg.chorus = E.v.fuse;
            E.cfg.res = E.v.fuse * 0.6f;
        }
    } else if (v == WQ_PM) {
        E.cfg.chorus = fk_abs(E.v.fuse);
        if (E.v.fuse > 0.0f)
            E.cfg.res = E.v.fuse * 0.6f;
    } else if (E.v.fuse < 0.0f) {
        E.cfg.dmod = -E.v.fuse;
    }
}

/* ---- 21 METAL CLUSTER (DUNGEON) und 22 DIFFUSE CLUSTER (REFLEKTA): vier Stimmen mit freiem Cluster-Scanning, wie die
 *      Algorithmen 8 und 9 der ersten Bank am Modul; FUSE links Stereo-Chorus, rechts Sättigung */
static void gen_freecl(float *gl, float *gr, int n, int diffuse)
{
    gen_cluster4(gl, gr, n, diffuse ? (E.r_set + 1) % 4 : E.r_set, 0, 0.24f);
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}
