/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die Stufen nach dem Generator: Chorus, Bitcrush, Sättigung, Phaser, Bandpass, das Filter FIELD, und der
 * Prozessor: ein Stereo-Delay (int16, bis 0,74 s) mit
 *   DUNGEON   vier Abgriffe (SEED verteilt sie), dahinter zwei lange Allpässe: metallisch, klirrend
 *   REFLEKTA  das Eingangssignal durch vier Allpässe zerstreut, zwei Abgriffe über Kreuz: weich, diffus
 * und in der Rückkopplung ein Frequenzverschieber (CELL; ein Hilbert-Paar aus zwei Allpassketten) oder, bei
 * GRAIN CLOUD, ein Tonhöhenverschieber aus zwei Lese-Köpfen. Teil von fikh_core.c. */

/* die Hilbert-Allpässe (Olli Niemitalo: zwei Ketten, deren Phasen 90° auseinander liegen) */
static const float HIL_A[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
static const float HIL_B[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
static inline float hil_chain(float *z, const float *a, float x)
{
    int k;
    for (k = 0; k < 4; k++) {
        float *s = z + 4 * k, y = a[k] * (x + s[3]) - s[1];
        s[1] = s[0];
        s[0] = x;
        s[3] = s[2];
        s[2] = y;
        x = y;
    }
    return x;
}

/* das Delay: lesen mit linearer Interpolation, d Samples zurück */
static inline float dl_read(const int16_t *b, uint32_t w, float d)
{
    float p = (float)w - d, fr;
    int i;
    if (p < 0.0f)
        p += (float)FK_DL_N;
    i = (int)p;
    fr = p - (float)i;
    return ((float)b[i & (FK_DL_N - 1)] + (float)(b[(i + 1) & (FK_DL_N - 1)] - b[i & (FK_DL_N - 1)]) * fr) * (1.0f / 29000.0f);
}

/* die Allpass-Ringe: REFLEKTA 4 je Kanal, DUNGEON 2 je Kanal */
static const uint16_t AP_LEN[12] = {142, 107, 379, 277, 151, 113, 397, 263, 619, 887, 653, 911};
static uint32_t ap_off[12], ap_w[12];
_Static_assert(142 + 107 + 379 + 277 + 151 + 113 + 397 + 263 + 619 + 887 + 653 + 911 <= FK_AP_N, "FK_AP_N: die Allpass-Ringe passen nicht");

static void proc_init(void)
{
    uint32_t i, o = 0;
    for (i = 0; i < 12u; i++) {
        ap_off[i] = o;
        ap_w[i] = 0;
        o += AP_LEN[i];
    }
}
static inline float ap_run(int i, float x, float g)
{
    return fk_ap(E.m.ap + ap_off[i], AP_LEN[i], &ap_w[i], x, g);
}

/* ein Block: gl/gr vom Generator -> lr (float, -1..1) */
static void proc_block(const float *gl, const float *gr, float *ol, float *or_, int n)
{
    const fk_cfg *k = &E.cfg;
    int i, c, refl = FK_ALGOS[E.algo].proc == PROC_REFLEKTA, grain = E.algo == 8, disp = FK_ALGOS[E.algo].proc == PROC_DISPERSOR;
    float dco[12];
    float mixa = E.v.mix * 1.5707963f, dry = fm_cosf(mixa), wet = fm_sinf(mixa) * 1.15f;
    float fb = fk_abs(E.v.feed) * 1.04f, form = E.v.form, fneg = fk_clamp(-E.v.feed * 12.0f, 0.0f, 1.0f);   /* FEED links: weich hinüber, nicht umgeschaltet */
    float dtarget = fk_clamp(fk_time_ms(E.v.time) * (FK_SR / 1000.0f), 8.0f, (float)FK_DL_N - 9000.0f);
    float gdun = 0.22f + 0.45f * form + 0.22f * k->diff, gref = 0.38f + 0.32f * form + 0.18f * k->diff;
    float damp = fk_lp_coef(refl ? 4200.0f : 7600.0f), hpc = fk_lp_coef(28.0f);
    float shift = E.shift_hz, cw = fm_cosf(FK_TAU * shift / FK_SR), sw = fm_sinf(FK_TAU * shift / FK_SR);
    float gw = 1600.0f + 6400.0f * form, gstep = 0.0f, crush_q = 0.0f, chl = E.ch_lfo;
    float satg = 1.0f + k->sat * 6.0f, satn = k->sat > 0.0f ? 0.85f / fk_sat(satg * 0.5f) * 0.5f : 1.0f;
    float atk = 1.0f / (fk_atk_ms(E.v.atk) * (FK_SR / 1000.0f) + 1.0f), rel = 1.0f / (fk_rel_ms(E.v.rel) * (FK_SR / 1000.0f) + 1.0f);
    float etg = (E.gate || E.hold) && E.note >= 0 ? E.vel : 0.0f, xg = E.xf, xstep = E.xf_dir * (1.0f / 1100.0f), og, sg, xo = (float)E.xf_out;
    float duck = E.duck_g, dstep = (E.duck ? -1.0f : 1.0f) * (1.0f / 441.0f);
    float phs_a = 0.0f, phs_c[2], frz = E.frz, ftg = E.freeze ? 1.0f : 0.0f, fsh = fk_abs(E.v.field);
    float fmv[2], fms[2], ddry, dwet, dfb;
    if (!E.ramp_ok) {
        E.dry_p = dry;
        E.wet_p = wet;
        E.fb_p = fb;
    }
    ddry = (dry - E.dry_p) / (float)n;          /* MIX und FEED gleiten je Sample */
    dwet = (wet - E.wet_p) / (float)n;
    dfb = (fb - E.fb_p) / (float)n;
    dry = E.dry_p;
    wet = E.wet_p;
    fb = E.fb_p;
    E.dry_p += ddry * (float)n;
    E.wet_p += dwet * (float)n;
    E.fb_p += dfb * (float)n;
    {   /* FORM: je diffuser, desto mehr schweben die Abgriffe (zwei langsame LFOs, links und rechts verschieden) */
        int ch;
        float dep = form * form * 0.0045f;
        for (ch = 0; ch < 2; ch++) {
            float m0 = E.fm_v[ch];
            E.fm_ph[ch] = fk_wrap(E.fm_ph[ch] + (0.23f + 0.17f * (float)ch) * (float)n / FK_SR);
            E.fm_v[ch] = dep * (fk_sin(E.fm_ph[ch]) + 0.4f * fk_sin(E.fm_ph[ch] * 2.0f + 0.3f));
            fmv[ch] = m0;
            fms[ch] = (E.fm_v[ch] - m0) / (float)n;
        }
    }
    for (i = 0; i < 12; i++)                            /* DISPERSOR: FORM wie stark, SEED verteilt die Allpässe */
        dco[i] = fk_clamp(-0.15f + 0.95f * form + 0.18f * E.dsp_a[i], -0.6f, 0.94f);
    if (grain) {                                        /* die Körner: die Leseköpfe wandern mit (1 - Verhältnis) */
        float ratio = fm_exp2f(E.v.cell * 1.0f);
        gstep = (1.0f - ratio) / gw;
    }
    if (k->crush > 0.0f)
        crush_q = fm_exp2f(-(15.0f - k->crush * 12.5f));
    if (k->phaser > 0.0f) {                             /* der Phaser: vier Kerben, langsam schwebend */
        float lf = fk_sin(E.ph_lfo), base = 300.0f + 2600.0f * k->phaser;
        int ch;
        for (ch = 0; ch < 2; ch++) {
            float hz = base * fm_exp2f((ch ? -lf : lf) * 1.2f), t = fk_clamp(FK_PI * hz / FK_SR, 0.001f, 1.4f);
            phs_c[ch] = (t - 1.0f) / (t + 1.0f);
        }
        phs_a = 0.5f + 0.25f * k->phaser;
        E.ph_lfo = fk_wrap(E.ph_lfo + 0.11f * (float)n / FK_SR);
    }
    for (i = 0; i < n; i++) {
        float x[2], gm;
        E.dsm += (dtarget - E.dsm) * 0.00045f;           /* TIME gleitet: das Delay zieht die Tonhöhe mit */
        for (c = 0; c < 2; c++) {                       /* die Abgriffe gleiten je Sample (je Block sprängen sie: Knacksen) */
            int j;
            for (j = 1; j < 4; j++)
                E.tap[c][j] += (E.tap_t[c][j] - E.tap[c][j]) * 0.00032f;
        }
        if (fk_abs(xstep) > 0.0f) {                     /* die Blende beim Wechsel des Algorithmus */
            xg += xstep;
            if (xg <= 0.0f) {
                xg = 0.0f;
                xstep = 0.0f;
            } else if (xg >= 1.0f) {
                xg = 1.0f;
                xstep = 0.0f;
                xo = 0.0f;
                E.xf_out = 0;
            }
        }
        duck = fk_clamp(duck + dstep, 0.0f, 1.0f);
        dry += ddry;
        wet += dwet;
        fb += dfb;
        sg = xg * xg * (3.0f - 2.0f * xg);       /* die Blende: eine weiche S-Kurve statt einer Geraden */
        og = 1.0f - xo + xo * sg;                 /* wechselt der Prozessor mit, blendet der ganze Ausgang */
        x[0] = fk_soft(gl[i] * sg);               /* jede Stufe weich begrenzt: der Generator */
        x[1] = fk_soft(gr[i] * sg);
        gm = (x[0] + x[1]) * 0.5f;
        E.env += (etg - E.env) * (etg > E.env ? atk : rel);
        /* Chorus: zwei langsam schwebende Abgriffe, links und rechts gegenläufig */
        chl += 0.31f / FK_SR;
        if (chl >= 1.0f)
            chl -= 1.0f;
        for (c = 0; c < 2; c++) {
            float *b = E.m.ch[c], v = x[c];
            b[E.chw] = v;
            if (k->chorus > 0.0f) {
                float d = 330.0f + 230.0f * fk_sin(chl + (c ? 0.25f : 0.0f)), p = (float)E.chw - d, fr, t;
                int r;
                if (p < 0.0f)
                    p += (float)FK_CH_N;
                r = (int)p;
                fr = p - (float)r;
                t = b[r] + (b[(r + 1) & (FK_CH_N - 1)] - b[r]) * fr;
                v = v * (1.0f - 0.45f * k->chorus) + t * 0.85f * k->chorus;
            }
            if (crush_q > 0.0f)
                v = fm_floorf(v / crush_q + 0.5f) * crush_q;
            if (k->sat > 0.0f)
                v = fk_lerp(v, fk_sat(v * satg) * satn, k->sat);
            if (k->phaser > 0.0f) {
                float a = phs_c[c], y = v;
                y = fk_ap1(&E.phz[c][0], y, a);
                y = fk_ap1(&E.phz[c][1], y, a);
                y = fk_ap1(&E.phz[c][2], y, a);
                y = fk_ap1(&E.phz[c][3], y, a);
                v = v * (1.0f - phs_a * 0.5f) + y * phs_a * 0.5f;
            }
            if (k->bpm > 0.0f) {                         /* ein resonanter Bandpass, dazu ein Sinus auf BASIS */
                float lp, bp, hp;
                fk_svf_run(&E.bpf[c], &E.bpc, v, &lp, &bp, &hp);
                v = fk_lerp(v, bp * 1.6f + k->psine * fk_sin(E.sine_ph) * 0.35f, k->bpm);
            }
            {                                           /* FIELD: Filter und Klangformer in einem */
                float lp, bp, hp, lp2, bp2, hp2;
                if (fsh > 0.0f)                         /* je weiter aus der Mitte, desto mehr formt es vor dem Filter */
                    v = fk_lerp(v, fk_sat(v * (1.0f + fsh * 3.0f)) * (1.0f + fsh * 0.4f), fsh * 0.7f);
                fk_svf_run(&E.flt[c], &E.fc, v, &lp, &bp, &hp);       /* erst der Tiefpass, dann der Hochpass: */
                fk_svf_run(&E.flh[c], &E.fch, lp, &lp2, &bp2, &hp2);  /* kein Umschalten, kein Knacken in der Mitte */
                v = hp2 + (E.field_hp ? bp2 : bp) * fsh * 0.6f;
                v = fk_soft(v);                         /* (weich begrenzt: das Filter) */
            }
            x[c] = v * E.env * sg;             /* (auch nach Chorus und Filter: beim Wechsel ist es dort ganz still) */
        }
        E.sine_ph = ph_step(E.sine_ph, E.f0 * (1.0f / FK_SR));
        E.chw = (E.chw + 1u) & (FK_CH_N - 1u);
        /* der Prozessor */
        {
            float y[2], s[2], in[2], raw0[2], D = E.dsm, rot_c, rot_s;
            for (c = 0; c < 2; c++) {
                in[c] = x[c];
                if (refl) {                             /* REFLEKTA: das Eingangssignal zerstreut */
                    int b = c * 4;
                    in[c] = ap_run(b + 3, ap_run(b + 2, ap_run(b + 1, ap_run(b, in[c], gref), gref), gref), gref);
                }
            }
            for (c = 0; c < 2; c++) {
                const int16_t *b = E.m.dl[c];
                float Dm = D * (1.0f + fmv[c]), dd = Dm * (1.0f + k->dmod * 0.035f * gm), t0 = dl_read(b, E.dw, dd);
                fmv[c] += fms[c];
                raw0[c] = dl_read(b, E.dw, D);
                if (refl) {
                    float t1 = dl_read(E.m.dl[c ^ 1], E.dw, Dm * E.tap[c][1]);
                    y[c] = t0 * (1.0f - 0.32f * form) + t1 * 0.42f * form;
                } else {
                    float v = t0;
                    if (form > 0.02f) {
                        float t1 = dl_read(b, E.dw, Dm * E.tap[c][1]), t2 = dl_read(b, E.dw, Dm * E.tap[c][2]);
                        float t3 = dl_read(b, E.dw, Dm * E.tap[c][3]);
                        v = t0 * (1.0f - 0.45f * form) + (t1 + t2 + t3) * 0.36f * form;
                    }
                    if (disp) {                 /* DISPERSOR: zwölf Allpässe erster Ordnung: die Echos zirpen */
                        int j;
                        for (j = 0; j < 12; j++)
                            v = fk_ap1(&E.dsp_z[c][j], v, dco[j]);
                        y[c] = v;
                    } else {
                        v = ap_run(8 + c * 2, v, gdun);
                        y[c] = ap_run(9 + c * 2, v, gdun * 0.9f);
                    }
                }
                if (grain) {                            /* zwei Köpfe, je ein Sinus²-Fenster: zusammen immer 1 */
                    float p0 = E.gp, p1 = p0 + 0.5f, w0, h0, h1;
                    if (p1 >= 1.0f)
                        p1 -= 1.0f;
                    w0 = fk_sin(p0 * 0.5f);
                    w0 *= w0;
                    h0 = dl_read(b, E.dw, D + p0 * gw + 2.0f);
                    h1 = dl_read(b, E.dw, D + p1 * gw + 2.0f);
                    s[c] = h0 * w0 + h1 * (1.0f - w0);
                }
            }
            if (grain) {
                E.gp += gstep;
                E.gp -= fm_floorf(E.gp);
            } else if (shift != 0.0f) {                 /* der Frequenzverschieber: y * e^(i w t) (nur das obere Band) */
                rot_c = E.rc;
                rot_s = E.rs;
                for (c = 0; c < 2; c++) {
                    float re = hil_chain(E.hil[c][0], HIL_A, y[c]), im = hil_chain(E.hil[c][1], HIL_B, y[c]);
                    float re1 = E.hd[c];
                    E.hd[c] = re;
                    s[c] = re1 * rot_c - im * rot_s;
                }
                E.rc = rot_c * cw - rot_s * sw;
                E.rs = rot_s * cw + rot_c * sw;
            } else {
                s[0] = y[0];
                s[1] = y[1];
            }
            frz += (ftg - frz) * 0.0004f;
            for (c = 0; c < 2; c++) {
                float f = fk_lerp(y[c], s[c], fneg), wv = fk_lerp(0.5f * (y[c] + s[c]), s[c], fneg), z, o;
                z = fk_lp1(&E.dz[c], f, damp);
                z -= fk_lp1(&E.hz[c], z, hpc);
                o = fk_sat((in[c] + z * fb * og) * 0.92f);   /* (wechselt der Prozessor: die Schleife mit leiser, sonst kehrt der Sprung als Knacks wieder) */
                if (frz > 0.0f)                         /* FREEZE: das rohe Delay kreist unverändert, nichts kommt dazu */
                    o = fk_lerp(o, fk_clamp(raw0[c], -1.0f, 1.0f), frz);
                E.m.dl[c][E.dw] = (int16_t)(o * 29000.0f);
                if (c)
                    or_[i] = (x[c] * dry + wv * wet) * duck * og;
                else
                    ol[i] = (x[c] * dry + wv * wet) * duck * og;
            }
            E.dw = (E.dw + 1u) & (FK_DL_N - 1u);
        }
    }
    E.xf = xg;
    if (xstep == 0.0f)
        E.xf_dir = 0.0f;
    E.duck_g = duck;
    E.ch_lfo = chl;
    E.frz = frz;
    {   /* der Zeiger des Verschiebers bleibt auf dem Einheitskreis */
        float m = E.rc * E.rc + E.rs * E.rs;
        m = (3.0f - m) * 0.5f;
        E.rc *= m;
        E.rs *= m;
    }
}
