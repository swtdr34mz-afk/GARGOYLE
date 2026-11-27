/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE – GROOVE (siehe garg_groove.h). Teil von chip_core.c.
 *
 * Die Geräusche klingen wie die Soundeffekte alter Konsolen und DOS-Spiele, nicht wie ein moderner Synth: ein
 * 4-Bit-Dreieck oder eine Pulswelle (Herz, Trommel, Tropfen), zwei Pulswellen gegeneinander (Glocke, Kette), der
 * Rausch-Generator mit seinem Schieberegister (Schritt, Flügel, Knistern; im kurzen Modus metallisch). Lautstärke in
 * 16 Stufen, Tonhöhe und Lautstärke ändern sich nur 60-mal in der Sekunde (wie die Bildrate damals), am Ende 6 Bit.
 * Jeder Style hat zwei Spuren über einen Takt aus 16 Schritten; DENSITY entscheidet, welche Schritte klingen. */
#include "garg_groove.h"

const char *const GR_STYLE_NAME[GR_NSTYLES] = {"HEARTBEAT", "FOOTSTEPS", "DRIPS", "CHAINS", "WINGS",
                                               "RITUAL", "BELL", "BONES", "RUMBLE", "TORCH"};
const char *const GR_PARAM_NAME[GR_NP] = {"DENSITY", "BREATH", "DISTANCE", "TEMPO", "LEVEL", "HUMAN", "FOLLOW", "TUNE"};
/*                            DENS BRTH DIST TEMPO  LVL HUMAN FOLLOW TUNE */
const uint8_t GR_INIT[GR_NP] = {50, 70, 50, 35, 92, 50, 127, 64};

/* die Geräusche */
enum { H_NONE, H_LUB, H_DUB, H_STEP, H_SCRAPE, H_DRIP, H_CHAIN, H_RATTLE, H_WING, H_DRUM, H_SLAP, H_BELL, H_BELLHI,
       H_BONE, H_BONELO, H_RUMBLE, H_GROWL, H_CRACK, H_POP };

/* Muster: 'X' immer (betont: die Pumpe), 'x' ab wenig DENSITY, 'o' ab der Hälfte, 'g' erst ganz dicht (leise),
 * '?' zufällig (je dichter, desto öfter), '.' nichts */
typedef struct {
    const char *a, *b;                   /* zwei Spuren, je 16 Schritte */
    uint8_t ha, hb;                      /* ihre Geräusche */
    uint8_t wander;                      /* 1: es kommt näher und geht (Schritte), 2: es kreist (Flügel) */
} gr_style;

static const gr_style STY[GR_NSTYLES] = {
    {"X...x...X...x...", ".X...x...X...x..", H_LUB, H_DUB, 0},          /* HEARTBEAT */
    {"X...x...X...x...", "..g...?...g...?.", H_STEP, H_SCRAPE, 1},       /* FOOTSTEPS */
    {"?..?.?..x..?.?..", "X.......o.....?.", H_DRIP, H_DRIP, 0},         /* DRIPS */
    {"X.....?.x...o.?.", "..g.?...o..?..g.", H_CHAIN, H_RATTLE, 0},      /* CHAINS */
    {"X.......x.......", "....o.......?...", H_WING, H_WING, 2},         /* WINGS */
    {"X..x..x.X..x.o..", "....o.......g.?.", H_DRUM, H_SLAP, 0},         /* RITUAL */
    {"X...............", "........o...?...", H_BELL, H_BELLHI, 0},       /* BELL */
    {"x.g.x?g.x.go.?g.", "X.......o.......", H_BONE, H_BONELO, 0},       /* BONES */
    {"X...............", "........x...?...", H_RUMBLE, H_GROWL, 0},      /* RUMBLE */
    {"????????????????", "......?.......o.", H_CRACK, H_POP, 0},         /* TORCH */
};

