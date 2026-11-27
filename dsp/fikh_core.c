/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: der Klangkern als eine Übersetzungseinheit (auf dem FM-1 mit -O2, fm1.toml "o2"). Hier: der Zustand,
 * die Noten, der Wurf von SEED, die Drift, und der Block. Die Teile sind eingebunden:
 *   fikh_params.c  Parameter, Algorithmen, Anzeige     fikh_tables.c  Wavetables
 *   fikh_gen.c     die neun Generatoren                fikh_proc.c    Filter, Effekte, der Prozessor */
#include "fikh_core.h"
#include "fikh_dsp.h"

float *fk_sin_tab;

/* (der Kern hat keine C-Bibliothek: Nullen und Kopieren selbst) */
static void fk_zero(void *d, uint32_t n)
{
    uint8_t *b = (uint8_t *)d;
    while (n--)
        *b++ = 0;
}
static void fk_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *b = (uint8_t *)d;
    const uint8_t *a = (const uint8_t *)s;
    while (n--)
        *b++ = *a++;
}

typedef struct {
    float chorus, sat, res, bpm, psine, dmod, diff, crush, phaser;
} fk_cfg;

typedef struct {                         /* die geglätteten Werte: bipolar -1..1, sonst 0..1 */
    float warp, span, morph, fuse, mix, feed, form, time, field, cell, atk, rel, glide, drift, width, level, scan;
    float vmix, vdecay, vx, vfilt, vdepth, vrate, vstretch;
} fk_vals;

static struct {
    fk_mem m;
    int algo, algo_next;
    float xf, xf_dir;                    /* die Blende beim Wechsel */
    int xf_out;                          /* der Prozessor wechselt auch: den ganzen Ausgang mit ausblenden */
    uint32_t *clr_p[8], clr_w[8];        /* still leeren, in Stücken je Block (auf einmal käme der Block zu spät) */
    int clr_n;
    int ramp_ok;                         /* die Rampen unten haben ihren Startwert */
    float lg_p, wd_p, dry_p, wet_p, fb_p;   /* Pegel, Breite, MIX, FEED: je Sample geglättet, nicht je Block */
    uint8_t raw[FK_NP];
    volatile uint8_t fresh;
    float tgt[FK_NP];                    /* die Ziele, normiert */
    fk_vals v;                           /* geglättet, mit DRIFT */
    float sm[FK_NP];                     /* geglättet, ohne DRIFT */
    int seed;                            /* der Wurf, der gerade gilt (-1: neu würfeln) */
    float r_off[8], r_off2[8], r_wave[8], r_wave2[8], r_vol[8], r_pan[8];
    int r_set;
    float tap_t[2][4], tap[2][4];
    /* die Noten */
    int stack[16], nstack;
    int note, gate, hold;
    float vel, pitch, pitch_t, bend, f0, env;
    /* die Oszillatoren */
    float ph[2][8], fb[2][8], semi[4], nz[2];
    float ph2[2][8], fb2[2][8], sa[8], sb[8];   /* Cluster-Scanning: die zweite Unterstimme, die Versätze (gleitend) */
    float cs_f[8][8], cs_w[8][8], cs_a[8][8];   /* je Stimme acht Punkte: Frequenz-Versatz, Welle, Lautstärke (SEED) */
    int cs_i0;
    float fm_ph[2], fm_v[2];             /* FORM: das Schweben der Abgriffe */
    float mtr[2];                        /* die Pegelanzeige (VOL), links und rechts */
    int clip;
    float dr[2][8], drt[2][8];           /* die eigene Drift jedes Oszillators, je Kanal (Cent) */
    float mac_ph, mac_a, mac_b, mac_v[2];   /* DRIFT: die langsame Bewegung von MORPH und SCAN */
    uint32_t noise;
    fk_cfg cfg;
    /* Filter, Effekte */
    fk_svf flt[2], flh[2], bpf[2];      /* FIELD: Tiefpass und Hochpass hintereinander, nie umgeschaltet */
    fk_svf_c fc, fch, bpc;
    int field_hp;
    uint32_t chw;
    float ch_lfo, ph_lfo, phz[2][4], sine_ph;
    uint32_t cbw;
    float cbz[4];
    /* der Prozessor */
    uint32_t dw;
    float dsm, dz[2], hz[2], gp;
    float hil[2][2][16], hd[2], rc, rs, shift_hz;
    /* der Ausgang */
    float dcx[2], dcy[2], lvl, wlvl, duck_g;
    int duck, eco, freeze;
    float frz;                           /* FREEZE, weich ein- und ausgeblendet (0..1) */
    int rv_trig;                         /* eine neue Note (RISE) */
    /* BANK 2 */
    fk_svf nst[2][4];                    /* NOTCH STACK */
    float rad_ph[6], rad_hp[6], rad_g[6], rad_t, rad_vt, rad_qsb, rad_pop, rad_lp[2];   /* RADIO */
    fk_svf rad_f[2], rad_n;
    fk_svf_c rad_fc[2], rad_nc;
    float wx_gust, wx_g0, wx_g1, wx_gain, wx_t[2], wx_rc[4], wx_rx[4], wx_rpan[4], wx_r1[4], wx_r2[4], wx_ip[2], wx_is[2];
    fk_svf wx_f[2];                      /* WEATHER */
    int arch_trig;
    float arch_env, arch_lp[2], arch_y1[2][6], arch_y2[2][6];   /* ARCHITECT */
    float ss_lfo[7];                     /* SUPERSAW */
    float dsp_z[2][12], dsp_a[12];       /* der DISPERSOR */
} E;
static const float *X_duck, *X_l, *X_r;   /* GARGOYLE: fk_ext */
static float X_send;

