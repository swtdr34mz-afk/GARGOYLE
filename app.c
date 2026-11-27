/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE: Bedienung, Seiten, Speicher, Bildschirm. Teil des Unity-Builds (device/unity.c, host/plat_host.c), nach
 * der Plattform: gfx.c und announce.c sind sichtbar. Der Klang ist dsp/fikh_core.c (der Kern von FIKH-S) und
 * dsp/garg_groove.c (der GROOVE).
 *
 * Die Idee: der Klang von FIKH-S, aber live spielbar. Statt sechs Seiten mit abstrakten Reglern vier Makros, die in
 * jedem Raum gleich heißen und gleich wirken; hinter jedem bewegen sich mehrere Parameter auf abgestimmten Kurven.
 * Die Mitte (64) ist der Klang ab Werk (der Sweetspot): dort rastet jeder Makro ein.
 *
 *   Tasten     spielen (mono, die letzte Taste gewinnt), mit Anschlag; OCT- / OCT+ die Oktave
 *   HOME       PLAY:   TONE (dunkel - hell), MOVE (still - brodelnd), GRIT (sauber - kaputt), SPACE (trocken - endlos)
 *   HOME 2x    GROOVE: DENSITY, BREATH (die Pumpe), DISTANCE (nah - fern), TEMPO
 *   HOME 3x    SETUP:  LEVEL, HUMAN (Zufall), FOLLOW (dem Grundton folgen), TUNE
 *   EDIT       DEEP:   COLOR (MORPH), GLIDE, WIDTH, LEVEL
 *   FX         FX:     MODE (DREAM, DARK, RISE, LIGHT, RAIN), X (je Modus), ECHO (die Delay-Zeit), AGE (STRETCH)
 *   SELECT     WRATH: ein Makro über allem, für live (der Drache erwacht), 4 je Raste
 *   PRESET     der Raum (22 Räume = die 22 Algorithmen von FIKH-S)
 *   ALGORITHM  SCAN (grob, für live); auf den GROOVE-Seiten: der Style
 *   PLAY       HOLD: der Drone bleibt stehen, die Tasten ändern nur noch den Grundton
 *   REC        WÜRFELN (SEED und SCAN neu; 1 s: dazu COLOR und ECHO); auf den GROOVE-Seiten: der GROOVE an / aus
 *   ARP        FREEZE: Delay und Hall halten, was sie haben
 *   SEQ        1 s halten: der Raum zurück auf den Werksklang
 *   GLO        VOL: die Pegel im Bild
 *   SAVE       speichern (sonst von selbst, wenn es still ist); SAVE + eine der ersten 8 weißen Tasten: ein Snapshot,
 *              SEL + die Taste: zurückgeholt (die Makros gleiten hinüber)
 *   MIDI       Noten (alle Kanäle), Pitch Bend ±2, CC 1 COLOR, CC 70..73 TONE MOVE GRIT SPACE, CC 75 WRATH, Clock / Start / Stop
 *              für den GROOVE */
#include "fastmath.h"
#include "fikh_core.h"
#include "garg_groove.h"
#include "ui_serif.h"

#define SR 44100.0f

/* ------------------------------------------------------- der Klang-Speicher --- */
static int16_t mem_dl[2][FK_DL_N] __attribute__((section(".pool")));
static float mem_ch[2][FK_CH_N] __attribute__((section(".pool")));
static float mem_ap[FK_AP_N] __attribute__((section(".pool")));
static float mem_cb[4][FK_CB_N] __attribute__((section(".pool")));
static float mem_wt[FK_WT_SETS * FK_WT_WAVES * FK_WT_LEN] __attribute__((section(".pool")));
static float mem_sn[FK_SIN_N + 1] __attribute__((section(".pool")));
static int16_t mem_rv[FK_RV_N] __attribute__((section(".pool")));
static float mem_rva[FK_RVA_N] __attribute__((section(".pool")));
static int16_t mem_sh[FK_SH_N] __attribute__((section(".pool")));

/* ------------------------------------------------------------- die Räume --- */
/* jeder Algorithmus von FIKH-S ist ein Raum im Dungeon */
static const char *const ROOM_NAME[FK_NALGO] = {
    "CRYPT", "FORGE", "CRYSTAL CAVE", "WATCHTOWER", "CHAPEL", "THRONE HALL", "BELFRY", "THE WELL", "MIST HALL",
    "THE VOID", "ECHO SHAFT", "DUNGEON", "ROOST", "ORACLE", "STORM GATE", "MASONRY", "ABYSS", "GREAT HALL",
    "RUNE VAULT", "LABYRINTH", "TREASURY", "GHOST HALL"};

/* ------------------------------------------------------------- Speicher --- */
#define SET_MAGIC 0x31475247u            /* "GRG1" */
#define NSNAP 8
enum { M_TONE, M_MOVE, M_GRIT, M_SPACE };
enum { D_COLOR, D_GLIDE, D_WIDTH, D_LEVEL };
enum { X_MODE, X_X, X_ECHO, X_AGE };
typedef struct {
    uint8_t used, algo, oct, rsv;
    uint8_t m[4], d[4], ss[2], fx[4], rsv2[2];
} snap_t;
typedef struct {
    uint32_t magic;
    uint8_t algo, oct, hold, snap;       /* snap: der zuletzt geholte Snapshot (0: keiner, 1..8) */
    uint8_t m[FK_NALGO][4];              /* die Makros je Raum (64: der Werksklang) */
    uint8_t d[FK_NALGO][4];              /* DEEP je Raum */
    uint8_t ss[FK_NALGO][2];             /* SEED, SCAN je Raum */
    uint8_t fx[4];                       /* FX: für alle Räume gleich */
    gr_set grv;                          /* der GROOVE: global, ein Raumwechsel ändert den Beat nicht */
    uint8_t wrath, rsv;                  /* WRATH (SELECT): global, über allem */
    snap_t snaps[NSNAP];
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "die Einstellungen passen nicht in ein Speicherobjekt");
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static void room_defaults(int a)
{
    const uint8_t *in = FK_ALGOS[a].init;
    int i;
    for (i = 0; i < 4; i++)
        set.m[a][i] = 64;
    set.d[a][D_COLOR] = in[P_MORPH];
    set.d[a][D_GLIDE] = in[P_GLIDE];
    set.d[a][D_WIDTH] = in[P_WIDTH];
    set.d[a][D_LEVEL] = in[P_LEVEL];
    set.ss[a][0] = in[P_SEED];
    set.ss[a][1] = in[P_SCAN];
}

static void settings_defaults(void)
{
    int a;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.oct = 2;                         /* 0..4 = -2..+2: die Tasten F3..G5 */
    set.hold = 1;                        /* ein Drone-Synth: HOLD ist an */
    for (a = 0; a < FK_NALGO; a++)
        room_defaults(a);
    set.fx[X_MODE] = FK_VERB_INIT[P_VMODE - FK_NPA];
    set.fx[X_X] = FK_VERB_INIT[P_VX - FK_NPA];
    set.fx[X_ECHO] = 64;
    set.fx[X_AGE] = FK_VERB_INIT[P_VSTRETCH - FK_NPA];
    for (a = 0; a < GR_NP; a++)
        set.grv.p[a] = GR_INIT[a];
}

static void settings_load(void)
{
    int a, i, got = plat_store_load(OBJ_SETTINGS, &set, sizeof set), ok;
    ok = got == (int)sizeof set && set.magic == SET_MAGIC && set.algo < FK_NALGO && set.oct <= 4u && set.hold <= 1u
         && set.snap <= NSNAP && set.grv.on <= 1u && set.grv.style < GR_NSTYLES && set.wrath <= 127u;
    for (i = 0; ok && i < NSNAP; i++)
        if (set.snaps[i].used > 1u || set.snaps[i].algo >= FK_NALGO || set.snaps[i].oct > 4u)
            ok = 0;
    for (a = 0; ok && a < FK_NALGO; a++)
        for (i = 0; i < 4; i++)
            if (set.m[a][i] > 127u || set.d[a][i] > 127u || (i < 2 && set.ss[a][i] > 127u))
                ok = 0;
    for (i = 0; ok && i < 4; i++)
        if (set.fx[i] > 127u)
            ok = 0;
    for (i = 0; ok && i < GR_NP; i++)
        if (set.grv.p[i] > 127u)
            ok = 0;
    if (!ok)
        settings_defaults();
}

static void dirty(void)
{
    set_dirty = 1;
    dirty_at = plat_ms();
}

/* ------------------------------------------ die Makros: was sie bewegen --- */
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* von der Mitte (m = 64: der Wert ab Werk, init) nach unten bis lo, nach oben bis hi */
static uint8_t mac(int init, int lo, int hi, int m)
{
    float t;
    int v;
    if (m < 64) {
        t = (float)(64 - m) / 64.0f;
        v = init + (int)((float)(lo - init) * t + (lo < init ? -0.5f : 0.5f));
    } else {
        t = (float)(m - 64) / 63.0f;
        v = init + (int)((float)(hi - init) * t + (hi < init ? -0.5f : 0.5f));
    }
    return (uint8_t)iclamp(v, 0, 127);
}

/* alle Parameter der Engine aus dem Raum, seinen Makros, DEEP und FX */
static void build(int a, const uint8_t *m, const uint8_t *d, const uint8_t *ss, const uint8_t *fx, uint8_t *o)
{
    const uint8_t *in = FK_ALGOS[a].init, *vi = FK_VERB_INIT;
    int T = m[M_TONE], M = m[M_MOVE], G = m[M_GRIT], S = m[M_SPACE], w;
    memcpy(o, in, FK_NPA);
    memcpy(o + FK_NPA, vi, FK_NPV);
#define VI(p) vi[(p) - FK_NPA]
    /* TONE: das Filter zu (links) oder weit auf (rechts), der Hall dunkler / heller */
    o[P_FIELD] = mac(in[P_FIELD], 6, 86, T);
    o[P_VFILT] = mac(VI(P_VFILT), 6, 124, T);
    /* MOVE: Drift, Spreizung, das Schweben des Halls */
    o[P_DRIFT] = mac(in[P_DRIFT], 0, 127, M);
    o[P_SPAN] = mac(in[P_SPAN], 64, in[P_SPAN] >= 64 ? 127 : 0, M);
    o[P_VDEPTH] = mac(VI(P_VDEPTH), 0, 124, M);
    o[P_VRATE] = mac(VI(P_VRATE), 6, 100, M);
    /* GRIT: Kreuzmodulation weg von der Mitte, FUSE in die Sättigung, oben dazu die Frequenzverschiebung */
    w = in[P_WARP];
    o[P_WARP] = mac(w, 64, w < 64 ? 0 : 127, G);
    o[P_FUSE] = mac(in[P_FUSE], 64, 122, G);
    if (a != 8 && G > 96)                /* (GRAIN CLOUD: CELL ist die Tonhöhe, er bleibt) */
        o[P_CELL] = (uint8_t)iclamp(in[P_CELL] + (G - 96) * 22 / 31, 0, 127);
    o[P_FEED] = mac(in[P_FEED], in[P_FEED] - 10, in[P_FEED] >= 64 ? 112 : 16, G > 64 ? 64 + (G - 64) / 2 : G);
    /* SPACE: Hall, Delay, diffus; ganz rechts endlos */
    o[P_VMIX] = mac(VI(P_VMIX), 0, 118, S);
    o[P_VDECAY] = mac(VI(P_VDECAY), 28, 127, S);
    o[P_MIX] = mac(in[P_MIX], 10, 112, S);
    o[P_FORM] = mac(in[P_FORM], 30, 124, S);
    /* DEEP */
    o[P_MORPH] = d[D_COLOR];
    o[P_GLIDE] = d[D_GLIDE];
    o[P_WIDTH] = d[D_WIDTH];
    o[P_LEVEL] = d[D_LEVEL];
    o[P_SEED] = ss[0];
    o[P_SCAN] = ss[1];
    /* FX */
    o[P_VMODE] = fx[X_MODE];
    o[P_VX] = fx[X_X];
    o[P_TIME] = mac(in[P_TIME], 8, 124, fx[X_ECHO]);
    o[P_VSTRETCH] = fx[X_AGE];
#undef VI
}

