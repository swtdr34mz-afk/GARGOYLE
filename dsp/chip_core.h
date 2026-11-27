/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE – der Klangkern: ein 8-Bit-Drone-Synth. Sechzehn "Algorithmen" aus den Klangchips der alten Heimcomputer
 * und DOS-Spiele: Pulswellen und der Rausch-Generator des NES, die 4-Bit-Wellen des Game Boy, die Arpeggio-Akkorde
 * des C64 und sein Ringmodulator, die schmutzigen Polynom-Zähler des Atari POKEY, die FM der AdLib-Karte, der 1-Bit-
 * Piepser des PC. Danach ein Filter (TORCH), die Hüllkurve, Bits und Abtastrate wie damals (BITS, AGE) und ein Echo,
 * das in 8 Bit speichert (jede Wiederholung wird körniger). Kein Hall.
 *
 * Die Regler (wie bei FIKH-S, eigene Namen):
 *   HOME  CURSE (Kreuzmodulation: links Ring, rechts Sync/FM), HORDE (die Stimmen: links Oktaven, rechts Verstimmung),
 *         RUNE (Wellenform, Pulsbreite, Tabelle), ALCHEMY (je Algorithmus)
 *   FX    ECHO (trocken/nass), HAUNT (Rückkopplung: links Ping-Pong, rechts gerade), DECAY (die Wiederholungen dumpfer
 *         und körniger), DEPTH (die Zeit)
 *   ENV   TORCH (Filter: links Tiefpass, rechts Hochpass), PORTAL (Tonhöhe ±24 Halbtöne), AWAKEN, SLUMBER (Hüllkurve)
 *   EDIT  CREEP (Gleiten), WANDER (Drift), BITS (16 .. 1 Bit), VOLUME
 *   LFO   AGE (die Abtastrate, bis 2 kHz), VIBRATO, SPEED (Vibrato, Arpeggios, Takt der Muster), WIDTH (für alle gleich)
 *   FATE  (REC) würfelt: Tabellen, Verstimmungen, Muster       QUEST (ALGORITHM) fährt die Varianten durch
 * Float, 44,1 kHz, Stereo. Die Namen fk_ stammen vom Vorgänger (FIKH-S): dieselbe Schnittstelle. */
#pragma once
#include <stdint.h>

#define FK_SR 44100.0f
#define FK_NALGO 16
#define FK_BANK 8
#define FK_BLOCK_MAX 256

enum {
    P_CURSE, P_HORDE, P_RUNE, P_ALCHEMY,     /* HOME */
    P_ECHO, P_HAUNT, P_DECAY, P_DEPTH,       /* FX */
    P_TORCH, P_PORTAL, P_AWAKEN, P_SLUMBER,  /* ENV */
    P_CREEP, P_WANDER, P_BITS, P_VOLUME,     /* EDIT */
    P_FATE, P_QUEST,                         /* REC, ALGORITHM */
    FK_NPA,                                  /* bis hier: je Algorithmus; danach für alle gleich */
    P_AGE = FK_NPA, P_VIBRATO, P_SPEED, P_WIDTH,   /* LFO */
    FK_NP
};
#define FK_NPG (FK_NP - FK_NPA)
extern const uint8_t FK_GLOBAL_INIT[FK_NPG];

typedef struct {
    const char *name;
    uint8_t bip;                             /* 1: Mitte neutral */
} fk_param_info;
extern const fk_param_info FK_PARAMS[FK_NP];

typedef struct {
    const char *name;                        /* kurz, groß */
    const char *sub;                         /* woher es kommt */
    const char *alch;                        /* was ALCHEMY hier tut */
    uint8_t init[FK_NPA];
} fk_algo_info;
extern const fk_algo_info FK_ALGOS[FK_NALGO];

/* der Speicher: das Echo, 8 Bit je Sample (wie damals) */
#define FK_EC_N 32768u                       /* je Kanal: 0,74 s */
typedef struct {
    int8_t *ec[2];
} fk_mem;

void fk_init(const fk_mem *m);
void fk_algo(int a);
void fk_params(const uint8_t p[FK_NP]);
void fk_note_on(int note, int vel);
void fk_note_off(int note);
void fk_hold(int on);
void fk_all_off(void);
void fk_panic(void);
void fk_bend(float semis);
void fk_freeze(int on);                      /* FREEZE: das Echo hält, was es hat */
int fk_frozen(void);
void fk_eco(int on);
int fk_eco_on(void);
void fk_duck(int on);
int fk_ducked(void);
void fk_render(float *l, float *r, int n);
void fk_ext(const float *duck, const float *xl, const float *xr, float send);   /* der GROOVE: Pumpe, ins Echo */

int fk_note(void);
float fk_pitch(void);
int fk_gate(void);
float fk_env(void);
float fk_level(void);
float fk_meter(int c);
int fk_clipped(void);
int fk_cur_algo(void);
const char *fk_value_text(int p, int v, char *t);
