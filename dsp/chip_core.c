/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE – der Klangkern (siehe chip_core.h). Eine Übersetzungseinheit: der GROOVE (garg_groove.c) ist am Ende
 * eingebunden. Alle Klänge hier synthetisiert, nach den Prinzipien der alten Chips (keine Kopie eines Chips). */
#include "chip_core.h"
#include "fastmath.h"
#include "garg_groove.h"

#define TAU 6.2831853f
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float absf(float x) { return x < 0.0f ? -x : x; }
static inline float wrap1(float p) { return p - (float)(int)p; }
static inline uint32_t rng_u(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float rng_f(uint32_t *s) { return (float)(rng_u(s) >> 8) * (1.0f / 16777216.0f); }
static inline float softc(float x) { return x / (1.0f + absf(x) * 0.6f) * 1.3f; }
static inline float fk_sin(float ph)          /* sin(2 pi ph), eine Näherung (Parabel, verfeinert): reicht für FM */
{
    float x = wrap1(ph + 0.5f) * 2.0f - 1.0f, y = 4.0f * x * (1.0f - absf(x));
    return -(y + 0.225f * y * (absf(y) - 1.0f));
}
static void fk_zero(void *d, uint32_t n)       /* (kein libc in dieser Übersetzungseinheit) */
{
    uint8_t *p = (uint8_t *)d;
    while (n--)
        *p++ = 0;
}

/* ---- die Bausteine der alten Chips ---- */
static inline float w_pulse(float ph, float duty) { return ph < duty ? 1.0f : -1.0f; }
static inline float w_tri4(float ph)          /* das Dreieck des NES: 32 Stufen, 4 Bit */
{
    int i = (int)(ph * 32.0f) & 31, v = i < 16 ? 15 - i : i - 16;
    return (float)v * (2.0f / 15.0f) - 1.0f;
}
static inline float w_saw4(float ph) { return (float)((int)(ph * 16.0f) & 15) * (2.0f / 15.0f) - 1.0f; }
/* ein Rausch-Generator wie im NES: ein 15-Bit-Schieberegister, getaktet mit hz; short: der kurze Modus (93 Schritte,
 * fast ein Ton) */
typedef struct { uint16_t r; float acc; float out; } lfsr_t;
static inline float w_noise(lfsr_t *n, float hz, int shortm)
{
    n->acc += hz * (1.0f / FK_SR);
    while (n->acc >= 1.0f) {
        uint16_t b = (uint16_t)((n->r ^ (n->r >> (shortm ? 6 : 1))) & 1u);
        n->r = (uint16_t)((n->r >> 1) | (b << 14));
        n->acc -= 1.0f;
        n->out = (n->r & 1u) ? 1.0f : -1.0f;
    }
    return n->out;
}
/* POKEY: Polynom-Zähler (4, 5, 9, 17 Bit), mit einem Teiler abgetastet: "unreine", knurrende Töne */
typedef struct { uint32_t p4, p5, p9, p17; float acc; float out; int t; } poky_t;
static inline float w_poky(poky_t *k, float hz, int mode)
{
    k->acc += hz * 2.0f * (1.0f / FK_SR);
    while (k->acc >= 1.0f) {
        k->acc -= 1.0f;
        k->p4 = ((k->p4 << 1) | (((k->p4 >> 3) ^ (k->p4 >> 2)) & 1u)) & 15u;
        k->p5 = ((k->p5 << 1) | (((k->p5 >> 4) ^ (k->p5 >> 2)) & 1u)) & 31u;
        k->p9 = ((k->p9 << 1) | (((k->p9 >> 8) ^ (k->p9 >> 4)) & 1u)) & 511u;
        k->p17 = ((k->p17 << 1) | (((k->p17 >> 16) ^ (k->p17 >> 11)) & 1u)) & 131071u;
        switch (mode) {                       /* die Verzerrungen */
        case 0: k->t ^= 1; break;                                     /* rein */
        case 1: if (k->p5 & 1u) k->t ^= 1; break;                     /* 5 Bit, dann Rechteck */
        case 2: k->t = (int)(k->p4 & 1u); break;                      /* 4 Bit: Brummen */
        case 3: if (k->p5 & 1u) k->t = (int)(k->p4 & 1u); break;      /* 5 + 4 */
        case 4: k->t = (int)(k->p9 & 1u); break;                      /* 9 Bit */
        default: k->t = (int)(k->p17 & 1u); break;                    /* 17 Bit: Rauschen */
        }
        k->out = k->t ? 1.0f : -1.0f;
    }
    return k->out;
}
/* eine 4-Bit-Wellentabelle wie im Game Boy: 32 Werte 0..15 */
static inline float w_wt(const uint8_t *t, float ph) { return (float)t[(int)(ph * 32.0f) & 31] * (2.0f / 15.0f) - 1.0f; }

/* ---- der Zustand ---- */
#define NV 4
/* der Zustand eines laufenden Algorithmus: zweimal da, damit beim MORPH zwei gleichzeitig klingen */
typedef struct {
    float ph[NV], ph2[NV], duty_ph;
    lfsr_t nz[NV];
    poky_t pk[NV];
    float fmfb[NV];
    float arp_ph;                             /* die Arpeggios und Muster (SPEED) */
    int arp_i, step_note;
    float sh_l, sh_r, sh_acc;
    float bp[2][2];                           /* das eigene Filter (WIND OF ABYSS) */
} vst_t;
static vst_t *V;                              /* der Algorithmus, der gerade rechnet */

static struct {
    fk_mem m;
    int algo, algo_next;
    float xf, xf_dir;                         /* die Blende beim Wechsel */
    uint8_t raw[FK_NP];
    float sm[FK_NP];                          /* geglättet, 0..1 */
    float v_curse, v_horde, v_rune, v_alch;   /* bipolar: -1..1 / 0..1 */
    /* die Noten */
    int note, gate, hold, held[16], nheld;
    float vel, pitch, target, bend, glide;
    float env, atk, rel;
    /* die Stimmen */
    vst_t vs[2];                              /* die Stimmen: [0] der Algorithmus, [1] der nächste (MORPH) */
    float drift[NV], dv[NV];
    float wv[8], wt_[8], wcnt[8];             /* WANDER: langsame Zufallswege (Stimmung je Stimme, RUNE, TORCH, Pan, Atem) */
    uint32_t dw[8];                           /* die Diffusoren: Schreibstellen */
    float wow;
    uint8_t wt[4][32];                        /* die Tabellen (FATE würfelt sie) */
    uint8_t mraw[4];                          /* MORPH: CURSE, HORDE, RUNE, ALCHEMY des nächsten Algorithmus */
    float om_ph, om_v, om_sh, om_out;         /* OMEN: Phase, Wert, CHANCE gehalten, Ausgang (mal POWER) */
    float om_vol0;                            /* die Lautstärke am Ende des letzten Blocks (gleitend) */
    float om_f0, olp[2][2];                   /* der Tiefpass von OMEN */
    float om_pitch, om_torch, om_rune, om_curse, om_alch, om_vol, om_aura, om_lofi;
    int om_step, om_bar;
    float msm[4];
    int last_fate, last_quest;
    float vib_ph;
    uint32_t rw;                              /* AURA: der Resonator */
    float rlp[FK_NRES], rlen[FK_NRES];
    uint32_t rng;
    /* das Filter, das Echo */
    float f_lp[2][2], f_hp[2][2];
    uint32_t ew;
    float ed, ed_t, elp[2];
    int freeze, duck, eco;
    float duck_g;
    float lvl, mtr[2];
    int clip;
    float dcx[2], dcy[2];
} E;
static const float *X_duck, *X_l, *X_r;
static float X_send;

#include "chip_params.c"

static float mtof(float m) { return 440.0f * fm_exp2f((m - 69.0f) * (1.0f / 12.0f)); }

static void reseed(int s)
{
    int t, i;
    uint32_t r = 0x9E3779B9u ^ (uint32_t)(s * 2654435761u) ^ (uint32_t)E.algo * 40503u;
    for (t = 0; t < 4; t++) {                 /* die Tabellen: eine weiche Grundform plus gewürfelte Obertöne */
        float h2 = rng_f(&r), h3 = rng_f(&r), h5 = rng_f(&r) * 0.7f, ph = rng_f(&r);
        for (i = 0; i < 32; i++) {
            float x = (float)i / 32.0f;
            float v = fk_sin(x) + h2 * fk_sin(2.0f * x + ph) + h3 * 0.6f * fk_sin(3.0f * x) + h5 * fk_sin(5.0f * x + ph);
            v = clampf(v / (1.0f + h2 + h3 * 0.6f + h5), -1.0f, 1.0f);
            if (t == 3)
                v = v > 0.0f ? 1.0f : -1.0f;
            E.wt[t][i] = (uint8_t)clampf(7.5f + v * 7.5f + 0.5f, 0.0f, 15.0f);
        }
    }
    for (i = 0; i < NV; i++) {
        E.dv[i] = (rng_f(&r) - 0.5f) * 2.0f;
        int v;
        uint16_t nzr = (uint16_t)(1u + (rng_u(&r) & 0x7FFEu));
        for (v = 0; v < 2; v++) {
            E.vs[v].pk[i].p4 = 5u + (uint32_t)i;
            E.vs[v].pk[i].p5 = 9u + (uint32_t)i;
            E.vs[v].pk[i].p9 = 77u + (uint32_t)i * 3u;
            E.vs[v].pk[i].p17 = 1234u + (uint32_t)i * 777u;
            E.vs[v].nz[i].r = (uint16_t)(nzr ^ (uint16_t)(v * 0x2A5u)) | 1u;
        }
    }
    E.rng = r | 1u;
}

/* ---- die Schnittstelle ---- */
void fk_init(const fk_mem *m)
{
    fk_zero(&E, sizeof E);
    E.m = *m;
    fk_zero(E.m.ec[0], FK_EC_N * 2u);
    fk_zero(E.m.ec[1], FK_EC_N * 2u);
    fk_zero(E.m.df, FK_DF_N * 4u);
    {
        int j;
        for (j = 0; j < FK_NRES; j++)
            fk_zero(E.m.res[j], FK_RS_N * sizeof(float));
    }
    E.algo_next = -1;
    E.xf = 1.0f;
    E.xf_dir = 1.0f;
    E.note = 48;
    E.pitch = E.target = 48.0f;
    E.vel = 1.0f;
    E.rng = 0x1234567u;
    E.last_fate = -1;
    E.duck_g = 1.0f;
    E.om_vol = E.om_vol0 = 1.0f;
    reseed(0);
}
void fk_algo(int a)
{
    a = a < 0 ? 0 : a >= FK_NALGO ? FK_NALGO - 1 : a;
    if (a == E.algo && E.algo_next < 0)
        return;
    E.algo_next = a;
    E.xf_dir = -1.0f;
}
void fk_morph(const uint8_t p4[4])
{
    int i;
    for (i = 0; i < 4; i++)
        E.mraw[i] = p4[i];
}
void fk_sync(float bpm) { omen_sync_bpm = bpm; }
float fk_omen(void) { return E.om_out; }
void fk_params(const uint8_t p[FK_NP])
{
    int i;
    for (i = 0; i < FK_NP; i++)
        E.raw[i] = p[i];
}

static void gate_on(void)
{
    if (!E.gate && E.env < 0.001f)
        E.vs[0].arp_i = E.vs[1].arp_i = 0;
    E.gate = 1;
}
void fk_note_on(int note, int vel)
{
    int i;
    for (i = 0; i < E.nheld; i++)
        if (E.held[i] == note)
            break;
    if (i == E.nheld && E.nheld < 16)
        E.held[E.nheld++] = note;
    E.note = note;
    E.target = (float)note;
    E.vel = 0.4f + 0.6f * (float)vel / 127.0f;
    gate_on();
}
void fk_note_off(int note)
{
    int i, j;
    for (i = 0; i < E.nheld; i++)
        if (E.held[i] == note) {
            for (j = i; j + 1 < E.nheld; j++)
                E.held[j] = E.held[j + 1];
            E.nheld--;
            break;
        }
    if (E.nheld) {
        E.note = E.held[E.nheld - 1];
        E.target = (float)E.note;
    } else {
        E.gate = 0;
    }
}
void fk_hold(int on) { E.hold = on; }
void fk_all_off(void)
{
    E.nheld = 0;
    E.gate = 0;
}
void fk_panic(void)
{
    fk_all_off();
    E.env = 0.0f;
    fk_zero(E.m.ec[0], FK_EC_N * 2u);
    fk_zero(E.m.ec[1], FK_EC_N * 2u);
    fk_zero(E.m.df, FK_DF_N * 4u);
}
void fk_bend(float semis) { E.bend = semis; }
void fk_freeze(int on) { E.freeze = on; }
int fk_frozen(void) { return E.freeze; }
void fk_eco(int on) { E.eco = on; }
int fk_eco_on(void) { return E.eco; }
void fk_duck(int on) { E.duck = on; }
int fk_ducked(void) { return E.duck && E.duck_g <= 0.0f; }
void fk_ext(const float *duck, const float *xl, const float *xr, float send)
{
    X_duck = duck;
    X_l = xl;
    X_r = xr;
    X_send = send;
}

/* ---- die Algorithmen: je ein Block, Stereo, um 0.5 herum ----
 * f: die Grundfrequenz (mit Vibrato), c: CURSE (-1..1), h: HORDE (-1..1), r: RUNE (0..1), a: ALCHEMY (0..1),
 * q: QUEST (0..1), sp: SPEED (Hz, für Arpeggios und Muster) */
typedef struct { float f, c, h, r, a, q, sp, wand; } ctl;

/* die Stimmen-Frequenzen nach HORDE: links Oktaven darunter, rechts Verstimmung (mit FATE gewürfelt) */
static float horde_ratio(int i, float h)
{
    if (!i)
        return 1.0f;
    if (h < 0.0f) {
        static const float OCT[NV] = {1.0f, 0.5f, 2.0f, 0.25f};
        return 1.0f + (OCT[i] - 1.0f) * clampf(-h * 1.4f, 0.0f, 1.0f);
    }
    return fm_exp2f(E.dv[i] * h * h * 0.06f + (float)(i & 1) * h * 0.004f);
}
static float drift_of(int i) { return fm_exp2f(E.drift[i & 3] * (1.0f / 1200.0f)); }
static inline void adv(float *ph, float hz) { *ph = wrap1(*ph + hz * (1.0f / FK_SR)); }

/* 1 TORCHLIGHT: zwei Pulswellen (NES), die Pulsbreite wandert; CURSE links Ring, rechts Sync */
static void a_torch(const ctl *k, float *l, float *r, int n)
{
    int i;
    float d0 = 0.06f + 0.44f * k->r, sw = 0.05f + k->a * 2.5f;
    for (i = 0; i < n; i++) {
        float f1 = k->f * drift_of(0), f2 = k->f * horde_ratio(1, k->h) * drift_of(1), a, b, d2;
        V->duty_ph = wrap1(V->duty_ph + sw / FK_SR);
        d2 = 0.06f + 0.44f * (0.5f + 0.5f * fk_sin(V->duty_ph)) * (0.3f + 0.7f * k->r);
        adv(&V->ph[0], f1);
        if (k->c > 0.0f) {                    /* Sync: die zweite startet mit der ersten neu, höher gestimmt */
            float ratio = 1.0f + k->c * 3.0f;
            V->ph2[1] = wrap1(V->ph2[1] + f1 * ratio / FK_SR);
            if (V->ph[0] < f1 / FK_SR)
                V->ph2[1] = 0.0f;
            b = w_pulse(V->ph2[1], d2);
        } else {
            adv(&V->ph[1], f2);
            b = w_pulse(V->ph[1], d2);
        }
        a = w_pulse(V->ph[0], d0);
        if (k->c < 0.0f)
            b = b + (a * b - b) * (-k->c);
        l[i] = (a * 0.6f + b * 0.4f) * 0.45f;
        r[i] = (a * 0.4f + b * 0.6f) * 0.45f;
    }
}

/* 2 CRYPT CHOIR: der Game Boy, wie er war: zwei Pulskanäle (Pulsbreite 12,5 / 25 / 50 / 75 %, RUNE), der
 * Wellenkanal mit 32 Werten zu 4 Bit (QUEST: die Welle) und seiner Lautstärke in drei Stufen (ALCHEMY: Puls / Welle),
 * die Tonhöhen als 11-Bit-Teiler gerastert (131072 / (2048 - x)); HORDE links: der zweite Puls eine Quinte oder Oktave
 * tiefer, rechts: um ganze Teiler-Schritte verstimmt (der Chorus der Game-Boy-Musik); CURSE: die Sweep-Einheit des
 * ersten Kanals (links nach unten, rechts nach oben, sie springt immer wieder zurück). Harte Panoramen wie am
 * Kopfhörer: Puls 1 links, Puls 2 rechts, die Welle in der Mitte; am Ende der Hochpass des Game Boy. */
static const uint8_t GB_WAVE[7][32] = {
    {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
    {8, 9, 11, 12, 13, 14, 14, 15, 15, 15, 14, 14, 13, 12, 11, 9, 8, 6, 4, 3, 2, 1, 1, 0, 0, 0, 1, 1, 2, 3, 4, 6},
    {15, 15, 15, 15, 15, 15, 15, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {8, 12, 14, 15, 14, 12, 10, 9, 9, 10, 12, 13, 12, 10, 7, 4, 2, 1, 2, 4, 5, 6, 5, 3, 1, 0, 1, 3, 5, 6, 6, 7},
};
static float gb_freq(float f, float clk)   /* auf die 11-Bit-Teiler des Game Boy gerastert */
{
    float x = 2048.0f - clk / f;
    x = (float)(int)(x + 0.5f);
    x = clampf(x, 0.0f, 2047.0f);
    return clk / (2048.0f - x);
}
static void a_crypt(const ctl *k, float *l, float *r, int n)
{
    static const float DUTY[4] = {0.125f, 0.25f, 0.5f, 0.75f};
    int i, wv = (int)(k->q * 7.99f), vs;
    const uint8_t *tab = wv < 7 ? GB_WAVE[wv] : E.wt[0];
    float duty = DUTY[(int)(k->r * 3.99f)], wvol, pvol, f1, f2, f3;
    vs = k->a < 0.25f ? 3 : k->a < 0.5f ? 2 : k->a < 0.75f ? 1 : 0;   /* die Welle: aus, 25, 50, 100 % */
    wvol = vs == 3 ? 0.0f : vs == 2 ? 0.25f : vs == 1 ? 0.5f : 1.0f;
    pvol = (float)(15 - (int)(k->a * 8.0f)) / 15.0f;                    /* die Pulse: 4 Bit, leiser je mehr Welle */
    f1 = k->f * drift_of(0);
    if (k->h < 0.0f)                      /* eine Quinte, dann eine Oktave tiefer */
        f2 = f1 * (k->h < -0.5f ? 0.5f : 0.6674f);
    else
        f2 = f1;
    f1 = gb_freq(f1, 131072.0f);
    f2 = gb_freq(f2, 131072.0f);
    if (k->h > 0.0f) {                    /* der Game-Boy-Chorus: ganze Teiler-Schritte daneben */
        float x = 2048.0f - 131072.0f / f2 + (float)(int)(1.0f + k->h * 8.0f);
        f2 = 131072.0f / (2048.0f - clampf(x, 0.0f, 2047.0f));
    }
    f3 = gb_freq(k->f * drift_of(2), 65536.0f);
    for (i = 0; i < n; i++) {
        float a, b, c, f1s = f1;
        if (k->c != 0.0f) {               /* die Sweep-Einheit: in Schritten (128 Hz / Takt), springt zurück */
            V->arp_ph += (8.0f + k->sp * 1.5f) / FK_SR;
            if (V->arp_ph >= 1.0f) {
                V->arp_ph -= 1.0f;
                V->arp_i = (V->arp_i + 1) % (2 + (int)(absf(k->c) * 6.0f));
            }
            f1s = f1 * fm_exp2f((float)V->arp_i * (k->c > 0.0f ? 1.0f : -1.0f) * (0.125f + absf(k->c) * 0.375f));
        }
        adv(&V->ph[0], f1s);
        adv(&V->ph[1], f2);
        adv(&V->ph[2], f3 * (1.0f / 32.0f) * 32.0f);
        a = w_pulse(V->ph[0], duty) * pvol;
        b = w_pulse(V->ph[1], duty == 0.75f ? 0.25f : duty) * pvol;
        c = ((float)(tab[(int)(V->ph[2] * 32.0f) & 31] >> vs) * (2.0f / 15.0f) - (wvol > 0.0f ? 1.0f * wvol : 0.0f)) * (vs == 3 ? 0.0f : 1.0f);
        /* der Hochpass des Game Boy (der Kondensator am Ausgang) */
        V->sh_l += ((a + c) - V->sh_l) * 0.004f;
        V->sh_r += ((b + c) - V->sh_r) * 0.004f;
        l[i] = ((a + c) - V->sh_l) * 0.30f;
        r[i] = ((b + c) - V->sh_r) * 0.30f;
    }
}

/* die Akkorde (Halbtöne über dem Grundton), für die Arpeggios */
static const int8_t CHORD[8][4] = {{0, 3, 7, 12}, {0, 4, 7, 12}, {0, 5, 7, 12}, {0, 3, 6, 9},
                                   {0, 7, 12, 19}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 2, 7, 14}};

/* 3 GOBLIN CHORDS: ein Arpeggio so schnell, dass es wie ein Akkord klingt (C64); QUEST: der Akkord, RUNE: die Welle,
 * ALCHEMY: über wie viele Oktaven, HORDE: eine zweite Stimme */
static void a_goblin(const ctl *k, float *l, float *r, int n)
{
    int i, ch = (int)(k->q * 7.99f), span = 1 + (int)(k->a * 2.99f);
    float rate = 18.0f + k->sp * 3.0f;
    for (i = 0; i < n; i++) {
        int st;
        float f, x, y;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            V->arp_i = (V->arp_i + 1) % (4 * span);
        }
        st = CHORD[ch][V->arp_i & 3] + 12 * (V->arp_i / 4);
        f = k->f * fm_exp2f((float)st / 12.0f) * drift_of(0);
        adv(&V->ph[0], f);
        x = k->r < 0.33f ? w_pulse(V->ph[0], 0.125f + k->r) : k->r < 0.66f ? w_saw4(V->ph[0]) : w_tri4(V->ph[0]);
        adv(&V->ph[1], f * horde_ratio(1, k->h) * 0.5f);
        y = w_pulse(V->ph[1], 0.5f);
        if (k->c < 0.0f)
            x = x + (x * y - x) * (-k->c);
        else if (k->c > 0.0f)
            x = x + (y - x) * k->c * 0.5f;
        l[i] = (x * 0.7f + y * 0.3f * absf(k->h)) * 0.42f;
        r[i] = (x * 0.7f + y * 0.3f * absf(k->h)) * 0.42f;
    }
}

/* 4 DRAGON BREATH: der Rausch-Generator im kurzen Modus (fast ein Ton) mit einer Pulswelle darunter;
 * RUNE: wie hoch das Rauschen getaktet wird, ALCHEMY: Rauschen / Puls, CURSE: der lange Modus mischt sich hinein */
static void a_breath(const ctl *k, float *l, float *r, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        float clk = k->f * (8.0f + k->r * 120.0f), a = w_noise(&V->nz[0], clk, 1), b = w_noise(&V->nz[1], clk * horde_ratio(1, k->h), 1);
        float p;
        adv(&V->ph[2], k->f * 0.5f * drift_of(2));
        p = w_pulse(V->ph[2], 0.25f);
        if (k->c > 0.0f) {
            a = a + (w_noise(&V->nz[2], clk * 2.0f, 0) - a) * k->c;
            b = b + (w_noise(&V->nz[3], clk * 2.1f, 0) - b) * k->c;
        } else if (k->c < 0.0f) {
            a *= 1.0f + (p - 1.0f) * (-k->c);
            b *= 1.0f + (p - 1.0f) * (-k->c);
        }
        l[i] = (a * k->a + p * (1.0f - k->a)) * 0.38f;
        r[i] = (b * k->a + p * (1.0f - k->a)) * 0.38f;
    }
}

