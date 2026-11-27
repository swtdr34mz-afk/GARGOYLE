/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die neun Generatoren der ersten Bank. Jeder rechnet einen Block Stereo (gl, gr) und sagt in E.cfg, was
 * die gemeinsamen Stufen danach tun sollen (Chorus, Sättigung, Bitcrush, Phaser, Diffusion ...). Die
 * Phasen laufen in Umdrehungen (0..1). Teil von fikh_core.c. */

/* 2^x für |x| <= 1.5, schnell genug pro Sample (Kreuzmodulation) */
static inline float fk_exp2s(float x)
{
    x = fk_clamp(x, -1.5f, 1.5f);
    return 1.0f + x * (0.6931472f + x * (0.2402265f + x * 0.0555041f));
}
static inline float ph_step(float p, float d)
{
    p += d;
    return p >= 1.0f ? p - fm_floorf(p) : p < 0.0f ? p - fm_floorf(p) : p;
}

/* eine Glocke um c (0..1), Breite s: das "Cluster-Scannen" über die Oszillatoren */
static inline float scan_bell(float x, float c, float s)
{
    float d = (x - c) / s;
    return fm_expf(-d * d);
}

/* das Inkrement eines Oszillators: Halbtöne über BASIS, dazu die eigene Drift dieses Kanals */
static inline float osc_inc(int c, int i, float semi)
{
    float f = E.f0 * fm_exp2f((semi + E.dr[c][i] * 0.01f) * (1.0f / 12.0f));
    return fk_clamp(f * (1.0f / FK_SR), 0.0f, 0.45f);
}

/* ---- 1 METAL PAIR: zwei analoge Oszillatoren; WARP links exponentielle FM, rechts Ringmodulation;
 *      SPAN der Abstand des Modulators (±2 Oktaven), MORPH die Welle des Trägers, SCAN die des Modulators */
static void gen_metal(float *gl, float *gr, int n)
{
    float w = E.v.warp, m_semi = E.v.span * 24.0f + E.r_off[0] * 0.25f;
    int c, i;
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, dtc = osc_inc(c, 0, 0.0f), dtm = osc_inc(c, 1, m_semi);
        float pc = E.ph[c][0], pm = E.ph[c][1];
        for (i = 0; i < n; i++) {
            float m = fk_vosc(pm, dtm, E.v.scan), d = dtc, y;
            pm = ph_step(pm, dtm);
            if (w < 0.0f)
                d = fk_clamp(d * fk_exp2s(m * -w * 1.5f), 0.0f, 0.45f);
            y = fk_vosc(pc, d, E.v.morph);
            pc = ph_step(pc, d);
            if (w > 0.0f)
                y = y * (1.0f - w) + y * m * w * 1.5f;
            o[i] = y * 0.66f;
        }
        E.ph[c][0] = pc;
        E.ph[c][1] = pm;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.res = E.v.fuse * 0.85f;
}

/* ---- 2 SHAPER PAIR: ein phasenmodulierter Sinus durch einen Waveshaper; WARP links Index und Rückkopplung des
 *      Modulators, rechts die des Trägers; SPAN die Trägerfrequenz (±1 Oktave, rechts Stereo); MORPH die Stärke
 *      des Shapers, SCAN seine Familie (Falter, Sättigung, Tschebyschow) */