/* WRATH (SELECT): ein Makro über allem, für live – der Drache erwacht: Kreuzmodulation und Sättigung bis zum Anschlag,
 * die Rückkopplung kocht, Metall in der Frequenzverschiebung, die Farbe wandert, der Hall wächst mit */
static uint8_t lerp8(int a, int b, float t) { return (uint8_t)iclamp(a + (int)((float)(b - a) * t + 0.5f), 0, 127); }
static void wrath(int a, int wv, uint8_t *o)
{
    float w = (float)wv / 127.0f;
    if (wv <= 0)
        return;
    o[P_WARP] = lerp8(o[P_WARP], o[P_WARP] >= 64 ? 127 : 0, w * 0.85f);
    o[P_FUSE] = lerp8(o[P_FUSE], 124, w * 0.8f);
    o[P_FEED] = lerp8(o[P_FEED], o[P_FEED] >= 64 ? 122 : 6, w * 0.7f);
    if (a != 8)
        o[P_CELL] = lerp8(o[P_CELL], 104, w * w * 0.7f);
    o[P_MORPH] = (uint8_t)iclamp(o[P_MORPH] + (int)(w * 36.0f), 0, 127);
    o[P_SPAN] = lerp8(o[P_SPAN], o[P_SPAN] >= 64 ? 127 : 0, w * 0.5f);
    o[P_DRIFT] = lerp8(o[P_DRIFT], 127, w * 0.6f);
    if (o[P_VMIX] < (uint8_t)(w * 100.0f))
        o[P_VMIX] = (uint8_t)(w * 100.0f);
    if (o[P_VDECAY] < (uint8_t)(60.0f + w * 60.0f))
        o[P_VDECAY] = (uint8_t)(60.0f + w * 60.0f);
}

static void all_params(uint8_t *o)
{
    build(set.algo, set.m[set.algo], set.d[set.algo], set.ss[set.algo], set.fx, o);
    wrath(set.algo, set.wrath, o);
}
static void push_params(void)
{
    uint8_t o[FK_NP];
    uint32_t f;
    all_params(o);
    f = plat_lock();
    fk_params(o);
    plat_unlock(f);
}
static void push_groove(void)
{
    uint32_t f = plat_lock();
    gr_settings(&set.grv);
    plat_unlock(f);
}
static void e_on(int note, int vel)
{
    uint32_t f = plat_lock();
    fk_note_on(note, vel);
    plat_unlock(f);
}
static void e_off(int note)
{
    uint32_t f = plat_lock();
    fk_note_off(note);
    plat_unlock(f);
}