#define GR_NV 10
typedef struct {
    int on, wait;                        /* wait: so viele Samples noch bis zum Einsatz (Zufall, Wiederholung) */
    uint8_t type, duck;
    float vel, env, atk, dec, gain;      /* env 0..1; atk: Schritt je Sample im Anstieg; dec: Faktor je Sample */
    int rising;
    float ph, f, fend, pe, pdec;         /* Tonhöhe: f -> fend (pe 1 -> 0) */
    float ph2, ring;                     /* zwei Pulswellen gegeneinander: Verhältnis (0: keine) */
    float tone, noise, pan;
    lfsr_t ln;                           /* der Rausch-Generator */
    float nf, nq, nsweep;                /* sein Takt (Hz), nq > 0.6: kurzer Modus (metallisch), Faktor je Block */
    int frame;                           /* bis zum nächsten "Bild" (60 je Sekunde) */
    float fcur, ecur;                    /* Tonhöhe und Lautstärke, nur je Bild neu */
    int reps, rgap;                      /* Wiederholungen und ihr Abstand (Samples) */
    float rfall;                         /* jede Wiederholung leiser */
} gr_voice;

static struct {
    gr_set s;
    int running, step, ext_stop;
    float pos, ss;                       /* im Schritt (Samples), die Länge eines Schritts */
    uint32_t ticks, since_clock, hits, rng, bar;
    int clock_step;                      /* die MIDI-Clock hat einen Schritt fällig */
    gr_voice v[GR_NV];
    float dk, dkv, dk_rel;               /* die Pumpe */
    float lp[2][2];                      /* FERNE: zwei Pole Tiefpass je Kanal */
    float envm, wph;                     /* die Anzeige; das Wandern */
    int alt;                             /* Schritte: links, rechts */
    int at;                              /* der Schritt fällt auf dieses Sample des Blocks */
} G;

static float gr_db_coef(float sec) { return fm_exp2f(-1.4427f / (sec * FK_SR + 1.0f)); }   /* exp(-1/(t*SR)) */
static float gr_p(int i) { return (float)G.s.p[i] * (1.0f / 127.0f); }

int gr_bpm(int v) { return 30 + (v * 110 + 63) / 127; }   /* 30 .. 140 */

void gr_init(void)
{
    int i;
    fk_zero(&G, sizeof G);
    G.s.style = 0;
    for (i = 0; i < GR_NP; i++)
        G.s.p[i] = GR_INIT[i];
    G.step = -1;
    G.rng = 0x5EEDu;
    G.since_clock = 1u << 30;
}

void gr_settings(const gr_set *s)
{
    int was = G.s.on;
    G.s = *s;
    if (G.s.style >= GR_NSTYLES)
        G.s.style = 0;
    if (G.s.on && !was) {                /* an: sofort, von vorn (ein MIDI-Stopp gilt nicht mehr) */
        G.step = -1;
        G.pos = 0.0f;
        G.ext_stop = 0;
    }
}

void gr_clock(void)
{
    G.since_clock = 0;
    if (++G.ticks >= 6u) {               /* 24 je Viertel: 6 je Sechzehntel */
        G.ticks = 0;
        G.clock_step = 1;
    }
}
void gr_start(void)
{
    G.ext_stop = 0;
    G.step = -1;
    G.ticks = 0;
    G.clock_step = 1;
    G.pos = 0.0f;
}
void gr_stop(void) { G.ext_stop = 1; }

/* die Wurzel eines Geräuschs: der Grundton in die Oktave 55..110 Hz gefaltet */
static float gr_root_hz(float midi)
{
    float hz = 440.0f * fm_exp2f((midi - 69.0f) * (1.0f / 12.0f));
    while (hz > 110.0f)
        hz *= 0.5f;
    while (hz < 55.0f)
        hz *= 2.0f;
    return hz;
}

static gr_voice *gr_alloc(void)
{
    int i, best = 0;
    float lo = 9.0f;
    for (i = 0; i < GR_NV; i++)
        if (!G.v[i].on)
            return &G.v[i];
    for (i = 0; i < GR_NV; i++)          /* alle belegt: die leiseste */
        if (G.v[i].env * G.v[i].vel < lo) {
            lo = G.v[i].env * G.v[i].vel;
            best = i;
        }
    return &G.v[best];
}

