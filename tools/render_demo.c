/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S, GARGOYLE) */
/* GARGOYLE: der Klangkern (von FIKH-S) allein am Rechner; GROOVE=0..9 in der Umgebung: der GROOVE dazu (dieser Style). Rendert jeden Algorithmus als WAV (Stereo, 16 bit) und misst Pegel,
 * Breite und die Rechenzeit.
 *   cc -O2 -Idsp -Ifm1-base/include tools/render_demo.c dsp/fikh_core.c -lm -o build/render_demo
 *   build/render_demo build/demo [Sekunden] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "fikh_core.h"
#include "garg_groove.h"

static int16_t dl0[FK_DL_N], dl1[FK_DL_N];
static float ch0[FK_CH_N], ch1[FK_CH_N], ap[FK_AP_N], cb[4][FK_CB_N], wt[FK_WT_SETS * FK_WT_WAVES * FK_WT_LEN], sn[FK_SIN_N + 1], rva[FK_RVA_N];
static int16_t rv[FK_RV_N], sh[FK_SH_N];

static void wav_head(FILE *f, int frames)
{
    int sr = 44100, bytes = frames * 4, v;
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    {
        short fmt = 1, chn = 2, ba = 4, bits = 16;
        int br = sr * 4;
        fwrite(&fmt, 2, 1, f);
        fwrite(&chn, 2, 1, f);
        fwrite(&sr, 4, 1, f);
        fwrite(&br, 4, 1, f);
        fwrite(&ba, 2, 1, f);
        fwrite(&bits, 2, 1, f);
    }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    float secs = argc > 2 ? (float)atof(argv[2]) : 12.0f;
    fk_mem m = {{dl0, dl1}, {ch0, ch1}, ap, {cb[0], cb[1], cb[2], cb[3]}, wt, sn, rv, rva, sh};
    int a;
    for (a = 0; a < FK_NALGO; a++) {
        char path[256];
        FILE *f;
        int frames = (int)(secs * 44100.0f), done = 0, bad = 0, blk = 128;
        double sum = 0, side = 0, mid = 0, pk = 0, t0;
        uint8_t p[FK_NP];
        snprintf(path, sizeof path, "%s/algo%d.wav", dir, a + 1);
        f = fopen(path, "wb");
        if (!f)
            return 1;
        wav_head(f, frames);
        fk_init(&m);
        memcpy(p, FK_ALGOS[a].init, FK_NPA);
        memcpy(p + FK_NPA, FK_VERB_INIT, FK_NPV);
        fk_algo(a);
        fk_params(p);
        fk_hold(1);
        fk_note_on(a == 16 ? 31 : 43, 100);   /* G2 (FOLD BASS: G1): ein Drone */
        if (getenv("GROOVE")) {
            gr_set g;
            int i;
            gr_init();
            g.on = 1;
            g.style = (uint8_t)(atoi(getenv("GROOVE")) % GR_NSTYLES);
            for (i = 0; i < GR_NP; i++)
                g.p[i] = GR_INIT[i];
            g.p[GR_DENS] = 127;
            gr_settings(&g);
        }
        t0 = (double)clock();
        while (done < frames) {
            float l[128], r[128];
            int i, n = frames - done < blk ? frames - done : blk;
            double tsec = (double)done / 44100.0;
            /* langsam an den Reglern drehen, wie ein Spieler */
            p[P_MORPH] = (uint8_t)(FK_ALGOS[a].init[P_MORPH] + 30.0 * sin(tsec * 0.35));
            p[P_SCAN] = (uint8_t)(FK_ALGOS[a].init[P_SCAN] + 20.0 * sin(tsec * 0.21 + 1.0));
            if (tsec > secs * 0.5) {          /* zweite Hälfte: WARP und FEED weiter auf, CELL ein wenig */
                p[P_WARP] = 100;
                p[P_FEED] = 104;
                p[P_CELL] = 76;
            }
            if (tsec > secs * 0.75 && tsec < secs * 0.75 + 0.01)
                fk_note_on(36, 100);
            fk_params(p);
            if (getenv("GROOVE")) {
                static float dl[128], dr[128], wl[128], wr[128], dk[128];
                int j;
                gr_render(dl, dr, wl, wr, dk, n, 43.0f);
                fk_ext(dk, wl, wr, gr_send());
                fk_render(l, r, n);
                for (j = 0; j < n; j++) {
                    l[j] += dl[j];
                    r[j] += dr[j];
                }
            } else {
                fk_render(l, r, n);
            }
            for (i = 0; i < n; i++) {
                short s[2];
                if (!isfinite(l[i]) || !isfinite(r[i]))
                    bad++;
                sum += (double)l[i] * l[i] + (double)r[i] * r[i];
                mid += 0.25 * (l[i] + r[i]) * (l[i] + r[i]);
                side += 0.25 * (l[i] - r[i]) * (l[i] - r[i]);
                if (fabs(l[i]) > pk)
                    pk = fabs(l[i]);
                if (fabs(r[i]) > pk)
                    pk = fabs(r[i]);
                s[0] = (short)(l[i] * 32000.0f);
                s[1] = (short)(r[i] * 32000.0f);
                fwrite(s, 2, 2, f);
            }
            done += n;
        }
        fclose(f);
        printf("%d %-12s rms %6.1f dB  peak %5.2f  side/mid %4.2f  cpu %5.2f%% of real time%s\n", a + 1,
               FK_ALGOS[a].name, 10.0 * log10(sum / (2.0 * frames) + 1e-12), pk, sqrt(side / (mid + 1e-12)),
               100.0 * ((double)clock() - t0) / CLOCKS_PER_SEC / secs, bad ? "  NaN!" : "");
    }
    return 0;
}
