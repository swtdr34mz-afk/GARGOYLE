/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die Generatoren der BANK 2. Nach den Themen, die Forge TME für die zweite Bank des VHIKK X nennt (Wavetable
 * mit 0-Hz-Träger, gekoppelte Sinus mit Dispersor, Sinus-getriebener Kerbenstapel, Sinus-Schwarm, "architectural
 * morphing", "environmental synthesis", "alien radio channel-surfing"), dazu ein Faltbass und ein Supersaw. Eigener
 * Code. Teil von fikh_core.c (nach fikh_gen.c). */

/* ---- 10 ZERO CARRIER: ein Träger bei 0 Hz: er steht still, nur zwei Modulatoren bewegen seine Phase durch die
 *      Wavetable. SPAN die Frequenz des zweiten (±1 Oktave um BASIS); WARP links der erste tiefer mit Rückkopplung,
 *      rechts der zweite; MORPH die Welle; SCAN wo die Phase ruht. FUSE links Delay-Modulation, rechts Sättigung */
static void gen_zero(float *gl, float *gr, int n)
{
    float w = E.v.warp, d1 = 0.22f + (w < 0.0f ? -w * 0.9f : 0.0f), d2 = 0.12f + (w > 0.0f ? w * 0.9f : 0.05f);
    float fb = w < 0.0f ? -w * 0.18f : 0.0f, rest = E.v.scan;
    int c, i, set = (E.r_set + 1) % 4;
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, da = osc_inc(c, 0, 0.0f), db = osc_inc(c, 1, E.v.span * 12.0f);
        float pa = E.ph[c][0], pb = E.ph[c][1], ya = E.fb[c][0], y = E.fb[c][1];
        for (i = 0; i < n; i++) {
            float a = fk_sin(pa + fb * ya), b = fk_sin(pb), ph;
            ya = a;
            pa = ph_step(pa, da);
            pb = ph_step(pb, db);
            ph = rest + d1 * a + d2 * b;
            y = wt_read(E.m.wt, set, E.v.morph, ph - fm_floorf(ph));
            o[i] = y * 0.63f;
        }
        E.ph[c][0] = pa;
        E.ph[c][1] = pb;
        E.fb[c][0] = ya;
        E.fb[c][1] = y;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.dmod = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 12 NOTCH STACK: zwei Sinus, übersteuert, durch einen Stapel von vier Kerbfiltern, die MORPH durch das
 *      Spektrum schiebt (SEED verteilt sie). SPAN der zweite Sinus (±1 Oktave), WARP links Kreuz-FM, rechts PM;
 *      SCAN nach dem Treiber ein Gleichrichter / Waveshaper; FUSE links verrauschte, rechts harte Sättigung */
static void gen_notch(float *gl, float *gr, int n)
{
    float w = E.v.warp, u = E.v.fuse, drv = 2.5f + fk_abs(u) * 6.0f, sc = E.v.scan, xm = w < 0.0f ? -w * 1.2f : 0.0f;
    float pm = w > 0.0f ? w * 0.4f : 0.0f;
    fk_svf_c nc[4];
    int c, i, k;
    static const float NR[4] = {1.5f, 2.5f, 3.6f, 5.2f};
    for (k = 0; k < 4; k++)
        fk_svf_set(&nc[k], E.f0 * NR[k] * (1.0f + 0.25f * E.r_off2[k]) * fm_exp2f(E.v.morph * 3.5f - 1.2f), 0.3f);
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, da = osc_inc(c, 0, 0.0f), db = osc_inc(c, 1, E.v.span * 12.0f + 0.03f * (float)c);
        float pa = E.ph[c][0], pb = E.ph[c][1], yb = E.fb[c][1], nz = E.nz[c];
        for (i = 0; i < n; i++) {
            float a, b, x, lp, bp, hp;
            b = fk_sin(pb);
            pb = ph_step(pb, db);
            pa = ph_step(pa, xm > 0.0f ? fk_clamp(da * fk_exp2s(yb * xm), 0.0f, 0.45f) : da);
            a = fk_sin(pa + pm * b);
            yb = b;
            x = (a + 0.7f * b) * drv;
            if (u < 0.0f) {                     /* verrauscht: das Rauschen moduliert den Treiber */
                nz += (fk_rand11(&E.noise) - nz) * 0.05f;
                x *= 1.0f + nz * -u * 1.5f;
                x = fk_sat(x);
            } else {
                x = fk_clamp(x, -1.0f, 1.0f);
            }
            if (sc > 0.0f)                       /* der Gleichrichter (halb, dann voll) und ein Falter */
                x = sc < 0.5f ? fk_lerp(x, fk_abs(x) * 2.0f - 0.8f, sc * 2.0f) : fk_lerp(fk_abs(x) * 2.0f - 0.8f, fk_fold(x * 2.5f), sc * 2.0f - 1.0f);
            for (k = 0; k < 4; k++) {
                fk_svf_run(&E.nst[c][k], &nc[k], x, &lp, &bp, &hp);
                x = lp + hp;                     /* die Kerbe: alles außer dem Band */
            }
            o[i] = x * 0.75f;
        }
        E.ph[c][0] = pa;
        E.ph[c][1] = pb;
        E.fb[c][1] = yb;
        E.nz[c] = nz;
    }
}

/* ---- 13 SINE SWARM: acht Sinus, zufällig um BASIS verstreut (SEED, SPAN ±12 Halbtöne); MORPH scannt über den
 *      Schwarm, SCAN würfelt ihre Lautstärken; WARP links Paar-PM, rechts Kreuz-FM; FUSE links Diffusion, rechts
 *      Sättigung */
static void gen_swarm(float *gl, float *gr, int n)
{
    float w = E.v.warp, sp = fk_abs(E.v.span), amp[8], a2 = 0.0f, g, pm = w < 0.0f ? -w * 0.3f : 0.0f;
    float xm = w > 0.0f ? w * 0.8f : 0.0f;
    int c, i, k, N = E.eco ? 5 : 8;
    for (k = 0; k < N; k++) {
        amp[k] = (0.25f + scan_bell((float)k / (float)(N - 1), E.v.morph, 0.3f)) * fk_lerp(1.0f, E.r_vol[k] * 1.9f, E.v.scan);
        a2 += amp[k] * amp[k];
    }
    g = 0.42f / fm_sqrtf(a2 + 1e-6f);
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d[8], p[8], y[8];
        for (k = 0; k < N; k++) {
            float st = E.r_off[k] * sp * 12.0f + E.r_wave[k] * 0.3f;
            if (c && E.v.span > 0.0f)
                st += E.r_off2[k] * E.v.span * 0.3f;
            d[k] = osc_inc(c, k, st);
            p[k] = E.ph[c][k];
            y[k] = E.fb[c][k];
        }
        for (i = 0; i < n; i++) {
            float s = 0.0f;
            for (k = 0; k < N; k++) {
                float dk = d[k], prt = y[(k ^ 1) < N ? (k ^ 1) : k];
                if (xm > 0.0f)
                    dk *= fk_exp2s(y[(k + 3) % N] * xm);
                p[k] = ph_step(p[k], dk);
                y[k] = fk_sin(p[k] + pm * prt);
                s += amp[k] * y[k];
            }
            o[i] = s * g;
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

/* ---- 14 RADIO: ein Kurzwellenempfänger, der durch fremde Sender rauscht. MORPH ist die Skala: sechs Sender
 *      (SEED verteilt sie und was sie senden: Pieptöne, Zirpen, Stimmen aus Formanten, schwebende Akkorde),
 *      dazwischen Rauschen und Knacken. Knapp daneben pfeift die Überlagerung (WARP rechts), WARP links lässt sie
 *      schwinden (Fading). SPAN die Tonhöhe der Sender, SCAN wie stark es rauscht. FUSE links Chorus, rechts
 *      ein übersteuerter Lautsprecher */
static void gen_radio(float *gl, float *gr, int n)
{
    float w = E.v.warp, dial = E.v.morph, stat = 0.15f + 0.85f * E.v.scan, best = 0.0f, str[6], pos[6], dts = 1.0f / FK_SR;
    float fade = 1.0f, het = w > 0.0f ? w : 0.0f;
    int s, i, c, typ[6];
    uint32_t h = (uint32_t)E.seed * 2654435761u;
    for (s = 0; s < 6; s++) {
        float d;
        pos[s] = ((float)s + 0.5f) / 6.0f + E.r_off2[s] * 0.05f;
        d = (dial - pos[s]) / 0.05f;
        str[s] = fm_expf(-d * d);
        typ[s] = (int)((fk_hash(h + (uint32_t)s * 7919u) >> 8) % 4u);
        best = str[s] > best ? str[s] : best;
    }
    if (w < 0.0f) {                              /* Fading: der Empfang atmet */
        E.rad_qsb += (0.18f + 0.5f * -w) * (float)n * dts;
        E.rad_qsb -= fm_floorf(E.rad_qsb);
        fade = 1.0f + w * 0.85f * (0.5f + 0.5f * fk_sin(E.rad_qsb));
    }
    E.rad_t += (float)n * dts;
    fk_svf_set(&E.rad_nc, 2100.0f, 0.3f);
    {                                            /* VOICE: die Formanten springen, ~6 mal pro Sekunde, ein neuer Vokal */
        static const float F1[5] = {730, 530, 270, 570, 300}, F2[5] = {1090, 1840, 2290, 840, 870};
        if (E.rad_t - E.rad_vt > 0.17f) {
            int v = (int)(fk_rng(&E.noise) % 5u);
            E.rad_vt = E.rad_t;
            fk_svf_set(&E.rad_fc[0], F1[v], 0.85f);
            fk_svf_set(&E.rad_fc[1], F2[v], 0.85f);
        }
    }
    for (i = 0; i < n; i++) {
        float sig = 0.0f, st, nz = fk_rand11(&E.noise), lp, bp, hp;
        for (s = 0; s < 6; s++) {
            float f, v = 0.0f, a = str[s];
            if (a < 0.01f)
                continue;
            f = E.f0 * fm_exp2f(E.r_off[s] * E.v.span * 1.0f + 1.0f);
            switch (typ[s]) {
            case 0: {                            /* BEEP: ein Muster aus 16 Schritten, 5 pro Sekunde */
                int step = (int)(E.rad_t * 5.0f) & 15;
                int on = (fk_hash(h ^ (uint32_t)s * 31u) >> step) & 1u;
                E.rad_g[s] += ((on ? 1.0f : 0.0f) - E.rad_g[s]) * 0.01f;
                v = fk_sin(E.rad_ph[s]) * E.rad_g[s];
                E.rad_ph[s] = ph_step(E.rad_ph[s], f * dts);
                break;
            }
            case 1: {                            /* CHIRP: ein Ton, der immer wieder nach oben rutscht */
                float sw = E.rad_t * 0.7f + (float)s * 0.13f;
                sw -= fm_floorf(sw);
                v = fk_sin(E.rad_ph[s]);
                E.rad_ph[s] = ph_step(E.rad_ph[s], f * (1.0f + 2.5f * sw * sw) * dts);
                break;
            }
            case 2: {                            /* VOICE: ein Pulszug durch zwei Formanten */
                float pls = E.rad_ph[s] < 0.08f ? 1.0f : -0.087f, a1, b1, c1, a2, b2, c2;
                E.rad_ph[s] = ph_step(E.rad_ph[s], f * 0.5f * dts);
                fk_svf_run(&E.rad_f[0], &E.rad_fc[0], pls, &a1, &b1, &c1);
                fk_svf_run(&E.rad_f[1], &E.rad_fc[1], pls, &a2, &b2, &c2);
                v = (b1 + 0.7f * b2) * 0.8f;
                break;
            }
            default:                             /* CHORD: Oktave, Quinte, Terz darüber, langsam schwebend */
                v = (fk_sin(E.rad_ph[s] * 2.0f) + fk_sin(E.rad_ph[s] * 3.0f) * 0.7f + fk_sin(E.rad_ph[s] * 5.0f) * 0.5f) * 0.45f
                    * (0.6f + 0.4f * fk_sin(E.rad_t * 0.21f + (float)s * 0.3f));
                E.rad_ph[s] = ph_step(E.rad_ph[s], f * dts * 0.5f);
                break;
            }
            if (het > 0.0f && a < 0.97f) {       /* knapp daneben: die Überlagerung pfeift */
                float off = (dial - pos[s]) * 3000.0f;
                E.rad_hp[s] = ph_step(E.rad_hp[s], fk_abs(off) * dts);
                v += fk_sin(E.rad_hp[s]) * het * a * (1.0f - a) * 1.6f;
            }
            sig += v * a;
        }
        /* das Rauschen: ein Band um 2 kHz, dazu Knacken, umso mehr, je schlechter der Empfang */
        fk_svf_run(&E.rad_n, &E.rad_nc, nz, &lp, &bp, &hp);
        st = bp * (1.0f - 0.85f * best) * stat * 1.4f;
        if (fk_rand01(&E.noise) < 0.0004f * stat * (1.0f - best))
            E.rad_pop = (fk_rand11(&E.noise)) * 0.8f;
        st += E.rad_pop;
        E.rad_pop *= 0.92f;
        sig = sig * fade;
        E.rad_lp[0] += (sig - E.rad_lp[0]) * 0.32f;   /* wie ein kleiner Lautsprecher: oben zu */
        for (c = 0; c < 2; c++) {
            float x = E.rad_lp[0] + st * (c ? 0.8f + 0.2f * fk_rand11(&E.noise) : 1.0f);
            (c ? gr : gl)[i] = x * 1.5f;
        }
    }
    E.rad_pop = fm_flush(E.rad_pop);
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 15 WEATHER: Wind, Regen, Insekten, auf BASIS gestimmt. MORPH blendet vom Wind (ein Band, das in Böen
 *      wandert) über Regen (Tropfen, die Resonatoren in der Tonleiter anschlagen) zu Insekten (hohes, gepulstes
 *      Zirpen). WARP links Turbulenz, rechts wird alles tonaler; SPAN die Tonhöhe, SCAN wie stürmisch */
static void gen_weather(float *gl, float *gr, int n)
{
    float wW = scan_bell(0.0f, E.v.morph, 0.42f), wR = scan_bell(0.5f, E.v.morph, 0.32f), wI = scan_bell(1.0f, E.v.morph, 0.35f);
    float ton = E.v.warp > 0.0f ? E.v.warp : 0.0f, turb = E.v.warp < 0.0f ? -E.v.warp : 0.0f, storm = E.v.scan;
    float dts = 1.0f / FK_SR, rate = (6.0f + 60.0f * storm) * dts;
    fk_svf_c wc[2];
    int c, i, k;
    static const uint8_t DEG[7] = {0, 3, 5, 7, 10, 12, 15};
    E.wx_gust += (0.15f + 0.4f * storm) * (float)n * dts;
    if (E.wx_gust >= 1.0f) {
        E.wx_gust -= 1.0f;
        E.wx_g0 = E.wx_g1;
        E.wx_g1 = fk_rand01(&E.noise);
    }
    {
        float gt = fk_lerp(E.wx_g0, E.wx_g1, 0.5f - 0.5f * fm_cosf(E.wx_gust * FK_PI));
        for (c = 0; c < 2; c++)
            fk_svf_set(&wc[c], E.f0 * fm_exp2f(E.v.span * 1.5f) * (1.5f + 3.5f * gt + 0.3f * (float)c), 0.35f + 0.6f * ton);
        E.wx_gain = 0.4f + 0.9f * gt * (0.4f + storm);
    }
    for (i = 0; i < n; i++) {
        float out[2] = {0.0f, 0.0f};
        /* Regen: ein Tropfen, ab und zu, schlägt einen Resonator in der Tonleiter an */
        if (wR > 0.02f && fk_rand01(&E.noise) < rate) {
            int r = (int)(fk_rng(&E.noise) % 4u), deg = DEG[fk_rng(&E.noise) % 7u];
            float f = E.f0 * fm_exp2f(((float)deg + 12.0f + E.v.span * 12.0f) / 12.0f), wv = FK_TAU * fk_clamp(f, 20.0f, 9000.0f) * dts;
            E.wx_rc[r] = 2.0f * 0.9985f * fm_cosf(wv);
            E.wx_rx[r] = 0.5f + 0.5f * fk_rand01(&E.noise);
            E.wx_rpan[r] = fk_rand01(&E.noise);
        }
        for (c = 0; c < 2; c++) {
            float nz = fk_rand11(&E.noise), lp, bp, hp, wind, rain = 0.0f, bug = 0.0f;
            /* Wind */
            E.wx_t[c] += (nz - E.wx_t[c]) * (0.02f + 0.1f * turb * fk_rand01(&E.noise));
            fk_svf_run(&E.wx_f[c], &wc[c], nz * (1.0f - ton * 0.7f) + E.wx_t[c] * 2.0f, &lp, &bp, &hp);
            wind = (bp * (0.6f + ton) + lp * 0.25f) * E.wx_gain;
            /* Regen: vier Resonatoren, mal links, mal rechts */
            for (k = 0; k < 4; k++) {
                float y = E.wx_rc[k] * E.wx_r1[k] - 0.997f * E.wx_r2[k] + (c == 0 ? E.wx_rx[k] : 0.0f);
                if (c == 0) {
                    E.wx_r2[k] = E.wx_r1[k];
                    E.wx_r1[k] = y;
                    E.wx_rx[k] = 0.0f;
                }
                rain += E.wx_r1[k] * (c ? E.wx_rpan[k] : 1.0f - E.wx_rpan[k]);
            }
            rain = rain * 0.05f + hp * 0.04f * storm;
            /* Insekten: zwei hohe Töne, schnell gepulst, in Schüben */
            for (k = 0; k < 2; k++) {
                float pu = E.wx_ip[k] < 0.35f ? 1.0f : 0.0f, burst = fk_sin(E.rad_t * (0.3f + 0.17f * (float)k) + (float)k * 0.4f);
                if (c == 0) {
                    E.wx_ip[k] = ph_step(E.wx_ip[k], (38.0f + 14.0f * (float)k) * dts);
                    E.wx_is[k] = ph_step(E.wx_is[k], E.f0 * fm_exp2f(3.0f + 0.4f * (float)k + E.v.span) * dts);
                }
                bug += fk_sin(E.wx_is[k]) * pu * (burst > 0.2f ? burst : 0.0f) * ((c ^ k) ? 0.9f : 0.4f);
            }
            out[c] = wind * wW + rain * wR + bug * wI * 0.22f;
        }
        E.rad_t += dts;
        gl[i] = out[0] * 2.0f;
        gr[i] = out[1] * 2.0f;
    }
    for (k = 0; k < 4; k++) {
        E.wx_r1[k] = fm_flush(E.wx_r1[k]);
        E.wx_r2[k] = fm_flush(E.wx_r2[k]);
    }
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 16 ARCHITECT: ein Körper aus sechs Resonanzen (Moden), den MORPH umbaut: Saite, Stab, Platte, Glocke, Raum.
 *      Angeschlagen von jeder Note und von selbst (SCAN: wie oft), WARP links gestrichen (dauerndes Rauschen), rechts
 *      härter angeschlagen; SPAN links gedämpft (Holz), rechts lang klingend und breit; SEED verschiebt die Moden */
static void gen_arch(float *gl, float *gr, int n)
{
    static const float SH[5][6] = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
                                   {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f},
                                   {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f},
                                   {0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f},
                                   {1.0f, 1.12f, 1.41f, 1.68f, 1.89f, 2.24f}};
    float m = E.v.morph * 3.999f, fr = m - fm_floorf(m), dec = 0.25f * fm_exp2f((E.v.span + 1.0f) * 2.6f);
    float bow = E.v.warp < 0.0f ? -E.v.warp : 0.0f, hard = E.v.warp > 0.0f ? E.v.warp : 0.0f, dts = 1.0f / FK_SR;
    float rate = 0.25f * fm_exp2f(E.v.scan * 6.0f), cf[6], rr[6], gk[6];
    int si = (int)m, c, i, k;
    for (k = 0; k < 6; k++) {
        float r = fm_exp2f(fk_lerp(fm_log2f(SH[si][k]), fm_log2f(SH[si + 1][k]), fr)) * (1.0f + E.r_off[k] * 0.02f);
        float f = fk_clamp(E.f0 * r, 20.0f, 16000.0f), d = dec / fm_sqrtf(r);
        rr[k] = fm_expf(-1.0f / (d * FK_SR));
        cf[k] = 2.0f * rr[k] * fm_cosf(FK_TAU * f * dts);
        gk[k] = fm_sinf(FK_TAU * f * dts) * (1.0f / (1.0f + 0.3f * (float)k));   /* ein Anschlag: gleich laut in jeder Lage */
    }
    for (i = 0; i < n; i++) {
        float ex[2];
        if (E.arch_trig || fk_rand01(&E.noise) < rate * dts) {   /* ein Anschlag */
            E.arch_trig = 0;
            E.arch_env = 0.6f + 0.4f * fk_rand01(&E.noise);
        }
        for (c = 0; c < 2; c++) {
            float nz = fk_rand11(&E.noise), e = E.arch_env, s = 0.0f;
            E.arch_lp[c] += (nz - E.arch_lp[c]) * (0.08f + 0.85f * hard);
            ex[c] = E.arch_lp[c] * e * 2.5f + nz * bow * 0.06f;
            for (k = 0; k < 6; k++) {
                float y = cf[k] * E.arch_y1[c][k] - rr[k] * rr[k] * E.arch_y2[c][k] + ex[c] * gk[k] * (c ? 0.8f + 0.4f * E.r_pan[k] * (E.v.span > 0.0f ? 1.0f : 0.0f) : 1.0f);
                E.arch_y2[c][k] = E.arch_y1[c][k];
                E.arch_y1[c][k] = y;
                s += y;
            }
            (c ? gr : gl)[i] = fk_sat(s * 1.6f) * 0.5f;
        }
        E.arch_env *= 0.992f;
    }
    for (c = 0; c < 2; c++)
        for (k = 0; k < 6; k++) {
            E.arch_y1[c][k] = fm_flush(E.arch_y1[c][k]);
            E.arch_y2[c][k] = fm_flush(E.arch_y2[c][k]);
        }
    E.arch_env = fm_flush(E.arch_env);
    if (E.v.fuse < 0.0f)
        E.cfg.diff = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}

/* ---- 17 FOLD BASS: zwei Sägezähne und ein Sub eine Oktave tiefer, durch einen Wavefolder: der Bass der Bank.
 *      MORPH wie stark gefaltet wird, SCAN die Symmetrie (asymmetrisch: neue Obertöne), SPAN die Verstimmung des
 *      zweiten (±7 Halbtöne), WARP links FM vom Sub, rechts Ringmodulation; FUSE links Chorus, rechts Resonanz */
static void gen_fold(float *gl, float *gr, int n)
{
    float w = E.v.warp, drv = 1.0f + E.v.morph * 6.5f, bias = (E.v.scan - 0.5f) * 0.9f, fm = w < 0.0f ? -w * 0.4f : 0.0f;
    float rm = w > 0.0f ? w : 0.0f, dc = fk_fold(bias);
    int c, i;
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d1 = osc_inc(c, 0, 0.0f), d2 = osc_inc(c, 1, E.v.span * 7.0f), ds = osc_inc(c, 2, -12.0f);
        float p1 = E.ph[c][0], p2 = E.ph[c][1], ps = E.ph[c][2];
        for (i = 0; i < n; i++) {
            float sub = fk_sin(ps), s1, s2, x, dd = d1;
            ps = ph_step(ps, ds);
            if (fm > 0.0f)
                dd = fk_clamp(d1 * (1.0f + sub * fm), 0.0f, 0.45f);
            s1 = 2.0f * p1 - 1.0f - fk_blep(p1, dd);
            s2 = 2.0f * p2 - 1.0f - fk_blep(p2, d2);
            p1 = ph_step(p1, dd);
            p2 = ph_step(p2, d2);
            x = (s1 + 0.8f * s2) * 0.45f + sub * 0.55f;
            if (rm > 0.0f)
                x = fk_lerp(x, x * sub * 1.6f, rm);
            o[i] = (fk_fold(x * drv + bias) - dc) * 0.38f + sub * 0.22f;
        }
        E.ph[c][0] = p1;
        E.ph[c][1] = p2;
        E.ph[c][2] = ps;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.res = E.v.fuse * 0.8f;
}

/* ---- 18 SUPERSAW: sieben Sägezähne, verstimmt (SPAN bis ±1 Halbton, rechts in Stereo), SEED zerzaust sie ein
 *      wenig. MORPH vom Sägezahn zum schwebenden Puls (PWM), SCAN die Mitte gegen die Seiten; WARP links ein Falter,
 *      rechts ein Sub eine Oktave tiefer mit Treiber; FUSE links Chorus, rechts Sättigung */
static void gen_ssaw(float *gl, float *gr, int n)
{
    static const float DT[7] = {-1.0f, -0.66f, -0.33f, 0.0f, 0.33f, 0.66f, 1.0f};
    float sp = fk_abs(E.v.span), w = E.v.warp, fold = w < 0.0f ? -w * 2.5f : 0.0f, sub = w > 0.0f ? w : 0.0f;
    float side = 0.35f + 0.65f * E.v.scan, mo = E.v.morph, lv[7], pw[7];
    int c, i, k, N = E.eco ? 5 : 7, k0 = E.eco ? 1 : 0;
    for (k = 0; k < 7; k++) {
        lv[k] = k == 3 ? 1.0f : side;
        E.ss_lfo[k] = ph_step(E.ss_lfo[k], (0.11f + 0.07f * (float)k) * (float)n / FK_SR);
        pw[k] = 0.5f + 0.38f * fk_sin(E.ss_lfo[k]);
    }
    for (c = 0; c < 2; c++) {
        float *o = c ? gr : gl, d[7], p[7], ds = osc_inc(c, 7, -12.0f), ps = E.ph[c][7];
        for (k = k0; k < k0 + N; k++) {
            float st = DT[k] * (sp + 0.04f) + E.r_off[k] * 0.06f;
            if (c && E.v.span > 0.0f)
                st += DT[6 - k] * E.v.span * 0.15f;
            d[k] = osc_inc(c, k, st);
            p[k] = E.ph[c][k];
        }
        for (i = 0; i < n; i++) {
            float s = 0.0f;
            for (k = k0; k < k0 + N; k++) {
                float ph = p[k], saw = 2.0f * ph - 1.0f - fk_blep(ph, d[k]), y = saw;
                if (mo > 0.01f) {
                    float p2 = ph + 1.0f - pw[k], pl;
                    p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
                    pl = (ph < pw[k] ? 1.0f : -1.0f) + fk_blep(ph, d[k]) - fk_blep(p2, d[k]);
                    y = fk_lerp(saw, pl * 0.8f, mo);
                }
                p[k] = ph_step(ph, d[k]);
                s += y * lv[k] * (c ? (k & 1 ? 1.15f : 0.85f) : (k & 1 ? 0.85f : 1.15f));
            }
            s *= 0.32f;
            if (fold > 0.0f)
                s = fk_fold(s * (1.0f + fold)) * 0.8f;
            if (sub > 0.0f) {
                s += fk_sat(fk_sin(ps) * (1.0f + 3.0f * sub)) * sub * 0.35f;
                ps = ph_step(ps, ds);
            }
            o[i] = s;
        }
        for (k = k0; k < k0 + N; k++)
            E.ph[c][k] = p[k];
        E.ph[c][7] = ps;
    }
    if (E.v.fuse < 0.0f)
        E.cfg.chorus = -E.v.fuse;
    else
        E.cfg.sat = E.v.fuse;
}