/* ------------------------------------------------------------------ Seiten --- */
enum { PG_PLAY, PG_GROOVE, PG_SETUP, PG_DEEP, PG_FX, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PLAY", "GROOVE", "GROOVE SETUP", "DEEP", "FX"};
/* was ein Regler ist: eine Quelle (Makro, GROOVE, DEEP, FX) und ihr Platz */
enum { SRC_M, SRC_G, SRC_D, SRC_X };
static const uint8_t PAGE_SRC[NPAGES] = {SRC_M, SRC_G, SRC_G, SRC_D, SRC_X};
static const uint8_t PAGE_IDX[NPAGES][4] = {
    {M_TONE, M_MOVE, M_GRIT, M_SPACE},
    {GR_DENS, GR_BREATH, GR_DIST, GR_TEMPO},
    {GR_LEVEL, GR_HUMAN, GR_FOLLOW, GR_TUNE},
    {D_COLOR, D_GLIDE, D_WIDTH, D_LEVEL},
    {X_MODE, X_X, X_ECHO, X_AGE},
};
static const char *const MAC_NAME[4] = {"TONE", "MOVE", "GRIT", "SPACE"};
static const char *const DEEP_NAME[4] = {"COLOR", "GLIDE", "WIDTH", "LEVEL"};
static const char *const X_NAME[5] = {"VIBRATO", "OCT DOWN", "SWELL", "SHIMMER", "DIFFUSE"};   /* X je Modus */
static const char *const MAC_RANGE[4][2] = {{"DARK", "BRIGHT"}, {"STILL", "CHURN"}, {"CLEAN", "BROKEN"},
                                            {"DRY", "ENDLESS"}};

static uint8_t *kval(int pg, int k)
{
    int i = PAGE_IDX[pg][k];
    switch (PAGE_SRC[pg]) {
    case SRC_M: return &set.m[set.algo][i];
    case SRC_G: return &set.grv.p[i];
    case SRC_D: return &set.d[set.algo][i];
    default: return &set.fx[i];
    }
}
static const char *kname(int pg, int k)
{
    switch (PAGE_SRC[pg]) {
    case SRC_M: return MAC_NAME[k];
    case SRC_G: return GR_PARAM_NAME[PAGE_IDX[pg][k]];
    case SRC_D: return DEEP_NAME[k];
    default:
        if (k == X_X)
            return X_NAME[fk_vmode_of(set.fx[X_MODE])];
        return k == X_MODE ? "MODE" : k == X_ECHO ? "ECHO" : "AGE";
    }
}
/* der Sweetspot: die Mitte rastet ein (die Makros, ECHO) */
static int ksweet(int pg, int k) { return PAGE_SRC[pg] == SRC_M || (PAGE_SRC[pg] == SRC_X && k == X_ECHO); }

/* was neu gezeichnet werden muss: Bereiche des Bildschirms */
enum { R_ROW0 = 1, R_ROW1 = 2, R_ROW2 = 4, R_ROW3 = 8, R_HEAD = 16, R_MAP = 32, R_READ = 64, R_FOOT = 128,
       R_ALL = 0x7FFF };

static struct {
    uint8_t page, intro;
    uint32_t intro_at, frame;
    uint32_t keys, btn;
    int8_t key_note[NKEYS];
    uint32_t knob_at[4];                 /* wann jeder Regler zuletzt gedreht wurde (er leuchtet) */
    uint8_t rest[4];                     /* Rasten in der Mitte (Sweetspot) */
    uint8_t snap_used;                   /* SEL / SAVE gehalten: eine Taste wurde benutzt */
    uint8_t vol;                         /* VOL: die Pegel im Bild */
    uint32_t clip_until;
    float mt[2];
    int read_k;                          /* der Regler in der Anzeige (-1: keiner) */
    uint32_t read_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t rec_t, seq_t;               /* REC / SEQ gedrückt seit (0: nicht, oder schon gehandelt) */
    uint32_t dice_at;
    uint32_t trans_at;                   /* ein Raumwechsel: der Blick wird kurz dunkel */
    uint32_t wrath_until;                /* WRATH wurde gedreht: die Anzeige */
    uint32_t redraw;
    int batt_drawn;
    uint32_t load_at;
    uint8_t hot;
    uint8_t saving;                      /* 1: leise geblendet, um zu speichern */
    uint32_t save_at;
    uint8_t batt_test;
    uint32_t hits_seen, beat_at;         /* GROOVE: der letzte Schlag (die HOME-LED blitzt, das Herz oben) */
} ui;

static volatile uint8_t midi_touched;    /* ein CC aus dem Audio-Interrupt: neu zeichnen, speichern */

static float fabsf_(float x) { return x < 0.0f ? -x : x; }
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    while (*b)
        b++;
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* der Wert eines Reglers, wie er angezeigt wird */
static const char *kvalue_text(int pg, int k, char *t)
{
    int v = *kval(pg, k);
    uint8_t o[FK_NP];
    t[0] = 0;
    switch (PAGE_SRC[pg]) {
    case SRC_M:                          /* von der Mitte aus: 0 ist der Werksklang */
        if (v > 64)
            text_cat(t, "+");
        int_text(v - 64, t);
        break;
    case SRC_G:
        switch (PAGE_IDX[pg][k]) {
        case GR_TEMPO:
            int_text(gr_bpm(v), t);
            text_cat(t, " BPM");
            break;
        case GR_FOLLOW: text_cat(t, v >= 64 ? "ON" : "OFF"); break;
        case GR_TUNE:
            if (v > 64)
                text_cat(t, "+");
            int_text(v - 64, t);
            text_cat(t, " ST");
            break;
        default:
            int_text((v * 100 + 63) / 127, t);
            text_cat(t, "%");
            break;
        }
        break;
    case SRC_D:
        all_params(o);
        return fk_value_text(k == D_COLOR ? P_MORPH : k == D_GLIDE ? P_GLIDE : k == D_WIDTH ? P_WIDTH : P_LEVEL,
                             o[k == D_COLOR ? P_MORPH : k == D_GLIDE ? P_GLIDE : k == D_WIDTH ? P_WIDTH : P_LEVEL], t);
    default:
        if (k == X_MODE)
            return FK_VMODE_NAME[fk_vmode_of(v)];
        if (k == X_ECHO) {
            all_params(o);
            return fk_value_text(P_TIME, o[P_TIME], t);
        }
        int_text((v * 100 + 63) / 127, t);
        text_cat(t, "%");
        break;
    }
    return t;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.read_until = 0;
    ui.redraw |= R_READ;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_knob(int k)
{
    ui.read_k = k;
    ui.read_until = plat_ms() + 1500u;
    ui.msg_until = 0;
    ui.redraw |= R_READ;
    if (ann_on()) {
        char s[48] = "", t[24];
        text_cat(s, kname(ui.page, k));
        text_cat(s, " ");
        text_cat(s, kvalue_text(ui.page, k, t));
        ann_say(s, 0, ANN_NOW);
    }
}

static int key_base(void) { return 53 + 12 * ((int)set.oct - 2); }

/* Beschleunigung: schnell gedreht, größere Schritte */
static int32_t accel(int enc, int32_t e, int32_t slow, int32_t fast)
{
    static uint32_t last[NE];
    uint32_t now = plat_ms(), dt = now - last[enc];
    last[enc] = now;
    return e * (dt < 45u ? fast : slow);
}

/* grob: 2,5 pro Raste (abwechselnd 2 und 3), schnell gedreht 7,5: jede Raste hört man (wie BA-55) */
static int coarse(int k, int32_t e)
{
    static int rem[NE];
    int n = (int)accel(k, e, 1, 3) * 5 + rem[k], q = n / 2;
    rem[k] = n - q * 2;
    return q;
}

static void knob_changed(int k)
{
    if (PAGE_SRC[ui.page] == SRC_G)
        push_groove();
    else
        push_params();
    dirty();
    say_knob(k);
    if (PAGE_SRC[ui.page] == SRC_X && k == X_MODE)
        ui.redraw |= R_ROW1;             /* X heißt je Modus anders */
}

static void knob_turn(int k, int32_t e)
{
    uint8_t *p = kval(ui.page, k);
    int v = *p, nv, src = PAGE_SRC[ui.page], idx = PAGE_IDX[ui.page][k];
    ui.knob_at[k] = plat_ms();
    ui.redraw |= (uint32_t)R_ROW0 << k;
    if (src == SRC_X && idx == X_MODE) {          /* MODE: eine Raste, ein Modus */
        int m = iclamp(fk_vmode_of(v) + (e > 0 ? 1 : -1), 0, 4);
        nv = m * 128 / 5 + 12;
    } else if (src == SRC_G && idx == GR_FOLLOW) {   /* FOLLOW: an / aus */
        nv = e > 0 ? 127 : 0;
    } else if (src == SRC_G && idx == GR_TUNE) {     /* TUNE: Halbtöne, ±12 */
        nv = iclamp(v + (e > 0 ? 1 : -1), 52, 76);
    } else if (src == SRC_G && idx == GR_TEMPO) {    /* TEMPO: fein, schnell gedreht schneller */
        nv = v + (int)accel(EN_K1 + k, e, 1, 4);
    } else if (src == SRC_M || src == SRC_G || (src == SRC_X && idx == X_ECHO)) {
        int step = coarse(EN_K1 + k, e);
        if (ksweet(ui.page, k) && v == 64 && ++ui.rest[k] < 3) {   /* der Sweetspot: zwei Rasten halten */
            say_knob(k);
            return;
        }
        if (ksweet(ui.page, k) && ((v < 64 && v + step >= 64) || (v > 64 && v + step <= 64))) {
            nv = 64;
            ui.rest[k] = 0;
        } else {
            nv = v + step;
            ui.rest[k] = 0;
        }
    } else {
        nv = v + (int)accel(EN_K1 + k, e, 1, 4);
    }
    nv = iclamp(nv, 0, 127);
    if (nv == v) {
        say_knob(k);
        return;
    }
    *p = (uint8_t)nv;
    knob_changed(k);
}

static void page_set(int pg)
{
    int i;
    for (i = 0; i < 4; i++) {
        ui.knob_at[i] = 0;
        ui.rest[i] = 0;
    }
    ui.page = (uint8_t)pg;
    ui.read_until = 0;
    ui.redraw |= R_ALL;
    app_say(PAGE_NAME[pg], pg == PG_GROOVE || pg == PG_SETUP ? (set.grv.on ? GR_STYLE_NAME[set.grv.style] : "OFF")
                                                              : "");
}

static void algo_set(int a)
{
    uint32_t f;
    uint8_t o[FK_NP];
    set.algo = (uint8_t)((a + FK_NALGO) % FK_NALGO);
    all_params(o);
    f = plat_lock();
    fk_algo(set.algo);
    fk_params(o);
    plat_unlock(f);
    dirty();
    ui.trans_at = plat_ms();
    ui.redraw |= R_ALL;
    app_say(ROOM_NAME[set.algo], FK_ALGOS[set.algo].name);
}

/* ---- die Snapshots: ein ganzer Klang auf einer Taste ---- */
static void snap_store(int s)
{
    snap_t *n = &set.snaps[s];
    char t[8] = "SNAP ";
    n->used = 1;
    n->algo = set.algo;
    n->oct = set.oct;
    memcpy(n->m, set.m[set.algo], 4);
    memcpy(n->d, set.d[set.algo], 4);
    memcpy(n->ss, set.ss[set.algo], 2);
    memcpy(n->fx, set.fx, 4);
    set.snap = (uint8_t)(s + 1);
    dirty();
    t[5] = (char)('1' + s);
    t[6] = 0;
    app_say(t, "STORED");
}

static void snap_load(int s)
{
    snap_t *n = &set.snaps[s];
    uint8_t o[FK_NP];
    uint32_t f;
    char t[8] = "SNAP ";
    int same;
    t[5] = (char)('1' + s);
    t[6] = 0;
    if (!n->used) {
        app_say(t, "EMPTY");
        return;
    }
    same = n->algo == set.algo;
    set.algo = n->algo;
    set.oct = n->oct;
    memcpy(set.m[set.algo], n->m, 4);
    memcpy(set.d[set.algo], n->d, 4);
    memcpy(set.ss[set.algo], n->ss, 2);
    memcpy(set.fx, n->fx, 4);
    set.snap = (uint8_t)(s + 1);
    all_params(o);
    f = plat_lock();
    if (!same)
        fk_algo(set.algo);               /* (derselbe Raum: die Makros gleiten hinüber) */
    fk_params(o);
    plat_unlock(f);
    dirty();
    ui.redraw |= R_ALL;
    app_say(t, ROOM_NAME[set.algo]);
}

/* WÜRFELN: SEED und SCAN neu (deep: dazu COLOR und ECHO); der Zufall aus der Zeit */
static void dice(int deep)
{
    static uint32_t r = 0x6A09E667u;
    r ^= plat_ms() * 2654435761u;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    set.ss[set.algo][0] = (uint8_t)(r % 128u);
    set.ss[set.algo][1] = (uint8_t)((r >> 8) % 128u);
    if (deep) {
        set.d[set.algo][D_COLOR] = (uint8_t)((r >> 15) % 128u);
        set.fx[X_ECHO] = (uint8_t)(24u + (r >> 23) % 80u);
    }
    push_params();
    dirty();
    ui.dice_at = plat_ms();
    ui.redraw |= R_ALL;
    app_say(deep ? "DEEP DICE" : "DICE", "");
}

static void factory(void)
{
    room_defaults(set.algo);
    push_params();
    dirty();
    ui.redraw |= R_ALL;
    app_say("INIT", ROOM_NAME[set.algo]);
}

static void hold_set(int on)
{
    uint32_t f = plat_lock();
    set.hold = (uint8_t)on;
    fk_hold(on);
    plat_unlock(f);
    dirty();
    ui.redraw |= R_FOOT;
    app_say("HOLD", on ? "ON" : "OFF");
}

static void groove_toggle(void)
{
    set.grv.on = (uint8_t)!set.grv.on;
    push_groove();
    dirty();
    ui.redraw |= R_ALL;
    app_say("GROOVE", set.grv.on ? GR_STYLE_NAME[set.grv.style] : "OFF");
}

static void style_turn(int32_t e)
{
    set.grv.style = (uint8_t)((set.grv.style + GR_NSTYLES + (e > 0 ? 1 : -1)) % GR_NSTYLES);
    push_groove();
    dirty();
    ui.redraw |= R_MAP | R_HEAD;
    app_say("STYLE", GR_STYLE_NAME[set.grv.style]);
}

static void save_now(void)               /* leise blenden, dann schreiben (app_frame), dann wieder auf */
{
    if (ui.saving)
        return;
    ui.saving = 1;
    ui.save_at = plat_ms();
    fk_duck(1);
}

static const uint8_t SNAP_KEY[NSNAP] = {0, 2, 4, 6, 7, 9, 11, 12};   /* die ersten acht weißen Tasten */
static int snap_of_key(uint32_t k)
{
    int i;
    for (i = 0; i < NSNAP; i++)
        if (SNAP_KEY[i] == k)
            return i;
    return -1;
}
static int snap_mode(void)               /* 1: SEL gehalten (laden), 2: SAVE gehalten (ablegen), 0: spielen */
{
    uint32_t b = plat_buttons();
    return (b >> B_SAVE & 1u) ? 2 : (b >> B_SEL & 1u) ? 1 : 0;
}

static void keys_input(uint32_t keys)
{
    uint32_t k;
    int sm = snap_mode();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if (sm && (keys & bit) && !(ui.keys & bit)) {   /* SEL / SAVE gehalten: die Taste ist ein Snapshot */
            int s = snap_of_key(k);
            ui.snap_used = 1;
            if (s >= 0) {
                if (sm == 2)
                    snap_store(s);
                else
                    snap_load(s);
            }
            ui.key_note[k] = -1;
            continue;
        }
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = iclamp(key_base() + (int)k, 0, 127);
            ui.key_note[k] = (int8_t)note;
            e_on(note, (int)touch_vel(k));
            ui.redraw |= R_HEAD;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.key_note[k] >= 0)
                e_off(ui.key_note[k]);
            ui.key_note[k] = -1;
        }
    }
}

static int groove_page(void) { return ui.page == PG_GROOVE || ui.page == PG_SETUP; }

static void button(uint32_t i)
{
    switch (i) {
    case B_HOME:                         /* PLAY, GROOVE, GROOVE SETUP (wie bei BA-55) */
        page_set(ui.page == PG_PLAY ? PG_GROOVE : ui.page == PG_GROOVE ? PG_SETUP : PG_PLAY);
        break;
    case B_EDIT: page_set(PG_DEEP); break;
    case B_FX: page_set(PG_FX); break;
    case B_ENV:
    case B_LFO: app_say("HOME EDIT FX", "HOME 2X: GROOVE"); break;
    case B_PLAY: hold_set(!set.hold); break;
    case B_REC: ui.rec_t = plat_ms(); break;
    case B_SEQ: ui.seq_t = plat_ms(); break;
    case B_ARP: {
        uint32_t f = plat_lock();
        int on = !fk_frozen();
        fk_freeze(on);
        plat_unlock(f);
        ui.redraw |= R_FOOT | R_MAP;
        app_say("FREEZE", on ? "ON" : "OFF");
        break;
    }
    case B_SAVE:                         /* SAVE: losgelassen ohne Taste: speichern; mit Taste: ein Snapshot */
    case B_SEL:
        ui.snap_used = 0;
        ui.redraw |= R_READ;
        break;
    case B_OCTDN:
    case B_OCTUP: {
        char t[8] = "";
        set.oct = (uint8_t)iclamp((int)set.oct + (i == B_OCTUP ? 1 : -1), 0, 4);
        dirty();
        if (set.oct > 2)
            text_cat(t, "+");
        int_text((int)set.oct - 2, t);
        app_say("OCTAVE", t);
        ui.redraw |= R_FOOT;
        break;
    }
    case B_GLO:                          /* VOL: die Pegel im Bild */
        ui.vol = (uint8_t)!ui.vol;
        ui.redraw |= R_MAP;
        app_say("VOL", ui.vol ? "OUT L / R" : "OFF");
        break;
    default: break;
    }
}