static float fk_time_ms(float u);
static float fk_atk_ms(float u);
#include "fikh_params.c"
#include "fikh_tables.c"
#include "fikh_gen.c"
#include "fikh_cluster.c"
#include "fikh_gen2.c"
#include "fikh_proc.c"
#include "fikh_verb.c"

/* ------------------------------------------------------------------ SEED --- */
static void reseed(int s)
{
    uint32_t r = fk_hash((uint32_t)s * 2654435761u + (uint32_t)E.algo * 977u + 12345u), k;
    for (k = 0; k < 8u; k++) {
        E.r_off[k] = fk_rand11(&r);
        E.r_off2[k] = fk_rand11(&r);
        E.r_wave[k] = fk_rand01(&r);
        E.r_wave2[k] = fk_rand01(&r);
        E.r_vol[k] = fk_rand01(&r);
        E.r_pan[k] = fk_rand11(&r);
    }
    E.r_off[0] *= 0.25f;                 /* eine Stimme bleibt nah an BASIS: der Grundton trägt */
    E.r_set = (int)(fk_rng(&r) % 3u);
    for (k = 0; k < 2u; k++) {
        E.tap_t[k][0] = 1.0f;
        E.tap_t[k][1] = 0.30f + 0.55f * fk_rand01(&r);
        E.tap_t[k][2] = 0.14f + 0.40f * fk_rand01(&r);
        E.tap_t[k][3] = 0.55f + 0.42f * fk_rand01(&r);
    }
    for (k = 0; k < 12u; k++)
        E.dsp_a[k] = fk_rand11(&r);
    {                                    /* das Cluster-Scanning: acht Punkte je Stimme */
        uint32_t v, q;
        for (v = 0; v < 8u; v++)
            for (q = 0; q < 8u; q++) {
                E.cs_f[v][q] = fk_rand11(&r);
                E.cs_w[v][q] = fk_rand01(&r);
                E.cs_a[v][q] = 0.45f + 0.55f * fk_rand01(&r);
            }
        E.cs_f[0][0] *= 0.2f;            /* am Anfang der Reihe trägt eine Stimme nah am Grundton */
    }
    E.seed = s;
}

/* ------------------------------------------------------------------ Noten --- */
void fk_note_on(int note, int vel)
{
    int i, j = 0;
    for (i = 0; i < E.nstack; i++)       /* dieselbe Note nicht doppelt */
        if (E.stack[i] != note)
            E.stack[j++] = E.stack[i];
    E.nstack = j;
    if (E.nstack >= 16) {
        for (i = 1; i < 16; i++)
            E.stack[i - 1] = E.stack[i];
        E.nstack = 15;
    }
    E.stack[E.nstack++] = note;
    E.rv_trig = 1;
    E.arch_trig = 1;
    if (E.note < 0)
        E.pitch = (float)note;           /* die erste Note: kein Gleiten von irgendwo */
    E.note = note;
    E.pitch_t = (float)note;
    E.vel = 0.55f + 0.45f * (float)vel / 127.0f;
    E.gate = 1;
}

