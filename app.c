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
 *   SELECT     der Raum (22 Räume = die 22 Algorithmen von FIKH-S), der Dungeon-Plan erscheint; PRESET ebenso
 *   ALGORITHM  SCAN (grob, für live); auf den GROOVE-Seiten: der Style
 *   PLAY       HOLD: der Drone bleibt stehen, die Tasten ändern nur noch den Grundton
 *   REC        WÜRFELN (SEED und SCAN neu; 1 s: dazu COLOR und ECHO); auf den GROOVE-Seiten: der GROOVE an / aus
 *   ARP        FREEZE: Delay und Hall halten, was sie haben
 *   SEQ        1 s halten: der Raum zurück auf den Werksklang
 *   GLO        VOL: die Pegel im Bild
 *   SAVE       speichern (sonst von selbst, wenn es still ist); SAVE + eine der ersten 8 weißen Tasten: ein Snapshot,
 *              SEL + die Taste: zurückgeholt (die Makros gleiten hinüber)
 *   MIDI       Noten (alle Kanäle), Pitch Bend ±2, CC 1 COLOR, CC 70..73 TONE MOVE GRIT SPACE, Clock / Start / Stop
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
    uint8_t rsv[2];
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
         && set.snap <= NSNAP && set.grv.on <= 1u && set.grv.style < GR_NSTYLES;
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

static void all_params(uint8_t *o) { build(set.algo, set.m[set.algo], set.d[set.algo], set.ss[set.algo], set.fx, o); }
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
    uint32_t plan_until;                 /* der Dungeon-Plan (nach einem Raumwechsel) */
    uint32_t redraw;
    int batt_drawn;
    uint32_t load_at;
    uint8_t hot;
    uint8_t saving;                      /* 1: leise geblendet, um zu speichern */
    uint32_t save_at;
    uint8_t batt_test;
    uint32_t hits_seen, beat_at;         /* GROOVE: der letzte Schlag (die HOME-LED blitzt, das Herz oben) */
    float ecg[96];                       /* GROOVE: der Verlauf (wie ein EKG) */
    float spike;                         /* der nächste Ausschlag (jeder Schlag) */
    uint32_t ecg_w;
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
    ui.plan_until = 0;
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
    ui.plan_until = plat_ms() + 1800u;
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
    if ((e = plat_enc(EN_SELECT)) != 0)          /* der Raum */
        algo_set((int)set.algo + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_PRESET)) != 0)
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
 * Ein Dungeon aus einem alten Brettspiel: dunkler Stein, ein Rahmen in Rot und Gold, gemeißelte Schrift, Fackellicht.
 * Die vier Regler haben feste Farben (die Helden): Rot, Orange, Grün, Blau, auf jeder Seite gleich. Alles
 * kantengeglättet (ui_gfx.h). Gezeichnet wird immer die ganze Szene, aber nur in den Bereich, der neu muss. */
#undef C_WHITE
#define C_WHITE RGB(238, 236, 228)
#include "ui_gfx.h"

#define GOLD rgb3(201, 149, 47)
#define GOLD_HI rgb3(240, 192, 80)
#define GOLD_DIM rgb3(110, 82, 40)
#define RED rgb3(214, 52, 40)
#define RED_DIM rgb3(70, 16, 12)
#define FRAME_RED rgb3(130, 28, 20)
#define FIRE rgb3(255, 138, 42)
#define C_GOLD RGB(232, 182, 74)
#define C_GOLD_HI RGB(246, 214, 140)
#define C_GOLD_DIM RGB(150, 120, 80)
#define C_STONE RGB(168, 151, 122)
#define C_RED RGB(240, 80, 60)

static const col3 HERO[4] = {{224, 80, 58}, {232, 150, 46}, {95, 191, 104}, {90, 146, 232}};

#define SCOPE_N 1024u
static float scope_ring[SCOPE_N];
static volatile uint32_t scope_w;