static void gr_hit(int type, float vel, float rl, int wait, int accent)
{
    gr_voice *v;
    float r = fk_rand01(&G.rng);
    if (type == H_NONE || vel <= 0.0f)
        return;
    if (type == H_DRIP || type == H_CRACK || type == H_RATTLE || type == H_SCRAPE || type == H_BELLHI || type == H_POP
        || type == H_BONE)
        accent = 0;                      /* die kleinen Geräusche pumpen nicht */
    v = gr_alloc();
    fk_zero(v, sizeof *v);
    v->on = 1;
    v->wait = wait;
    v->type = (uint8_t)type;
    v->vel = vel;
    v->rising = 1;
    v->duck = (uint8_t)accent;
    v->gain = 1.0f;
    v->nq = 0.3f;
    v->nsweep = 1.0f;
    v->pdec = gr_db_coef(0.03f);
    switch (type) {
    case H_LUB:                          /* das Herz: ein tiefer, weicher Schlag */
        v->f = rl * 2.0f; v->fend = rl * 0.75f; v->pdec = gr_db_coef(0.025f);
        v->atk = 3.0f; v->dec = 0.16f; v->tone = 1.0f; v->noise = 0.25f; v->nf = 110.0f; break;
    case H_DUB:
        v->f = rl * 2.4f; v->fend = rl * 0.9f; v->pdec = gr_db_coef(0.02f);
        v->atk = 3.0f; v->dec = 0.12f; v->tone = 1.0f; v->noise = 0.2f; v->nf = 130.0f; v->gain = 0.72f; break;
    case H_STEP:                         /* ein schwerer Schritt auf Stein: links, rechts */
        v->f = rl * 1.3f; v->fend = rl * 0.8f; v->atk = 2.0f; v->dec = 0.13f; v->tone = 0.55f;
        v->noise = 0.9f; v->nf = 260.0f + 120.0f * r; v->nq = 0.35f; v->gain = 0.8f;
        v->pan = (G.alt ^= 1) ? -0.45f : 0.45f; break;
    case H_SCRAPE:                       /* etwas schleift über den Boden */
        v->atk = 45.0f; v->dec = 0.18f; v->noise = 1.0f; v->nf = 1700.0f; v->nq = 0.25f; v->nsweep = 0.985f;
        v->gain = 0.3f; v->pan = (r - 0.5f) * 1.2f; break;
    case H_DRIP: {                       /* ein Tropfen: der Ton springt nach oben, kurz, viel Hall */
        static const float DEG[5] = {1.0f, 1.5f, 2.0f, 1.2f, 2.25f};
        float f0 = rl * 8.0f * DEG[(int)(r * 4.99f)];
        v->f = f0; v->fend = f0 * 2.3f; v->pdec = gr_db_coef(0.008f);
        v->atk = 1.0f; v->dec = 0.07f; v->tone = 1.0f; v->gain = 0.55f; v->pan = (fk_rand01(&G.rng) - 0.5f) * 1.4f;
        break;
    }
    case H_CHAIN:                        /* Kettenglieder: Metall, mehrmals kurz hintereinander */
    case H_RATTLE:
        v->f = rl * (type == H_CHAIN ? 23.0f : 31.0f); v->fend = v->f; v->ring = 1.41f;
        v->atk = 1.0f; v->dec = type == H_CHAIN ? 0.045f : 0.025f; v->tone = 0.6f; v->noise = 0.8f;
        v->nf = type == H_CHAIN ? 3800.0f : 5200.0f; v->nq = 0.85f;
        v->reps = type == H_CHAIN ? 2 + (int)(r * 4.0f) : 1 + (int)(r * 2.0f);
        v->rgap = (int)((0.022f + 0.022f * fk_rand01(&G.rng)) * FK_SR); v->rfall = 0.78f;
        v->gain = type == H_CHAIN ? 0.42f : 0.24f; v->pan = (r - 0.5f) * 0.9f; break;
    case H_WING:                         /* ein Flügelschlag: ein Luftstoß, der nach unten zieht, zweimal */
        v->atk = 70.0f; v->dec = 0.22f; v->noise = 1.0f; v->nf = 700.0f; v->nq = 0.2f; v->nsweep = 0.97f;
        v->reps = 1; v->rgap = (int)(0.19f * FK_SR); v->rfall = 0.7f; v->gain = 0.55f; break;
    case H_DRUM:                         /* eine Rahmentrommel */
        v->f = rl * 2.2f; v->fend = rl * 1.5f; v->pdec = gr_db_coef(0.04f);
        v->atk = 2.0f; v->dec = 0.32f; v->tone = 1.0f; v->noise = 0.25f; v->nf = 700.0f; v->gain = 0.85f; break;
    case H_SLAP:
        v->f = rl * 4.0f; v->fend = rl * 3.6f; v->atk = 1.0f; v->dec = 0.045f; v->tone = 0.5f; v->noise = 0.9f;
        v->nf = 1200.0f; v->nq = 0.5f; v->gain = 0.5f; v->pan = 0.25f; break;
    case H_BELL:                         /* eine tiefe Glocke in der Ferne */
    case H_BELLHI:
        v->f = rl * (type == H_BELL ? 4.0f : 12.0f); v->fend = v->f; v->ring = 2.76f;
        v->atk = 1.0f; v->dec = type == H_BELL ? 2.4f : 1.5f; v->tone = 1.0f; v->gain = type == H_BELL ? 0.5f : 0.22f;
        v->pan = type == H_BELL ? -0.15f : 0.35f; break;
    case H_BONE:                         /* Holz und Knochen: trockenes Klappern */
    case H_BONELO:
        v->f = rl * (type == H_BONE ? 14.0f : 7.0f); v->fend = v->f * 0.85f; v->pdec = gr_db_coef(0.01f);
        v->atk = 0.5f; v->dec = type == H_BONE ? 0.022f : 0.035f; v->tone = 0.6f; v->noise = 0.8f;
        v->nf = type == H_BONE ? 1800.0f : 900.0f; v->nq = 0.88f; v->gain = 0.55f;
        if (type == H_BONE && r > 0.6f) {
            v->reps = 1;
            v->rgap = (int)(0.06f * FK_SR);
            v->rfall = 0.6f;
        }
        v->pan = (fk_rand01(&G.rng) - 0.5f) * 1.0f; break;
    case H_RUMBLE:                       /* das Gewölbe bebt */
    case H_GROWL:
        v->f = rl * 0.6f; v->fend = rl * 0.5f; v->pdec = gr_db_coef(0.3f);
        v->atk = 60.0f; v->dec = type == H_RUMBLE ? 1.2f : 0.5f; v->tone = 1.0f; v->noise = 0.7f; v->nf = 70.0f;
        v->nq = 0.2f; v->gain = type == H_RUMBLE ? 1.0f : 0.6f; break;
    case H_CRACK:                        /* die Fackel knistert */
        v->atk = 0.3f; v->dec = 0.004f + 0.004f * r; v->noise = 1.0f; v->nf = 2500.0f + 2500.0f * r; v->nq = 0.3f;
        v->reps = (int)(fk_rand01(&G.rng) * 3.0f); v->rgap = (int)((0.008f + 0.012f * r) * FK_SR); v->rfall = 0.8f;
        v->gain = 0.5f; v->pan = (fk_rand01(&G.rng) - 0.5f) * 1.4f; break;
    default:                             /* H_POP: ein Funke springt */
        v->f = rl * 6.0f; v->fend = rl * 5.0f; v->atk = 0.5f; v->dec = 0.015f; v->tone = 0.4f; v->noise = 0.9f;
        v->nf = 900.0f; v->nq = 0.5f; v->gain = 0.6f; v->pan = (r - 0.5f) * 1.0f; break;
    }
    v->atk = 1.0f / (v->atk * 0.001f * FK_SR + 1.0f);
    v->dec = gr_db_coef(v->dec);
    v->pe = 1.0f;
    v->ln.r = (uint16_t)(1u + (fk_rng(&G.rng) & 0x7FFEu));
    v->fcur = v->f;
    G.hits++;
}

