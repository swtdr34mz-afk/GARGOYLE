/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE: die Regler, die sechzehn Algorithmen und wie ihre Werte angezeigt werden. Teil von chip_core.c. */

const fk_param_info FK_PARAMS[FK_NP] = {
    {"CURSE", 1}, {"HORDE", 1}, {"RUNE", 0}, {"ALCHEMY", 0},
    {"ECHO", 0}, {"HAUNT", 1}, {"DECAY", 0}, {"DEPTH", 0},
    {"TORCH", 1}, {"PORTAL", 1}, {"AWAKEN", 0}, {"SLUMBER", 0},
    {"CREEP", 0}, {"WANDER", 0}, {"BITS", 0}, {"VOLUME", 0},
    {"FATE", 0}, {"QUEST", 0},
    {"AGE", 0}, {"AURA", 0}, {"SPEED", 0}, {"WIDTH", 0},
    {"RATE", 0}, {"SHAPE", 0}, {"POWER", 0}, {"PATH", 0},
    {"RATE", 0}, {"SHAPE", 0}, {"POWER", 0}, {"PATH", 0},
    {"RATE", 0}, {"SHAPE", 0}, {"POWER", 0}, {"PATH", 0},
    {"RATE", 0}, {"SHAPE", 0}, {"POWER", 0}, {"PATH", 0},
};
const char *const FK_SHAPE_NAME[FK_NSHAPE] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "STAIRS", "CHANCE",
                                                   "DRIFT", "SWELL", "HEART", "BOUNCE", "CHAOS", "TURING"};
const char *const FK_PATH_NAME[FK_NPATH] = {"BREATH", "FLAME", "HAUNT", "AURA", "RUNE", "CURSE", "DECAY", "STORM", "MORPH", "TORCH"};
/* OMEN: RATE frei 0,016 .. 20 Hz; zum GROOVE in Notenwerten (Zyklen je Takt) */
static float omen_hz(int v) { return 0.016f * fm_exp2f((float)v * (10.29f / 127.0f)); }
static const float OMEN_CPB[8] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
static const char *const OMEN_DIV[8] = {"4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32"};
static float omen_sync_bpm;

/* AGE, AURA, SPEED, WIDTH, RATE, SHAPE, POWER, PATH: wie sie anfangen (AURA an: ein Drone von Anfang an; OMEN aus) */
const uint8_t FK_GLOBAL_INIT[FK_NPG] = {10, 64, 50, 96, 64, 0, 0, 6, 40, 0, 0, 19, 80, 0, 0, 57, 30, 0, 0, 108};

/*                                                CURSE HORDE RUNE ALCH  ECHO HAUNT DECAY DEPTH  TORCH PORTAL AWAKEN SLUMB  CREEP WANDER BITS VOL  FATE QUEST */
const fk_algo_info FK_ALGOS[FK_NALGO] = {
    {"TORCHLIGHT", "NES PULSE PAIR", "DUTY SWEEP", {64, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 3, 0}},
    {"CRYPT CHOIR", "GAME BOY", "PULSE / WAVE", {64, 80, 40, 70, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 40, 96, 9, 0}},
    {"GOBLIN CHORDS", "C64 ARPEGGIO", "OCTAVES", {64, 70, 20, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 8, 0}},
    {"DRAGON BREATH", "NES NOISE", "NOISE / PULSE", {64, 72, 40, 70, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 92, 19, 0}},
    {"PC SPEAKER", "1-BIT BEEPER", "REALSOUND", {74, 70, 60, 0, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 50, 90, 5, 0}},
    {"POKEY CURSE", "ATARI POLYS", "2ND CHANNEL", {64, 72, 40, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 90, 13, 0}},
    {"SID RITUAL", "C64 RING / SYNC", "RATIO", {40, 72, 50, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 23, 0}},
    {"ADLIB HALL", "OPL FM", "MULTIPLE", {50, 72, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 54, 96, 29, 0}},
    {"BEHOLDER", "TRACKER VIBRATO", "VIBRATO", {64, 76, 40, 50, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 31, 0}},
    {"TREASURE", "FM BELLS", "BELL RATIO", {64, 70, 50, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 96, 33, 0}},
    {"SKELETON MARCH", "TRIANGLE + TICKS", "DENSITY", {64, 70, 30, 50, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 41, 0}},
    {"MAGE TOWER", "WAVE MORPH", "DRIFT SPEED", {70, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 58, 96, 43, 0}},
    {"THRONE OF BONES", "OCTAVE ARPEGGIO", "OCTAVES", {64, 70, 50, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 47, 0}},
    {"PIT OF DOOM", "DIVIDER LOGIC", "DEPTH", {64, 70, 40, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 64, 92, 53, 0}},
    {"WIND OF ABYSS", "FILTERED NOISE", "STORM SPEED", {40, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 96, 59, 0}},
    {"SPELLBOOK", "RANDOM NOTES", "LEAPS", {70, 70, 30, 40, 64, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 57, 0}},
};