static int32_t g_ox;                     /* der Ausschnitt: links (Bildschirm-x) */
static int32_t g_x0, g_y0, g_x1, g_y1;   /* und seine Grenzen (Bildschirm) */

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
static void s_rect(float x, float y, float w, float h, col3 c, float a)   /* ein gefülltes Rechteck (ganze Pixel) */
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
static void s_frame(float x0, float y0, float x1, float y1, float r, col3 c, float a)
{
    if (!vis(x0 - 1, y0 - 1, x1 + 2, y1 + 2))
        return;
    s_line(x0 + r, y0, x1 - r, y0, c, a);
    s_line(x0 + r, y1, x1 - r, y1, c, a);
    s_line(x0, y0 + r, x0, y1 - r, c, a);
    s_line(x1, y0 + r, x1, y1 - r, c, a);
    if (r > 0.5f) {
        s_arc(x0 + r, y0 + r, r, 1.0f, -90.0f, 0.0f, c, a, 0.0f);
        s_arc(x1 - r, y0 + r, r, 1.0f, 0.0f, 90.0f, c, a, 0.0f);
        s_arc(x1 - r, y1 - r, r, 1.0f, 90.0f, 180.0f, c, a, 0.0f);
        s_arc(x0 + r, y1 - r, r, 1.0f, 180.0f, 270.0f, c, a, 0.0f);
    }
}
/* Text mit Meißelkante: ein dunkler Schatten darunter, eine helle Kante darüber */
static void s_text(float x, float y, const felucca_font_t *f, const char *s, uint16_t c, int engrave)
{
    int32_t w = text_w(f, s);
    if (!vis(x - 2, y - 2, x + (float)w + 2, y + (float)f->h + 2))
        return;
    if (engrave) {
        cv_text((int32_t)SX(x) + 1, (int32_t)y + 1, f, s, RGB(0, 0, 0));
        if (engrave > 1)
            cv_text((int32_t)SX(x), (int32_t)y + 2, f, s, RGB(70, 14, 8));
    }
    cv_text((int32_t)SX(x), (int32_t)y, f, s, c);
}
static void s_ctext(float cx, float y, const felucca_font_t *f, const char *s, uint16_t c, int engrave)
{
    s_text(cx - (float)(text_w(f, s) / 2), y, f, s, c, engrave);
}
static void s_rtext(float rx, float y, const felucca_font_t *f, const char *s, uint16_t c, int engrave)
{
    s_text(rx - (float)text_w(f, s), y, f, s, c, engrave);
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

/* ein gefülltes Vieleck (gerade-ungerade), Zeile für Zeile, mit weichen Enden; die Kanten danach glatt nachgezogen */
#define POLY_MAX 64
static void s_poly(const float *px, const float *py, int n, col3 c, float a)
{
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    int32_t y, i, j;
    for (i = 0; i < n; i++) {
        x0 = px[i] < x0 ? px[i] : x0;
        x1 = px[i] > x1 ? px[i] : x1;
        y0 = py[i] < y0 ? py[i] : y0;
        y1 = py[i] > y1 ? py[i] : y1;
    }
    if (!vis(x0, y0, x1 + 1.0f, y1 + 1.0f))
        return;
    for (y = (int32_t)y0; y <= (int32_t)y1; y++) {
        float xs[POLY_MAX], yc = (float)y + 0.5f;
        int m = 0;
        if (y < g_y0 || y >= g_y1)
            continue;
        for (i = 0, j = n - 1; i < n; j = i++) {
            if ((py[i] > yc) != (py[j] > yc) && m < POLY_MAX)
                xs[m++] = px[i] + (yc - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
        }
        for (i = 1; i < m; i++) {        /* sortieren (wenige) */
            float t = xs[i];
            for (j = i - 1; j >= 0 && xs[j] > t; j--)
                xs[j + 1] = xs[j];
            xs[j + 1] = t;
        }
        for (i = 0; i + 1 < m; i += 2) {
            float a0 = xs[i], a1 = xs[i + 1];
            int32_t x;
            for (x = (int32_t)a0; x <= (int32_t)a1; x++) {
                float cov = 1.0f;
                if (x < g_x0 || x >= g_x1)
                    continue;
                if ((float)x < a0)
                    cov = (float)x + 1.0f - a0;
                if ((float)x + 1.0f > a1)
                    cov -= (float)x + 1.0f - a1;
                if (cov > 0.0f)
                    px_blend(x - g_ox, y, c, a * sat01(cov));
            }
        }
    }
    for (i = 0, j = n - 1; i < n; j = i++)
        s_line(px[j], py[j], px[i], py[i], c, a * 0.6f);
}

/* der Gargoyle: gekauert auf seinem Sockel, die Flügel offen (eine eigene Zeichnung, Koordinaten 0..640 x 0..340) */
static const int16_t GARG_PT[][2] = {
    {320, 72}, {306, 74}, {292, 46}, {298, 84}, {294, 98}, {284, 104}, {262, 92}, {230, 60}, {196, 36}, {184, 32},
    {182, 44}, {190, 48}, {176, 74}, {160, 120}, {150, 176}, {170, 156}, {184, 170}, {198, 146}, {216, 160},
    {230, 136}, {252, 148}, {270, 128}, {266, 150}, {258, 172}, {268, 196}, {300, 196}, {306, 178}, {320, 182},
    {334, 178}, {340, 196}, {372, 196}, {382, 172}, {374, 150}, {370, 128}, {388, 148}, {410, 136}, {424, 160},
    {442, 146}, {456, 170}, {470, 156}, {490, 176}, {480, 120}, {464, 74}, {450, 48}, {458, 44}, {456, 32},
    {444, 36}, {410, 60}, {378, 92}, {356, 104}, {346, 98}, {342, 84}, {348, 46}, {334, 74}};
#define GARG_N ((int)(sizeof GARG_PT / sizeof GARG_PT[0]))
/* gezeichnet mit der Mitte (cx, Kopf oben bei y0) und dem Maßstab sc */
static void gargoyle(float cx, float y0, float sc, col3 fill, float fa, col3 edge, float ea)
{
    float px[GARG_N + 1], py[GARG_N + 1];
    int i;
    for (i = 0; i < GARG_N; i++) {
        px[i] = cx + ((float)GARG_PT[i][0] - 320.0f) * sc;
        py[i] = y0 + ((float)GARG_PT[i][1] - 28.0f) * sc;
    }
    if (fa > 0.0f)
        s_poly(px, py, GARG_N, fill, fa);
    if (ea > 0.0f)
        for (i = 0; i < GARG_N; i++)
            s_line(px[i], py[i], px[(i + 1) % GARG_N], py[(i + 1) % GARG_N], edge, ea);
    {   /* der Sockel */
        float sx0 = cx - 74.0f * sc, sy0 = y0 + 168.0f * sc, sw = 148.0f * sc, sh = 18.0f * sc;
        if (fa > 0.0f)
            s_rect(sx0, sy0, sw, sh, fill, fa);
        if (ea > 0.0f)
            s_frame(sx0, sy0, sx0 + sw, sy0 + sh, 0.0f, edge, ea);
    }
}
/* seine Augen: zwei glühende Schlitze, open 0..1 (sie öffnen sich von der Mitte aus) */
static void garg_eyes(float cx, float y0, float sc, float open, float glow)
{
    float ey = y0 + (90.0f - 28.0f) * sc, dx = 11.0f * sc, w = 6.5f * sc, h = 3.2f * sc * open;
    int s;
    if (open <= 0.01f)
        return;
    for (s = -1; s <= 1; s += 2) {
        float ex = cx + (float)s * dx, t;
        s_bloom(ex, ey, 9.0f * sc + 4.0f, FIRE, 0.25f * glow);
        for (t = -w; t <= w; t += 0.5f) {    /* ein Mandelauge: in der Mitte am höchsten */
            float hh = h * (1.0f - (t / w) * (t / w));
            if (hh > 0.2f)
                s_line(ex + t, ey - hh, ex + t, ey + hh, rgb3(255, 110, 40), 0.55f + 0.45f * glow);
        }
        s_disc(ex, ey, h * 0.45f + 0.3f, rgb3(255, 220, 150), 0.7f * glow * open);
    }
}

/* der Grund: dunkler, warmer Stein mit feinem Korn und ganz leisen Fugen */
static void bg_fill(int32_t ry0, int32_t h)
{
    int32_t x, y;
    for (y = 0; y < h; y++) {
        int32_t sy = ry0 + y, row = sy / 16, fug = (sy % 16) == 0;
        float vy = ((float)sy - 120.0f) / 150.0f;
        for (x = 0; x < (int32_t)cv_w; x++) {
            int32_t sx = x + g_ox;
            uint32_t n = hash_ui((uint32_t)sx * 73856093u ^ (uint32_t)sy * 19349663u);
            float vx = ((float)sx - 120.0f) / 160.0f, g;
            int vj = ((sx + (row & 1) * 14) % 28) == 0;
            g = 13.0f * (1.0f - 0.35f * (vx * vx + vy * vy)) + (float)(n >> 29) * 0.5f - ((fug || vj) ? 2.5f : 0.0f);
            cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(RGB((int)(g + 3.0f), (int)(g + 1.2f), (int)g));
        }
    }
}

/* ---- die Lage auf dem Bildschirm ---- */
#define HEAD_Y1 38
#define MAP_X0 9.0f
#define MAP_Y0 40.0f
#define MAP_X1 231.0f
#define MAP_Y1 128.0f
#define ROW_Y0 132.0f
#define ROW_H 17.0f
#define READ_Y 203.0f
#define FOOT_Y 218.0f
static float row_y(int k) { return ROW_Y0 + (float)k * ROW_H; }

/* der Rahmen: außen rot, innen Gold, kleine Rauten in den Ecken */
static void frame(void)
{
    static const float C[4][2] = {{6, 6}, {234, 6}, {234, 234}, {6, 234}};
    int i;
    s_frame(1.5f, 1.5f, 238.5f, 238.5f, 3.0f, FRAME_RED, 1.0f);
    s_frame(2.5f, 2.5f, 237.5f, 237.5f, 3.0f, FRAME_RED, 0.8f);
    s_frame(4.5f, 4.5f, 235.5f, 235.5f, 3.0f, GOLD, 0.85f);
    for (i = 0; i < 4; i++) {
        float x = C[i][0], y = C[i][1];
        if (!vis(x - 4, y - 4, x + 4, y + 4))
            continue;
        s_line(x - 3.0f, y, x, y - 3.0f, GOLD_HI, 0.9f);
        s_line(x, y - 3.0f, x + 3.0f, y, GOLD_HI, 0.9f);
        s_line(x + 3.0f, y, x, y + 3.0f, GOLD_HI, 0.9f);
        s_line(x, y + 3.0f, x - 3.0f, y, GOLD_HI, 0.9f);
    }
}

/* ein Herz (für den Akku und den GROOVE): fill 0..1 von links, sonst nur der Umriss */
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
                px_blend(x - g_ox, y, mix3(rgb3(255, 140, 120), on, t * 0.7f + 0.3f), cov);
            } else {
                px_blend(x - g_ox, y, rgb3(30, 8, 6), cov * 0.9f);
                if (edge)
                    px_blend(x - g_ox, y, off, (float)edge / 9.0f);
            }
        }
}