void fk_note_off(int note)
{
    int i, j = 0;
    for (i = 0; i < E.nstack; i++)
        if (E.stack[i] != note)
            E.stack[j++] = E.stack[i];
    E.nstack = j;
    if (E.nstack > 0) {                  /* zurück zur Taste davor, die noch gehalten wird */
        E.note = E.stack[E.nstack - 1];
        E.pitch_t = (float)E.note;
    } else {
        E.gate = 0;
    }
}

void fk_hold(int on) { E.hold = on; }
void fk_all_off(void)
{
    E.nstack = 0;
    E.gate = 0;
}
void fk_panic(void)
{
    fk_all_off();
    E.hold = 0;
    E.env = 0.0f;
}
void fk_bend(float semis) { E.bend = semis; }
void fk_freeze(int on) { E.freeze = on; }
int fk_frozen(void) { return E.freeze; }
void fk_eco(int on) { E.eco = on; }
int fk_eco_on(void) { return E.eco; }
void fk_duck(int on) { E.duck = on; }
void fk_ext(const float *duck, const float *xl, const float *xr, float send)
{
    X_duck = duck;
    X_l = xl;
    X_r = xr;
    X_send = send;
}
int fk_ducked(void) { return E.duck && E.duck_g <= 0.0f; }

/* ------------------------------------------------------------- Parameter --- */
void fk_params(const uint8_t p[FK_NP])
{
    fk_copy(E.raw, p, FK_NP);
    E.fresh = 1;
}

void fk_algo(int a)
{
    if (a < 0 || a >= FK_NALGO || (a == E.algo && E.algo_next < 0))
        return;
    if (FK_ALGOS[a].proc != FK_ALGOS[E.algo].proc || (a == 8) != (E.algo == 8))
        E.xf_out = 1;
    E.algo_next = a;
    E.xf_dir = -1.0f;                    /* ausblenden; gewechselt wird, wenn es still ist */
}

static void targets(void)
{
    int i;
    for (i = 0; i < FK_NP; i++)
        E.tgt[i] = FK_PARAMS[i].bip ? fk_bip(E.raw[i]) : (float)E.raw[i] / 127.0f;
    if (E.seed != (int)E.raw[P_SEED])
        reseed(E.raw[P_SEED]);
}

void fk_init(const fk_mem *m)
{
    int i, c;
    fk_zero(&E, sizeof E);
    E.m = *m;
    fk_zero(E.m.dl[0], FK_DL_N * sizeof(int16_t));
    fk_zero(E.m.dl[1], FK_DL_N * sizeof(int16_t));
    fk_zero(E.m.ch[0], FK_CH_N * sizeof(float));
    fk_zero(E.m.ch[1], FK_CH_N * sizeof(float));
    fk_zero(E.m.ap, FK_AP_N * sizeof(float));
    fk_zero(E.m.rv, FK_RV_N * sizeof(int16_t));
    fk_zero(E.m.rva, FK_RVA_N * sizeof(float));
    fk_zero(E.m.sh, FK_SH_N * sizeof(int16_t));
    for (i = 0; i < 4; i++)
        fk_zero(E.m.comb[i], FK_CB_N * sizeof(float));
    fk_sin_tab = E.m.sn;
    for (i = 0; i <= (int)FK_SIN_N; i++)
        E.m.sn[i] = fm_sin_turns((float)i / (float)FK_SIN_N);
    wt_build(E.m.wt);
    proc_init();
    verb_init();
    E.algo_next = -1;
    E.note = -1;
    E.xf = 1.0f;
    E.duck_g = 1.0f;
    E.rc = 1.0f;
    E.noise = 0x2545F491u;
    E.seed = -1;
    E.cs_i0 = -1;
    for (c = 0; c < 2; c++)
        for (i = 0; i < 8; i++)
            E.ph2[c][i] = E.ph[c][i] = (float)((i * 37) % 64) / 64.0f;   /* links und rechts in Phase: breit nur durch die Drift */
    fk_copy(E.raw, FK_ALGOS[0].init, FK_NPA);
    fk_copy(E.raw + FK_NPA, FK_VERB_INIT, FK_NPV);
    targets();
    for (i = 0; i < FK_NP; i++)
        E.sm[i] = E.tgt[i];
    E.dsm = fk_time_ms(E.sm[P_TIME]) * (FK_SR / 1000.0f);
    fk_copy(E.tap, E.tap_t, sizeof E.tap);
    E.pitch = E.pitch_t = 48.0f;
}