/* 5 PC SPEAKER: nur 1 Bit. Drei Töne durch schnelles Umschalten (die Mehrstimmigkeit der DOS-Spiele); RUNE: die
 * Pulsbreite, ALCHEMY: "RealSound" (eine Sinusschwingung über die Pulsbreite), HORDE / CURSE: die Intervalle */
static void a_speaker(const ctl *k, float *l, float *r, int n)
{
    int i;
    float i1 = fm_exp2f((7.0f + k->h * 5.0f) / 12.0f), i2 = fm_exp2f((12.0f + k->c * 7.0f) / 12.0f);
    float rate = 30.0f + k->sp * 4.0f;
    for (i = 0; i < n; i++) {
        float f, out;
        int sel;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            V->arp_i = (V->arp_i + 1) % 3;
        }
        sel = V->arp_i;
        f = k->f * (sel == 0 ? 1.0f : sel == 1 ? i1 : i2) * drift_of(sel);
        adv(&V->ph[0], f);
        if (k->a > 0.02f) {                   /* RealSound: die Pulsbreite folgt einem Sinus, mit 15 kHz getaktet */
            float s = fk_sin(V->ph[0]) * 0.5f + 0.5f;
            adv(&V->ph[1], 15000.0f);
            out = V->ph[1] < (s * k->a + (1.0f - k->a) * (0.05f + 0.45f * k->r)) ? 1.0f : -1.0f;
            out = V->ph[0] < 0.5f || k->a > 0.5f ? out : -out;
        } else {
            out = w_pulse(V->ph[0], 0.05f + 0.45f * k->r);
        }
        l[i] = r[i] = out * 0.32f;
    }
}