/* der Akku: drei Herzen, stufenlos; leer blinkt er; an USB ein Blitz daneben */
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
#define BT_X 196.0f
static void battery(uint32_t now)
{
    int lvl = batt_level(), pct, usb, i;
    float f;
    if (lvl < 0 || !vis(BT_X - 10.0f, FOOT_Y, 232.0f, FOOT_Y + 14.0f))
        return;
    usb = lvl >= 4;
    pct = batt_pct(lvl);
    f = (float)pct / 100.0f * 3.0f;
    if (!usb && pct < 8 && ((now / 500u) & 1u))
        f = 0.0f;
    for (i = 0; i < 3; i++)
        heart(BT_X + 6.0f + (float)i * 11.5f, FOOT_Y + 7.0f, 4.6f, f - (float)i, RED, FRAME_RED);
    if (usb) {
        float bx = BT_X - 8.0f, by = FOOT_Y + 2.0f;
        s_line(bx + 3.0f, by, bx + 0.5f, by + 5.0f, GOLD_HI, 0.95f);
        s_line(bx + 0.5f, by + 5.0f, bx + 4.0f, by + 5.0f, GOLD_HI, 0.95f);
        s_line(bx + 4.0f, by + 5.0f, bx + 1.5f, by + 10.0f, GOLD_HI, 0.95f);
    }
}