static void gen_shaper(float *gl, float *gr, int n)
{
    float w = E.v.warp, idx = 0.10f + (w < 0.0f ? -w * 0.55f : 0.06f), bm = w < 0.0f ? -w * 0.11f : 0.0f;
    float bc = w > 0.0f ? w * 0.16f : 0.0f, drv = 1.0f + E.v.morph * 5.0f, sc = E.v.scan;
    int c, i;
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, st = E.v.span * 12.0f + (c && E.v.span > 0.0f ? E.v.span * 0.18f : 0.0f);
        float dtc = osc_inc(c, 0, st), dtm = osc_inc(c, 1, 0.0f), pc = E.ph[c][0], pm = E.ph[c][1];
        float mp = E.fb[c][1], cp = E.fb[c][0];
        for (i = 0; i < n; i++) {
            float m = fk_sin(pm + bm * mp), x, s0, s1, s2, y3, y;
            mp = m;
            x = fk_sin(pc + idx * m + bc * cp);
            cp = x;
            pm = ph_step(pm, dtm);
            pc = ph_step(pc, dtc);
            s0 = fk_fold(x * drv);
            s1 = fk_sat(x * drv * 1.3f);
            y3 = fk_sat(x * drv);
            s2 = y3 * (4.0f * y3 * y3 - 3.0f);
            y = sc < 0.5f ? fk_lerp(s0, s1, sc * 2.0f) : fk_lerp(s1, s2, sc * 2.0f - 1.0f);
            o[i] = y * 0.52f;
        }
        E.ph[c][0] = pc;
        E.ph[c][1] = pm;
        E.fb[c][0] = cp;
        E.fb[c][1] = mp;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.dmod = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* (fikh_cluster.c) */
static void gen_cluster4(float *gl, float *gr, int n, int set, int quant, float gain);

/* die Tonleiter der diatonischen Cluster: natürliches Moll */
static float minor_semi(int deg)
{
    static const uint8_t ST[7] = {0, 2, 3, 5, 7, 8, 10};
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    return (float)(ST[deg - o * 7] + 12 * o);
}

/* ---- 5 DIATONIC: vier Stimmen mit Cluster-Scanning, in Moll quantisiert (SPAN spreizt in Tonleiterstufen, rechts
 *      Stereo); MORPH scannt die Paare aus Tonstufe und Welle; SCAN verschiebt die Wellen; FUSE links Stereo-Chorus, rechts Sättigung */
static void gen_diatonic(float *gl, float *gr, int n)
{
    gen_cluster4(gl, gr, n, 1, 1, 0.22f);
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 6 CHORD SCAN: vier Stimmen spielen einen Akkord; MORPH scannt durch zehn Akkorde (die Töne gleiten);
 *      SPAN die Lage (rechts nach oben gespreizt und Stereo, links nach unten); WARP die Wellenform; SCAN der Abstand
 *      der Phaser-Kerben; FUSE links Chorus, rechts Sättigung */
static void gen_chords(float *gl, float *gr, int n)
{
    static const int8_t CH[10][4] = {{0, 7, 12, 19}, {0, 3, 7, 10}, {0, 2, 7, 12}, {0, 5, 7, 12}, {0, 3, 7, 14},
                                     {0, 4, 7, 11}, {0, 7, 10, 15}, {0, 5, 10, 15}, {0, 3, 10, 17}, {0, 7, 14, 21}};
    int ci = (int)(E.v.morph * 9.99f), c, i, k;
    float sp = E.v.span, pos = fk_clamp(0.30f + E.v.warp * 0.30f, 0.0f, 1.0f), fold = E.v.warp > 0.5f ? (E.v.warp - 0.5f) * 4.0f : 0.0f;
    for (k = 0; k < 4; k++) {
        float t = (float)CH[ci][k], o = fm_floorf(fk_abs(sp) * 2.99f * (float)k / 3.0f);
        t += sp >= 0.0f ? 12.0f * o : -12.0f * o;
        E.semi[k] += (t - E.semi[k]) * 0.08f;
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d[4], p[4], a[4];
        for (k = 0; k < 4; k++) {
            float pan = sp > 0.0f ? ((k & 1) ? 1.0f : -1.0f) * sp * 0.6f : 0.0f;
            d[k] = osc_inc(c, k, E.semi[k]);
            p[k] = E.ph[c][k];
            a[k] = c ? 1.0f + pan : 1.0f - pan;
        }
        for (i = 0; i < n; i++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) {
                float y;
                p[k] = ph_step(p[k], d[k]);
                y = wt_read(E.m.wt, 0, pos, p[k]);
                if (fold > 0.0f)
                    y = fk_fold(y * (1.0f + fold));
                s += a[k] * y;
            }
            o[i] = s * 0.30f;
        }
        for (k = 0; k < 4; k++)
            E.ph[c][k] = p[k];
    }
    E.cfg.phaser = E.v.scan;
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 7 PARTIALS: acht Teiltöne; SPAN links unharmonisch gestreckt, rechts von allen zu nur ungeraden Obertönen;
 *      MORPH scannt über die Teiltöne; SCAN würfelt ihre Lautstärken; WARP links PM unter Nachbarn, rechts
 *      Kreuz-FM; FUSE links Diffusion, rechts Sättigung */
static void gen_partials(float *gl, float *gr, int n)
{
    float w = E.v.warp, sp = E.v.span, ratio[8], amp[8], a2 = 0.0f, g, pm = w < 0.0f ? -w * 0.22f : 0.0f;
    float xm = w > 0.0f ? w * 0.9f : 0.0f;
    int c, i, k, N = E.eco ? 5 : 8;
    for (k = 0; k < N; k++) {
        float h = (float)(k + 1);
        if (sp < 0.0f)
            ratio[k] = h * (1.0f - sp * 0.06f * (float)k) * (1.0f - sp * E.r_off[k] * 0.025f);
        else
            ratio[k] = fk_lerp(h, 2.0f * h - 1.0f, sp);
        amp[k] = (0.3f + scan_bell((float)k / 7.0f, E.v.morph, 0.22f)) * fk_lerp(1.0f, E.r_vol[k] * 1.8f, E.v.scan);
        amp[k] /= fm_sqrtf(h * 0.6f + 0.4f);
        if (E.f0 * ratio[k] > 17000.0f)
            amp[k] = 0.0f;
        a2 += amp[k] * amp[k];
    }
    g = 0.5f / fm_sqrtf(a2 + 1e-6f);
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d[8], p[8], y[8];
        float df = fm_exp2f(E.dr[c][0] * (0.01f / 12.0f));
        for (k = 0; k < N; k++) {
            d[k] = fk_clamp(E.f0 * ratio[k] * df * fm_exp2f(E.dr[c][k] * (0.004f / 12.0f)) * (1.0f / FK_SR), 0.0f, 0.45f);
            p[k] = E.ph[c][k];
            y[k] = E.fb[c][k];
        }
        for (i = 0; i < n; i++) {
            float s = 0.0f, prev = y[N - 1];
            for (k = 0; k < N; k++) {
                float dk = d[k];
                if (xm > 0.0f)
                    dk *= fk_exp2s(prev * xm * 0.5f);
                p[k] = ph_step(p[k], dk);
                y[k] = fk_sin(p[k] + pm * prev);
                prev = y[k];
                s += amp[k] * y[k];
            }
            o[i] = s * g * 0.62f;
        }
        for (k = 0; k < N; k++) {
            E.ph[c][k] = p[k];
            E.fb[c][k] = y[k];
        }
    }
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 8 COMB SWARM: ein Schwarm von vier Kammfiltern um BASIS, angeregt von Säge und Rauschen; SPAN spreizt ihre
 *      Stimmung (rechts Stereo); MORPH scannt über die Kämme und hellt die Anregung auf; SCAN verstimmt sie
 *      zufällig; WARP links verrauschte, rechts gesättigte Rückkopplung; FUSE links Diffusion, rechts Sättigung */
static void gen_combs(float *gl, float *gr, int n)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), len[4], a[4], pl[4], pr[4], fb, nzk, sk;
    float dts = E.f0 * (1.0f / FK_SR), ps = E.ph[0][0], brite = 0.2f + 0.8f * E.v.morph, nlp = E.nz[0];
    uint32_t wi = E.cbw, k;
    int i;
    fb = w < 0.0f ? 0.94f - w * 0.045f : 0.95f + w * 0.045f;
    nzk = w < 0.0f ? -w * 0.06f : 0.0f;
    sk = w > 0.0f ? 1.0f + w * 2.5f : 1.0f;
    for (k = 0; k < 4; k++) {
        float st = E.r_off[k] * sp * 12.0f + E.v.scan * E.r_off2[k] * 4.0f, f;
        f = E.f0 * fm_exp2f((st + E.dr[k & 1][k] * 0.01f) * (1.0f / 12.0f));
        len[k] = fk_clamp(FK_SR / f, 8.0f, (float)FK_CB_N - 4.0f);
        a[k] = 0.3f + scan_bell((float)k / 3.0f, E.v.morph, 0.35f);
        pl[k] = 1.0f - (E.v.span > 0.0f ? E.r_pan[k] * E.v.span * 0.8f : 0.0f);
        pr[k] = 2.0f - pl[k];
    }
    for (i = 0; i < n; i++) {
        float ex, sl = 0.0f, sr = 0.0f, saw = 2.0f * ps - 1.0f - fk_blep(ps, dts), nz = fk_rand11(&E.noise);
        ps = ph_step(ps, dts);
        nlp += (nz - nlp) * brite;
        ex = saw * 0.10f + nlp * 0.12f;
        for (k = 0; k < 4; k++) {
            float *b = E.m.comb[k], rp = (float)wi - len[k], fr, y, z;
            int r0;
            if (rp < 0.0f)
                rp += (float)FK_CB_N;
            r0 = (int)rp;
            fr = rp - (float)r0;
            y = b[r0] + (b[(r0 + 1) & (FK_CB_N - 1)] - b[r0]) * fr;
            z = E.cbz[k] += (y - E.cbz[k]) * 0.55f;
            if (sk > 1.0f)
                z = fk_sat(z * sk) / sk * 1.2f;
            b[wi] = fm_flush(ex + fb * z + nz * nzk);
            sl += a[k] * pl[k] * y;
            sr += a[k] * pr[k] * y;
        }
        wi = (wi + 1u) & (FK_CB_N - 1u);
        gl[i] = sl * 0.30f;
        gr[i] = sr * 0.30f;
    }
    E.cbw = wi;
    E.ph[0][0] = ps;
    E.nz[0] = nlp;
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 9 GRAIN CLOUD: vier Stimmen in einem freien Cluster; der Prozessor verschiebt die Tonhöhe in Körnern
 *      (CELL ±12 Halbtöne, FORM die Korngröße); FUSE links Diffusion, rechts Sättigung */
static void gen_grain(float *gl, float *gr, int n)
{
    gen_cluster4(gl, gr, n, 2, 0, 0.50f);
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}
