/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE: Bedienung, Seiten, Speicher, Bildschirm. Teil des Unity-Builds (device/unity.c, host/plat_host.c), nach
 * der Plattform: gfx.c und announce.c sind sichtbar. Der Klang ist dsp/chip_core.c (ein 8-Bit-Drone-Synth aus den
 * Klangchips der alten Heimcomputer und DOS-Spiele) und dsp/garg_groove.c (der GROOVE).
 *
 * Bedienung wie bei FIKH-S: ein Regler, ein Parameter, sechs Seiten:
 *   Tasten     spielen (mono, die letzte Taste gewinnt), mit Anschlag; OCT- / OCT+ die Oktave
 *   HOME       SUMMON: CURSE, HORDE, RUNE, ALCHEMY
 *   HOME 2x    GROOVE: DENSITY, BREATH (die Pumpe), DISTANCE (nah - fern, ins Echo), TEMPO
 *   HOME 3x    SETUP:  LEVEL, HUMAN (Zufall), FOLLOW (dem Grundton folgen), TUNE
 *   FX         ECHO:   ECHO, HAUNT, DECAY, DEPTH (das 8-Bit-Echo)
 *   ENV        RITUAL: TORCH, PORTAL, AWAKEN, SLUMBER
 *   EDIT       LORE:   CREEP, WANDER, BITS, VOLUME
 *   GLO        SPELL:  AGE, AURA (der Resonator: aus Chip-Tönen wird eine Fläche), SPEED, WIDTH (für alle gleich)
 *   LFO        OMEN:   RATE, SHAPE, POWER, PATH – ein LFO auf mehrere Ziele, im Takt des GROOVE (für alle gleich)
 *   SELECT     WRATH: ein Makro über allem, für live, 4 je Raste
 *   PRESET     der Algorithmus (16, in zwei Bänken)
 *   ALGORITHM  QUEST = MORPH in den nächsten Algorithmus; auf den GROOVE-Seiten: der Style
 *   PLAY       HOLD: der Drone bleibt stehen, die Tasten ändern nur noch den Grundton
 *   REC        FATE: würfeln (FATE neu, damit auch die Varianten; 1 s: dazu RUNE und ALCHEMY); auf den GROOVE-Seiten: GROOVE an / aus
 *   ARP        FREEZE: das Echo hält, was es hat
 *   SEQ        1 s halten: der Algorithmus zurück auf seinen Werksklang
 *   SAVE       speichern (sonst von selbst, wenn es still ist); SAVE + eine der ersten 8 weißen Tasten: ein Snapshot,
 *              SEL + die Taste: zurückgeholt
 *   MIDI       Noten (alle Kanäle), Pitch Bend ±2, CC 1 RUNE, CC 70..73 CURSE HORDE RUNE ALCHEMY, CC 75 WRATH,
 *              Clock / Start / Stop für den GROOVE */
#include "fastmath.h"
#include "chip_core.h"
#include "garg_groove.h"
#include "ui_serif.h"

#define SR 44100.0f

/* ------------------------------------------------------- der Klang-Speicher --- */
static int16_t mem_ec[2][FK_EC_N] __attribute__((section(".pool")));
static float mem_df[FK_DF_N] __attribute__((section(".pool")));
static float mem_res[FK_NRES][FK_RS_N] __attribute__((section(".pool")));

/* ------------------------------------------------------------- Speicher --- */
#define SET_MAGIC 0x36475247u            /* "GRG6": mit OMEN */
#define SET_MAGIC4 0x34475247u           /* "GRG4" (v0.1.0): wird übernommen, QUEST auf 0 */
#define SET_MAGIC5 0x35475247u           /* "GRG5" (v0.1.1): wird übernommen */
#define NSNAP 8
typedef struct {
    uint8_t used, algo, oct, rsv;
    uint8_t p[FK_NPA];
    uint8_t g[FK_NPG];
} snap_t;
typedef struct {
    uint32_t magic;
    uint8_t algo, oct, hold, snap;       /* snap: der zuletzt geholte Snapshot (0: keiner, 1..8) */
    uint8_t p[FK_NALGO][FK_NPA];         /* jeder Algorithmus mit seinen Reglern */
    uint8_t g[FK_NPG];                   /* SPELL: für alle gleich */
    gr_set grv;                          /* der GROOVE: global, ein Wechsel ändert den Beat nicht */
    uint8_t wrath, rsv;                  /* WRATH (SELECT): global, über allem */
    snap_t snaps[NSNAP];
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "die Einstellungen passen nicht in ein Speicherobjekt");
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static void algo_defaults(int a) { memcpy(set.p[a], FK_ALGOS[a].init, FK_NPA); }