/* eine Fackel: die Halterung, die Flamme flackert (t: Sekunden), hell mit dem Klang */
static void torch(float x, float y, float t, float lvl, int frozen)
{
    float fl = 0.75f + 0.15f * fm_sinf(t * 23.0f + x) + 0.1f * fm_sinf(t * 37.0f + x * 0.3f);
    col3 fc = frozen ? rgb3(120, 190, 255) : FIRE, core = frozen ? rgb3(220, 240, 255) : rgb3(255, 214, 120);
    if (!vis(x - 16, y - 18, x + 16, y + 16))
        return;
    s_bloom(x, y - 4.0f, 14.0f + 6.0f * lvl, fc, (0.18f + 0.25f * lvl) * fl);
    s_line(x - 2.0f, y + 2.0f, x + 2.0f, y + 2.0f, rgb3(90, 64, 34), 1.0f);
    s_line(x, y + 2.0f, x, y + 9.0f, rgb3(90, 64, 34), 1.0f);
    s_line(x - 3.0f, y + 9.0f, x + 3.0f, y + 9.0f, GOLD_DIM, 1.0f);
    s_disc(x, y - 1.5f, 2.6f * fl, fc, 0.95f);
    s_disc(x + 0.4f * fm_sinf(t * 17.0f), y - 4.0f * fl, 1.8f * fl, fc, 0.85f);
    s_disc(x, y - 1.0f, 1.3f, core, 0.95f);
}

/* der Nebel: das echte Ausgangssignal als weiche, blasse Schwade (FREEZE: eisblau) */
static void mist(float x0, float y, float w, float h, int frozen)
{
    uint32_t wr = scope_w, start = wr - 900u, i, trig = 0;
    int j, nn = (int)w;
    float pk = 0.02f, prev = 0.0f;
    col3 c = frozen ? rgb3(150, 200, 255) : rgb3(170, 176, 196);
    if (!vis(x0 - 2, y - h, x0 + w + 2, y + h))
        return;
    for (i = 0; i < 300u; i++) {
        float a = scope_ring[(start + i) % SCOPE_N], b = scope_ring[(start + i + 1u) % SCOPE_N];
        if (a <= 0.0f && b > 0.0f) {
            trig = i;
            break;
        }
    }
    for (j = 0; j < nn; j++) {
        float v = fabsf_(scope_ring[(start + trig + (uint32_t)j * 4u) % SCOPE_N]);
        pk = v > pk ? v : pk;
    }
    for (j = 0; j < nn; j++) {
        float v = scope_ring[(start + trig + (uint32_t)j * 4u) % SCOPE_N] / pk, yy = y - v * h * 0.8f;
        float edge = sat01((float)j / 12.0f) * sat01((float)(nn - j) / 12.0f);
        if (j)
            s_line(x0 + (float)j - 1.0f, prev, x0 + (float)j, yy, c, 0.55f * edge);
        s_line(x0 + (float)j, yy + 1.5f, x0 + (float)j, yy + 3.5f, c, 0.12f * edge);
        prev = yy;
    }
}

/* ---- die Teile der Szene ---- */

/* oben: die Nummer im roten Siegel, der Name des Raums, darunter der Algorithmus; rechts ein Herz, das mit dem
 * GROOVE schlägt */
static void head(uint32_t now)
{
    int a = set.algo;
    char t[8] = "";
    if (!vis(6, 6, 234, HEAD_Y1))
        return;
    s_rect(12.0f, 11.0f, 22.0f, 17.0f, rgb3(60, 12, 8), 1.0f);
    s_frame(11.5f, 10.5f, 34.5f, 28.5f, 1.5f, GOLD, 0.9f);
    if (a + 1 < 10)
        text_cat(t, "0");
    int_text(a + 1, t);
    s_ctext(23.0f, 13.0f, &FONT_SERIF_S, t, C_GOLD_HI, 1);
    if (groove_page()) {
        s_ctext(124.0f, 9.0f, &FONT_SERIF_M, GR_STYLE_NAME[set.grv.style], C_GOLD, 2);
        s_ctext(124.0f, 26.0f, &FONT_SERIF_XS, set.grv.on ? (gr_synced() ? "GROOVE  MIDI SYNC" : "GROOVE  ON")
                                                          : "GROOVE  OFF  (REC)", C_GOLD_DIM, 1);
    } else {
        s_ctext(124.0f, 9.0f, &FONT_SERIF_M, ROOM_NAME[a], C_GOLD, 2);
        s_ctext(124.0f, 26.0f, &FONT_SERIF_XS, FK_ALGOS[a].name, C_GOLD_DIM, 1);
    }
    {   /* das Herz: schlägt mit dem GROOVE, sonst ruhig */
        float beat = set.grv.on && gr_running() ? sat01(1.0f - (float)(now - ui.beat_at) / 220.0f) : 0.0f;
        heart(220.0f, 19.0f, 5.0f + 1.3f * beat, set.grv.on ? 1.0f : 0.0f, RED, GOLD_DIM);
        if (beat > 0.0f)
            s_bloom(220.0f, 19.0f, 12.0f, RED, 0.3f * beat);
    }
    s_line(14.0f, 37.5f, 226.0f, 37.5f, GOLD_DIM, 0.6f);
}