/* ------------------------------------------------------------------ Block --- */
/* Zustände, die gegen null auslaufen, werden sonst denormal (winzig): das rechnet langsam. Einmal je Block auf 0 */
static void flush_states(void)
{
    int c, i;
    float *f;
    E.env = fm_flush(E.env);
    for (c = 0; c < 2; c++) {
        E.dz[c] = fm_flush(E.dz[c]);
        E.hz[c] = fm_flush(E.hz[c]);
        E.dcx[c] = fm_flush(E.dcx[c]);
        E.dcy[c] = fm_flush(E.dcy[c]);
        E.hd[c] = fm_flush(E.hd[c]);
        E.nz[c] = fm_flush(E.nz[c]);
        E.flt[c].ic1 = fm_flush(E.flt[c].ic1);
        E.flt[c].ic2 = fm_flush(E.flt[c].ic2);
        E.flh[c].ic1 = fm_flush(E.flh[c].ic1);
        E.flh[c].ic2 = fm_flush(E.flh[c].ic2);
        E.bpf[c].ic1 = fm_flush(E.bpf[c].ic1);
        E.bpf[c].ic2 = fm_flush(E.bpf[c].ic2);
        for (i = 0; i < 4; i++)
            E.phz[c][i] = fm_flush(E.phz[c][i]);
        for (i = 0; i < 8; i++) {
            E.fb[c][i] = fm_flush(E.fb[c][i]);
            E.fb2[c][i] = fm_flush(E.fb2[c][i]);
        }
    }
    f = &E.hil[0][0][0];
    for (i = 0; i < 2 * 2 * 16; i++)
        f[i] = fm_flush(f[i]);
    for (i = 0; i < 4; i++)
        E.cbz[i] = fm_flush(E.cbz[i]);
    for (c = 0; c < 2; c++) {
        for (i = 0; i < 4; i++) {
            E.nst[c][i].ic1 = fm_flush(E.nst[c][i].ic1);
            E.nst[c][i].ic2 = fm_flush(E.nst[c][i].ic2);
        }
        for (i = 0; i < 12; i++)
            E.dsp_z[c][i] = fm_flush(E.dsp_z[c][i]);
        E.rad_f[c].ic1 = fm_flush(E.rad_f[c].ic1);
        E.rad_f[c].ic2 = fm_flush(E.rad_f[c].ic2);
        E.wx_f[c].ic1 = fm_flush(E.wx_f[c].ic1);
        E.wx_f[c].ic2 = fm_flush(E.wx_f[c].ic2);
        E.wx_t[c] = fm_flush(E.wx_t[c]);
        E.arch_lp[c] = fm_flush(E.arch_lp[c]);
        E.rad_lp[c] = fm_flush(E.rad_lp[c]);
    }
    E.rad_n.ic1 = fm_flush(E.rad_n.ic1);
    E.rad_n.ic2 = fm_flush(E.rad_n.ic2);
}