static void held_buttons(uint32_t btn)
{
    uint32_t now = plat_ms();
    if (!(btn >> B_SAVE & 1u) && (ui.btn >> B_SAVE & 1u)) {   /* SAVE losgelassen */
        if (!ui.snap_used)
            save_now();
        ui.redraw |= R_READ;
    }
    if (!(btn >> B_SEL & 1u) && (ui.btn >> B_SEL & 1u)) {
        if (!ui.snap_used)
            app_say("SNAPSHOTS", "SEL/SAVE + KEY");
        ui.redraw |= R_READ;
    }
    if (ui.rec_t) {
        if (groove_page()) {             /* auf den GROOVE-Seiten: der GROOVE an / aus */
            groove_toggle();
            ui.rec_t = 0;
        } else if (!(btn >> B_REC & 1u)) {
            dice(0);
            ui.rec_t = 0;
        } else if (now - ui.rec_t >= 1000u) {
            dice(1);
            ui.rec_t = 0;
        }
    }
    if (ui.seq_t) {
        if (!(btn >> B_SEQ & 1u)) {
            app_say("SEQ", "HOLD 1 S: INIT");
            ui.seq_t = 0;
        } else if (now - ui.seq_t >= 1000u) {
            factory();
            ui.seq_t = 0;
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    keys_input(keys);
    for (i = 0; i < NB; i++)
        if ((btn >> i & 1u) && !(ui.btn >> i & 1u))
            button(i);
    held_buttons(btn);
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob_turn((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* WRATH: grob, jede Raste hört man */
        int v = iclamp((int)set.wrath + (int)accel(EN_SELECT, e, 4, 10), 0, 127);
        if (v != set.wrath) {
            set.wrath = (uint8_t)v;
            push_params();
            dirty();
        }
        ui.wrath_until = plat_ms() + 1500u;
        ui.msg_until = 0;
        ui.redraw |= R_READ | R_HEAD | R_MAP;
    }
    if ((e = plat_enc(EN_PRESET)) != 0)          /* der Raum */
        algo_set((int)set.algo + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (groove_page()) {                     /* auf den GROOVE-Seiten: der Style */
            style_turn(e);
        } else {                                 /* SCAN: grob, für live */
            int v = iclamp((int)set.ss[set.algo][1] + coarse(EN_ALGO, e), 0, 127);
            if (v != set.ss[set.algo][1]) {
                char t[8] = "";
                set.ss[set.algo][1] = (uint8_t)v;
                push_params();
                dirty();
                int_text(v, t);
                app_say("SCAN", t);
            }
        }
    }
    if (btn != ui.btn)
        ui.redraw |= R_FOOT;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

static void lights(void)
{
    uint32_t now = plat_ms(), btn = 0, dimb = (1u << NB) - 1u, lit = 0, k;
    btn |= 1u << (ui.page == PG_DEEP ? B_EDIT : ui.page == PG_FX ? B_FX : B_HOME);
    if (set.grv.on && gr_running() && now - ui.beat_at < 90u)   /* GROOVE: HOME blitzt mit jedem Schlag */
        btn ^= 1u << B_HOME;
    if (set.hold)
        btn |= LED_PLAY_GREEN;
    if (fk_frozen())
        btn |= 1u << B_ARP;
    if (set_dirty)
        btn |= 1u << B_SAVE;
    if (ui.vol)
        btn |= 1u << B_GLO;
    if (now - ui.dice_at < 160u || (groove_page() && set.grv.on))
        btn |= 1u << B_REC;
    if (set.oct != 2)
        btn |= 1u << (set.oct > 2 ? B_OCTUP : B_OCTDN);
    if (snap_mode()) {                   /* SEL / SAVE gehalten: die belegten Snapshot-Tasten, der letzte blinkt */
        uint32_t on = 0, i;
        for (i = 0; i < NSNAP; i++)
            if (set.snaps[i].used && !(set.snap == i + 1u && ((now / 200u) & 1u)))
                on |= 1u << SNAP_KEY[i];
        for (i = 0; i < NSNAP; i++)
            lit |= 1u << SNAP_KEY[i];
        plat_lights(btn | ui.btn, on, dimb & ~(1u << B_PLAY), lit, 1);
        return;
    }
    for (k = 0; k < NKEYS; k++)
        if ((key_base() + (int)k) % 12 == 0)
            lit |= 1u << k;
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), ui.keys, dimb & ~(1u << B_PLAY), lit, 1);
}

/* ============================================================== Zeichnen ===
 * Wie ein Dungeon-Crawler der frühen 90er: oben links der Blick in den Gang (vorgerendert: tools/gen_art.py, art.h),
 * am Ende im Dunkeln ein Drache: nur seine Augen und die Glut; die Fackeln flackern mit dem Klang. Darum ein
 * Rahmen aus blaugrauem Marmor mit erhabenen Feldern: rechts die Kuppel mit der Nummer des Raums (im GROOVE die 16
 * Schritte als Runen), darunter die Seite und HOLD / Oktave / Akku (drei Herzen); unten die vier Helden (die Regler,
 * jeder in seiner Farbe), ganz unten das Nachrichtenfeld. Gezeichnet wird nur, was sich ändert. */
#undef C_WHITE
#define C_WHITE RGB(238, 236, 228)
#include "ui_gfx.h"
#include "art.h"

#define GOLD rgb3(214, 168, 70)
#define GOLD_HI rgb3(250, 214, 120)
#define GOLD_DIM rgb3(120, 90, 40)
#define RED rgb3(220, 48, 36)
#define RED_DIM rgb3(70, 16, 12)
#define FIRE rgb3(255, 140, 40)
#define FACE rgb3(92, 98, 150)               /* die Felder: blau-violett, erhaben */
#define FACE_HI rgb3(164, 170, 220)
#define FACE_LO rgb3(34, 36, 64)
#define C_GOLD RGB(232, 186, 84)
#define C_GOLD_HI RGB(250, 222, 150)
#define C_GOLD_DIM RGB(150, 120, 70)
#define C_TXT RGB(244, 244, 236)
#define C_TXT_DIM RGB(150, 154, 186)
#define C_RED RGB(240, 80, 60)

static const col3 HERO[4] = {{226, 70, 52}, {236, 150, 44}, {96, 196, 96}, {96, 150, 240}};
static const char *const HERO_CLASS[4] = {"FIGHTER", "DWARF", "ELF", "MAGE"};
/* welcher Stein in welchem Raum: 0 rote Ziegel, 1 blauer Stein, 2 Moos, 3 Sandstein, 4 Gruft */
static const uint8_t ROOM_PAL[FK_NALGO] = {4, 0, 1, 3, 3, 0, 1, 2, 1, 4, 1, 2, 2, 4, 1, 3, 4, 0, 3, 2, 0, 4};

#define SCOPE_N 1024u
static float scope_ring[SCOPE_N];
static volatile uint32_t scope_w;

static int32_t g_ox;
static int32_t g_x0, g_y0, g_x1, g_y1;

static int vis(float x0, float y0, float x1, float y1)
{
    return x1 >= (float)g_x0 && x0 < (float)g_x1 && y1 >= (float)g_y0 && y0 < (float)g_y1;
}
#define SX(x) ((x) - (float)g_ox)

static void s_line(float x0, float y0, float x1, float y1, col3 c, float a)
{
    if (vis(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, (x0 > x1 ? x0 : x1) + 1.0f, (y0 > y1 ? y0 : y1) + 1.0f))
        aa_line(SX(x0), y0, SX(x1), y1, c, a);
}
static void s_disc(float x, float y, float r, col3 c, float a)
{
    if (vis(x - r - 1, y - r - 1, x + r + 1, y + r + 1))
        aa_disc(SX(x), y, r, c, a);
}
static void s_bloom(float x, float y, float r, col3 c, float a)
{
    if (vis(x - r, y - r, x + r, y + r))
        bloom(SX(x), y, r, c, a);
}
static void s_arc(float x, float y, float rm, float w, float a0, float a1, col3 c, float a, float glow)
{
    float R = rm + w + glow + 2.0f;
    if (vis(x - R, y - R, x + R, y + R))
        ring_arc(SX(x), y, rm, w, a0, a1, c, a, glow);
}
static void s_rect(float x, float y, float w, float h, col3 c, float a)
{
    int32_t i, j, x0, x1, y0, y1;
    if (!vis(x, y, x + w, y + h))
        return;
    x0 = (int32_t)x < g_x0 ? g_x0 : (int32_t)x;
    x1 = (int32_t)(x + w) > g_x1 ? g_x1 : (int32_t)(x + w);
    y0 = (int32_t)y < g_y0 ? g_y0 : (int32_t)y;
    y1 = (int32_t)(y + h) > g_y1 ? g_y1 : (int32_t)(y + h);
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            px_blend(i - g_ox, j, c, a);
}
/* Text: Pixelschrift mit hartem schwarzem Schatten, wie damals */
static void s_text(float x, float y, const felucca_font_t *f, const char *s, uint16_t c, int shadow)
{
    int32_t w = text_w(f, s);
    if (!vis(x - 2, y - 2, x + (float)w + 2, y + (float)f->h + 2))
        return;
    if (shadow)
        cv_text((int32_t)SX(x) + 1, (int32_t)y + 1, f, s, RGB(0, 0, 0));
    cv_text((int32_t)SX(x), (int32_t)y, f, s, c);
}
static void s_ctext(float cx, float y, const felucca_font_t *f, const char *s, uint16_t c, int sh)
{
    s_text(cx - (float)(text_w(f, s) / 2), y, f, s, c, sh);
}
static void s_rtext(float rx, float y, const felucca_font_t *f, const char *s, uint16_t c, int sh)
{
    s_text(rx - (float)text_w(f, s), y, f, s, c, sh);
}

static uint32_t hash_ui(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* der Grund: dunkler blaugrauer Marmor mit Schlieren (wie der Rahmen der alten Spiele) */
static void bg_fill(int32_t ry0, int32_t h)
{
    int32_t x, y;
    for (y = 0; y < h; y++) {
        int32_t sy = ry0 + y;
        for (x = 0; x < (int32_t)cv_w; x++) {
            int32_t sx = x + g_ox;
            uint32_t n = hash_ui((uint32_t)sx * 73856093u ^ (uint32_t)sy * 19349663u);
            float fx = (float)sx, fy = (float)sy, w, g;
            w = fm_sinf(fx * 0.045f + 2.4f * fm_sinf(fy * 0.031f + fx * 0.011f) + 1.3f * fm_sinf(fy * 0.083f - fx * 0.02f));
            w = w * w;
            g = 30.0f + 22.0f * w * w + (float)(n >> 29) * 1.2f;
            cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(RGB((int)(g * 0.80f), (int)(g * 0.84f), (int)(g * 1.22f)));
        }
    }
}

/* ein erhabenes Feld: Fläche, helle Kante oben links, dunkle unten rechts (2 px) */
static void bevel(float x0, float y0, float x1, float y1, col3 face, float fa, int sunk)
{
    col3 hi = sunk ? FACE_LO : FACE_HI, lo = sunk ? FACE_HI : FACE_LO;
    if (!vis(x0 - 1, y0 - 1, x1 + 1, y1 + 1))
        return;
    if (fa > 0.0f)
        s_rect(x0, y0, x1 - x0, y1 - y0, face, fa);
    s_rect(x0, y0, x1 - x0, 1.0f, hi, 1.0f);
    s_rect(x0, y0 + 1.0f, x1 - x0 - 1.0f, 1.0f, hi, 0.55f);
    s_rect(x0, y0, 1.0f, y1 - y0, hi, 1.0f);
    s_rect(x0 + 1.0f, y0 + 1.0f, 1.0f, y1 - y0 - 2.0f, hi, 0.55f);
    s_rect(x0, y1 - 1.0f, x1 - x0, 1.0f, lo, 1.0f);
    s_rect(x0 + 1.0f, y1 - 2.0f, x1 - x0 - 1.0f, 1.0f, lo, 0.6f);
    s_rect(x1 - 1.0f, y0, 1.0f, y1 - y0, lo, 1.0f);
    s_rect(x1 - 2.0f, y0 + 1.0f, 1.0f, y1 - y0 - 2.0f, lo, 0.6f);
}

/* ---- die Lage ---- */
#define VX0 6
#define VY0 6
#define VX1 (VX0 + ART_VW)
#define VY1 (VY0 + ART_VH)
#define RX0 176.0f
#define RX1 234.0f
#define PY0 120.0f
#define PH 39.0f
#define MY0 204.0f
static float panel_x(int k) { return (k & 1) ? 121.0f : 6.0f; }
static float panel_y(int k) { return PY0 + (float)(k >> 1) * (PH + 2.0f); }

/* ein Herz (Akku, GROOVE): fill 0..1 von links */
static void heart(float cx, float cy, float s, float fill, col3 on, col3 off)
{
    int32_t x, y, R = (int32_t)(s * 1.4f) + 2;
    float left = cx - s * 1.15f, wd = s * 2.3f;
    if (!vis(cx - (float)R, cy - (float)R, cx + (float)R, cy + (float)R))
        return;
    for (y = (int32_t)cy - R; y <= (int32_t)cy + R; y++)
        for (x = (int32_t)cx - R; x <= (int32_t)cx + R; x++) {
            int sx, sy, in = 0, edge = 0;
            float cov, fx;
            if (!px_ok(x - g_ox, y) || y < g_y0 || y >= g_y1)
                continue;
            for (sy = 0; sy < 3; sy++)
                for (sx = 0; sx < 3; sx++) {
                    float u = ((float)x + 0.17f + (float)sx * 0.33f - cx) / s;
                    float v = -((float)y + 0.17f + (float)sy * 0.33f - cy) / s * 1.15f + 0.3f;
                    float q = u * u + v * v - 1.0f, f = q * q * q - u * u * v * v * v;
                    float u2 = u * 0.8f, v2 = v * 0.8f + 0.06f;
                    float q2 = u2 * u2 + v2 * v2 - 1.0f, f2 = q2 * q2 * q2 - u2 * u2 * v2 * v2 * v2;
                    if (f <= 0.0f) {
                        in++;
                        if (f2 > 0.0f)
                            edge++;
                    }
                }
            if (!in)
                continue;
            cov = (float)in / 9.0f;
            fx = ((float)x + 0.5f - left) / wd;
            if (fx <= fill) {
                float t = sat01(((float)y - (cy - s)) / (2.0f * s));
                px_blend(x - g_ox, y, mix3(rgb3(255, 150, 130), on, t * 0.7f + 0.3f), cov);
            } else {
                px_blend(x - g_ox, y, rgb3(24, 8, 8), cov * 0.9f);
                if (edge)
                    px_blend(x - g_ox, y, off, (float)edge / 9.0f);
            }
        }
}

static int batt_level(void)
{
#ifdef PLAT_HOST
    if (ui.batt_test)
        return ui.batt_test - 2;
#endif
    return plat_battery();
}
static int batt_pct(int lvl)
{
    int32_t raw = plat_battery_raw();
    if (lvl >= 4)
        return 100;
#ifdef PLAT_HOST
    if (ui.batt_test)
        return lvl * 33;
#endif
    if (raw <= 0)
        return lvl * 33;
    raw = raw - 515;
    return raw < 0 ? 0 : raw > 100 ? 100 : (int)raw;
}

/* ---- der Gang ---- */
/* eine Fackel: die Flamme flackert, hell mit dem Klang (und jedem Schlag des GROOVE) */
static void torch_flame(float x, float y, float t, float lvl, float beat, int frozen)
{
    float fl = 0.78f + 0.14f * fm_sinf(t * 21.0f + x) + 0.08f * fm_sinf(t * 43.0f + x * 0.7f) + 0.25f * beat;
    col3 fc = frozen ? rgb3(110, 180, 255) : FIRE, core = frozen ? rgb3(225, 245, 255) : rgb3(255, 226, 140);
    s_bloom(x, y - 3.0f, 15.0f + 6.0f * lvl + 6.0f * beat, fc, (0.22f + 0.22f * lvl + 0.2f * beat) * fl);
    s_line(x - 2.5f, y + 3.0f, x + 2.5f, y + 3.0f, rgb3(40, 30, 22), 1.0f);    /* der Halter, Eisen */
    s_line(x, y + 3.0f, x, y + 10.0f, rgb3(46, 34, 26), 1.0f);
    s_line(x - 1.5f, y + 10.0f, x + 1.5f, y + 10.0f, rgb3(60, 46, 34), 1.0f);
    s_disc(x, y, 2.8f * fl, fc, 0.95f);
    s_disc(x + 0.5f * fm_sinf(t * 15.0f), y - 3.2f * fl, 1.9f * fl, fc, 0.85f);
    s_disc(x - 0.3f * fm_sinf(t * 23.0f), y - 5.6f * fl, 1.0f * fl, fc, 0.6f);
    s_disc(x, y + 0.4f, 1.3f, core, 0.95f);
}

/* der Drache im Dunkeln: man sieht nur seine Augen (Bernstein, ein senkrechter Schlitz), darunter die Glut in
 * seinem Rachen und Funken, die aufsteigen. open 0..1, glow 0..1, wr: WRATH (die Augen weiter auf, der Schlitz enger,
 * das Feuer stärker) */
static void dragon(float cx, float cy, float sc, float open, float glow, float wr, float t, int frozen)
{
    int s, i;
    col3 iris0 = frozen ? rgb3(160, 220, 255) : rgb3(255, 214, 90), iris1 = frozen ? rgb3(40, 90, 200) : rgb3(230, 70, 10);
    float fire = sat01(0.25f + 0.5f * glow + 0.6f * wr);
    if (!frozen) {                       /* die Glut im Rachen, unter den Augen */
        s_bloom(cx, cy + 16.0f * sc, (10.0f + 10.0f * fire) * sc, rgb3(255, 90, 20), 0.18f * fire * open);
        s_bloom(cx, cy + 15.0f * sc, (4.0f + 4.0f * fire) * sc, rgb3(255, 190, 80), 0.25f * fire * open);
        for (i = 0; i < 6; i++) {        /* Funken steigen auf */
            uint32_t h = (uint32_t)i * 2654435761u;
            float ph = t * (0.35f + 0.1f * (float)(h >> 28)) + (float)(h >> 20 & 255u) / 255.0f, u = ph - fm_floorf(ph);
            float ex = cx + ((float)(h >> 8 & 255u) / 255.0f - 0.5f) * 26.0f * sc + 3.0f * fm_sinf(t * 3.0f + (float)i);
            float ey = cy + (16.0f - 30.0f * u) * sc;
            s_disc(ex, ey, 0.55f * sc, rgb3(255, 170, 60), (1.0f - u) * fire * open);
        }
    }
    for (s = -1; s <= 1; s += 2) {
        float ex = cx + (float)s * 11.0f * sc, ey = cy, w = 5.5f * sc, h = (2.2f + 1.2f * wr) * sc * open, u;
        float tilt = (float)s * 0.22f;   /* schräg nach innen: böse */
        if (open <= 0.02f)
            continue;
        s_bloom(ex, ey, (6.0f + 4.0f * glow + 4.0f * wr) * sc, iris1, (0.22f + 0.25f * glow + 0.25f * wr) * open);
        for (u = -w; u <= w; u += 0.5f) {   /* das Auge: eine Mandel, der Rand dunkel, innen hell */
            float k = h * (1.0f - (u / w) * (u / w)), yy = ey + u * tilt, v;
            for (v = -k; v <= k; v += 0.5f) {
                float r = fm_sqrtf((u / w) * (u / w) + (v / (h + 0.01f)) * (v / (h + 0.01f)));
                col3 c = mix3(iris0, iris1, sat01(r * 1.1f));
                float pw = (0.45f - 0.3f * wr) * sc;            /* der Schlitz */
                if (u > -pw && u < pw && r < 0.95f)
                    c = rgb3(10, 4, 2);
                s_rect(ex + u, yy + v, 0.5f, 0.5f, c, 1.0f);
            }
        }
        s_disc(ex - 1.6f * sc, ey - 0.8f * sc * open, 0.45f * sc, rgb3(255, 250, 230), 0.8f * open);
        s_line(ex - w - 1.0f, ey - h - 1.5f * sc - (float)s * w * tilt, ex + w + 1.0f, ey - h - 0.5f * sc + (float)s * w * tilt,
               rgb3(0, 0, 0), 0.8f);     /* die Braue: ein Schatten darüber */
    }
}

/* der Blick: das vorgerenderte Bild, das Licht flackert mit, FREEZE macht es eisig, WRATH rot; beim Raumwechsel
 * kurz dunkel (ein Schritt in den nächsten Raum) */
static void view(uint32_t now)
{
    const uint16_t *src = ART_CORR[ROOM_PAL[set.algo]];
    float t = (float)now * 0.001f, lvl = sat01(fk_level() * 2.5f), env = fk_env();
    float beat = set.grv.on && gr_running() ? sat01(1.0f - (float)(now - ui.beat_at) / 180.0f) : 0.0f;
    float fl = 0.93f + 0.05f * fm_sinf(t * 17.0f) + 0.03f * fm_sinf(t * 31.0f) + 0.10f * lvl + 0.10f * beat;
    float wr = (float)set.wrath / 127.0f, tr = 1.0f;
    int frozen = fk_frozen(), i;
    int32_t x, y, xa, xb, ya, yb;
    if (!vis(VX0 - 3, VY0 - 3, VX1 + 3, VY1 + 3))
        return;
    if (now - ui.trans_at < 420u) {     /* ein Schritt: erst dunkel, dann wieder hell */
        float u = (float)(now - ui.trans_at) / 420.0f;
        tr = u < 0.35f ? 1.0f - u / 0.35f : (u - 0.35f) / 0.65f;
        tr = tr * tr;
    }
    xa = VX0 > g_x0 ? VX0 : g_x0;
    xb = VX1 < g_x1 ? VX1 : g_x1;
    ya = VY0 > g_y0 ? VY0 : g_y0;
    yb = VY1 < g_y1 ? VY1 : g_y1;
    {
        int32_t kq = (int32_t)(fl * tr * 256.0f), kr = (int32_t)((fl * tr) * (256.0f + 70.0f * wr)), kb = kq;
        if (frozen)
            kr = (int32_t)((float)kq * 0.75f);
        for (y = ya; y < yb; y++) {
            const uint16_t *row = src + (uint32_t)(y - VY0) * ART_VW;
            for (x = xa; x < xb; x++) {
                uint16_t c = row[x - VX0];
                int32_t r = (int32_t)(c >> 11), g = (int32_t)((c >> 5) & 63u), b = (int32_t)(c & 31u);
                if (frozen) {            /* eisig: zur Helligkeit hin, blau */
                    int32_t l = (r * 2 + g + b * 2) / 5;
                    r = (r + l) / 2;
                    g = (g + l * 2) / 2;
                    b = (b + l * 3) / 2;
                }
                r = (r * kr) >> 8;
                g = (g * (frozen ? kq : (int32_t)((float)kq * (1.0f - 0.25f * wr)))) >> 8;
                b = (b * (frozen ? (kb * 5) / 4 : (int32_t)((float)kb * (1.0f - 0.3f * wr)))) >> 8;
                r = r > 31 ? 31 : r;
                g = g > 63 ? 63 : g;
                b = b > 31 ? 31 : b;
                cv_px[(uint32_t)(y + cv_oy) * cv_w + (uint32_t)(x - g_ox)] = swap16((uint16_t)(r << 11 | g << 5 | b));
            }
        }
    }
    if (tr > 0.05f) {
        for (i = 0; i < 2; i++)
            torch_flame((float)VX0 + ART_TORCH[i][0], (float)VY0 + ART_TORCH[i][1] - 6.0f, t + (float)i * 1.7f,
                        lvl * tr, beat, frozen);
        dragon((float)VX0 + (float)ART_VW * 0.5f, (float)VY0 + 50.0f, 1.0f, tr,
               sat01(env * 0.6f + lvl) * 0.8f + 0.5f * beat, wr, t, frozen);
    }
    if (ui.vol) {                        /* VOL: zwei Säulen links und rechts im Bild */
        int c, clip = now < ui.clip_until;
        for (c = 0; c < 2; c++) {
            float x0 = c ? (float)VX1 - 10.0f : (float)VX0 + 4.0f, h = 80.0f * ui.mt[c];
            s_rect(x0, (float)VY0 + 14.0f, 6.0f, 80.0f, rgb3(10, 8, 12), 0.85f);
            s_rect(x0, (float)VY0 + 94.0f - h, 6.0f, h, clip ? rgb3(255, 60, 40) : rgb3(90, 200, 90), 0.95f);
        }
    }
    /* der Rahmen um den Blick: eingelassen */
    s_rect((float)VX0 - 2.0f, (float)VY0 - 2.0f, (float)ART_VW + 4.0f, 2.0f, FACE_LO, 1.0f);
    s_rect((float)VX0 - 2.0f, (float)VY0 - 2.0f, 2.0f, (float)ART_VH + 4.0f, FACE_LO, 1.0f);
    s_rect((float)VX0 - 2.0f, (float)VY1, (float)ART_VW + 4.0f, 2.0f, FACE_HI, 0.9f);
    s_rect((float)VX1, (float)VY0 - 2.0f, 2.0f, (float)ART_VH + 4.0f, FACE_HI, 0.9f);
}

/* ---- die Kuppel: schwarzer Marmor, goldener Rand; die Nummer des Raums, im GROOVE die 16 Schritte ---- */
static void dome(uint32_t now)
{
    float cx = (RX0 + RX1) * 0.5f, cy = 46.0f, r = 27.0f, wr = (float)set.wrath / 127.0f;
    int32_t x, y, i, st = gr_step();
    char t[8] = "";
    if (!vis(RX0 - 2, 6, RX1 + 2, 50))
        return;
    for (y = (int32_t)(cy - r - 2.0f); y < (int32_t)cy; y++)   /* die Kuppel: Marmor mit Glanz */
        for (x = (int32_t)(cx - r - 2.0f); x <= (int32_t)(cx + r + 2.0f); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy), cov, g;
            float sw = fm_sinf(dx * 0.35f + 3.0f * fm_sinf(dy * 0.22f + dx * 0.05f));
            if (d > r + 0.5f || y < g_y0 || y >= g_y1 || x < g_x0 || x >= g_x1)
                continue;
            cov = sat01(r - d + 0.5f);
            g = 10.0f + 14.0f * sw * sw + 30.0f * sat01(1.0f - fm_sqrtf((dx + 8.0f) * (dx + 8.0f) + (dy + 16.0f) * (dy + 16.0f)) / 14.0f);
            px_blend(x - g_ox, y, rgb3(g + 40.0f * wr, g, g * 1.15f), cov);
        }
    s_arc(cx, cy, r, 2.4f, -90.0f, 90.0f, GOLD, 1.0f, 0.0f);
    s_arc(cx, cy, r - 2.6f, 0.9f, -90.0f, 90.0f, GOLD_DIM, 0.9f, 0.0f);
    if (wr > 0.02f)
        s_arc(cx, cy, r, 2.0f, -90.0f, -90.0f + 180.0f * wr, RED, 0.9f, 2.5f);
    s_rect(cx - r - 3.0f, cy, 2.0f * r + 6.0f, 3.0f, GOLD, 1.0f);
    s_rect(cx - r - 3.0f, cy + 3.0f, 2.0f * r + 6.0f, 1.0f, GOLD_DIM, 1.0f);
    for (i = 0; i < 16; i++) {           /* die Runen am Rand: im GROOVE die Schritte */
        float a = (-84.0f + (float)i * 11.2f) * 0.0174533f, rx = cx + fm_sinf(a) * (r - 6.0f), ry = cy - fm_cosf(a) * (r - 6.0f);
        int cur = i == st && set.grv.on;
        if (ry > cy - 2.0f)
            continue;
        s_disc(rx, ry, cur ? 1.9f : 0.9f, cur ? FIRE : (i % 4 ? GOLD_DIM : GOLD), 1.0f);
        if (cur)
            s_bloom(rx, ry, 6.0f, FIRE, 0.6f);
    }
    if (ui.page == PG_GROOVE || ui.page == PG_SETUP) {
        float beat = set.grv.on && gr_running() ? sat01(1.0f - (float)(now - ui.beat_at) / 220.0f) : 0.0f;
        heart(cx, cy - 11.0f, 7.0f + 1.6f * beat, set.grv.on ? 1.0f : 0.0f, RED, GOLD_DIM);
        if (beat > 0.0f)
            s_bloom(cx, cy - 11.0f, 14.0f, RED, 0.35f * beat);
    } else {
        int_text(set.algo + 1, t);
        s_ctext(cx, cy - 20.0f, &FONT_SERIF_M, t, C_GOLD_HI, 1);
    }
}

/* ---- rechts: die Seite (ein Knopf wie CAMP), HOLD, FREEZE, die Oktave, drei Herzen ---- */
static void side(uint32_t now)
{
    float cx = (RX0 + RX1) * 0.5f;
    char t[12] = "";
    int lvl, i;
    if (vis(RX0, 52, RX1, 69)) {
        bevel(RX0, 52.0f, RX1, 68.0f, FACE, 1.0f, 0);
        s_ctext(cx, 54.0f, &FONT_SERIF_S, ui.page == PG_SETUP ? "SETUP" : PAGE_NAME[ui.page], C_GOLD, 1);
    }
    if (!vis(RX0, 71, RX1, 117))
        return;
    bevel(RX0, 72.0f, RX1, 116.0f, rgb3(14, 14, 22), 1.0f, 1);
    s_disc(RX0 + 8.0f, 81.0f, 2.4f, set.hold ? RED : RED_DIM, 1.0f);
    if (set.hold)
        s_bloom(RX0 + 8.0f, 81.0f, 7.0f, RED, 0.5f);
    s_text(RX0 + 14.0f, 75.0f, &FONT_XS, "HOLD", set.hold ? C_TXT : C_TXT_DIM, 1);
    if (fk_frozen() || fk_eco_on()) {
        s_text(RX0 + 5.0f, 87.0f, &FONT_XS, fk_frozen() ? "FREEZE" : "ECO", fk_frozen() ? RGB(150, 210, 255) : C_RED, 1);
    } else {
        text_cat(t, "OCT ");
        if (set.oct > 2)
            text_cat(t, "+");
        int_text((int)set.oct - 2, t);
        s_text(RX0 + 5.0f, 87.0f, &FONT_XS, t, set.oct != 2 ? C_GOLD_HI : C_TXT_DIM, 1);
    }
    lvl = batt_level();
    if (lvl >= 0) {                      /* der Akku: drei Herzen, stufenlos; leer blinken sie; an USB ein Blitz */
        int usb = lvl >= 4, pct = batt_pct(lvl);
        float f = (float)pct / 100.0f * 3.0f;
        if (!usb && pct < 8 && ((now / 500u) & 1u))
            f = 0.0f;
        for (i = 0; i < 3; i++)
            heart(cx - 13.0f + (float)i * 13.0f + (usb ? 4.0f : 0.0f), 106.0f, 5.0f, f - (float)i, RED, rgb3(110, 30, 24));
        if (usb) {
            float bx = RX0 + 3.0f, by = 101.0f;
            s_line(bx + 3.0f, by, bx + 0.5f, by + 5.0f, GOLD_HI, 0.95f);
            s_line(bx + 0.5f, by + 5.0f, bx + 4.0f, by + 5.0f, GOLD_HI, 0.95f);
            s_line(bx + 4.0f, by + 5.0f, bx + 1.5f, by + 10.0f, GOLD_HI, 0.95f);
        }
    }
}

/* ein kleines Bild je Held: Schwert, Axt, Bogen, Stab (Stahl auf seiner Farbe) */
static void hero_icon(int k, float x, float y, int hot)
{
    col3 st = rgb3(214, 218, 230), dk = rgb3(60, 44, 30), h = HERO[k];
    float cx = x + 15.0f, cy = y + 15.0f;
    s_rect(x, y, 30.0f, 30.0f, rgb3(16, 14, 22), 1.0f);
    s_bloom(cx, cy, 20.0f, h, hot ? 0.75f : 0.45f);
    switch (k) {
    case 0:                              /* das Schwert */
        s_line(cx - 7.0f, cy + 7.0f, cx + 7.0f, cy - 7.0f, st, 1.0f);
        s_line(cx - 6.0f, cy + 7.0f, cx + 8.0f, cy - 7.0f, rgb3(150, 156, 170), 1.0f);
        s_line(cx - 9.0f, cy + 3.0f, cx - 3.0f, cy + 9.0f, GOLD, 1.0f);
        s_line(cx - 7.5f, cy + 7.5f, cx - 10.0f, cy + 10.0f, dk, 1.0f);
        break;
    case 1:                              /* die Axt */
        s_line(cx - 6.0f, cy + 9.0f, cx + 4.0f, cy - 8.0f, dk, 1.0f);
        s_line(cx - 5.0f, cy + 9.0f, cx + 5.0f, cy - 8.0f, rgb3(110, 80, 50), 1.0f);
        s_disc(cx + 4.0f, cy - 5.0f, 5.0f, st, 1.0f);
        s_disc(cx + 1.0f, cy - 3.0f, 3.0f, rgb3(16, 14, 22), 1.0f);
        break;
    case 2:                              /* der Bogen */
        s_arc(cx + 4.0f, cy, 10.0f, 1.6f, 200.0f, 340.0f, rgb3(140, 100, 60), 1.0f, 0.0f);
        s_line(cx - 5.4f, cy - 3.5f, cx - 5.4f, cy + 3.5f, st, 0.0f);
        s_line(cx - 4.0f, cy - 9.0f, cx - 4.0f, cy + 9.0f, st, 0.8f);
        s_line(cx - 4.0f, cy, cx + 8.0f, cy, st, 1.0f);
        s_line(cx + 8.0f, cy, cx + 5.0f, cy - 2.5f, st, 1.0f);
        s_line(cx + 8.0f, cy, cx + 5.0f, cy + 2.5f, st, 1.0f);
        break;
    default:                             /* der Stab mit der Kugel */
        s_line(cx - 5.0f, cy + 10.0f, cx + 3.0f, cy - 5.0f, rgb3(120, 86, 52), 1.0f);
        s_bloom(cx + 4.0f, cy - 7.0f, 7.0f, rgb3(150, 200, 255), 0.7f);
        s_disc(cx + 4.0f, cy - 7.0f, 3.2f, rgb3(170, 220, 255), 1.0f);
        s_disc(cx + 3.0f, cy - 8.0f, 1.1f, rgb3(255, 255, 255), 0.9f);
        break;
    }
    s_rect(x, y, 30.0f, 1.0f, FACE_LO, 1.0f);
    s_rect(x, y, 1.0f, 30.0f, FACE_LO, 1.0f);
    s_rect(x, y + 29.0f, 30.0f, 1.0f, FACE_HI, 1.0f);
    s_rect(x + 29.0f, y, 1.0f, 30.0f, FACE_HI, 1.0f);
}

/* ein Held = ein Regler: Bild, Name, der Wert, der Balken in seiner Farbe (der helle Strich: der Sweetspot) */
static void panel(int k, uint32_t now)
{
    float x = panel_x(k), y = panel_y(k), bx0 = x + 40.0f, bx1 = x + 109.0f;
    int hot = ui.knob_at[k] && now - ui.knob_at[k] < 800u, v = *kval(ui.page, k);
    col3 h = HERO[k];
    char t[24];
    if (!vis(x, y, x + 113.0f, y + PH))
        return;
    bevel(x, y, x + 113.0f, y + PH, hot ? mix3(FACE, h, 0.18f) : FACE, 1.0f, 0);
    hero_icon(k, x + 5.0f, y + 4.5f, hot);
    s_text(bx0, y + 3.0f, &FONT_B, kname(ui.page, k), hot ? C_TXT : RGB(232, 234, 246), 1);
    s_rtext(bx1, y + 20.0f, &FONT_XS, kvalue_text(ui.page, k, t), hot ? C_GOLD_HI : C_GOLD, 1);
    s_rect(bx0, y + 31.0f, bx1 - bx0, 5.0f, rgb3(14, 12, 20), 1.0f);
    s_rect(bx0, y + 31.0f, (bx1 - bx0) * (float)v / 127.0f, 5.0f, h, 1.0f);
    s_rect(bx0, y + 31.0f, (bx1 - bx0) * (float)v / 127.0f, 1.0f, mix3(h, rgb3(255, 255, 255), 0.5f), 0.8f);
    s_rect(bx0, y + 35.0f, bx1 - bx0, 1.0f, FACE_HI, 0.6f);
    if (ksweet(ui.page, k)) {
        float sx = bx0 + (bx1 - bx0) * 64.0f / 127.0f;
        s_rect(sx, y + 29.0f, 1.0f, 8.0f, rgb3(250, 240, 210), v == 64 ? 1.0f : 0.55f);
    }
    if (!hot && PAGE_SRC[ui.page] != SRC_G)
        s_text(bx0, y + 20.0f, &FONT_XS, HERO_CLASS[k], C_TXT_DIM, 0);
    if (hot) {
        s_rect(x + 2.0f, y + 2.0f, 109.0f, 1.0f, h, 0.9f);
        s_rect(x + 2.0f, y + PH - 3.0f, 109.0f, 1.0f, h, 0.9f);
    }
}

/* ---- das Nachrichtenfeld: zwei Zeilen, weiß auf schwarz ---- */
static void message(uint32_t now)
{
    char l[48] = "", v[24];
    int sm = snap_mode(), wr = (int)set.wrath;
    if (!vis(6, MY0, 234, 235))
        return;
    bevel(6.0f, MY0, 234.0f, 235.0f, rgb3(4, 4, 8), 1.0f, 1);
    /* Zeile 1: der Raum (oder eine Meldung) */
    if (now < ui.msg_until) {
        s_text(11.0f, MY0 + 4.0f, &FONT_XS, ui.msg[0], C_GOLD_HI, 0);
        s_text(11.0f + (float)text_w(&FONT_XS, ui.msg[0]) + 6.0f, MY0 + 4.0f, &FONT_XS, ui.msg[1], C_TXT, 0);
    } else if (ui.page == PG_GROOVE || ui.page == PG_SETUP) {
        text_cat(l, "GROOVE ");
        text_cat(l, GR_STYLE_NAME[set.grv.style]);
        s_text(11.0f, MY0 + 4.0f, &FONT_XS, l, C_GOLD, 0);
        s_rtext(229.0f, MY0 + 4.0f, &FONT_XS, set.grv.on ? (gr_synced() ? "MIDI SYNC" : "ON") : "OFF - PRESS REC",
                set.grv.on ? RGB(120, 230, 120) : C_TXT_DIM, 0);
    } else {
        if (set.algo + 1 < 10)
            text_cat(l, "0");
        int_text(set.algo + 1, l);
        text_cat(l, " ");
        text_cat(l, ROOM_NAME[set.algo]);
        s_text(11.0f, MY0 + 4.0f, &FONT_XS, l, C_GOLD, 0);
        s_rtext(229.0f, MY0 + 4.0f, &FONT_XS, FK_ALGOS[set.algo].name, C_TXT_DIM, 0);
    }
    /* Zeile 2: der Regler, der gerade gedreht wird; Snapshots; WRATH */
    l[0] = 0;
    if (sm) {
        int i;
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, sm == 2 ? "STORE SNAPSHOT:" : "LOAD SNAPSHOT:", C_TXT, 0);
        for (i = 0; i < NSNAP; i++) {
            float x = 130.0f + (float)i * 12.0f;
            int cur = set.snap == (uint8_t)(i + 1), used = set.snaps[i].used;
            s_disc(x, MY0 + 23.0f, 2.6f, cur && used ? RED : used ? GOLD : rgb3(50, 50, 70), 1.0f);
            if (cur && used)
                s_bloom(x, MY0 + 23.0f, 6.0f, RED, 0.45f);
        }
        return;
    }
    if (now < ui.wrath_until) {
        text_cat(l, "WRATH ");
        int_text((wr * 100 + 63) / 127, l);
        text_cat(l, "%  ");
        text_cat(l, wr == 0 ? "IT SLEEPS" : wr < 40 ? "IT STIRS" : wr < 80 ? "IT AWAKENS" : wr < 120 ? "IT RAGES" : "FULL WRATH");
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, l, RGB(255, 110, 80), 0);
        return;
    }
    if (now < ui.read_until && ui.read_k >= 0) {
        int k = ui.read_k, val = *kval(ui.page, k);
        text_cat(l, kname(ui.page, k));
        text_cat(l, " ");
        text_cat(l, kvalue_text(ui.page, k, v));
        if (PAGE_SRC[ui.page] == SRC_M) {
            text_cat(l, "  ");
            text_cat(l, val == 64 ? "SWEETSPOT" : MAC_RANGE[k][val > 64]);
        }
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, l, c565(HERO[k]), 0);
        return;
    }
    if (set.wrath) {
        text_cat(l, "WRATH ");
        int_text((wr * 100 + 63) / 127, l);
        text_cat(l, "%");
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, l, RGB(220, 90, 70), 0);
    } else {
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, "SELECT: WRATH  PRESET: ROOM", C_TXT_DIM, 0);
    }
}