/* der Boden der Karte: Steinplatten wie auf dem Spielbrett */
static void floor_tiles(void)
{
    int32_t x, y;
    if (!vis(MAP_X0, MAP_Y0, MAP_X1, MAP_Y1))
        return;
    for (y = (int32_t)MAP_Y0; y < (int32_t)MAP_Y1; y++) {
        if (y < g_y0 || y >= g_y1)
            continue;
        for (x = (int32_t)MAP_X0; x < (int32_t)MAP_X1; x++) {
            uint32_t n;
            int tx = (x - (int32_t)MAP_X0) % 11, ty = (y - (int32_t)MAP_Y0) % 11;
            float g;
            if (x < g_x0 || x >= g_x1)
                continue;
            n = hash_ui((uint32_t)(x / 11) * 911u ^ (uint32_t)(y / 11) * 3571u);
            g = 24.0f + (float)(n >> 29) * 1.4f + (float)(hash_ui((uint32_t)x * 7919u ^ (uint32_t)y * 104729u) >> 30);
            if (tx == 0 || ty == 0)
                g = 15.0f;
            px_put(x - g_ox, y, rgb3(g + 4.0f, g + 1.5f, g - 1.0f));
        }
    }
    s_frame(MAP_X0 - 0.5f, MAP_Y0 - 0.5f, MAP_X1 + 0.5f, MAP_Y1 + 0.5f, 0.0f, rgb3(70, 54, 40), 1.0f);
}

/* der Raum: eine Kammer mit zwei Fackeln, der Gargoyle in der Mitte (die Augen glühen mit dem Klang), Gänge zu den
 * Nachbarräumen, der Nebel ist das Signal */
static void chamber(uint32_t now)
{
    float t = (float)now * 0.001f, lvl = sat01(fk_level() * 2.5f), env = fk_env(), pump = gr_pump();
    int frozen = fk_frozen();
    col3 wall = frozen ? rgb3(150, 196, 230) : GOLD;
    float cx0 = 62.0f, cy0 = 47.0f, cx1 = 178.0f, cy1 = 121.0f;
    /* die Gänge und die Nachbarräume (dunkel) */
    s_rect(MAP_X0 + 1.0f, 78.0f, cx0 - MAP_X0 - 1.0f, 12.0f, rgb3(10, 8, 6), 0.55f);
    s_rect(cx1, 78.0f, MAP_X1 - cx1 - 1.0f, 12.0f, rgb3(10, 8, 6), 0.55f);
    s_frame(14.5f, 52.5f, 44.5f, 115.5f, 0.0f, GOLD_DIM, 0.55f);
    s_frame(195.5f, 52.5f, 225.5f, 115.5f, 0.0f, GOLD_DIM, 0.55f);
    {
        char a[4] = "", b[4] = "";
        int pa = (set.algo + FK_NALGO - 1) % FK_NALGO, nb = (set.algo + 1) % FK_NALGO;
        int_text(pa + 1, a);
        int_text(nb + 1, b);
        s_ctext(29.5f, 58.0f, &FONT_SERIF_XS, a, C_GOLD_DIM, 1);
        s_ctext(210.5f, 58.0f, &FONT_SERIF_XS, b, C_GOLD_DIM, 1);
    }
    /* die Kammer: ihr Boden im Fackellicht, die Wände doppelt */
    s_rect(cx0, cy0, cx1 - cx0, cy1 - cy0, rgb3(10, 7, 5), 0.35f);
    s_bloom(120.0f, 86.0f, 48.0f, frozen ? rgb3(90, 150, 220) : FIRE, 0.10f + 0.22f * lvl);
    s_frame(cx0 + 0.5f, cy0 + 0.5f, cx1 - 0.5f, cy1 - 0.5f, 0.0f, wall, 0.9f);
    s_frame(cx0 + 2.5f, cy0 + 2.5f, cx1 - 2.5f, cy1 - 2.5f, 0.0f, GOLD_DIM, 0.6f);
    torch(cx0 + 10.0f, cy0 + 12.0f, t, lvl, frozen);
    torch(cx1 - 10.0f, cy0 + 12.0f, t + 1.7f, lvl, frozen);
    /* der Gargoyle: duckt sich mit der Pumpe */
    gargoyle(120.0f, 53.0f + 2.0f * pump, 0.21f, rgb3(16, 12, 10), 0.95f, GOLD_DIM, 0.75f);
    garg_eyes(120.0f, 53.0f + 2.0f * pump, 0.21f, 1.0f, 0.25f + 0.75f * sat01(env * 0.6f + lvl));
    mist(cx0 + 4.0f, 108.0f, cx1 - cx0 - 8.0f, 9.0f, frozen);
    if (ui.vol) {                        /* VOL: die Pegel als zwei rote Säulen in den Gängen */
        int c, clip = now < ui.clip_until;
        for (c = 0; c < 2; c++) {
            float x = c ? 203.0f : 22.0f, h = 58.0f * ui.mt[c];
            s_rect(x, 56.0f, 15.0f, 58.0f, rgb3(26, 6, 6), 1.0f);
            s_rect(x, 114.0f - h, 15.0f, h, clip ? rgb3(255, 60, 40) : RED, 0.9f);
            s_frame(x - 0.5f, 55.5f, x + 15.5f, 114.5f, 0.0f, clip ? RED : GOLD_DIM, 0.9f);
        }
    }
}