static void block_controls(int n)
{
    float k = 1.0f - fm_expf(-(float)n / (0.035f * FK_SR)), dk, gl;
    int i, c;
    if (E.fresh && E.algo_next < 0) {    /* (während des Ausblendens bleibt alles, wie es klang) */
        E.fresh = 0;
        targets();
    }
    for (i = 0; i < FK_NP; i++)
        E.sm[i] = fm_flush(E.sm[i] + (E.tgt[i] - E.sm[i]) * k);
    /* DRIFT: jeder Oszillator wandert für sich (bis ±30 Cent), MORPH und SCAN atmen langsam mit */
    dk = E.sm[P_DRIFT];
    for (c = 0; c < 2; c++)
        for (i = 0; i < 8; i++) {
            if (fk_rand01(&E.noise) < (float)n / (0.45f * FK_SR))
                E.drt[c][i] = fk_rand11(&E.noise) * 30.0f;
            /* rechts: zum Teil die Drift von links (WIDTH: wie viel eigene), damit es breit, aber nicht hohl wird */
            E.dr[c][i] += ((c ? fk_lerp(E.drt[0][i], E.drt[1][i], 0.25f + 0.45f * E.sm[P_WIDTH]) : E.drt[0][i]) * dk
                           - E.dr[c][i]) * (float)n * (1.0f / (0.8f * FK_SR));
        }
    E.mac_ph += (0.05f + 0.25f * dk) * (float)n / FK_SR;
    if (E.mac_ph >= 1.0f) {
        E.mac_ph -= 1.0f;
        E.mac_a = E.mac_b;
        E.mac_b = fk_rand11(&E.noise);
    }
    {
        float t = 0.5f - 0.5f * fm_cosf(E.mac_ph * FK_PI), w = fk_lerp(E.mac_a, E.mac_b, t);
        E.mac_v[0] = w;
        E.mac_v[1] = fk_sin(E.mac_ph * 0.5f + w * 0.2f);
    }
    E.v.warp = E.sm[P_WARP];
    E.v.span = E.sm[P_SPAN];
    E.v.morph = fk_clamp(E.sm[P_MORPH] + E.mac_v[0] * 0.18f * dk, 0.0f, 1.0f);
    E.v.fuse = E.sm[P_FUSE];
    E.v.mix = E.sm[P_MIX];
    E.v.feed = E.sm[P_FEED];
    E.v.form = E.sm[P_FORM];
    E.v.time = E.sm[P_TIME];
    E.v.field = E.sm[P_FIELD];
    E.v.cell = E.sm[P_CELL];
    E.v.atk = E.sm[P_ATK];
    E.v.rel = E.sm[P_REL];
    E.v.glide = E.sm[P_GLIDE];
    E.v.drift = dk;
    E.v.width = E.sm[P_WIDTH];
    E.v.level = E.sm[P_LEVEL];
    E.v.vmix = E.sm[P_VMIX];
    E.v.vdecay = E.sm[P_VDECAY];
    E.v.vx = E.sm[P_VX];
    E.v.vfilt = E.sm[P_VFILT];
    E.v.vdepth = E.sm[P_VDEPTH];
    E.v.vrate = E.sm[P_VRATE];
    E.v.vstretch = E.sm[P_VSTRETCH];
    E.v.scan = fk_clamp(E.sm[P_SCAN] + E.mac_v[1] * 0.14f * dk, 0.0f, 1.0f);
    /* BASIS: die Tonhöhe gleitet (GLIDE) */
    gl = fk_glide_ms(E.v.glide);
    if (gl <= 0.0f)
        E.pitch = E.pitch_t;
    else
        E.pitch += (E.pitch_t - E.pitch) * (1.0f - fm_expf(-(float)n / (gl * 0.001f * FK_SR)));
    E.f0 = 440.0f * fm_exp2f((E.pitch + E.bend - 69.0f) * (1.0f / 12.0f));
    /* FIELD */
    E.field_hp = E.v.field > 0.0f;
    fk_zero(&E.cfg, sizeof E.cfg);
}

