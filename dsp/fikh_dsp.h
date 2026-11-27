/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: die kleinen Bausteine (inline): Zufall, Sättigung, Oszillatoren, Filter, Allpass. */
#pragma once
#include <stdint.h>
#include "fastmath.h"

#define FK_PI 3.14159265f
#define FK_TAU 6.2831853f

static inline float fk_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float fk_abs(float x) { return x < 0.0f ? -x : x; }
static inline float fk_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* ein schneller, deterministischer Zufall (xorshift32) */
static inline uint32_t fk_rng(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static inline float fk_rand01(uint32_t *s) { return (float)(fk_rng(s) >> 8) * (1.0f / 16777216.0f); }
static inline float fk_rand11(uint32_t *s) { return fk_rand01(s) * 2.0f - 1.0f; }
static inline uint32_t fk_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* weiche Sättigung: tanh-artig, ohne Knick, |y| < 1 */
static inline float fk_sat(float x)
{
    float a;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    a = x * x;
    return x * (27.0f + a) / (27.0f + 9.0f * a);
}
/* weich begrenzt erst oberhalb von 0,8 (jede Verstärkerstufe, wie am Modul: darunter unverändert) */
static inline float fk_soft(float x)
{
    if (x > 0.8f)
        return 0.8f + fk_sat((x - 0.8f) * 2.5f) * 0.4f;
    if (x < -0.8f)
        return -0.8f - fk_sat((-x - 0.8f) * 2.5f) * 0.4f;
    return x;
}
/* ein Falter (Wavefolder): bleibt in -1..1 und faltet zurück */
static inline float fk_fold(float x)
{
    x = (x + 1.0f) * 0.25f;
    x -= fm_floorf(x);
    x = x * 4.0f - 1.0f;
    return x > 1.0f ? 2.0f - x : x;
}

/* sin(2 pi ph), ph in Umdrehungen (beliebig): aus der Tabelle (fk_sin_tab), linear interpoliert (~ -84 dB) */
extern float *fk_sin_tab;
static inline float fk_sin(float ph)
{
    float x = ph - fm_floorf(ph), f = x * (float)FK_SIN_N, fr;
    int i = (int)f;
    fr = f - (float)i;
    i &= (int)FK_SIN_N - 1;
    return fk_sin_tab[i] + (fk_sin_tab[i + 1] - fk_sin_tab[i]) * fr;
}

static inline float fk_wrap(float ph) { return ph - fm_floorf(ph); }

/* PolyBLEP: die Kante einer Sägezahn-/Rechteckwelle geglättet (wenig Aliasing) */
static inline float fk_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
/* ein analoger Oszillator: m 0 Puls, 0.5 Säge, 1 Sinus (stufenlos) */
static inline float fk_vosc(float ph, float dt, float m)
{
    float saw = 2.0f * ph - 1.0f - fk_blep(ph, dt), y;
    if (m < 0.5f) {
        float p2 = ph + 0.5f, pul;
        p2 -= p2 >= 1.0f ? 1.0f : 0.0f;
        pul = (ph < 0.5f ? 1.0f : -1.0f) + fk_blep(ph, dt) - fk_blep(p2, dt);
        y = fk_lerp(pul * 0.8f, saw, m * 2.0f);
    } else {
        y = fk_lerp(saw, fk_sin(ph), (m - 0.5f) * 2.0f);
    }
    return y;
}

/* das Zustandsvariablen-Filter (Zavalishins TPT-Form): Tief-, Band-, Hochpass auf einmal */
typedef struct { float ic1, ic2; } fk_svf;
typedef struct { float g, k, a1, a2, a3; } fk_svf_c;
static inline void fk_svf_set(fk_svf_c *c, float hz, float res)
{
    float w = fk_clamp(hz, 15.0f, 19000.0f) * (FK_PI / FK_SR), g;
    /* tan(w) für w bis ~1.35: eine Näherung, genau genug (Padé) */
    float w2 = w * w;
    g = w * (1.0f + w2 * (-0.1111111f + w2 * 0.0009524f)) / (1.0f + w2 * (-0.4444444f + w2 * 0.0158730f));
    if (g < 0.0f || g > 40.0f)
        g = 40.0f;
    c->g = g;
    c->k = 2.0f - 1.96f * fk_clamp(res, 0.0f, 1.0f);
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
static inline void fk_svf_run(fk_svf *s, const fk_svf_c *c, float v0, float *lp, float *bp, float *hp)
{
    float v3 = v0 - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    *hp = v0 - c->k * v1 - v2;
}

/* ein Pol: Tiefpass mit dem Koeffizienten a (0..1) */
static inline float fk_lp1(float *z, float x, float a) { return *z += (x - *z) * a; }
static inline float fk_lp_coef(float hz) { float w = FK_TAU * hz / FK_SR; return w / (1.0f + w); }

/* ein Schroeder-Allpass in einem Ring: Länge n, Gewinn g */
static inline float fk_ap(float *buf, uint32_t n, uint32_t *w, float x, float g)
{
    float d = buf[*w], y = d - g * x;
    buf[*w] = fm_flush(x + g * y);
    if (++*w >= n)
        *w = 0;
    return y;
}

/* ein Allpass erster Ordnung (Phaser) */
static inline float fk_ap1(float *z, float x, float a)
{
    float y = a * x + *z;
    *z = x - a * y;
    return y;
}