/* der Dungeon-Plan: alle 22 Räume, der aktuelle brennt */
static void plan(void)
{
    int a;
    for (a = 0; a < FK_NALGO; a++) {
        int row = a < 9 ? 0 : a < 18 ? 1 : 2, col = a < 18 ? a % 9 : a - 18;
        float x = 14.0f + (float)col * 24.0f + (row == 2 ? 60.0f : 0.0f), y = 46.0f + (float)row * 27.0f;
        int cur = a == set.algo;
        char t[4] = "";
        int_text(a + 1, t);
        if (!vis(x - 4, y - 4, x + 26, y + 26))
            continue;
        if (cur) {
            s_bloom(x + 10.5f, y + 10.5f, 18.0f, FIRE, 0.35f);
            s_rect(x, y, 21.0f, 21.0f, rgb3(232, 160, 64), 1.0f);
            s_frame(x - 0.5f, y - 0.5f, x + 21.5f, y + 21.5f, 0.0f, GOLD_HI, 1.0f);
            s_ctext(x + 10.5f, y + 5.0f, &FONT_SERIF_S, t, RGB(28, 16, 8), 0);
        } else {
            s_rect(x, y, 21.0f, 21.0f, rgb3(18, 14, 11), 1.0f);
            s_frame(x - 0.5f, y - 0.5f, x + 21.5f, y + 21.5f, 0.0f, rgb3(80, 62, 44), 1.0f);
            s_ctext(x + 10.5f, y + 6.0f, &FONT_SERIF_XS, t, C_GOLD_DIM, 0);
        }
    }
}

/* GROOVE: der Verlauf wie ein EKG, rot über dem Stein, die 16 Schritte darunter */
static void groove_view(void)
{
    int i, n = (int)(sizeof ui.ecg / sizeof ui.ecg[0]), st = gr_step();
    float x0 = 18.0f, w = 204.0f, y = 84.0f, prev = y;
    for (i = 0; i < n; i++) {   /* ein Schlag: hoch, dann ein Stück unter die Linie (wie ein QRS) */
        float v = ui.ecg[(ui.ecg_w + (uint32_t)i) % (uint32_t)n], x = x0 + (float)i * w / (float)(n - 1);
        float vp = i ? ui.ecg[(ui.ecg_w + (uint32_t)i - 1u) % (uint32_t)n] : 0.0f;
        float yy = y - (v - 1.6f * vp) * 30.0f, fade = sat01((float)i / 20.0f);
        if (i) {
            s_line(x - w / (float)(n - 1), prev, x, yy, RED, 0.35f * fade);
            s_line(x - w / (float)(n - 1), prev - 0.8f, x, yy - 0.8f, rgb3(255, 140, 120), 0.9f * fade);
        }
        prev = yy;
    }
    s_bloom(x0 + w, prev, 8.0f, RED, 0.5f);
    for (i = 0; i < 16; i++) {
        float x = 25.0f + (float)i * 12.7f;
        int cur = i == st;
        s_disc(x, 118.0f, cur ? 2.4f : 1.5f, cur ? FIRE : (i % 4 ? GOLD_DIM : GOLD), cur ? 1.0f : 0.8f);
        if (cur)
            s_bloom(x, 118.0f, 7.0f, FIRE, 0.5f);
    }
    if (!set.grv.on)
        s_ctext(120.0f, 64.0f, &FONT_SERIF_S, "PRESS REC", C_GOLD_DIM, 1);
}

static void map_area(uint32_t now)
{
    if (!vis(MAP_X0 - 1, MAP_Y0 - 1, MAP_X1 + 1, MAP_Y1 + 1))
        return;
    floor_tiles();
    if (groove_page())
        groove_view();
    else if (now < ui.plan_until)
        plan();
    else
        chamber(now);
}

/* ein Regler: der Edelstein in seiner Heldenfarbe, der Name, der Balken (der Sweetspot als heller Strich), der Wert */
static void row(int k, uint32_t now)
{
    float y = row_y(k), yc = y + 8.0f, bx0 = 92.0f, bx1 = 184.0f;
    int hot = ui.knob_at[k] && now - ui.knob_at[k] < 800u, v = *kval(ui.page, k);
    col3 h = HERO[k];
    char t[24];
    uint16_t lc;
    if (!vis(8, y, 232, y + ROW_H))
        return;
    if (hot)
        s_rect(10.0f, y + 1.0f, 220.0f, ROW_H - 2.0f, h, 0.10f);
    s_disc(16.0f, yc, 3.4f, mix3(h, rgb3(255, 255, 255), 0.25f), 1.0f);
    s_disc(15.0f, yc - 1.0f, 1.2f, rgb3(255, 255, 255), 0.6f);
    if (hot)
        s_bloom(16.0f, yc, 9.0f, h, 0.45f);
    lc = c565(hot ? mix3(h, rgb3(255, 255, 255), 0.35f) : h);
    s_text(25.0f, y + 2.0f, &FONT_SERIF_S, kname(ui.page, k), lc, 1);
    s_rect(bx0, yc - 3.0f, bx1 - bx0, 6.0f, rgb3(36, 28, 22), 1.0f);
    s_rect(bx0, yc - 3.0f, (bx1 - bx0) * (float)v / 127.0f, 6.0f, h, hot ? 1.0f : 0.85f);
    s_rect(bx0, yc - 3.0f, (bx1 - bx0) * (float)v / 127.0f, 1.0f, rgb3(255, 255, 255), 0.25f);
    s_frame(bx0 - 0.5f, yc - 3.5f, bx1 + 0.5f, yc + 3.5f, 0.0f, rgb3(84, 66, 48), 1.0f);
    if (ksweet(ui.page, k)) {
        float sx = bx0 + (bx1 - bx0) * 64.0f / 127.0f;
        s_line(sx, yc - 6.0f, sx, yc + 6.0f, rgb3(243, 230, 200), v == 64 ? 1.0f : 0.6f);
    }
    s_rtext(229.0f, y + 4.0f, &FONT_SERIF_XS, kvalue_text(ui.page, k, t), hot ? C_GOLD_HI : C_GOLD_DIM, 1);
}