/* 6 POKEY CURSE: die Polynom-Zähler des Atari; RUNE: die Verzerrung (rein .. 17 Bit), zwei Kanäle (HORDE),
 * ALCHEMY: der zweite Kanal eine Oktave tiefer im anderen Modus, CURSE: die Kanäle verknüpft (XOR / Hochpass-Trick) */
static void a_pokey(const ctl *k, float *l, float *r, int n)
{
    int i, m1 = (int)(k->r * 5.99f), m2 = (m1 + 1 + (int)(k->a * 4.0f)) % 6;
    for (i = 0; i < n; i++) {
        float a = w_poky(&V->pk[0], k->f * drift_of(0), m1);
        float b = w_poky(&V->pk[1], k->f * horde_ratio(1, k->h) * (k->a > 0.5f ? 0.5f : 1.0f) * drift_of(1), m2), x, y;
        if (k->c > 0.0f) {                    /* XOR */
            float xo = (a > 0.0f) != (b > 0.0f) ? 1.0f : -1.0f;
            x = a + (xo - a) * k->c;
            y = b + (xo - b) * k->c;
        } else if (k->c < 0.0f) {             /* der "Hochpass": der zweite Kanal tastet den ersten ab */
            if (b > 0.0f)
                V->sh_l = a;
            x = a + (V->sh_l - a) * (-k->c);
            y = b;
        } else {
            x = a;
            y = b;
        }
        l[i] = (x * 0.6f + y * 0.4f) * 0.34f;
        r[i] = (x * 0.4f + y * 0.6f) * 0.34f;
    }
}