/* ein Schritt des Musters: welche Spuren klingen, wie laut, mit wie viel Zufall */
static void gr_do_step(float rl)
{
    const gr_style *st = &STY[G.s.style];
    float dens = gr_p(GR_DENS), human = gr_p(GR_HUMAN);
    int lane;
    for (lane = 0; lane < 2; lane++) {
        char c = (lane ? st->b : st->a)[G.step & 15];
        float vel = 0.0f;
        int wait;
        switch (c) {
        case 'X': vel = 1.0f; break;
        case 'x': vel = dens > 0.18f ? 0.85f : 0.0f; break;
        case 'o': vel = dens > 0.45f ? 0.7f : 0.0f; break;
        case 'g': vel = dens > 0.7f ? 0.45f : 0.0f; break;
        case '?': vel = fk_rand01(&G.rng) < 0.12f + 0.6f * dens ? 0.4f + 0.4f * fk_rand01(&G.rng) : 0.0f; break;
        default: break;
        }
        if (vel <= 0.0f)
            continue;
        vel *= 1.0f - human * 0.35f * fk_rand01(&G.rng);
        wait = (int)(fk_rand01(&G.rng) * human * 0.22f * G.ss);   /* HUMAN: etwas zu spät, nie zu früh */
        if (st->wander == 1) {           /* die Schritte kommen näher und gehen wieder (acht Takte) */
            float w = 0.5f + 0.5f * fk_sin(G.wph);
            vel *= 0.3f + 0.7f * w;
        }
        gr_hit(lane ? st->hb : st->ha, vel, rl, G.at + wait, c == 'X' || (c == 'x' && lane == 0));
        if (st->wander == 2) {   /* die Flügel kreisen: das Panorama wandert */
            int i;
            for (i = 0; i < GR_NV; i++)
                if (G.v[i].on && G.v[i].type == H_WING && G.v[i].rising && G.v[i].env == 0.0f)
                    G.v[i].pan = fk_sin(G.wph) * 0.8f;
        }
    }
}