/* die Anzeige: der Regler, der gerade gedreht wird (mit Richtung), sonst eine Meldung, sonst die Seite */
static void readout(uint32_t now)
{
    char l[48] = "", v[24];
    int sm = snap_mode();
    if (!vis(8, READ_Y - 2, 232, READ_Y + 13))
        return;
    s_line(14.0f, READ_Y - 1.5f, 226.0f, READ_Y - 1.5f, GOLD_DIM, 0.5f);
    if (sm) {
        int i;
        s_ctext(80.0f, READ_Y + 1.0f, &FONT_SERIF_XS, sm == 2 ? "STORE SNAP" : "LOAD SNAP", C_GOLD, 1);
        for (i = 0; i < NSNAP; i++) {
            float x = 132.0f + (float)i * 9.0f;
            int cur = set.snap == (uint8_t)(i + 1), used = set.snaps[i].used;
            s_disc(x, READ_Y + 6.0f, 2.4f, cur && used ? RED : used ? GOLD : rgb3(44, 36, 26), 1.0f);
            if (cur && used)
                s_bloom(x, READ_Y + 6.0f, 6.0f, RED, 0.45f);
        }
        return;
    }
    if (now < ui.read_until && ui.read_k >= 0) {
        int k = ui.read_k, val = *kval(ui.page, k);
        text_cat(l, kname(ui.page, k));
        text_cat(l, "  ");
        text_cat(l, kvalue_text(ui.page, k, v));
        if (PAGE_SRC[ui.page] == SRC_M) {        /* die Richtung in Worten */
            text_cat(l, "  ");
            text_cat(l, val == 64 ? "SWEETSPOT" : MAC_RANGE[k][val > 64]);
        }
        s_ctext(120.0f, READ_Y + 1.0f, &FONT_SERIF_XS, l, c565(HERO[k]), 1);
        return;
    }
    if (now < ui.msg_until) {
        text_cat(l, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(l, "  ");
            text_cat(l, ui.msg[1]);
        }
        s_ctext(120.0f, READ_Y + 1.0f, &FONT_SERIF_XS, l, C_GOLD_HI, 1);
        return;
    }
    text_cat(l, PAGE_NAME[ui.page]);
    s_ctext(120.0f, READ_Y + 1.0f, &FONT_SERIF_XS, l, C_GOLD_DIM, 1);
}

/* unten: HOLD und FREEZE als LEDs, die Oktave, der Akku (drei Herzen) */
static void foot(uint32_t now)
{
    int hold = set.hold, frz = fk_frozen();
    char t[12] = "";
    if (!vis(8, FOOT_Y - 1, 232, FOOT_Y + 15))
        return;
    s_disc(15.0f, FOOT_Y + 7.0f, 2.4f, hold ? RED : RED_DIM, 1.0f);
    if (hold)
        s_bloom(15.0f, FOOT_Y + 7.0f, 7.0f, RED, 0.5f);
    s_text(21.0f, FOOT_Y + 2.0f, &FONT_SERIF_XS, "HOLD", hold ? C_RED : C_GOLD_DIM, 1);
    if (frz || fk_eco_on()) {
        s_disc(55.0f, FOOT_Y + 7.0f, 2.4f, frz ? rgb3(120, 190, 255) : RED, 1.0f);
        s_bloom(55.0f, FOOT_Y + 7.0f, 7.0f, frz ? rgb3(120, 190, 255) : RED, 0.5f);
        s_text(61.0f, FOOT_Y + 2.0f, &FONT_SERIF_XS, frz ? "FREEZE" : "ECO", frz ? RGB(150, 200, 255) : C_RED, 1);
    }
    text_cat(t, "OCT ");
    if (set.oct > 2)
        text_cat(t, "+");
    int_text((int)set.oct - 2, t);
    s_ctext(140.0f, FOOT_Y + 2.0f, &FONT_SERIF_XS, t, set.oct != 2 ? C_RED : C_GOLD_DIM, 1);
    battery(now);
}

/* ---- die ganze Szene ---- */
static void scene(uint32_t now)
{
    int k;
    frame();
    head(now);
    map_area(now);
    for (k = 0; k < 4; k++)
        row(k, now);
    readout(now);
    foot(now);
}

