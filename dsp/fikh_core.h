/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S: der Klangkern. Ein experimenteller Drone-/Textur-Synth nach dem Konzept des Forge TME VHIKK X:
 * ein Generator (Oszillatoren, Cluster, Wavetables, Partials, Kammfilter) läuft durch ein Filter (FIELD) und einen
 * VCA in einen Prozessor (Delay mit Frequenzverschieber in der Rückkopplung, "Dungeon" metallisch oder
 * "Reflekta" diffus). Eigene Algorithmen: kein Code, kein Klang und keine Grafik von Forge TME.
 *
 * Die Bedienelemente wie am Modul:
 *   WARP   Kreuzmodulation (links eine Art, rechts eine andere; Mitte neutral)
 *   SPAN   Verstimmung / Spreizung der Oszillatoren (Mitte neutral; rechts dazu Stereo)
 *   MORPH  Klangfarbe / Wellenform des Generators durchfahren
 *   FUSE   je Algorithmus (links meist Chorus/Diffusor, rechts Sättigung/Resonanz)
 *   SEED   würfelt Verstimmungen, Wellen-Versätze, Delay-Abgriffe (jede Stufe ein neuer Wurf)
 *   SCAN   durchfährt zufällige Klangfarben (je Algorithmus)
 *   BASIS  die Grundfrequenz (die Tasten)
 *   TIME   die Länge der Delays
 *   MIX    trocken / nass
 *   FEED   Rückkopplung (links: verschobenes Signal zurück, rechts: mit dem rohen gemischt)
 *   FORM   vom klaren Echo zum diffusen Hall
 *   FIELD  Filter (links Tiefpass, rechts Hochpass, Mitte offen)
 *   CELL   Frequenzverschiebung (Algorithmus 9: Tonhöhenverschiebung)
 * Danach ein Ambient-Hall (fikh_verb.c) nach dem Vorbild des Walrus Audio SLÖER: DREAM, DARK, RISE, LIGHT, RAIN.
 * Die Engine rechnet mit float bei 44,1 kHz, Stereo. */
#pragma once
#include <stdint.h>

#define FK_SR 44100.0f
#define FK_NALGO 22                      /* BANK 1 und 2 zu neun, BANK 3 vier */
#define FK_BANK 9
#define FK_BLOCK_MAX 256

/* die Parameter, 0..127; die bipolaren mit der Mitte 64 */
enum {
    P_WARP, P_SPAN, P_MORPH, P_FUSE,     /* HOME */
    P_MIX, P_FEED, P_FORM, P_TIME,       /* FX */
    P_FIELD, P_CELL, P_ATK, P_REL,       /* FIELD */
    P_GLIDE, P_DRIFT, P_WIDTH, P_LEVEL,  /* EDIT */
    P_SEED, P_SCAN,                      /* SELECT, ALGORITHM */
    FK_NPA,                              /* bis hier: je Algorithmus; danach der Hall, für alle gleich */
    P_VMIX = FK_NPA, P_VDECAY, P_VX, P_VFILT,   /* VERB */
    P_VMODE, P_VDEPTH, P_VRATE, P_VSTRETCH,     /* VERB 2 */
    FK_NP
};
#define FK_NPV (FK_NP - FK_NPA)
extern const uint8_t FK_VERB_INIT[FK_NPV];
extern const char *const FK_VMODE_NAME[5];
int fk_vmode_of(int v);                  /* der Modus des Halls aus dem Wert von P_VMODE */

typedef struct {
    const char *name;                    /* wie auf dem Panel */
    uint8_t bip;                         /* 1: Mitte neutral */
} fk_param_info;
extern const fk_param_info FK_PARAMS[FK_NP];

/* die Algorithmen: Name, Untertitel, Prozessor */
enum { PROC_DUNGEON, PROC_REFLEKTA, PROC_DISPERSOR };
typedef struct {
    const char *name;                    /* kurz, groß */
    const char *sub;                     /* was er ist */
    uint8_t proc;
    uint8_t init[FK_NPA];                /* wie er anfängt */
} fk_algo_info;
extern const fk_algo_info FK_ALGOS[FK_NALGO];

/* der Speicher der großen Puffer (die App legt sie in den RAM-Pool) */
#define FK_DL_N 32768u                   /* Delay je Kanal (int16): 0,74 s */
#define FK_CH_N 1024u                    /* Chorus je Kanal (float) */
#define FK_AP_N 4900u                    /* die Allpässe (float): 12 Ringe */
#define FK_CB_N 2048u                    /* je Kamm (float), 4 Kämme */
#define FK_WT_LEN 256u
#define FK_WT_WAVES 8u
#define FK_WT_SETS 4u
#define FK_SIN_N 1024u                   /* die Sinustabelle (float, +1) */
#define FK_RV_N 16560u                   /* der Hall: acht Leitungen (int16) */
#define FK_RVA_N 2288u                   /* seine Allpässe (float) */
#define FK_SH_N 2048u                    /* der Oktavierer (int16) */
typedef struct {
    int16_t *dl[2];
    float *ch[2];
    float *ap;
    float *comb[4];
    float *wt;                           /* FK_WT_SETS * FK_WT_WAVES * FK_WT_LEN */
    float *sn;                           /* FK_SIN_N + 1 */
    int16_t *rv;                         /* FK_RV_N */
    float *rva;                          /* FK_RVA_N */
    int16_t *sh;                         /* FK_SH_N */
} fk_mem;

void fk_init(const fk_mem *m);
void fk_algo(int a);                     /* der Algorithmus 0..8 (mit einer kurzen Blende) */
void fk_params(const uint8_t p[FK_NP]);  /* die Parameter (aus der Hauptschleife; die Engine glättet sie) */
void fk_note_on(int note, int vel);      /* eine Taste: die Tonhöhe (BASIS), mono, letzte Note gewinnt */
void fk_note_off(int note);
void fk_hold(int on);                    /* HOLD: der VCA bleibt offen (ein Drone) */
void fk_all_off(void);
void fk_panic(void);
void fk_bend(float semis);
void fk_freeze(int on);                  /* FREEZE: der Prozessor hält, was er hat, für immer; nichts Neues hinein */
int fk_frozen(void);
void fk_eco(int on);                     /* ECO: weniger Oszillatoren, wenn die CPU nicht reicht */
int fk_eco_on(void);
void fk_duck(int on);                    /* vor einem Flash-Schreibzugriff: leise blenden */
int fk_ducked(void);
void fk_render(float *l, float *r, int n);
/* GARGOYLE: von außen hinein (für den nächsten fk_render, je Sample): duck dämpft den Klang vor dem Hall (die ATEM-Pumpe
 * des GROOVE), xl/xr gehen mit send (0..1) in den Hall (FERNE). NULL: nichts. */
void fk_ext(const float *duck, const float *xl, const float *xr, float send);

/* für den Bildschirm und die Tests */
int fk_note(void);                       /* die klingende Note (-1 keine) */
float fk_pitch(void);                    /* die Tonhöhe, wie sie gerade (gleitend) steht, MIDI */
int fk_gate(void);
float fk_env(void);                      /* der VCA 0..1 */
float fk_level(void);                    /* der Ausgang, geglättet */
float fk_wet_level(void);
float fk_meter(int c);                   /* VOL: der Spitzenpegel des Kanals (0..1, fällt ab) */
int fk_clipped(void);                    /* VOL: übersteuert seit dem letzten Aufruf (1) */                /* der Prozessor, geglättet */
int fk_cur_algo(void);
uint32_t fk_seed_hash(void);
const char *fk_value_text(int p, int v, char *t);   /* der Wert eines Parameters, wie er angezeigt wird */