/* eine Stimme über den ganzen Block (Stimme für Stimme: schneller als Sample für Sample über alle Stimmen) */
static void gr_voice_block(gr_voice *v, float *l, float *r, float *trig, int n)
{
    int i;
    float pl = 1.0f - (v->pan > 0.0f ? v->pan : 0.0f), pr = 1.0f + (v->pan < 0.0f ? v->pan : 0.0f);
    float g = v->vel * v->gain;
    for (i = 0; i < n; i++) {
        float s = 0.0f, a;
        if (v->wait) {
            v->wait--;
            continue;
        }
        if (v->rising) {
            v->env += v->atk;
            if (v->env >= 1.0f) {
                v->env = 1.0f;
                v->rising = 0;
                if (v->duck && trig[i] < v->vel)
                    trig[i] = v->vel;    /* die Pumpe: der Drone weicht dem Schlag */
            }
        } else {
            v->env *= v->dec;
        }
        v->pe *= v->pdec;
        if (--v->frame <= 0) {           /* ein neues Bild: Tonhöhe und Lautstärke in Stufen */
            v->frame = 735;
            v->fcur = v->fend + (v->f - v->fend) * v->pe;
            v->ecur = (float)(int)(v->env * 15.0f + 0.5f) * (1.0f / 15.0f);
        }
        if (v->tone > 0.0f) {
            float f = v->fcur, t;
            v->ph += f * (1.0f / FK_SR);
            v->ph -= (float)(int)v->ph;
            t = f < 260.0f ? w_tri4(v->ph) : w_pulse(v->ph, 0.25f);
            if (v->ring > 0.0f) {
                v->ph2 += f * v->ring * (1.0f / FK_SR);
                v->ph2 -= (float)(int)v->ph2;
                t = (w_pulse(v->ph, 0.5f) + w_pulse(v->ph2, 0.5f)) * 0.5f;
            }
            s += t * v->tone * 0.8f;
        }
        if (v->noise > 0.0f)
            s += w_noise(&v->ln, v->nf * 4.0f, v->nq > 0.6f) * v->noise * 0.55f;
        a = s * v->ecur * g;
        l[i] += a * pl;
        r[i] += a * pr;
        if (!v->rising && v->env < 0.0005f) {
            if (v->reps > 0) {           /* noch einmal (die Kette, der zweite Flügelschlag) */
                v->reps--;
                v->wait = v->rgap;
                v->vel *= v->rfall;
                g = v->vel * v->gain;
                v->env = 0.0f;
                v->rising = 1;
                v->pe = 1.0f;
                if (v->type == H_WING) {
                    v->nf = 700.0f;
                }
            } else {
                v->on = 0;
                break;
            }
        }
    }
}

