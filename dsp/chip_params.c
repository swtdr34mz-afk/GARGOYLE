/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (GARGOYLE) */
/* GARGOYLE: die Regler, die sechzehn Algorithmen und wie ihre Werte angezeigt werden. Teil von chip_core.c. */

const fk_param_info FK_PARAMS[FK_NP] = {
    {"CURSE", 1}, {"HORDE", 1}, {"RUNE", 0}, {"ALCHEMY", 0},
    {"ECHO", 0}, {"HAUNT", 1}, {"DECAY", 0}, {"DEPTH", 0},
    {"TORCH", 1}, {"PORTAL", 1}, {"AWAKEN", 0}, {"SLUMBER", 0},
    {"CREEP", 0}, {"WANDER", 0}, {"BITS", 0}, {"VOLUME", 0},
    {"FATE", 0}, {"QUEST", 0},
    {"AGE", 0}, {"VIBRATO", 0}, {"SPEED", 0}, {"WIDTH", 0},
};

/* AGE, VIBRATO, SPEED, WIDTH: wie sie anfangen */
const uint8_t FK_GLOBAL_INIT[FK_NPG] = {10, 14, 50, 96};

/*                                                CURSE HORDE RUNE ALCH  ECHO HAUNT DECAY DEPTH  TORCH PORTAL AWAKEN SLUMB  CREEP WANDER BITS VOL  FATE QUEST */
const fk_algo_info FK_ALGOS[FK_NALGO] = {
    {"TORCHLIGHT", "NES PULSE PAIR", "DUTY SWEEP", {64, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 3, 0}},
    {"CRYPT CHOIR", "GAME BOY", "PULSE / WAVE", {64, 80, 40, 70, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 40, 96, 11, 20}},
    {"GOBLIN CHORDS", "C64 ARPEGGIO", "OCTAVES", {64, 70, 20, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 7, 0}},
    {"DRAGON BREATH", "NES NOISE", "NOISE / PULSE", {64, 72, 40, 70, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 92, 19, 0}},
    {"PC SPEAKER", "1-BIT BEEPER", "REALSOUND", {74, 70, 60, 0, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 50, 90, 5, 0}},
    {"POKEY CURSE", "ATARI POLYS", "2ND CHANNEL", {64, 72, 40, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 90, 13, 0}},
    {"SID RITUAL", "C64 RING / SYNC", "RATIO", {40, 72, 50, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 23, 0}},
    {"ADLIB HALL", "OPL FM", "MULTIPLE", {50, 72, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 54, 96, 29, 0}},
    {"BEHOLDER", "TRACKER VIBRATO", "VIBRATO", {64, 76, 40, 50, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 31, 0}},
    {"TREASURE", "FM BELLS", "BELL RATIO", {64, 70, 50, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 96, 37, 30}},
    {"SKELETON MARCH", "TRIANGLE + TICKS", "DENSITY", {64, 70, 30, 50, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 41, 0}},
    {"MAGE TOWER", "WAVE MORPH", "DRIFT SPEED", {70, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 58, 96, 43, 0}},
    {"THRONE OF BONES", "OCTAVE ARPEGGIO", "OCTAVES", {64, 70, 50, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 47, 0}},
    {"PIT OF DOOM", "DIVIDER LOGIC", "DEPTH", {64, 70, 40, 40, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 64, 92, 53, 0}},
    {"WIND OF ABYSS", "FILTERED NOISE", "STORM SPEED", {40, 74, 40, 30, 60, 100, 74, 94, 46, 64, 66, 96, 30, 58, 56, 96, 59, 0}},
    {"SPELLBOOK", "RANDOM NOTES", "LEAPS", {70, 70, 30, 40, 64, 100, 74, 94, 46, 64, 66, 96, 30, 58, 60, 96, 61, 0}},
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
        vt_int(t, (v * 100 + 63) / 127);
        if (p != P_FATE && p != P_QUEST)
            vt_cat(t, "%");
        break;
    }
    return t;
}