/* 7 SID RITUAL: zwei Oszillatoren wie im C64; RUNE: die Welle (Dreieck .. Säge .. Puls), ALCHEMY: das Verhältnis
 * des zweiten, CURSE links Ringmodulation, rechts Sync */
static void a_sid(const ctl *k, float *l, float *r, int n)
{
    static const float RAT[8] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f};
    int i;
    float ratio = RAT[(int)(k->a * 7.99f)];
    for (i = 0; i < n; i++) {
        float f1 = k->f * drift_of(0), f2 = f1 * ratio * horde_ratio(1, k->h), a, b, c3;
        adv(&V->ph[0], f1);
        adv(&V->ph[1], f2);
        if (k->c > 0.0f && V->ph[0] < f1 / FK_SR)
            V->ph[1] = wrap1(V->ph[1] * (1.0f - k->c));
        a = k->r < 0.33f ? w_tri4(V->ph[0]) : k->r < 0.66f ? w_saw4(V->ph[0]) : w_pulse(V->ph[0], 0.5f - (k->r - 0.66f));
        b = w_tri4(V->ph[1]);
        if (k->c < 0.0f)                      /* Ring: die Welle des ersten mit dem Vorzeichen des zweiten */
            a = a + ((V->ph[1] < 0.5f ? a : -a) - a) * (-k->c);
        adv(&V->ph[2], f1 * 1.003f * horde_ratio(2, k->h));
        c3 = w_saw4(V->ph[2]);
        l[i] = (a * 0.55f + b * 0.25f + c3 * 0.2f * absf(k->h)) * 0.5f;
        r[i] = (a * 0.45f + b * 0.35f + c3 * 0.2f * absf(k->h)) * 0.5f;
    }
}

/* 8 ADLIB HALL: zwei Operatoren FM wie auf der AdLib-Karte; RUNE: die Modulationstiefe, ALCHEMY: das Verhältnis,
 * CURSE links die Rückkopplung, rechts die Wellen der Karte (halber Sinus, Betrag, Viertel) */
static float opl_wave(float ph, int w)
{
    float s = fk_sin(ph);
    switch (w) {
    case 1: return s > 0.0f ? s : 0.0f;              /* halber Sinus */
    case 2: return absf(s);                           /* Betrag */
    case 3: return wrap1(ph * 2.0f) < 0.5f ? absf(s) : 0.0f;   /* Viertel */
    default: return s;
    }
}
static void a_adlib(const ctl *k, float *l, float *r, int n)
{
    static const float MUL[8] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f, 10.0f};
    int i, w = k->c > 0.0f ? (int)(k->c * 3.99f) : 0;
    float mul = MUL[(int)(k->a * 7.99f)], idx = k->r * 3.0f, fb = k->c < 0.0f ? -k->c * 0.9f : 0.0f;
    for (i = 0; i < n; i++) {
        float s = 0.0f;
        int v;
        for (v = 0; v < 2; v++) {
            float f = k->f * horde_ratio(v, k->h) * drift_of(v), m;
            adv(&V->ph2[v], f * mul);
            m = opl_wave(wrap1(V->ph2[v] + V->fmfb[v] * fb), w);
            V->fmfb[v] = m;
            adv(&V->ph[v], f);
            s += opl_wave(wrap1(V->ph[v] + m * idx * 0.5f), w ? (w + v) & 3 : 0) * (v ? 0.4f : 0.6f);
        }
        s = (float)(int)(s * 32.0f) * (1.0f / 32.0f);   /* die Karte rechnete grob: 6 Bit Pegel */
        l[i] = r[i] = s * 0.5f;
    }
}

/* 9 BEHOLDER: drei Pulswellen mit Vibrato aus Tabellen wie in Tracker-Musik; ALCHEMY: wie wild die Vibratos */
static void a_beholder(const ctl *k, float *l, float *r, int n)
{
    static const float VT[8] = {0.0f, 0.7f, 1.0f, 0.7f, 0.0f, -0.7f, -1.0f, -0.7f};
    int i, v;
    for (i = 0; i < n; i++) {
        float sl = 0.0f, sr = 0.0f;
        for (v = 0; v < 3; v++) {
            float vr = (4.0f + (float)v * 2.3f) * (0.3f + k->sp * 0.05f), f, x;
            V->ph2[v] = wrap1(V->ph2[v] + vr / FK_SR);
            f = k->f * horde_ratio(v, k->h) * drift_of(v) * fm_exp2f(VT[(int)(V->ph2[v] * 8.0f) & 7] * k->a * 0.5f / 12.0f);
            adv(&V->ph[v], f);
            x = w_pulse(V->ph[v], 0.08f + 0.42f * k->r * (1.0f - 0.3f * (float)v));
            if (k->c < 0.0f && v)
                x = x + (x * w_pulse(V->ph[0], 0.5f) - x) * (-k->c);
            sl += x * (v == 1 ? 0.2f : 0.4f);
            sr += x * (v == 2 ? 0.2f : 0.4f);
        }
        l[i] = sl * 0.45f;
        r[i] = sr * 0.45f;
    }
}

/* 10 TREASURE: Glöckchen aus FM, immer wieder angeschlagen (SPEED), auf den Tönen eines Akkords (QUEST) */
static void a_treasure(const ctl *k, float *l, float *r, int n)
{
    int i, ch = (int)(k->q * 7.99f);
    float rate = 2.0f + k->sp * 0.6f, ratio = 1.4f + k->a * 2.6f;
    for (i = 0; i < n; i++) {
        float f, m, s;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            V->arp_i = (int)(rng_u(&E.rng) & 7u);
            V->sh_acc = 1.0f;                  /* angeschlagen */
        }
        V->sh_acc *= 0.99985f - 0.0003f * (1.0f - k->r);
        f = k->f * 2.0f * fm_exp2f((float)(CHORD[ch][V->arp_i & 3] + 12 * (V->arp_i >> 2)) / 12.0f) * drift_of(0);
        adv(&V->ph2[0], f * ratio);
        m = fk_sin(V->ph2[0]) * (1.0f + 2.0f * k->r) * V->sh_acc;
        adv(&V->ph[0], f);
        s = fk_sin(wrap1(V->ph[0] + m * 0.3f)) * V->sh_acc;
        adv(&V->ph[1], k->f * horde_ratio(1, k->h) * 0.5f);
        s += w_tri4(V->ph[1]) * 0.25f;         /* ein leiser Ton darunter, damit es ein Drone bleibt */
        if (k->c != 0.0f)
            s = s + (s * w_pulse(V->ph[1], 0.5f) - s) * absf(k->c);
        s = (float)(int)(s * 16.0f) * (1.0f / 16.0f);
        l[i] = s * (0.5f + 0.1f * (float)(V->arp_i & 1));
        r[i] = s * (0.6f - 0.1f * (float)(V->arp_i & 1));
    }
}

/* 11 SKELETON MARCH: der 4-Bit-Dreieck-Bass des NES und Rausch-Ticks im Takt (SPEED); ALCHEMY: wie dicht,
 * CURSE: die Tonhöhe der Ticks */
static void a_skeleton(const ctl *k, float *l, float *r, int n)
{
    int i;
    float rate = 2.0f + k->sp * 0.25f;
    for (i = 0; i < n; i++) {
        float b, t;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            if (rng_f(&E.rng) < 0.25f + 0.75f * k->a)
                V->sh_acc = 1.0f;
        }
        V->sh_acc *= 0.9993f;
        adv(&V->ph[0], k->f * 0.5f * drift_of(0));
        adv(&V->ph[1], k->f * 0.5f * horde_ratio(1, k->h) * drift_of(1));
        b = (k->r < 0.5f ? w_tri4(V->ph[0]) : w_saw4(V->ph[0])) * 0.7f + w_tri4(V->ph[1]) * 0.3f * absf(k->h);
        t = w_noise(&V->nz[0], 2000.0f + (k->c + 1.0f) * 12000.0f, k->c > 0.5f) * V->sh_acc * V->sh_acc;
        l[i] = (b * 0.75f + t * 0.5f) * 0.5f;
        r[i] = (b * 0.75f + t * 0.45f) * 0.5f;
    }
}

/* 12 MAGE TOWER: die Welle wandert von Puls über Säge zu Dreieck und zur Tabelle (RUNE, ALCHEMY: wie schnell von
 * selbst); CURSE: Pulsbreiten-Modulation */
static float morph_wave(float ph, float m, const uint8_t *t, float duty)
{
    float x = m * 3.0f, a, b;
    int s = (int)x;
    x -= (float)s;
    x = (float)((int)(x * 4.0f)) * 0.25f;     /* in vier Stufen */
    switch (s) {
    case 0: a = w_pulse(ph, duty); b = w_saw4(ph); break;
    case 1: a = w_saw4(ph); b = w_tri4(ph); break;
    default: a = w_tri4(ph); b = w_wt(t, ph); break;
    }
    return a + (b - a) * x;
}
static void a_mage(const ctl *k, float *l, float *r, int n)
{
    int i, v;
    for (i = 0; i < n; i++) {
        float sl = 0.0f, sr = 0.0f, m;
        V->duty_ph = wrap1(V->duty_ph + (0.02f + k->a * 1.5f) / FK_SR);
        m = clampf(k->r + 0.3f * k->a * fk_sin(V->duty_ph), 0.0f, 0.999f);
        for (v = 0; v < 2; v++) {
            float f = k->f * horde_ratio(v, k->h) * drift_of(v), x;
            adv(&V->ph[v], f);
            x = morph_wave(V->ph[v], m, E.wt[v], 0.5f - 0.44f * absf(k->c) * (0.5f + 0.5f * fk_sin(V->duty_ph * 3.0f + (float)v)));
            sl += x * (v ? 0.35f : 0.65f);
            sr += x * (v ? 0.65f : 0.35f);
        }
        l[i] = sl * 0.45f;
        r[i] = sr * 0.45f;
    }
}