/* einen Ausschnitt des Bildschirms neu zeichnen */
static void region(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t h = y1 - y0, w = x1 - x0;
    if (w <= 0 || h <= 0)
        return;
    if ((uint32_t)(w * h) > APP_CANVAS_PX) {      /* zu groß: in Streifen */
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
    if (m & R_HEAD)
        region(6, 6, 234, HEAD_Y1 + 1);
    if (m & R_MAP)
        region((int32_t)MAP_X0 - 1, (int32_t)MAP_Y0 - 1, (int32_t)MAP_X1 + 2, (int32_t)MAP_Y1 + 2);
    for (k = 0; k < 4; k++)
        if (m & ((uint32_t)R_ROW0 << k))
            region(8, (int32_t)row_y(k), 232, (int32_t)(row_y(k) + ROW_H));
    if (m & R_READ)
        region(8, (int32_t)READ_Y - 3, 232, (int32_t)READ_Y + 14);
    if (m & R_FOOT)
        region(8, (int32_t)FOOT_Y - 1, 232, (int32_t)FOOT_Y + 15);
}

/* ---- die Eröffnung: schwarz; GARGOYLE erscheint; darüber öffnen sich zwei glühende Augen; ein Blitz, und für einen
 * Augenblick sieht man die Silhouette des Gargoyle; sie bleibt schwach im Dunkeln stehen ---- */
#define INTRO_MS 3600u
#define IN_SC 0.55f
#define IN_Y 30.0f
static void intro_scene(float t, int32_t y0, int32_t h)
{
    float txt = sat01((t - 0.25f) / 0.8f), eyes = sat01((t - 1.25f) / 0.3f), fl = 0.0f, sil = 0.0f;
    int32_t x, y;
    g_ox = 0;
    g_x0 = 0;
    g_x1 = 240;
    g_y0 = y0;
    g_y1 = y0 + h;
    cv_oy = -y0;
    if (t >= 1.85f && t < 1.93f)          /* der Blitz: zweimal */
        fl = 1.0f;
    else if (t >= 1.93f && t < 2.0f)
        fl = 0.15f;
    else if (t >= 2.0f && t < 2.07f)
        fl = 0.65f;
    else if (t >= 2.07f && t < 2.3f)
        fl = 0.65f * (1.0f - (t - 2.07f) / 0.23f) * 0.4f;
    if (t >= 1.85f)
        sil = t < 2.1f ? 1.0f : 0.25f + 0.75f * sat01(1.0f - (t - 2.1f) / 0.7f);
    for (y = 0; y < h; y++) {             /* der Grund: schwarz, im Blitz ein kaltes Licht von oben */
        float vy = (float)(y0 + y) / 240.0f;
        for (x = 0; x < 240; x++) {
            float vx = ((float)x - 120.0f) / 160.0f, g = fl * (1.0f - 0.55f * vy - 0.4f * vx * vx);
            cv_px[(uint32_t)y * 240u + (uint32_t)x] = swap16(RGB((int)(g * 230.0f), (int)(g * 232.0f), (int)(g * 255.0f)));
        }
    }
    if (sil > 0.0f) {
        if (fl > 0.3f)                   /* im Blitz: ein schwarzer Schatten gegen das Licht */
            gargoyle(120.0f, IN_Y, IN_SC, rgb3(4, 3, 3), 1.0f, rgb3(4, 3, 3), 0.0f);
        else                             /* danach: der Umriss, schwach im Dunkeln */
            gargoyle(120.0f, IN_Y, IN_SC, rgb3(10, 8, 7), 1.0f, GOLD_DIM, 0.55f * sil);
    }
    garg_eyes(120.0f, IN_Y, IN_SC, eyes, eyes * (0.8f + 0.2f * fm_sinf(t * 9.0f)));
    if (txt > 0.0f) {
        uint16_t c = c565(mix3(rgb3(0, 0, 0), GOLD_HI, txt));
        float dy = 6.0f * (1.0f - txt);
        s_bloom(120.0f, 166.0f + dy, 80.0f * txt, FIRE, 0.06f * txt);
        if (txt > 0.5f)
            cv_text(120 - text_w(&FONT_SERIF_XL, "GARGOYLE") / 2, (int32_t)(152.0f + dy) + 3, &FONT_SERIF_XL, "GARGOYLE",
                    c565(mix3(rgb3(0, 0, 0), rgb3(90, 18, 10), (txt - 0.5f) * 2.0f)));
        s_ctext(120.0f, 152.0f + dy, &FONT_SERIF_XL, "GARGOYLE", c, 1);
        s_ctext(120.0f, 192.0f, &FONT_SERIF_XS, "DRONE SYNTH", c565(mix3(rgb3(0, 0, 0), GOLD, sat01(txt * 1.5f - 0.5f))), 1);
        s_ctext(120.0f, 208.0f, &FONT_SERIF_XS, "BY SWTDR34MZ",
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
        ui.redraw |= R_ROW0 | R_ROW1 | R_ROW2 | R_ROW3;
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
            ui.spike = sat01(0.35f + gr_env() * 2.5f);
        }
        if (set.grv.on && !(ui.frame % 3u))
            ui.redraw |= R_HEAD;
        if (!(ui.frame % 2u)) {
            uint32_t n = (uint32_t)(sizeof ui.ecg / sizeof ui.ecg[0]);
            ui.ecg[ui.ecg_w % n] = ui.spike;
            ui.spike *= 0.25f;
            ui.ecg_w = (ui.ecg_w + 1u) % n;
        }
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
    if (!(ui.frame % 5u))                /* die Karte lebt: Fackeln, Nebel, Augen, EKG (~12 Bilder pro Sekunde) */
        ui.redraw |= R_MAP;
    if (ui.plan_until && now >= ui.plan_until) {
        ui.plan_until = 0;
        ui.redraw |= R_MAP;
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