/* ---- die ganze Szene ---- */
static void scene(uint32_t now)
{
    int k;
    view(now);
    dome(now);
    side(now);
    for (k = 0; k < 4; k++)
        panel(k, now);
    message(now);
}

static void region(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t h = y1 - y0, w = x1 - x0;
    if (w <= 0 || h <= 0)
        return;
    if ((uint32_t)(w * h) > APP_CANVAS_PX) {
        int32_t step = (int32_t)(APP_CANVAS_PX / (uint32_t)w), y;
        for (y = y0; y < y1; y += step)
            region(x0, y, x1, y + step < y1 ? y + step : y1);
        return;
    }
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    g_ox = x0;
    g_x0 = x0;
    g_x1 = x1;
    g_y0 = y0;
    g_y1 = y1;
    cv_oy = -y0;
    bg_fill(y0, h);
    scene(plat_ms());
    cv_oy = 0;
    cv_blit((uint32_t)x0, (uint32_t)y0);
}

static void draw(uint32_t m)
{
    int k;
    if (m == R_ALL) {
        region(0, 0, 240, 120);
        region(0, 120, 240, 240);
        return;
    }
    if (m & R_MAP)
        region(VX0 - 2, VY0 - 2, VX1 + 2, VY1 + 2);
    if (m & R_HEAD)
        region((int32_t)RX0 - 2, 4, (int32_t)RX1 + 2, 70);
    if (m & R_FOOT)
        region((int32_t)RX0 - 1, 71, (int32_t)RX1 + 1, 117);
    for (k = 0; k < 4; k++)
        if (m & ((uint32_t)R_ROW0 << k))
            region((int32_t)panel_x(k), (int32_t)panel_y(k), (int32_t)panel_x(k) + 113, (int32_t)(panel_y(k) + PH));
    if (m & R_READ)
        region(6, (int32_t)MY0, 234, 236);
}