static void settings_defaults(void)
{
    int a;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.oct = 2;                         /* 0..4 = -2..+2: die Tasten F3..G5 */
    for (a = 0; a < FK_NALGO; a++)
        algo_defaults(a);
    memcpy(set.g, FK_GLOBAL_INIT, FK_NPG);
    for (a = 0; a < GR_NP; a++)
        set.grv.p[a] = GR_INIT[a];
}

/* v0.1.0 (GRG4) und v0.1.1 (GRG5): SPELL hatte nur 4 globale Regler, noch kein OMEN; in v0.1.0 war QUEST die Variante */
#define NG4 4
typedef struct {
    uint8_t used, algo, oct, rsv;
    uint8_t p[FK_NPA];
    uint8_t g[NG4];
} snap4_t;
typedef struct {
    uint32_t magic;
    uint8_t algo, oct, hold, snap;
    uint8_t p[FK_NALGO][FK_NPA];
    uint8_t g[NG4];
    gr_set grv;
    uint8_t wrath, rsv;
    snap4_t snaps[NSNAP];
} settings4_t;

static void settings_load(void)
{
    int a, i, got = plat_store_load(OBJ_SETTINGS, &set, sizeof set), ok;
    if (got == (int)sizeof(settings4_t) && (set.magic == SET_MAGIC4 || set.magic == SET_MAGIC5)) {   /* übernehmen */
        static settings4_t o;
        int old = set.magic == SET_MAGIC4;
        memcpy(&o, &set, sizeof o);
        settings_defaults();
        set.algo = o.algo;
        set.oct = o.oct;
        set.hold = o.hold;
        set.snap = o.snap;
        memcpy(set.p, o.p, sizeof o.p);
        memcpy(set.g, o.g, NG4);
        set.grv = o.grv;
        set.wrath = o.wrath;
        for (i = 0; i < NSNAP; i++) {
            set.snaps[i].used = o.snaps[i].used;
            set.snaps[i].algo = o.snaps[i].algo;
            set.snaps[i].oct = o.snaps[i].oct;
            memcpy(set.snaps[i].p, o.snaps[i].p, FK_NPA);
            memcpy(set.snaps[i].g, o.snaps[i].g, NG4);
            memcpy(set.snaps[i].g + NG4, FK_GLOBAL_INIT + NG4, FK_NPG - NG4);
            if (old)
                set.snaps[i].p[P_QUEST] = 0;
        }
        for (a = 0; old && a < FK_NALGO; a++)
            set.p[a][P_QUEST] = 0;
        got = (int)sizeof set;
    }
    ok = got == (int)sizeof set && set.magic == SET_MAGIC && set.algo < FK_NALGO && set.oct <= 4u && set.hold <= 1u
         && set.snap <= NSNAP && set.grv.on <= 1u && set.grv.style < GR_NSTYLES && set.wrath <= 127u;
    for (i = 0; ok && i < NSNAP; i++)
        if (set.snaps[i].used > 1u || set.snaps[i].algo >= FK_NALGO || set.snaps[i].oct > 4u)
            ok = 0;
    for (a = 0; ok && a < FK_NALGO; a++)
        for (i = 0; i < FK_NPA; i++)
            if (set.p[a][i] > 127u)
                ok = 0;
    for (i = 0; ok && i < FK_NPG; i++)
        if (set.g[i] > 127u)
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

static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint8_t lerp8(int a, int b, float t) { return (uint8_t)iclamp(a + (int)((float)(b - a) * t + 0.5f), 0, 127); }
/* WRATH (SELECT): ein Makro über allem, für live – das Monster erwacht: CURSE bis zum Anschlag, die Horde
 * verstimmt, die Alchemie kocht, das Echo spukt, die Bits brechen weg, alles wird älter und schneller */
static void wrath(int wv, uint8_t *o)
{
    float w = (float)wv / 127.0f;
    if (wv <= 0)
        return;
    o[P_CURSE] = lerp8(o[P_CURSE], o[P_CURSE] >= 64 ? 127 : 0, w * 0.85f);
    o[P_HORDE] = lerp8(o[P_HORDE], o[P_HORDE] >= 64 ? 127 : 0, w * 0.6f);
    o[P_ALCHEMY] = lerp8(o[P_ALCHEMY], 127, w * 0.6f);
    o[P_RUNE] = (uint8_t)iclamp(o[P_RUNE] + (int)(w * 40.0f), 0, 127);
    o[P_HAUNT] = lerp8(o[P_HAUNT], o[P_HAUNT] >= 64 ? 124 : 4, w * 0.7f);
    if (o[P_ECHO] < (uint8_t)(w * 90.0f))
        o[P_ECHO] = (uint8_t)(w * 90.0f);
    o[P_BITS] = lerp8(o[P_BITS], 118, w * w * 0.8f);
    o[P_WANDER] = lerp8(o[P_WANDER], 127, w * 0.6f);
    o[P_AGE] = lerp8(o[P_AGE], 100, w * w * 0.7f);
    o[P_SPEED] = lerp8(o[P_SPEED], 127, w * 0.5f);
}

static void all_params(uint8_t *o)
{
    memcpy(o, set.p[set.algo], FK_NPA);
    memcpy(o + FK_NPA, set.g, FK_NPG);
    wrath(set.wrath, o);
}
static void push_params(void)
{
    uint8_t o[FK_NP];
    uint32_t f;
    uint8_t nx[FK_NP];
    all_params(o);
    memcpy(nx, set.p[(set.algo + 1u) % FK_NALGO], FK_NPA);   /* MORPH: der nächste Algorithmus mit seinen Reglern */
    memcpy(nx + FK_NPA, set.g, FK_NPG);
    wrath(set.wrath, nx);
    f = plat_lock();
    fk_params(o);
    fk_morph(nx + P_CURSE);
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
enum { PG_SUMMON, PG_GROOVE, PG_SETUP, PG_ECHO, PG_RITUAL, PG_LORE, PG_SPELL, PG_OMEN, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"SUMMON", "GROOVE", "SETUP", "ECHO", "RITUAL", "LORE", "SPELL", "OMEN"};
/* was ein Regler ist: ein Parameter der Engine (SRC_P) oder einer des GROOVE (SRC_G) */
enum { SRC_P, SRC_G };
static const uint8_t PAGE_SRC[NPAGES] = {SRC_P, SRC_G, SRC_G, SRC_P, SRC_P, SRC_P, SRC_P, SRC_P};
static const uint8_t PAGE_IDX[NPAGES][4] = {
    {P_CURSE, P_HORDE, P_RUNE, P_ALCHEMY},
    {GR_DENS, GR_BREATH, GR_DIST, GR_TEMPO},
    {GR_LEVEL, GR_HUMAN, GR_FOLLOW, GR_TUNE},
    {P_ECHO, P_HAUNT, P_DECAY, P_DEPTH},
    {P_TORCH, P_PORTAL, P_AWAKEN, P_SLUMBER},
    {P_CREEP, P_WANDER, P_BITS, P_VOLUME},
    {P_AGE, P_AURA, P_SPEED, P_WIDTH},
    {P_RATE, P_SHAPE, P_POWER, P_PATH},
};

static uint8_t *pval(int p) { return p >= FK_NPA ? &set.g[p - FK_NPA] : &set.p[set.algo][p]; }
static uint8_t *kval(int pg, int k)
{
    int i = PAGE_IDX[pg][k];
    return PAGE_SRC[pg] == SRC_G ? &set.grv.p[i] : pval(i);
}
static const char *kname(int pg, int k)
{
    int i = PAGE_IDX[pg][k];
    return PAGE_SRC[pg] == SRC_G ? GR_PARAM_NAME[i] : FK_PARAMS[i].name;
}
/* die Mitte rastet ein (die bipolaren) */
static int ksweet(int pg, int k) { return PAGE_SRC[pg] == SRC_P && FK_PARAMS[PAGE_IDX[pg][k]].bip; }

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
    t[0] = 0;
    if (PAGE_SRC[pg] == SRC_P)
        return fk_value_text(PAGE_IDX[pg][k], v, t);
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
}

/* OMEN läuft im Takt, wenn der GROOVE läuft */
static int fk_synced_ui(void) { return set.grv.on && gr_running(); }

static void knob_turn(int k, int32_t e)
{
    uint8_t *p = kval(ui.page, k);
    int v = *p, nv, src = PAGE_SRC[ui.page], idx = PAGE_IDX[ui.page][k];
    ui.knob_at[k] = plat_ms();
    ui.redraw |= (uint32_t)R_ROW0 << k;
    if (src == SRC_P && (idx == P_SHAPE || idx == P_PATH || (idx == P_RATE && fk_synced_ui()))) {
        /* OMEN: SHAPE, PATH und RATE im Takt springen je Raste eine Stufe weiter (auf die Mitte der Stufe) */
        int nz = idx == P_SHAPE ? FK_NSHAPE : 8, z = iclamp((v * nz >> 7) + (e > 0 ? 1 : -1), 0, nz - 1);
        nv = (z * 128 + 64) / nz;
    } else if (src == SRC_G && idx == GR_FOLLOW) {          /* FOLLOW: an / aus */
        nv = e > 0 ? 127 : 0;
    } else if (src == SRC_G && idx == GR_TUNE) {     /* TUNE: Halbtöne, ±12 */
        nv = iclamp(v + (e > 0 ? 1 : -1), 52, 76);
    } else if (src == SRC_G && idx != GR_TEMPO) {    /* GROOVE: grob */
        nv = v + coarse(EN_K1 + k, e);
    } else {                                         /* wie bei FIKH-S: fein, schnell gedreht schneller */
        int step = (int)accel(EN_K1 + k, e, 1, 4);
        if (src == SRC_P && idx == P_PORTAL) {       /* PORTAL: eine Raste, ein Halbton */
            int st = (int)((float)(v - 64) / 63.0f * 24.0f + (v < 64 ? -0.5f : 0.5f)) + (e > 0 ? 1 : -1);
            st = iclamp(st, -24, 24);
            nv = 64 + (int)((float)st * 63.0f / 24.0f + (st < 0 ? -0.5f : 0.5f));
            if (st == 0)
                nv = 64;
        } else if (ksweet(ui.page, k) && v == 64 && ++ui.rest[k] < 3) {   /* die Mitte rastet ein */
            say_knob(k);
            return;
        } else if (ksweet(ui.page, k) && ((v < 64 && v + step >= 64) || (v > 64 && v + step <= 64))) {
            nv = 64;
            ui.rest[k] = 0;
        } else {
            nv = v + step;
            ui.rest[k] = 0;
        }
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
    ui.trans_at = plat_ms();             /* ein anderer Gang: ein Schritt durchs Dunkel */
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
    app_say(FK_ALGOS[set.algo].name, FK_ALGOS[set.algo].sub);
}

/* ---- die Snapshots: ein ganzer Klang auf einer Taste ---- */
static void snap_store(int s)
{
    snap_t *n = &set.snaps[s];
    char t[8] = "SNAP ";
    n->used = 1;
    n->algo = set.algo;
    n->oct = set.oct;
    memcpy(n->p, set.p[set.algo], FK_NPA);
    memcpy(n->g, set.g, FK_NPG);
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
    memcpy(set.p[set.algo], n->p, FK_NPA);
    memcpy(set.g, n->g, FK_NPG);
    set.snap = (uint8_t)(s + 1);
    all_params(o);
    f = plat_lock();
    if (!same)
        fk_algo(set.algo);               /* (derselbe Algorithmus: die Regler gleiten hinüber) */
    fk_params(o);
    plat_unlock(f);
    dirty();
    ui.redraw |= R_ALL;
    app_say(t, FK_ALGOS[set.algo].name);
}

/* FATE: würfeln – FATE neu (und damit Tabellen, Verstimmung und die Variante) (deep: dazu RUNE und ALCHEMY); der Zufall aus der Zeit */
static void dice(int deep)
{
    static uint32_t r = 0x6A09E667u;
    r ^= plat_ms() * 2654435761u;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    set.p[set.algo][P_FATE] = (uint8_t)(r % 128u);
    if (deep) {
        set.p[set.algo][P_RUNE] = (uint8_t)((r >> 15) % 128u);
        set.p[set.algo][P_ALCHEMY] = (uint8_t)((r >> 23) % 128u);
    }
    push_params();
    dirty();
    ui.dice_at = plat_ms();
    ui.redraw |= R_ALL;
    app_say(deep ? "DEEP FATE" : "FATE", "THE DICE ARE CAST");
}

static void factory(void)
{
    algo_defaults(set.algo);
    push_params();
    dirty();
    ui.redraw |= R_ALL;
    app_say("INIT", FK_ALGOS[set.algo].name);
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
    case B_HOME:                         /* SUMMON, GROOVE, GROOVE SETUP (wie bei BA-55) */
        page_set(ui.page == PG_SUMMON ? PG_GROOVE : ui.page == PG_GROOVE ? PG_SETUP : PG_SUMMON);
        break;
    case B_FX: page_set(PG_ECHO); break;
    case B_ENV: page_set(PG_RITUAL); break;
    case B_EDIT: page_set(PG_LORE); break;
    case B_GLO: page_set(PG_SPELL); break;
    case B_LFO: page_set(PG_OMEN); break;
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
        } else {                                 /* QUEST = MORPH in den nächsten Algorithmus */
            int v = iclamp((int)set.p[set.algo][P_QUEST] + (int)accel(EN_ALGO, e, 1, 4), 0, 127);
            if (v != set.p[set.algo][P_QUEST]) {
                char t[32] = "";
                set.p[set.algo][P_QUEST] = (uint8_t)v;
                push_params();
                dirty();
                if (!v) {
                    text_cat(t, "OFF");
                } else {
                    int_text((v * 100 + 63) / 127, t);
                    text_cat(t, "% > ");
                    text_cat(t, FK_ALGOS[(set.algo + 1u) % FK_NALGO].name);
                }
                app_say("QUEST MORPH", t);
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
    {
        static const uint8_t PB[NPAGES] = {B_HOME, B_HOME, B_HOME, B_FX, B_ENV, B_EDIT, B_GLO, B_LFO};
        btn |= 1u << PB[ui.page];
    }
    if (set.grv.on && gr_running() && now - ui.beat_at < 90u)   /* GROOVE: HOME blitzt mit jedem Schlag */
        btn ^= 1u << B_HOME;
    if (set.hold)
        btn |= LED_PLAY_GREEN;
    if (fk_frozen())
        btn |= 1u << B_ARP;
    if (set_dirty)
        btn |= 1u << B_SAVE;
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
/* jede Seite ihr eigener Raum mit ihrem eigenen Monster (so merkt man sich, wo man ist); die Bilder: art.h
 * SUMMON Ork, GROOVE Skelett, SETUP Schatz, ECHO Kristallkugel, RITUAL Schleim, LORE Betrachter, SPELL Wasser,
 * OMEN die Kristallkugel in Violett, sie pulsiert mit dem LFO;
 * WRATH ab der Hälfte: der Drache; FREEZE: alles eisblau; SAVE gehalten (Snapshots): der Schatz */
enum { SC_ORC, SC_DRAGON, SC_ORB, SC_SKELETON, SC_SLIME, SC_BEHOLDER, SC_TREASURE, SC_WATER };
static const uint8_t PAGE_SCENE[NPAGES] = {SC_ORC, SC_SKELETON, SC_TREASURE, SC_ORB, SC_SLIME, SC_BEHOLDER, SC_WATER,
                                           SC_ORB};
static int snap_mode(void);
static int scene_now(void)
{
    if (snap_mode() == 2)
        return SC_TREASURE;
    if (set.wrath >= 64)
        return SC_DRAGON;
    return PAGE_SCENE[ui.page];
}

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
/* der Blick: das vorgerenderte Bild, das Licht flackert mit, FREEZE macht es eisig, WRATH rot; beim Raumwechsel
 * kurz dunkel (ein Schritt in den nächsten Raum) */
static void view(uint32_t now)
{
    int sc = scene_now();
    const uint8_t *src = ART_SCN[sc];
    const uint16_t *pal = ART_SCN_PAL[sc];
    float t = (float)now * 0.001f, lvl = sat01(fk_level() * 2.5f);
    float beat = set.grv.on && gr_running() ? sat01(1.0f - (float)(now - ui.beat_at) / 180.0f) : 0.0f;
    float fl = 0.93f + 0.05f * fm_sinf(t * 17.0f) + 0.03f * fm_sinf(t * 31.0f) + 0.10f * lvl + 0.10f * beat;
    float wr = (float)set.wrath / 127.0f, tr = 1.0f;
    int frozen = fk_frozen(), omen = scene_now() == SC_ORB && ui.page == PG_OMEN;
    float om = omen ? fk_omen() : 0.0f;
    if (omen)                            /* OMEN: die Kugel pulsiert mit dem LFO */
        fl *= 0.85f + 0.35f * (0.5f + 0.5f * om);
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
            const uint8_t *row = src + (uint32_t)(y - VY0) * ART_VW;
            for (x = xa; x < xb; x++) {
                uint16_t c = pal[row[x - VX0]];
                int32_t r = (int32_t)(c >> 11), g = (int32_t)((c >> 5) & 63u), b = (int32_t)(c & 31u);
                if (frozen) {            /* eisig: zur Helligkeit hin, blau */
                    int32_t l = (r * 2 + g + b * 2) / 5;
                    r = (r + l) / 2;
                    g = (g + l * 2) / 2;
                    b = (b + l * 3) / 2;
                }
                if (omen) {              /* OMEN: violett */
                    int32_t l = (r * 2 + g + b * 2) / 5;
                    r = (r + l) * 5 / 8;
                    g = (g + l) * 3 / 8;
                    b = (b * 3 + l * 2) / 4;
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
static void hero_panel(int k, uint32_t now)
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
    if (!hot && ui.page == PG_SUMMON)
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
        text_cat(l, FK_ALGOS[set.algo].name);
        s_text(11.0f, MY0 + 4.0f, &FONT_XS, l, C_GOLD, 0);
        s_rtext(229.0f, MY0 + 4.0f, &FONT_XS, FK_ALGOS[set.algo].sub, C_TXT_DIM, 0);
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
        if (ui.page == PG_SUMMON && k == 3) {    /* ALCHEMY: was sie hier tut */
            text_cat(l, "  ");
            text_cat(l, FK_ALGOS[set.algo].alch);
        }
        (void)val;
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, l, c565(HERO[k]), 0);
        return;
    }
    if (set.wrath) {
        text_cat(l, "WRATH ");
        int_text((wr * 100 + 63) / 127, l);
        text_cat(l, "%");
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, l, RGB(220, 90, 70), 0);
    } else {
        s_text(11.0f, MY0 + 17.0f, &FONT_XS, "SELECT: WRATH  PRESET: ALGO", C_TXT_DIM, 0);
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
        hero_panel(k, now);
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

/* ---- die Eröffnung: Toms Bild (tools/art/splash.png); die vier Fackeln brennen (Pixel-Flammen wie in den DOS-
 * Spielen), ihr Licht flackert über die Wände ---- */
#define INTRO_MS 4200u
#define SPL_Y ((240 - ART_SH) / 2)
/* die Fackeln im Bild (in seinen Pixeln, 1672 breit): Mitte, Fuß der Flamme, Breite, Höhe */
static const float SPL_TORCH[4][4] = {{102, 476, 104, 128}, {1570, 476, 104, 128}, {490, 314, 46, 66}, {1185, 314, 46, 66}};
static float torch_fl(float t, int i)
{
    return 0.82f + 0.10f * fm_sinf(t * 17.0f + (float)i * 2.1f) + 0.06f * fm_sinf(t * 29.0f + (float)i) +
           0.05f * fm_sinf(t * 7.3f + (float)i * 4.0f);
}
static void pix_flame(float cx, float base, float w, float h, int fr, float on)
{
    const uint8_t *f = ART_FLAME[fr % ART_FN];
    int32_t x, y, x0 = (int32_t)(cx - w * 0.5f), y0 = (int32_t)(base - h);
    for (y = y0; y < (int32_t)base; y++) {
        int j = (int)((float)(y - y0) / h * (float)ART_FH);
        if (y < g_y0 || y >= g_y1 || j < 0 || j >= ART_FH)
            continue;
        for (x = x0; x < x0 + (int32_t)w; x++) {
            int i = (int)((float)(x - x0) / w * (float)ART_FW);
            uint8_t v = f[j * ART_FW + (i < 0 ? 0 : i >= ART_FW ? ART_FW - 1 : i)];
            const uint8_t *c;
            if (v < 2 || x < 0 || x >= 240)
                continue;
            c = ART_FLAME_PAL[v];
            px_blend(x, y, rgb3((float)c[0], (float)c[1], (float)c[2]), on * (v > 3 ? 1.0f : 0.6f));
        }
    }
}
static void intro_scene(float t, int32_t y0, int32_t h)
{
    float on = sat01(t / 0.6f), lx[4], ly[4], li[4];
    int32_t x, y, i;
    g_ox = 0;
    g_x0 = 0;
    g_x1 = 240;
    g_y0 = y0;
    g_y1 = y0 + h;
    cv_oy = -y0;
    for (i = 0; i < 4; i++) {
        lx[i] = SPL_TORCH[i][0] * ART_SSCALE;
        ly[i] = (float)SPL_Y + (SPL_TORCH[i][1] - SPL_TORCH[i][3] * 0.4f) * ART_SSCALE;
        li[i] = torch_fl(t, (int)i) * (i < 2 ? 1.0f : 0.6f);
    }
    for (y = 0; y < h; y++) {
        int32_t sy = y0 + y - SPL_Y;
        uint16_t *row = cv_px + (uint32_t)y * 240u;
        if (sy < 0 || sy >= ART_SH) {
            for (x = 0; x < 240; x++)
                row[x] = 0;
            continue;
        }
        for (x = 0; x < 240; x++) {      /* das Bild, das Licht der Fackeln flackert darüber */
            uint16_t c = ART_SPL_PAL[ART_SPL[(uint32_t)sy * ART_SW + (uint32_t)x]];
            float L = 0.62f;
            int32_t r, g, b, k;
            for (i = 0; i < 4; i++) {
                float dx = (float)x - lx[i], dy = (float)(y0 + y) - ly[i], d2 = dx * dx + dy * dy;
                L += li[i] * 0.55f / (1.0f + d2 * (i < 2 ? 0.0011f : 0.004f));
            }
            k = (int32_t)(L * on * 256.0f);
            r = ((int32_t)(c >> 11) * k) >> 8;
            g = ((int32_t)((c >> 5) & 63u) * k) >> 8;
            b = ((int32_t)(c & 31u) * k) >> 8;
            r = r > 31 ? 31 : r;
            g = g > 63 ? 63 : g;
            b = b > 31 ? 31 : b;
            row[x] = swap16((uint16_t)(r << 11 | g << 5 | b));
        }
    }
    for (i = 0; i < 4; i++) {            /* die Flammen: sie bewegen sich, jede in ihrem Takt */
        float w = SPL_TORCH[i][2] * ART_SSCALE, hh = SPL_TORCH[i][3] * ART_SSCALE * 1.15f;
        float base = (float)SPL_Y + SPL_TORCH[i][1] * ART_SSCALE;
        s_bloom(lx[i], ly[i], (i < 2 ? 26.0f : 14.0f) * li[i], rgb3(255, 140, 40), 0.22f * on * li[i]);
        pix_flame(lx[i], base, w, hh, (int)(t * 13.0f) + (int)i * 3, on);
    }
    if (t > 0.8f)
        s_ctext(120.0f, (float)(SPL_Y + ART_SH + 14), &FONT_SERIF_XS, "8-BIT DRONE SYNTH  BY SWTDR34MZ",
                c565(mix3(rgb3(0, 0, 0), GOLD, sat01((t - 0.8f) / 0.6f))), 1);
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
    m.ec[0] = mem_ec[0];
    m.ec[1] = mem_ec[1];
    m.df = mem_df;
    for (i = 0; i < FK_NRES; i++)
        m.res[i] = mem_res[i];
    settings_load();
    set.hold = 0;                        /* nach dem Start still: HOLD aus, bis man es will */
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
        set.p[set.algo][P_RUNE] = (uint8_t)v;
    else if (cc >= 70 && cc <= 73)
        set.p[set.algo][P_CURSE + cc - 70] = (uint8_t)v;
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
    fk_sync(set.grv.on && gr_running() ? gr_tempo() : 0.0f);   /* OMEN im Takt des GROOVE */
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
        text_cat(t, FK_ALGOS[set.algo].name);
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
    else if (!strncmp(what, "set", 3))   /* setN: der gespeicherte Parameter N (ohne WRATH) */
        *got = *pval(atoi(what + 3) % FK_NP);
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
        *got = set.p[set.algo][P_QUEST];
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9') {   /* pN: der Parameter N der Engine (berechnet) */
        all_params(o);
        *got = o[atoi(what + 1) % FK_NP];
    } else
        return 0;
    return 1;
}
#endif