/* 13 THRONE OF BONES: ein Oktav-Arpeggio (Grundton, Oktave, zwei Oktaven) mit wandernder Pulsbreite */
static void a_throne(const ctl *k, float *l, float *r, int n)
{
    int i, octs = 2 + (int)(k->a * 2.99f);
    float rate = 10.0f + k->sp * 2.0f;
    for (i = 0; i < n; i++) {
        float f, x, sub;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            V->arp_i = (V->arp_i + 1) % octs;
        }
        V->duty_ph = wrap1(V->duty_ph + 0.3f / FK_SR);
        f = k->f * (float)(1 << V->arp_i) * drift_of(0);
        adv(&V->ph[0], f);
        x = w_pulse(V->ph[0], 0.1f + 0.35f * k->r * (0.6f + 0.4f * fk_sin(V->duty_ph)));
        adv(&V->ph[1], k->f * 0.5f * horde_ratio(1, k->h));
        sub = w_pulse(V->ph[1], 0.5f);
        if (k->c < 0.0f)
            x = x + (x * sub - x) * (-k->c);
        else
            x = x + (sub - x) * k->c * 0.5f;
        l[i] = (x * 0.7f + sub * 0.3f) * 0.42f;
        r[i] = (x * 0.6f + sub * 0.4f) * 0.42f;
    }
}

/* 14 PIT OF DOOM: Frequenzteiler wie in alten Orgeln und Zählern: ein Rechteck, darunter /2 /3 /4 /5 /6, logisch
 * gemischt; RUNE: welche Teiler, ALCHEMY: wie tief, CURSE: XOR statt Summe */
static void a_pit(const ctl *k, float *l, float *r, int n)
{
    static const uint8_t MASK[8] = {0x03, 0x05, 0x09, 0x0B, 0x15, 0x1D, 0x2B, 0x3F};
    int i, mask = MASK[(int)(k->r * 7.99f)], depth = 2 + (int)(k->a * 4.99f);
    for (i = 0; i < n; i++) {
        int d, cnt = 0, x = 0, on;
        float s = 0.0f;
        adv(&V->ph[0], k->f * 2.0f * drift_of(0));
        if (V->ph[0] < k->f * 2.0f / FK_SR)
            V->arp_i++;                         /* ein Zähler: jede Periode eins weiter */
        for (d = 1; d <= depth; d++) {
            if (!(mask >> (d - 1) & 1))
                continue;
            on = ((V->arp_i / d) & 1) ^ (d == 1 ? (V->ph[0] < 0.5f) : 0);
            x ^= on;
            s += on ? 1.0f : -1.0f;
            cnt++;
        }
        s = cnt ? s / (float)cnt : 0.0f;
        if (k->c > 0.0f)
            s = s + ((x ? 1.0f : -1.0f) - s) * k->c;
        adv(&V->ph[1], k->f * 0.5f * horde_ratio(1, k->h));
        s += w_pulse(V->ph[1], 0.5f) * 0.3f * absf(k->h);
        l[i] = r[i] = s * 0.4f;
    }
}

/* 15 WIND OF THE ABYSS: langes Rauschen durch einen schwingenden Bandpass, der dem Grundton folgt; RUNE: wie grob
 * das Rauschen getaktet ist, ALCHEMY: wie schnell der Sturm wandert */
static void a_wind(const ctl *k, float *l, float *r, int n)
{
    int i, c;
    float clk = 2000.0f + k->r * 40000.0f;
    V->duty_ph = wrap1(V->duty_ph + (0.03f + k->a * 0.8f) * (float)n / FK_SR);
    for (c = 0; c < 2; c++) {
        float fc = k->f * (2.0f + (float)c * horde_ratio(1, k->h)) * fm_exp2f(1.2f * fk_sin(V->duty_ph + (float)c * 0.25f));
        float g = clampf(TAU * fc / FK_SR, 0.001f, 0.9f), q = 0.08f + 0.12f * (1.0f - absf(k->c));
        float *o = c ? r : l;
        for (i = 0; i < n; i++) {
            float x = w_noise(&V->nz[c], clk, k->c > 0.6f), hp, bp, lp;
            lp = V->bp[c][1] + g * V->bp[c][0];
            hp = x - lp - q * V->bp[c][0];
            bp = g * hp + V->bp[c][0];
            V->bp[c][0] = bp;
            V->bp[c][1] = lp;
            o[i] = clampf(bp * 0.5f, -1.0f, 1.0f);
        }
        V->bp[c][0] = fm_flush(V->bp[c][0]);
        V->bp[c][1] = fm_flush(V->bp[c][1]);
    }
}

/* 16 SPELLBOOK: zufällige Töne aus einer Tonleiter (QUEST), im Takt (SPEED); mit dem Echo werden sie ein Drone;
 * ALCHEMY: wie weit sie springen, CURSE: wie oft eine Oktave dazwischen */
static void a_spell(const ctl *k, float *l, float *r, int n)
{
    static const int8_t SC[4][7] = {{0, 3, 5, 7, 10, 12, 15}, {0, 2, 3, 7, 8, 12, 14}, {0, 2, 4, 7, 9, 12, 14},
                                    {0, 1, 5, 7, 8, 12, 13}};
    int i, s = (int)(k->q * 3.99f);
    float rate = 1.0f + k->sp * 0.12f;
    for (i = 0; i < n; i++) {
        float f, x;
        V->arp_ph += rate / FK_SR;
        if (V->arp_ph >= 1.0f) {
            V->arp_ph -= 1.0f;
            V->step_note = SC[s][(int)(rng_f(&E.rng) * (2.0f + k->a * 4.99f))];
            if (rng_f(&E.rng) < absf(k->c) * 0.6f)
                V->step_note += k->c > 0.0f ? 12 : -12;
        }
        f = k->f * fm_exp2f((float)V->step_note / 12.0f) * drift_of(0);
        adv(&V->ph[0], f);
        x = k->r < 0.5f ? w_pulse(V->ph[0], 0.125f + k->r * 0.6f) : w_tri4(V->ph[0]);
        adv(&V->ph[1], k->f * 0.5f * horde_ratio(1, k->h));
        l[i] = (x * 0.7f + w_tri4(V->ph[1]) * 0.3f) * 0.45f;
        r[i] = (x * 0.6f + w_tri4(V->ph[1]) * 0.4f) * 0.45f;
    }
}

typedef void (*algo_fn)(const ctl *, float *, float *, int);
static const algo_fn ALGO_FN[FK_NALGO] = {a_torch, a_crypt, a_goblin, a_breath, a_speaker, a_pokey, a_sid, a_adlib,
                                          a_beholder, a_treasure, a_skeleton, a_mage, a_throne, a_pit, a_wind, a_spell};