/* ---- die Eröffnung: schwarz; GARGOYLE erscheint; darüber öffnen sich die Augen eines Drachen; ein Feuerstoß
 * erhellt alles von unten; danach glühen die Augen weiter im Dunkeln ---- */
#define INTRO_MS 3800u
static void intro_scene(float t, int32_t y0, int32_t h)
{
    float txt = sat01((t - 0.25f) / 0.8f), eyes = sat01((t - 1.25f) / 0.3f), fl = 0.0f, lit = 0.0f;
    int32_t x, y;
    int i;
    g_ox = 0;
    g_x0 = 0;
    g_x1 = 240;
    g_y0 = y0;
    g_y1 = y0 + h;
    cv_oy = -y0;
    if (t >= 1.85f && t < 1.93f)          /* das Feuer: zweimal */
        fl = 1.0f;
    else if (t >= 1.93f && t < 2.0f)
        fl = 0.12f;
    else if (t >= 2.0f && t < 2.08f)
        fl = 0.7f;
    else if (t >= 2.08f && t < 2.5f)
        fl = 0.7f * (1.0f - (t - 2.08f) / 0.42f) * 0.35f;
    if (t >= 2.0f)                        /* danach: er bleibt im kalten Restlicht stehen */
        lit = 0.30f + 0.55f * sat01(1.0f - (t - 2.0f) / 0.9f);
    for (y = 0; y < h; y++) {
        float vy = (float)(y0 + y) / 240.0f;
        for (x = 0; x < 240; x++) {
            float vx = ((float)x - 120.0f) / 160.0f, g = fl * (0.25f + 0.75f * vy - 0.3f * vx * vx);
            g = g < 0.0f ? 0.0f : g;
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(RGB((int)(g * 255.0f), (int)(g * 120.0f), (int)(g * 30.0f)));
        }
    }
    dragon(120.0f, 82.0f, 3.2f, eyes, 0.6f + 0.4f * fm_sinf(t * 5.0f) * eyes + lit, sat01((t - 1.9f) * 2.0f) * 0.6f, t, 0);
    if (fl > 0.0f) {                     /* der Feuerstoß: von unten herauf, orange */
        for (i = 0; i < 3; i++)
            s_bloom(120.0f + (float)(i - 1) * 50.0f, 240.0f, 150.0f, rgb3(255, 110, 20), 0.35f * fl);
    }
    if (txt > 0.0f) {
        uint16_t c = c565(mix3(rgb3(0, 0, 0), GOLD_HI, txt));
        float dy = 5.0f * (1.0f - txt);
        s_bloom(120.0f, 180.0f + dy, 80.0f * txt, FIRE, 0.05f * txt);
        if (txt > 0.5f)
            cv_text(120 - text_w(&FONT_SERIF_XL, "GARGOYLE") / 2, (int32_t)(164.0f + dy) + 3, &FONT_SERIF_XL, "GARGOYLE",
                    c565(mix3(rgb3(0, 0, 0), rgb3(90, 18, 10), (txt - 0.5f) * 2.0f)));
        s_ctext(120.0f, 164.0f + dy, &FONT_SERIF_XL, "GARGOYLE", c, 1);
        s_ctext(120.0f, 204.0f, &FONT_SERIF_XS, "DRONE SYNTH", c565(mix3(rgb3(0, 0, 0), GOLD, sat01(txt * 1.5f - 0.5f))), 1);
        s_ctext(120.0f, 219.0f, &FONT_SERIF_XS, "BY SWTDR34MZ",
                c565(mix3(rgb3(0, 0, 0), GOLD_DIM, sat01(txt * 1.5f - 0.5f))), 1);
    }
    cv_oy = 0;
}

