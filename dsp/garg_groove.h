/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE – GROOVE: keine Drummachine, sondern Geräusche aus dem Dungeon rund um den Drone. Zehn Styles (Herzschlag,
 * Schritte, Tropfen, Ketten, Flügel, Ritual, Glocke, Knochen, Grollen, Fackel), alles hier synthetisiert, auf den
 * Grundton gestimmt, mit Zufall (damit es nicht maschinell klingt) und einer weichen Pumpe (ATEM): der Drone atmet im
 * Takt mit. Läuft frei (TEMPO) oder mit der MIDI-Clock. Teil von fikh_core.c (eine Übersetzungseinheit). */
#pragma once
#include <stdint.h>

#define GR_NSTYLES 10
/* die Regler 0..127: GROOVE-Seite und SETUP-Seite */
enum { GR_DENS, GR_BREATH, GR_DIST, GR_TEMPO,    /* GROOVE: DENSITY, BREATH, DISTANCE, TEMPO */
       GR_LEVEL, GR_HUMAN, GR_FOLLOW, GR_TUNE,   /* SETUP: LEVEL, HUMAN, FOLLOW, TUNE */
       GR_NP };
extern const char *const GR_STYLE_NAME[GR_NSTYLES];
extern const char *const GR_PARAM_NAME[GR_NP];
extern const uint8_t GR_INIT[GR_NP];

typedef struct {
    uint8_t on, style;
    uint8_t p[GR_NP];
} gr_set;

void gr_init(void);
void gr_settings(const gr_set *s);       /* aus der Hauptschleife (gesperrt): die Einstellungen */
void gr_clock(void);                     /* MIDI 0xF8 (im Audio-Interrupt) */
void gr_start(void);                     /* MIDI 0xFA: von vorn */
void gr_stop(void);                      /* MIDI 0xFC: anhalten, bis Start oder der GROOVE neu an */
/* ein Block: trocken (mit FERNE gefiltert) in dl/dr, das Signal für den Hall in wl/wr, die Pumpe in duck (1: nichts) */
void gr_render(float *dl, float *dr, float *wl, float *wr, float *duck, int n, float root_midi);
float gr_send(void);                     /* wie viel in den Hall (0..1) */

/* für den Bildschirm und die Tests */
int gr_running(void);
int gr_step(void);                       /* 0..15, -1 steht */
uint32_t gr_hits(void);                  /* zählt jeden Schlag */
float gr_env(void);                      /* der Ausgang des GROOVE, geglättet (0..1) */
float gr_pump(void);                     /* wie tief der Drone gerade weggedrückt ist (0..1) */
int gr_synced(void);                     /* 1: die MIDI-Clock führt */
int gr_bpm(int v);                       /* TEMPO als BPM */