void gr_render(float *dl, float *dr, float *wl, float *wr, float *duck, int n, float root_midi)
{
    int i, k, follow = G.s.p[GR_FOLLOW] >= 64;
    float tune = fk_clamp((float)((int)G.s.p[GR_TUNE] - 64), -12.0f, 12.0f), rl, lvl, dist, lpc, br, rel;
    float bpm = (float)gr_bpm(G.s.p[GR_TEMPO]);
    int synced = G.since_clock < (uint32_t)(0.5f * FK_SR);
    rl = gr_root_hz((follow ? root_midi : 48.0f) + tune);
    G.ss = FK_SR * 60.0f / (bpm * 4.0f);
    G.running = G.s.on && !G.ext_stop;
    lvl = gr_p(GR_LEVEL);
    lvl = lvl * lvl * 0.8f;
    dist = gr_p(GR_DIST);
    lpc = fk_lp_coef(18000.0f * fm_exp2f(-dist * 5.0f));
    br = gr_p(GR_BREATH);
    rel = gr_db_coef(fk_clamp(0.5f * 60.0f / bpm, 0.12f, 0.9f));
    /* die Rauschfilter wandern je Block (Flügel, Schleifen) */
    for (k = 0; k < GR_NV; k++) {
        gr_voice *v = &G.v[k];
        if (v->on && v->noise > 0.0f && v->nsweep != 1.0f && !v->wait)
            v->nf *= v->nsweep;
    }
    G.wph += (float)n / (G.ss * 16.0f * 8.0f);   /* acht Takte */
    G.wph -= fm_floorf(G.wph);
    /* der Takt: frei oder mit der MIDI-Clock; ein Schritt mitten im Block setzt seine Geräusche dorthin */
    if (G.running) {
        for (i = 0; i < n; i++) {
            int adv = 0;
            if (synced) {
                if (G.clock_step) {
                    G.clock_step = 0;
                    adv = 1;
                }
            } else if (G.step < 0 || (G.pos += 1.0f) >= G.ss) {
                G.pos = 0.0f;
                adv = 1;
            }
            if (adv) {
                G.step = (G.step + 1) & 15;
                if (!G.step)
                    G.bar++;
                G.at = i;
                gr_do_step(rl);
            }
        }
    } else {
        G.clock_step = 0;
    }
    G.since_clock += (uint32_t)n;
    for (i = 0; i < n; i++)
        wl[i] = wr[i] = duck[i] = 0.0f;  /* duck: hier erst die Auslöser */
    for (k = 0; k < GR_NV; k++)
        if (G.v[k].on)
            gr_voice_block(&G.v[k], wl, wr, duck, n);
    for (i = 0; i < n; i++) {
        float l = (float)(int)(wl[i] * lvl * 32.0f) * (1.0f / 32.0f), r = (float)(int)(wr[i] * lvl * 32.0f) * (1.0f / 32.0f), m;   /* 6 Bit */
        wl[i] = l;
        wr[i] = r;
        G.lp[0][0] += (l - G.lp[0][0]) * lpc;   /* FERNE: weiter weg, dumpfer und leiser */
        G.lp[0][1] += (G.lp[0][0] - G.lp[0][1]) * lpc;
        G.lp[1][0] += (r - G.lp[1][0]) * lpc;
        G.lp[1][1] += (G.lp[1][0] - G.lp[1][1]) * lpc;
        dl[i] = G.lp[0][1] * (1.0f - 0.65f * dist);
        dr[i] = G.lp[1][1] * (1.0f - 0.65f * dist);
        m = fk_abs(l) > fk_abs(r) ? fk_abs(l) : fk_abs(r);
        G.envm += (m - G.envm) * (m > G.envm ? 0.02f : 0.0003f);
        /* die Pumpe: schnell hinein (2 ms), weich wieder heraus (eine halbe Zählzeit) */
        if (duck[i] > G.dk)
            G.dk = duck[i];
        G.dkv += (G.dk - G.dkv) * (G.dk > G.dkv ? 0.011f : 1.0f);
        G.dk *= rel;
        duck[i] = 1.0f - br * 0.78f * G.dkv;
    }
    for (k = 0; k < 2; k++) {
        G.lp[k][0] = fm_flush(G.lp[k][0]);
        G.lp[k][1] = fm_flush(G.lp[k][1]);
    }
}

float gr_send(void) { return G.s.on || G.envm > 0.0005f ? 0.08f + 0.85f * gr_p(GR_DIST) : 0.0f; }
int gr_running(void) { return G.running; }
int gr_step(void) { return G.running ? G.step : -1; }
uint32_t gr_hits(void) { return G.hits; }
float gr_env(void) { return G.envm; }
float gr_pump(void) { return G.dkv * gr_p(GR_BREATH); }
int gr_synced(void) { return G.since_clock < (uint32_t)(0.5f * FK_SR); }