static void intro_draw(float t)
{
    cv_begin(240, 120, C_BLACK);
    intro_scene(t, 0, 120);
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    intro_scene(t, 120, 120);
    cv_blit(0, 120);
}

/* ------------------------------------------------------------ der Vertrag --- */
void app_init(void)
{
    fk_mem m;
    int i;
    m.dl[0] = mem_dl[0];
    m.dl[1] = mem_dl[1];
    m.ch[0] = mem_ch[0];
    m.ch[1] = mem_ch[1];
    m.ap = mem_ap;
    for (i = 0; i < 4; i++)
        m.comb[i] = mem_cb[i];
    m.wt = mem_wt;
    m.sn = mem_sn;
    m.rv = mem_rv;
    m.rva = mem_rva;
    m.sh = mem_sh;
    settings_load();
    fk_init(&m);
    gr_init();
    gr_settings(&set.grv);
    fk_algo(set.algo);
    {
        uint8_t o[FK_NP];
        all_params(o);
        fk_params(o);
    }
    fk_hold(set.hold);
    for (i = 0; i < NKEYS; i++)
        ui.key_note[i] = -1;
    ui.read_k = -1;
}

static void midi_cc(int cc, int v)       /* im Audio-Interrupt */
{
    uint8_t o[FK_NP];
    if (cc == 1)
        set.d[set.algo][D_COLOR] = (uint8_t)v;
    else if (cc >= 70 && cc <= 73)
        set.m[set.algo][cc - 70] = (uint8_t)v;
    else if (cc == 75)
        set.wrath = (uint8_t)v;
    else
        return;
    all_params(o);
    fk_params(o);
    midi_touched = 1;
}