static void vt_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void vt_int(char *d, int v)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    while (*d)
        d++;
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *d++ = '-';
    while (n)
        *d++ = t[--n];
    *d = 0;
}
static void vt_ms(char *t, float ms)
{
    if (ms >= 1000.0f) {
        int d = (int)(ms / 100.0f + 0.5f);
        vt_int(t, d / 10);
        vt_cat(t, ".");
        vt_int(t, d % 10);
        vt_cat(t, "S");
    } else {
        vt_int(t, (int)(ms + 0.5f));
        vt_cat(t, "MS");
    }
}
static void vt_hz(char *t, float hz)
{
    if (hz >= 1000.0f) {
        int k10 = (int)(hz / 100.0f + 0.5f);
        vt_int(t, k10 / 10);
        vt_cat(t, ".");
        vt_int(t, k10 % 10);
        vt_cat(t, "K");
    } else {
        vt_int(t, (int)(hz + 0.5f));
    }
}

const char *fk_value_text(int p, int v, char *t)
{
    float u = (float)v / 127.0f, b = (float)(v - 64) / 63.0f;
    t[0] = 0;
    if (p >= P_OMEN)                         /* OMEN I..IV: alle vier gleich */
        p = P_OMEN + (p - P_OMEN) % 4;
    switch (p) {
    case P_TORCH:
        if (v == 64) {
            vt_cat(t, "OPEN");
        } else if (v < 64) {
            vt_cat(t, "LP ");
            vt_hz(t, 18000.0f * fm_exp2f(b * 9.0f));
        } else {
            vt_cat(t, "HP ");
            vt_hz(t, 20.0f * fm_exp2f(b * 8.5f));
        }
        break;
    case P_PORTAL: {
        int st = (int)(b * 24.0f + (b < 0.0f ? -0.5f : 0.5f));
        if (st > 0)
            vt_cat(t, "+");
        vt_int(t, st);
        vt_cat(t, " ST");
        break;
    }
    case P_AWAKEN: vt_ms(t, fm_exp2f(u * 13.0f)); break;
    case P_SLUMBER: vt_ms(t, 5.0f * fm_exp2f(u * 11.5f)); break;
    case P_CREEP:
        if (u < 0.01f)
            vt_cat(t, "OFF");
        else
            vt_ms(t, 2.0f * fm_exp2f(u * 10.5f));
        break;
    case P_DEPTH: vt_ms(t, 5.0f * fm_exp2f(u * 7.2f)); break;
    case P_BITS: vt_int(t, (int)(16.0f - u * 15.0f + 0.5f)); vt_cat(t, " BIT"); break;
    case P_AGE: vt_hz(t, 44100.0f * fm_exp2f(-u * 4.46f)); vt_cat(t, "HZ"); break;
    case P_SPEED: vt_int(t, (int)(1.0f + u * 59.0f + 0.5f)); vt_cat(t, " HZ"); break;
    case P_RATE:
        if (omen_sync_bpm > 0.0f) {
            vt_cat(t, OMEN_DIV[(v * 8) >> 7]);
        } else {
            float hz = omen_hz(v);
            if (hz < 1.0f) {                 /* langsam: Sekunden je Welle */
                vt_int(t, (int)(1.0f / hz + 0.5f));
                vt_cat(t, " S");
            } else {
                vt_int(t, (int)hz);
                vt_cat(t, ".");
                vt_int(t, (int)((hz - (float)(int)hz) * 10.0f));
                vt_cat(t, " HZ");
            }
        }
        break;
    case P_SHAPE: vt_cat(t, FK_SHAPE_NAME[(v * FK_NSHAPE) >> 7]); break;
    case P_PATH: vt_cat(t, FK_PATH_NAME[(v * FK_NPATH) >> 7]); break;
    case P_POWER:
        if (!v) {
            vt_cat(t, "OFF");
            break;
        }
        vt_int(t, (v * 100 + 63) / 127);
        vt_cat(t, "%");
        break;
    case P_VOLUME: vt_int(t, (int)(-30.0f + 36.0f * u + (u < 0.83f ? -0.5f : 0.5f))); vt_cat(t, "DB"); break;
    case P_HAUNT:
        if (v < 64)
            vt_cat(t, "PP ");
        vt_int(t, (int)((b < 0.0f ? -b : b) * 97.0f + 0.5f));
        vt_cat(t, "%");
        break;
    case P_CURSE:
    case P_HORDE:
        if (v > 64)
            vt_cat(t, "+");
        vt_int(t, v - 64);
        break;
    default:
        if (p == P_QUEST && !v) {
            vt_cat(t, "OFF");
            break;
        }
        vt_int(t, (v * 100 + 63) / 127);
        if (p != P_FATE)
            vt_cat(t, "%");
        break;
    }
    return t;
}