void fk_render(float *l, float *r, int n)
{
    static float gl[FK_BLOCK_MAX], gr[FK_BLOCK_MAX];
    int i, c;
    float lg, wd;
    if (n > FK_BLOCK_MAX)
        n = FK_BLOCK_MAX;
    if (E.algo_next >= 0 && E.xf <= 0.0f) {   /* still: jetzt wechseln, dann einblenden */
        E.algo = E.algo_next;
        E.algo_next = -1;
        E.seed = -1;
        targets();
        for (c = 0; c < 2; c++)
            for (i = 0; i < 8; i++)
                E.fb[c][i] = E.fb2[c][i] = 0.0f;
        E.cs_i0 = -1;
        /* der Generator ist still: seine Werte gleich ganz, kein Nachziehen durch die Cluster-Punkte */
        for (i = 0; i < FK_NPA; i++) {
            int gen = i == P_WARP || i == P_SPAN || i == P_MORPH || i == P_FUSE || i == P_FIELD || i == P_ATK
                      || i == P_REL || i == P_GLIDE || i == P_DRIFT || i == P_SCAN;
            if (gen || (E.xf_out && i != P_WIDTH && i != P_LEVEL))   /* blendet der Prozessor mit: auch seine */
                E.sm[i] = E.tgt[i];                                    /* (WIDTH, LEVEL nie: der Hall klingt weiter) */
        }
        /* alte Reste in den Puffern hätten eine Naht, die beim Wiedereinsteigen knackt: sie werden geleert, solange es
         * still ist, ein Stück je Block (alles auf einmal kostete den Block zu viel Zeit: er käme zu spät, das knackt) */
        E.clr_n = 0;
        if (E.algo == 7) {               /* COMB SWARM: seine Kammfilter */
            for (i = 0; i < 4; i++) {
                E.clr_p[E.clr_n] = (uint32_t *)(void *)E.m.comb[i];
                E.clr_w[E.clr_n++] = FK_CB_N;
            }
        }
        if (E.xf_out) {                  /* der Prozessor wechselt: er beginnt leer (der Hall klingt weiter) */
            E.clr_p[E.clr_n] = (uint32_t *)(void *)E.m.dl[0];
            E.clr_w[E.clr_n++] = FK_DL_N / 2u;
            E.clr_p[E.clr_n] = (uint32_t *)(void *)E.m.dl[1];
            E.clr_w[E.clr_n++] = FK_DL_N / 2u;
            E.clr_p[E.clr_n] = (uint32_t *)(void *)E.m.ap;
            E.clr_w[E.clr_n++] = FK_AP_N;
            fk_zero(E.dsp_z, sizeof E.dsp_z);
            fk_zero(E.hil, sizeof E.hil);
            fk_zero(E.hd, sizeof E.hd);
            fk_zero(E.dz, sizeof E.dz);
            fk_zero(E.hz, sizeof E.hz);
            fk_copy(E.tap, E.tap_t, sizeof E.tap);
        }
        if (E.xf_out)
            E.dsm = fk_clamp(fk_time_ms(E.sm[P_TIME]) * (FK_SR / 1000.0f), 8.0f, (float)FK_DL_N - 9000.0f);
        E.xf_dir = E.clr_n ? 0.0f : 1.0f;
    }
    if (E.clr_n) {                       /* ein Stück leeren (32 Wörter je Sample: 32 KiB je Block zu 256) */
        uint32_t w = (uint32_t)n * 32u;
        while (w && E.clr_n) {
            int k = E.clr_n - 1;
            uint32_t m = E.clr_w[k] < w ? E.clr_w[k] : w, j;
            uint32_t *p = E.clr_p[k];
            for (j = 0; j < m; j++)
                p[j] = 0u;
            E.clr_p[k] = p + m;
            E.clr_w[k] -= m;
            w -= m;
            if (!E.clr_w[k])
                E.clr_n--;
        }
        if (!E.clr_n)
            E.xf_dir = 1.0f;             /* leer: einblenden */
    }
    block_controls(n);
    switch (E.algo) {
    case 0: gen_metal(gl, gr, n); break;
    case 1: gen_shaper(gl, gr, n); break;
    case 2: gen_sines(gl, gr, n); break;
    case 3: gen_quad(gl, gr, n, WQ_QUAD); break;
    case 4: gen_diatonic(gl, gr, n); break;
    case 5: gen_chords(gl, gr, n); break;
    case 6: gen_partials(gl, gr, n); break;
    case 7: gen_combs(gl, gr, n); break;
    case 8: gen_grain(gl, gr, n); break;
    case 9: gen_zero(gl, gr, n); break;
    case 10: gen_disp(gl, gr, n); break;
    case 11: gen_notch(gl, gr, n); break;
    case 12: gen_swarm(gl, gr, n); break;
    case 13: gen_radio(gl, gr, n); break;
    case 14: gen_weather(gl, gr, n); break;
    case 15: gen_arch(gl, gr, n); break;
    case 16: gen_fold(gl, gr, n); break;
    case 17: gen_ssaw(gl, gr, n); break;
    case 18: gen_quad(gl, gr, n, WQ_PM); break;
    case 19: gen_quad(gl, gr, n, WQ_FOLD); break;
    case 20: gen_freecl(gl, gr, n, 0); break;
    default: gen_freecl(gl, gr, n, 1); break;
    }
    /* das Filter: FIELD und die Resonanz, die FUSE dazugibt */
    {   /* links schließt der Tiefpass, rechts öffnet der Hochpass; die Resonanz wandert weich mit hinüber */
        float f = E.v.field, wl = fk_clamp(1.0f - f * 8.0f, 0.0f, 1.0f), wh = fk_clamp(f * 8.0f, 0.0f, 1.0f);
        float lr = 0.08f + (f < 0.0f ? -f * 0.30f : 0.0f) + E.cfg.res * 0.9f * wl;
        float hr = 0.08f * wh + (f > 0.0f ? f * 0.30f : 0.0f) + E.cfg.res * 0.9f * wh;
        fk_svf_set(&E.fc, f < 0.0f ? 20000.0f * fm_exp2f(f * 9.5f) : 20000.0f, lr);
        fk_svf_set(&E.fch, f > 0.0f ? 20.0f * fm_exp2f(f * 9.0f) : 20.0f, hr);
    }
    if (E.cfg.bpm > 0.0f)
        fk_svf_set(&E.bpc, E.f0 * 3.0f * fm_exp2f(E.v.morph * 2.0f), 0.75f);
    E.shift_hz = E.algo == 8 ? 0.0f : fk_cell_hz(E.v.cell);
    proc_block(gl, gr, l, r, n);
    if (X_duck)                          /* GARGOYLE: die ATEM-Pumpe, vor dem Hall (sein Schweif bleibt weich) */
        for (i = 0; i < n; i++) {
            l[i] *= X_duck[i];
            r[i] *= X_duck[i];
        }
    verb_block(l, r, n);
    X_duck = 0;
    X_l = X_r = 0;
    /* der Ausgang: Gleichanteil weg, Breite, Pegel, weich begrenzt */
    lg = fm_exp2f(fk_level_db(E.v.level) * 0.1660964f) * 0.9f;
    wd = E.v.width * 1.1f;
    if (!E.ramp_ok) {
        E.lg_p = lg;
        E.wd_p = wd;
    }
    {   /* Pegel und Breite gleiten je Sample (je Block stufig: auf einem langen Hall hört man jede Stufe) */
        float dlg = (lg - E.lg_p) / (float)n, dwd = (wd - E.wd_p) / (float)n;
        lg = E.lg_p;
        wd = E.wd_p;
        E.lg_p += dlg * (float)n;
        E.wd_p += dwd * (float)n;
        for (i = 0; i < n; i++) {
            float a = l[i], b = r[i], m, s;
            lg += dlg;
            wd += dwd;
            E.dcy[0] = a - E.dcx[0] + 0.9975f * E.dcy[0];
            E.dcx[0] = a;
            E.dcy[1] = b - E.dcx[1] + 0.9975f * E.dcy[1];
            E.dcx[1] = b;
            m = (E.dcy[0] + E.dcy[1]) * 0.5f;
            s = (E.dcy[0] - E.dcy[1]) * 0.5f * wd;
            a = fk_sat((m + s) * lg);
            b = fk_sat((m - s) * lg);
            l[i] = a;
            r[i] = b;
            if (fk_abs(a) > E.mtr[0])
                E.mtr[0] = fk_abs(a);
            if (fk_abs(b) > E.mtr[1])
                E.mtr[1] = fk_abs(b);
            if (fk_abs(a) > 0.97f || fk_abs(b) > 0.97f)
                E.clip = 1;
            a = fk_abs(a) > fk_abs(b) ? fk_abs(a) : fk_abs(b);
            E.lvl += (a - E.lvl) * (a > E.lvl ? 0.01f : 0.0004f);
        }
    }
    E.ramp_ok = 1;
    flush_states();
}

/* ------------------------------------------------------------- Anzeige --- */
float fk_meter(int c)                    /* der Spitzenpegel seit dem letzten Aufruf (0..1), dann fällt er */
{
    float v = E.mtr[c & 1];
    E.mtr[c & 1] = v * 0.80f;
    return v;
}
int fk_clipped(void)
{
    int k = E.clip;
    E.clip = 0;
    return k;
}
int fk_note(void) { return (E.gate || E.hold) ? E.note : -1; }
float fk_pitch(void) { return E.pitch; }
int fk_gate(void) { return E.gate; }
float fk_env(void) { return E.env; }
float fk_level(void) { return E.lvl; }
float fk_wet_level(void) { return E.wlvl; }
int fk_cur_algo(void) { return E.algo_next >= 0 ? E.algo_next : E.algo; }
uint32_t fk_seed_hash(void) { return (uint32_t)E.seed; }

/* GARGOYLE: der GROOVE (dieselbe Übersetzungseinheit: er benutzt die Bausteine des Kerns) */
#include "garg_groove.c"