static void midi_in(void)                /* im Audio-Interrupt: Noten, Pitch Bend, CCs, die Clock */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ty = st & 0xF0u;
        if (st == 0xF8u) {
            gr_clock();
            continue;
        }
        if (st == 0xFAu || st == 0xFBu) {
            gr_start();
            continue;
        }
        if (st == 0xFCu) {
            gr_stop();
            continue;
        }
        if (st >= 0xF0u)
            continue;
        if (ty == 0x90u && d2)
            fk_note_on((int)d1, (int)d2);
        else if (ty == 0x80u || ty == 0x90u)
            fk_note_off((int)d1);
        else if (ty == 0xE0u)
            fk_bend(((float)(int)(d2 << 7 | d1) - 8192.0f) * (2.0f / 8192.0f));
        else if (ty == 0xB0u && (d1 == 123u || d1 == 120u))
            fk_all_off();
        else if (ty == 0xB0u)
            midi_cc((int)d1, (int)d2);
    }
}

/* weich begrenzt: der GROOVE kommt nach dem Kern dazu */
static inline float soft1(float x)
{
    float a = x < 0.0f ? -x : x;
    if (a <= 0.8f)
        return x;
    a = 0.8f + 0.2f * (a - 0.8f) / (0.2f + (a - 0.8f));
    return x < 0.0f ? -a : a;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float bl[FK_BLOCK_MAX], br[FK_BLOCK_MAX], dl[FK_BLOCK_MAX], dr[FK_BLOCK_MAX];
    static float wl[FK_BLOCK_MAX], wr[FK_BLOCK_MAX], dk[FK_BLOCK_MAX];
    float gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    uint32_t i, w;
    if (n > FK_BLOCK_MAX)
        n = FK_BLOCK_MAX;
    midi_in();
    gr_render(dl, dr, wl, wr, dk, (int)n, fk_pitch());
    fk_ext(dk, wl, wr, gr_send());
    fk_render(bl, br, (int)n);
    w = scope_w;
    for (i = 0; i < n; i++) {
        float a = soft1(bl[i] + dl[i]), b = soft1(br[i] + dr[i]);
        lr[2 * i] = (int32_t)(a * gain);
        lr[2 * i + 1] = (int32_t)(b * gain);
        scope_ring[(w + i) % SCOPE_N] = (bl[i] + br[i]) * 0.5f;
    }
    scope_w = w + n;
}

void app_panic(void) { fk_panic(); }

void app_splash(void)                   /* das erste Bild der Eröffnung: schwarz; sie läuft danach nahtlos weiter */
{
    lcd_fill(0, 0, 240, 240, RGB(0, 0, 0));
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, RGB(0, 0, 0));
    touch_on(1);
    ui.intro = 1;
    ui.intro_at = plat_ms();
    ui.redraw = R_ALL;
    ui.batt_drawn = -9;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    int k;
    if (ui.intro) {
        uint32_t t = now - ui.intro_at;
        if (t < INTRO_MS && !plat_keys() && !plat_buttons()) {
            intro_draw((float)t * 0.001f);
            plat_lights(0, 0, 0, 0, 0);
            return;
        }
        ui.intro = 0;
        ui.keys = plat_keys();
        ui.btn = plat_buttons();
        ui.redraw = R_ALL;
    }
    input();
    lights();
    ui.frame++;
    if (midi_touched) {
        midi_touched = 0;
        dirty();
        ui.redraw |= R_ROW0 | R_ROW1 | R_ROW2 | R_ROW3 | R_HEAD | R_READ;
    }
    if (ann_on() && ann_where()) {
        char t[64] = "GARGOYLE. ";
        text_cat(t, ROOM_NAME[set.algo]);
        text_cat(t, ". ");
        text_cat(t, PAGE_NAME[ui.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    {   /* der GROOVE: jeder Schlag (die HOME-LED, das Herz oben), der Verlauf */
        uint32_t h = gr_hits();
        if (h != ui.hits_seen) {
            ui.hits_seen = h;
            ui.beat_at = now;
        }
        if (set.grv.on && !(ui.frame % 3u) && (ui.page == PG_GROOVE || ui.page == PG_SETUP || gr_step() >= 0))
            ui.redraw |= R_HEAD;
    }
    if (ui.vol && !(ui.frame % 3u)) {    /* VOL: die Pegel, in dB (-48 .. 0) */
        int c;
        for (c = 0; c < 2; c++) {
            float pk = fk_meter(c), db = pk > 1e-5f ? 6.0206f * fm_log2f(pk) : -96.0f;
            ui.mt[c] = (db + 48.0f) / 48.0f;
            ui.mt[c] = ui.mt[c] < 0.0f ? 0.0f : ui.mt[c] > 1.0f ? 1.0f : ui.mt[c];
        }
        if (fk_clipped())
            ui.clip_until = now + 600u;
    }
    if (!(ui.frame % 4u) || now - ui.trans_at < 450u)   /* der Blick lebt: Fackeln, Augen, Glut */
        ui.redraw |= R_MAP;
    if (ui.wrath_until && now >= ui.wrath_until) {
        ui.wrath_until = 0;
        ui.redraw |= R_READ;
    }
    {   /* der Akku */
        int lvl = batt_level(), key = lvl < 0 ? -1 : lvl * 1000 + batt_pct(lvl) + (int)((now / 500u) & 1u) * 300;
        if (key != ui.batt_drawn) {
            ui.batt_drawn = key;
            ui.redraw |= R_FOOT;
        }
    }
    for (k = 0; k < 4; k++)
        if (ui.knob_at[k] && now - ui.knob_at[k] >= 800u) {
            ui.knob_at[k] = 0;
            ui.redraw |= (uint32_t)R_ROW0 << k;
        }
    if (ui.read_until && now >= ui.read_until) {
        ui.read_until = 0;
        ui.redraw |= R_READ;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw |= R_READ;
    }
    /* die Last: bleibt sie eine Sekunde über 88 %, rechnet die Engine mit weniger Oszillatoren (ECO) */
    if (now - ui.load_at >= 500u) {
        ui.load_at = now;
        if (plat_cpu_pct() > 88u && !fk_eco_on()) {
            if (++ui.hot >= 2) {
                fk_eco(1);
                app_say("ECO MODE", "CPU HIGH");
                ui.redraw |= R_FOOT;
            }
        } else {
            ui.hot = 0;
        }
    }
    /* speichern: auf SAVE (leise geblendet), sonst von selbst, wenn es still ist */
    if (ui.saving) {
        if (fk_ducked() || now - ui.save_at > 200u) {
            set.magic = SET_MAGIC;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
            fk_duck(0);
            ui.saving = 0;
        }
    } else if (set_dirty && now - dirty_at > 2000u && fk_level() < 0.0015f && fk_env() < 0.001f) {
        set.magic = SET_MAGIC;
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
    if (ui.redraw) {
        uint32_t r = ui.redraw;
        ui.redraw = 0;
        draw(r);
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    uint8_t o[FK_NP];
    if (!strncmp(what, "batt", 4) && what[4] >= '0' && what[4] <= '9') {   /* battN: den Akku vorgeben (Tests) */
        ui.batt_test = (uint8_t)(what[4] - '0');
        ui.redraw |= R_FOOT;
        *got = 0;
        return 1;
    }
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "algo"))
        *got = set.algo;
    else if (!strcmp(what, "engine_algo"))
        *got = fk_cur_algo();
    else if (!strcmp(what, "note"))
        *got = fk_note();
    else if (!strcmp(what, "hold"))
        *got = set.hold;
    else if (!strcmp(what, "freeze"))
        *got = fk_frozen();
    else if (!strcmp(what, "oct"))
        *got = set.oct;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "snap"))
        *got = set.snap;
    else if (!strncmp(what, "snapused", 8))  /* snapusedN: Platz N (1..8) belegt */
        *got = set.snaps[(atoi(what + 8) - 1) & 7].used;
    else if (!strcmp(what, "eco"))
        *got = fk_eco_on();
    else if (!strncmp(what, "level_x1000_ge", 14))   /* level_x1000_geN: der Ausgang mindestens N/1000 (1) */
        *got = (int)(fk_level() * 1000.0f) >= atoi(what + 14);
    else if (!strcmp(what, "level_x1000"))
        *got = (int)(fk_level() * 1000.0f);
    else if (!strncmp(what, "mac", 3))   /* macN: der Makro N (0..3) des Raums */
        *got = set.m[set.algo][atoi(what + 3) & 3];
    else if (!strncmp(what, "deep", 4))
        *got = set.d[set.algo][atoi(what + 4) & 3];
    else if (!strncmp(what, "fx", 2))
        *got = set.fx[atoi(what + 2) & 3];
    else if (!strcmp(what, "grv_on"))
        *got = set.grv.on;
    else if (!strcmp(what, "grv_style"))
        *got = set.grv.style;
    else if (!strcmp(what, "grv_running"))
        *got = gr_running();
    else if (!strcmp(what, "grv_synced"))
        *got = gr_synced();
    else if (!strncmp(what, "grv_hits_ge", 11))
        *got = (int)gr_hits() >= atoi(what + 11);
    else if (!strncmp(what, "grv", 3) && what[3] >= '0' && what[3] <= '9')
        *got = set.grv.p[atoi(what + 3) % GR_NP];
    else if (!strcmp(what, "wrath"))
        *got = set.wrath;
    else if (!strcmp(what, "scan"))
        *got = set.ss[set.algo][1];
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9') {   /* pN: der Parameter N der Engine (berechnet) */
        all_params(o);
        *got = o[atoi(what + 1) % FK_NP];
    } else
        return 0;
    return 1;
}
#endif