/* ---- ein Block ---- */
void fk_render(float *l, float *r, int n)
{
    static float gl[FK_BLOCK_MAX], gr[FK_BLOCK_MAX];
    ctl k;
    int i, c;
    float u, sm, lg, wd, bits, steps, age_hz, ecm, haunt, dec, dly, feed, cross, torch, fc_lp, fc_hp;
    float atk, rel, vib, f0;
    if (n > FK_BLOCK_MAX)
        n = FK_BLOCK_MAX;
    /* die Regler glätten */
    {   /* CREEP: jeder Regler zieht langsam nach (bis ~8 s), CREEP selbst und die Rastenden sofort */
        float cr = (float)E.raw[P_CREEP] * (1.0f / 127.0f), tau = 0.012f + cr * cr * 8.0f;
        float co = clampf((float)n / (FK_SR * tau), 0.0f, 1.0f);
        for (i = 0; i < FK_NP; i++) {
            float t = (float)E.raw[i] * (1.0f / 127.0f);
            int fast = i == P_CREEP || i == P_PORTAL || i == P_QUEST || i == P_FATE || i == P_VOLUME;
            E.sm[i] += (t - E.sm[i]) * (fast ? 0.15f : co);
        }
    }
    if (E.raw[P_FATE] != E.last_fate) {
        E.last_fate = E.raw[P_FATE];
        reseed(E.last_fate);
    }
    /* der Wechsel: ausblenden, wechseln, einblenden */
    E.xf += E.xf_dir * (float)n / (0.03f * FK_SR);
    if (E.xf <= 0.0f && E.algo_next >= 0) {
        E.algo = E.algo_next;
        E.algo_next = -1;
        E.xf_dir = 1.0f;
        reseed(E.raw[P_FATE]);
        for (c = 0; c < 2; c++) {
            for (i = 0; i < NV; i++)
                E.vs[c].ph[i] = E.vs[c].ph2[i] = 0.0f;
            E.vs[c].arp_i = 0;
        }
    }
    E.xf = clampf(E.xf, 0.0f, 1.0f);
#define B(p) ((E.sm[p] - 0.5f) * 2.0f)
    k.c = B(P_CURSE);
    k.h = B(P_HORDE);
    k.c = absf(k.c) < 0.03f ? 0.0f : k.c;
    k.h = absf(k.h) < 0.03f ? 0.0f : k.h;
    k.r = E.sm[P_RUNE];
    k.a = E.sm[P_ALCHEMY];
    /* die Varianten (Welle, Akkord, Tonleiter) würfelt FATE; QUEST ist der MORPH */
    k.q = ((float)(E.raw[P_FATE] & 7u) + 0.5f) * 0.125f;
    k.sp = 1.0f + E.sm[P_SPEED] * 59.0f;      /* 1 .. 60 Hz: die Bildrate der alten Spiele ist 50 / 60 */
    /* OMEN: ein LFO, der mehrere Ziele zugleich bewegt (PATH); frei oder im Takt des GROOVE (an jedem Takt ausgerichtet) */
    {
        static const float PW[FK_NPATH][8] = {
            /* nur der Klang, nicht das Stereofeld:
             * Tonhöhe (Cent), Filter schließen (Oktaven), RUNE, CURSE, Lautstärke, ALCHEMY, AURA, Lo-Fi */
            {0, 5.0f, 0.15f, 0, 0.45f, 0, 0, 0},      /* BREATH: das Filter atmet zu und auf, leiser im Ausatmen */
            {0, 7.5f, 0.3f, 0, 0, 0, 0, 0},           /* FLAME: ein tiefes Filter-Auf-und-Zu (Wah) */
            {60, 3.0f, 0.25f, 0, 0, 0, 0, 0},         /* HAUNT: die Tonhöhe schwankt (Vibrato bis ±60 Cent), dunkler */
            {0, 3.5f, 0.2f, 0, 0, 0, 0.8f, 0},        /* AURA: der Resonator schwillt an, das Licht wird dunkler */
            {0, 3.0f, 0.8f, 0, 0, 0.7f, 0, 0},        /* RUNE: Welle, Pulsbreite, Tabelle und ALCHEMY wandern (PWM) */
            {0, 0, 0.3f, 0.8f, 0, 0, 0, 0},           /* CURSE: Ring / Sync / FM pumpen (springt wie ein Arpeggio) */
            {0, 3.0f, 0, 0, 0, 0, 0, 1.0f},           /* DECAY: Bits und Abtastrate zerfallen im Takt */
            {20, 5.0f, 0.4f, 0.5f, 0.2f, 0.3f, 0.4f, 0.4f}};   /* STORM: alles */
        int rv = E.raw[P_RATE], sh = (E.raw[P_SHAPE] * FK_NSHAPE) >> 7, pa = (E.raw[P_PATH] * FK_NPATH) >> 7;
        float pw = E.sm[P_POWER], ph0 = E.om_ph, x, d;
        const float *w = PW[pa];
        if (omen_sync_bpm > 0.0f) {
            float cpb = OMEN_CPB[(rv * 8) >> 7];
            int st = gr_step();
            E.om_ph = wrap1(E.om_ph + cpb * omen_sync_bpm * (1.0f / 240.0f) * (float)n / FK_SR);
            if (st == 0 && E.om_step != 0) {  /* ein neuer Takt: die Phase genau dorthin, wo sie hingehört */
                E.om_bar++;
                E.om_ph = wrap1((float)E.om_bar * cpb);
            }
            E.om_step = st;
        } else {
            E.om_ph = wrap1(E.om_ph + omen_hz(rv) * (float)n / FK_SR);
        }
        x = E.om_ph;
        switch (sh) {
        case 0: x = -fk_sin(x); break;                              /* SINE */
        case 1: x = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; break;   /* TRIANGLE */
        case 2: x = 1.0f - 2.0f * x; break;                         /* SAW (fällt) */
        case 3: x = x < 0.5f ? 1.0f : -1.0f; break;                 /* SQUARE */
        case 4: x = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;    /* STAIRS: das Dreieck in 8 Stufen, wie im Chip */
            x = (float)(int)((x + 1.0f) * 3.999f) * (2.0f / 7.0f) - 1.0f; break;
        default:                                                    /* CHANCE: je Welle ein neuer Zufallswert */
            if (E.om_ph < ph0)
                E.om_sh = rng_f(&E.rng) * 2.0f - 1.0f;
            x = E.om_sh;
            break;
        }
        if (sh == 3 || sh == 5)                                     /* Sprünge: ganz kurz geglättet, kein Klicken */
            E.om_v += (x - E.om_v) * clampf((float)n / (FK_SR * 0.004f), 0.0f, 1.0f);
        else
            E.om_v = x;
        x = E.om_v;
        d = x * pw;
        E.om_out = d;
        E.om_pitch = d * w[0];
        E.om_torch = -pw * w[1] * (0.5f + 0.5f * x);               /* das Filter schließt sich (nur nach unten) */
        k.r = clampf(k.r + d * w[2], 0.0f, 0.999f);
        k.c = clampf(k.c + d * w[3], -1.0f, 1.0f);
        k.a = clampf(k.a + d * w[5], 0.0f, 0.999f);
        E.om_rune = d * w[2];
        E.om_curse = d * w[3];
        E.om_alch = d * w[5];
        E.om_vol = 1.0f - pw * w[4] * (0.5f + 0.5f * x);
        E.om_aura = pw * w[6] * (0.5f + 0.5f * x);
        E.om_lofi = pw * w[7] * (0.5f + 0.5f * x);
    }
    /* die Tonhöhe: Gleiten, Vibrato (in der Bildrate, stufig wie im Tracker), PORTAL */
    sm = E.sm[P_CREEP];
    E.glide = sm < 0.01f ? 1.0f : 1.0f - fm_exp2f(-(float)n / (FK_SR * 0.002f * fm_exp2f(sm * 12.5f)));
    E.pitch += (E.target - E.pitch) * E.glide;
    E.vib_ph = wrap1(E.vib_ph + (2.0f + E.sm[P_SPEED] * 8.0f) * (float)n / FK_SR);
    vib = 0.0f;                              /* (das Vibrato der Tracker hat BEHOLDER; hier schwebt WANDER) */
    {
        int portal = (int)(B(P_PORTAL) * 24.0f + (B(P_PORTAL) < 0.0f ? -0.5f : 0.5f));
        f0 = mtof(E.pitch + E.bend + vib + (float)portal + E.om_pitch * 0.01f);
    }
    k.f = clampf(f0, 8.0f, 6000.0f);
    {   /* WANDER: acht langsame Zufallswege, jeder sucht sich alle paar Sekunden ein neues Ziel und gleitet hin */
        float w = E.sm[P_WANDER];
        for (i = 0; i < 8; i++) {
            E.wcnt[i] -= (float)n;
            if (E.wcnt[i] <= 0.0f) {
                E.wt_[i] = rng_f(&E.rng) * 2.0f - 1.0f;
                E.wcnt[i] = FK_SR * (1.5f + 5.0f * rng_f(&E.rng));
            }
            E.wv[i] += (E.wt_[i] - E.wv[i]) * (float)n / (FK_SR * 2.5f);
        }
        for (i = 0; i < NV; i++)          /* die Stimmen: bis ±70 Cent auseinander */
            E.drift[i] = E.wv[i] * 70.0f * w * w;
        k.r = clampf(k.r + E.wv[4] * 0.4f * w, 0.0f, 0.999f);   /* die Klangfarbe wandert */
    }
    {   /* QUEST = MORPH: stufenlos in den nächsten Algorithmus (mit dessen eigenen Reglern); dazwischen klingen beide */
        static const float GAIN[FK_NALGO] = {1.0f, 1.4f, 1.3f, 1.65f, 1.55f, 1.6f, 2.6f, 1.25f,
                                             1.4f, 1.6f, 2.2f, 1.75f, 1.2f, 1.15f, 1.05f, 1.3f};
        static float bl[FK_BLOCK_MAX], br[FK_BLOCK_MAX];
        float m = E.sm[P_QUEST], ga, gb;
        int nx = (E.algo + 1) % FK_NALGO;
        ctl kb = k;
        for (i = 0; i < 4; i++)
            E.msm[i] += ((float)E.mraw[i] * (1.0f / 127.0f) - E.msm[i]) * 0.15f;
        kb.c = (E.msm[0] - 0.5f) * 2.0f;
        kb.h = (E.msm[1] - 0.5f) * 2.0f;
        kb.c = absf(kb.c) < 0.03f ? 0.0f : kb.c;
        kb.h = absf(kb.h) < 0.03f ? 0.0f : kb.h;
        kb.r = clampf(E.msm[2] + E.wv[4] * 0.4f * E.sm[P_WANDER] + E.om_rune, 0.0f, 0.999f);
        kb.c = clampf(kb.c + E.om_curse, -1.0f, 1.0f);
        kb.a = clampf(E.msm[3] + E.om_alch, 0.0f, 0.999f);
        ga = fm_sqrtf(1.0f - m) * GAIN[E.algo];               /* gleiche Leistung */
        gb = fm_sqrtf(m) * GAIN[nx];
        if (m < 0.004f) {
            ga = GAIN[E.algo];
            gb = 0.0f;
        } else if (m > 0.996f) {
            ga = 0.0f;
            gb = GAIN[nx];
        }
        if (ga > 0.0f) {
            V = &E.vs[0];
            ALGO_FN[E.algo](&k, gl, gr, n);
        } else {
            fk_zero(gl, (uint32_t)n * sizeof(float));
            fk_zero(gr, (uint32_t)n * sizeof(float));
        }
        if (gb > 0.0f) {
            V = &E.vs[1];
            ALGO_FN[nx](&kb, bl, br, n);
            for (i = 0; i < n; i++) {
                gl[i] = gl[i] * ga + bl[i] * gb;
                gr[i] = gr[i] * ga + br[i] * gb;
            }
        } else {
            for (i = 0; i < n; i++) {
                gl[i] *= ga;
                gr[i] *= ga;
            }
        }
    }
    {   /* WANDER: das Panorama wandert, der Klang atmet */
        float w = E.sm[P_WANDER], pan = E.wv[6] * 0.6f * w, br = 1.0f - 0.35f * w * (0.5f + 0.5f * E.wv[7]);
        for (i = 0; i < n; i++) {
            gl[i] *= (1.0f - pan) * br;
            gr[i] *= (1.0f + pan) * br;
        }
    }
    /* die Hüllkurve */
    atk = 1.0f * fm_exp2f(E.sm[P_AWAKEN] * 13.0f) * 0.001f * FK_SR;
    rel = 5.0f * fm_exp2f(E.sm[P_SLUMBER] * 11.5f) * 0.001f * FK_SR;
    /* TORCH: links schließt der Tiefpass, rechts öffnet der Hochpass (2 Pole, einfach wie damals) */
    torch = B(P_TORCH);
    fc_lp = torch < 0.0f ? 18000.0f * fm_exp2f(torch * 9.0f) : 18000.0f;
    fc_hp = torch > 0.0f ? 20.0f * fm_exp2f(torch * 8.5f) : 20.0f;
    {   /* WANDER: auch das Filter wandert (bis 1,5 Oktaven) */
        float wf = fm_exp2f(E.wv[5] * 1.5f * E.sm[P_WANDER]);
        fc_lp = clampf(fc_lp * wf, 40.0f, 18000.0f);
        fc_hp = clampf(fc_hp * wf, 10.0f, 15000.0f);
    }
    {
        float al = clampf(TAU * fc_lp / FK_SR, 0.0f, 1.0f), ah = clampf(TAU * fc_hp / FK_SR, 0.0f, 1.0f);
        /* BITS: 16 .. 1 Bit; AGE: die Abtastrate 44,1 kHz .. 2 kHz (Halten) */
        bits = clampf(16.0f - E.sm[P_BITS] * 15.0f - E.om_lofi * 9.0f, 1.0f, 16.0f);   /* OMEN DECAY: zerfällt */
        steps = fm_exp2f(bits - 1.0f);
        age_hz = 44100.0f * fm_exp2f(-clampf(E.sm[P_AGE] + E.om_lofi * 0.45f, 0.0f, 1.0f) * 4.46f);
        for (i = 0; i < n; i++) {
            float g = E.gate || E.hold, a = gl[i], b = gr[i];
            if (g)
                E.env += (1.0f - E.env) / (atk + 1.0f) * 1.5f;
            else
                E.env -= E.env / (rel + 1.0f) * 4.0f;
            for (c = 0; c < 2; c++) {
                float *x = c ? &b : &a;
                E.f_lp[c][0] += (*x - E.f_lp[c][0]) * al;
                E.f_lp[c][1] += (E.f_lp[c][0] - E.f_lp[c][1]) * al;
                E.f_hp[c][0] += (E.f_lp[c][1] - E.f_hp[c][0]) * ah;
                *x = E.f_lp[c][1] - E.f_hp[c][0];
            }
            a *= E.env * E.vel * E.xf;
            b *= E.env * E.vel * E.xf;
            if (bits < 15.5f) {
                a = (float)(int)(a * steps + (a < 0.0f ? -0.5f : 0.5f)) / steps;
                b = (float)(int)(b * steps + (b < 0.0f ? -0.5f : 0.5f)) / steps;
            }
            gl[i] = a;
            gr[i] = b;
        }
        E.f_lp[0][0] = fm_flush(E.f_lp[0][0]);
        E.f_lp[1][0] = fm_flush(E.f_lp[1][0]);
        E.f_lp[0][1] = fm_flush(E.f_lp[0][1]);
        E.f_lp[1][1] = fm_flush(E.f_lp[1][1]);
        E.f_hp[0][0] = fm_flush(E.f_hp[0][0]);
        E.f_hp[1][0] = fm_flush(E.f_hp[1][0]);
    }
    /* AURA: der Resonator – vier Kammfilter je Kanal auf Grundton, Oktave, Quinte und Duodezime, rechts leicht
     * verstimmt; die Chip-Töne regen sie an und sie klingen lange nach: aus Piepsen wird eine schwebende Fläche, die
     * Rückkopplung ist weich begrenzt. So werden die 8-Bit-Klänge zu Drones. */
    {
        static const float RAT[FK_NRES] = {1.0f, 2.0f, 1.5f, 3.0f, 1.0035f, 2.004f, 0.75f, 4.0f};
        float au = clampf(E.sm[P_AURA] + E.om_aura, 0.0f, 1.0f);
        int j;
        if (au > 0.005f || E.rlp[0] != 0.0f || E.rlp[4] != 0.0f) {
            float fb = 0.90f + 0.0985f * fm_sqrtf(au), damp = 0.30f + 0.25f * E.sm[P_DECAY], wet = au,
                  dry = 1.0f - 0.7f * au;
            float base = mtof(E.pitch + (float)(int)(B(P_PORTAL) * 24.0f));
            for (j = 0; j < FK_NRES; j++) {
                float len = FK_SR / clampf(base * RAT[j], 22.0f, 8000.0f);
                while (len > (float)FK_RS_N - 4.0f)
                    len *= 0.5f;          /* zu tief: eine Oktave höher */
                E.rlen[j] = E.rlen[j] <= 0.0f ? len : E.rlen[j] + (len - E.rlen[j]) * 0.05f;
            }
            for (i = 0; i < n; i++) {
                float ol = 0.0f, orr = 0.0f, in_l = gl[i] * 0.22f, in_r = gr[i] * 0.22f;
                for (j = 0; j < FK_NRES; j++) {
                    float *b = E.m.res[j], rp = (float)E.rw - E.rlen[j], fr, y, x;
                    uint32_t ri;
                    if (rp < 0.0f)
                        rp += (float)FK_RS_N;
                    ri = (uint32_t)rp;
                    fr = rp - (float)ri;
                    y = b[ri & (FK_RS_N - 1u)] + (b[(ri + 1u) & (FK_RS_N - 1u)] - b[ri & (FK_RS_N - 1u)]) * fr;
                    E.rlp[j] += (y - E.rlp[j]) * (1.0f - damp);
                    x = (j < 4 ? in_l : in_r) + E.rlp[j] * fb;
                    x = x / (1.0f + absf(x) * 0.25f);
                    b[E.rw & (FK_RS_N - 1u)] = absf(x) < 1e-12f ? 0.0f : x;   /* (keine Denormalen: sie sind langsam) */
                    if (j < 4)
                        ol += y;
                    else
                        orr += y;
                }
                E.rw = (E.rw + 1u) & (FK_RS_N - 1u);
                gl[i] = gl[i] * dry + ol * wet * 0.35f;
                gr[i] = gr[i] * dry + orr * wet * 0.35f;
            }
            for (j = 0; j < FK_NRES; j++)
                E.rlp[j] = fm_flush(E.rlp[j]);
        }
    }
    /* OMEN: ein eigener Tiefpass hinter AURA, links und rechts gleich (so wandert nichts im Stereofeld) */
    if (E.om_torch < -0.01f || absf(E.olp[0][1]) > 1e-6f) {
        float fc0 = 18000.0f * fm_exp2f(E.om_f0), fc1 = 18000.0f * fm_exp2f(E.om_torch);
        float a0 = clampf(TAU * fc0 / FK_SR, 0.0f, 1.0f), a1 = clampf(TAU * fc1 / FK_SR, 0.0f, 1.0f);
        for (i = 0; i < n; i++) {
            float al = a0 + (a1 - a0) * ((float)i / (float)n);
            E.olp[0][0] += (gl[i] - E.olp[0][0]) * al;
            E.olp[0][1] += (E.olp[0][0] - E.olp[0][1]) * al;
            E.olp[1][0] += (gr[i] - E.olp[1][0]) * al;
            E.olp[1][1] += (E.olp[1][0] - E.olp[1][1]) * al;
            gl[i] = E.olp[0][1];
            gr[i] = E.olp[1][1];
        }
        for (c = 0; c < 2; c++) {
            E.olp[c][0] = fm_flush(E.olp[c][0]);
            E.olp[c][1] = fm_flush(E.olp[c][1]);
        }
    }
    E.om_f0 = E.om_torch;
    /* AGE: halten */
    {
        static float acc, hl, hr;
        float st = age_hz / FK_SR;
        for (i = 0; i < n; i++) {
            acc += st;
            if (acc >= 1.0f) {
                acc -= 1.0f;
                hl = gl[i];
                hr = gr[i];
            }
            if (st < 0.999f) {
                gl[i] = hl;
                gr[i] = hr;
            }
        }
    }
    /* die Pumpe des GROOVE */
    if (X_duck)
        for (i = 0; i < n; i++) {
            gl[i] *= X_duck[i];
            gr[i] *= X_duck[i];
        }
    /* das Echo: HAUNT links Ping-Pong, rechts gerade (ganz außen endlos); DECAY macht aus den Echos Nebel: erst dumpfer,
     * dann verwaschen durch vier Allpässe je Seite in der Schleife (eine Fläche statt Wiederholungen), ganz oben körnig
     * wie 8 Bit. Die Zeit schwankt ein wenig wie ein altes Band (Chorus). */
    ecm = E.sm[P_ECHO];
    haunt = B(P_HAUNT);
    feed = absf(haunt) > 0.96f ? 1.0f : absf(haunt) * 0.985f;
    cross = haunt < 0.0f ? 1.0f : 0.0f;
    dec = E.sm[P_DECAY];
    dly = 5.0f * fm_exp2f(E.sm[P_DEPTH] * 7.2f) * 0.001f * FK_SR;
    dly = clampf(dly, 8.0f, (float)FK_EC_N - 64.0f);
    if (E.ed <= 0.0f)
        E.ed = dly;
    {
        static const uint16_t DL[8] = {347, 557, 839, 1153, 409, 613, 887, 1201};   /* (Summe < FK_DF_N) */
        float lpc = 1.0f - clampf(dec * 1.6f, 0.0f, 0.85f), gdf = clampf((dec - 0.25f) * 1.1f, 0.0f, 0.72f);
        float crush = dec > 0.88f ? (float)(int)(2.0f + (1.0f - dec) * 120.0f) : 0.0f;
        float send = X_l ? X_send : 0.0f, wow_d = 2.0f + 10.0f * dec;
        uint32_t off[8], o = 0, j;
        for (j = 0; j < 8; j++) {
            off[j] = o;
            o += DL[j];
        }
        if (E.freeze)
            feed = 1.0f;
        for (i = 0; i < n; i++) {
            float rl, rr, dl, dr, inl, inr, fr, wl, wr2;
            int32_t p0, p1;
            uint32_t ri;
            E.ed += (dly - E.ed) * 0.0005f;
            E.wow = wrap1(E.wow + 0.37f / FK_SR);
            fr = (float)E.ew - E.ed - wow_d * (1.0f + fk_sin(E.wow));
            if (fr < 0.0f)
                fr += (float)FK_EC_N;
            ri = (uint32_t)fr;
            p0 = E.m.ec[0][ri & (FK_EC_N - 1u)];
            p1 = E.m.ec[0][(ri + 1u) & (FK_EC_N - 1u)];
            rl = ((float)p0 + (float)(p1 - p0) * (fr - (float)ri)) * (1.0f / 16000.0f);
            p0 = E.m.ec[1][ri & (FK_EC_N - 1u)];
            p1 = E.m.ec[1][(ri + 1u) & (FK_EC_N - 1u)];
            rr = ((float)p0 + (float)(p1 - p0) * (fr - (float)ri)) * (1.0f / 16000.0f);
            E.elp[0] += (rl - E.elp[0]) * (E.freeze ? 1.0f : lpc);
            E.elp[1] += (rr - E.elp[1]) * (E.freeze ? 1.0f : lpc);
            dl = E.elp[0];
            dr = E.elp[1];
            if (gdf > 0.0f) {                 /* der Nebel: Allpässe in der Schleife */
                for (j = 0; j < 4; j++) {
                    float *bl = E.m.df + off[j], *br = E.m.df + off[j + 4];
                    uint32_t wl_ = E.dw[j], wr_ = E.dw[j + 4];
                    float vl = bl[wl_], vr = br[wr_], ul = dl + vl * gdf, ur = dr + vr * gdf;
                    bl[wl_] = ul;
                    br[wr_] = ur;
                    dl = vl - ul * gdf;
                    dr = vr - ur * gdf;
                    E.dw[j] = wl_ + 1u >= DL[j] ? 0u : wl_ + 1u;
                    E.dw[j + 4] = wr_ + 1u >= DL[j + 4] ? 0u : wr_ + 1u;
                }
            }
            if (crush > 0.0f && !E.freeze) {
                dl = (float)(int)(dl * crush) / crush;
                dr = (float)(int)(dr * crush) / crush;
            }
            inl = E.freeze ? 0.0f : gl[i] + (X_l ? X_l[i] * send : 0.0f);
            inr = E.freeze ? 0.0f : gr[i] + (X_r ? X_r[i] * send : 0.0f);
            if (cross) {
                wl = (inl + inr) * 0.5f + feed * dr;
                wr2 = feed * dl;
            } else {
                wl = inl + feed * dl;
                wr2 = inr + feed * dr;
            }
            wl = wl > 1.0f ? 1.0f + softc(wl - 1.0f) * 0.2f : wl < -1.0f ? -1.0f + softc(wl + 1.0f) * 0.2f : wl;
            wr2 = wr2 > 1.0f ? 1.0f + softc(wr2 - 1.0f) * 0.2f : wr2 < -1.0f ? -1.0f + softc(wr2 + 1.0f) * 0.2f : wr2;
            E.m.ec[0][E.ew] = (int16_t)clampf(wl * 16000.0f, -32000.0f, 32000.0f);
            E.m.ec[1][E.ew] = (int16_t)clampf(wr2 * 16000.0f, -32000.0f, 32000.0f);
            E.ew = (E.ew + 1u) & (FK_EC_N - 1u);
            gl[i] = gl[i] * (1.0f - 0.6f * ecm) + dl * ecm * 1.1f;
            gr[i] = gr[i] * (1.0f - 0.6f * ecm) + dr * ecm * 1.1f;
            if (send > 0.0f && ecm < 0.3f) {
                gl[i] += dl * send * 0.5f;
                gr[i] += dr * send * 0.5f;
            }
        }
        E.elp[0] = fm_flush(E.elp[0]);
        E.elp[1] = fm_flush(E.elp[1]);
    }
    X_duck = 0;
    X_l = X_r = 0;
    /* der Ausgang: Gleichanteil weg, Breite, Pegel, weich begrenzt */
    lg = fm_exp2f((-30.0f + 36.0f * E.sm[P_VOLUME]) * 0.1660964f) * 0.9f;
    wd = E.sm[P_WIDTH] * 1.2f;
    E.duck_g += ((E.duck ? 0.0f : 1.0f) - E.duck_g) * 0.2f;
    if (E.duck_g < 0.001f)
        E.duck_g = 0.0f;
    for (i = 0; i < n; i++) {
        float a = gl[i], b = gr[i], m, s;
        E.dcy[0] = a - E.dcx[0] + 0.9975f * E.dcy[0];
        E.dcx[0] = a;
        E.dcy[1] = b - E.dcx[1] + 0.9975f * E.dcy[1];
        E.dcx[1] = b;
        m = (E.dcy[0] + E.dcy[1]) * 0.5f;
        s = (E.dcy[0] - E.dcy[1]) * 0.5f * wd;
        {   /* OMEN: die Lautstärke am Ausgang (mit dem Echo), über den Block gleitend; beide Seiten gleich */
            float ov = E.om_vol0 + (E.om_vol - E.om_vol0) * ((float)i / (float)n);
            a = softc((m + s) * lg) * E.duck_g * ov;
            b = softc((m - s) * lg) * E.duck_g * ov;
        }
        l[i] = a;
        r[i] = b;
        u = absf(a) > absf(b) ? absf(a) : absf(b);
        if (absf(a) > E.mtr[0])
            E.mtr[0] = absf(a);
        if (absf(b) > E.mtr[1])
            E.mtr[1] = absf(b);
        if (u > 0.97f)
            E.clip = 1;
        E.lvl += (u - E.lvl) * (u > E.lvl ? 0.01f : 0.0004f);
    }
    E.om_vol0 = E.om_vol;
    E.dcy[0] = fm_flush(E.dcy[0]);
    E.dcy[1] = fm_flush(E.dcy[1]);
    E.env = fm_flush(E.env);
#undef B
}

/* ---- Anzeige ---- */
float fk_meter(int c)
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
int fk_cur_algo(void) { return E.algo_next >= 0 ? E.algo_next : E.algo; }

/* GARGOYLE: der GROOVE (dieselbe Übersetzungseinheit); seine kleinen Helfer */
static inline float fk_clamp(float x, float lo, float hi) { return clampf(x, lo, hi); }
static inline float fk_abs(float x) { return absf(x); }
static inline uint32_t fk_rng(uint32_t *s) { return rng_u(s); }
static inline float fk_rand01(uint32_t *s) { return rng_f(s); }
static inline float fk_lp_coef(float hz) { float w = TAU * hz / FK_SR; return w / (1.0f + w); }
#include "garg_groove.c"
