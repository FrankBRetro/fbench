/*
 * Fbench - pdfrep.c : PDF reports
 *
 *  blit_report()     one machine, the same tests with the blitter off and on
 *  profiles_report() every *.FBP profile in a folder: raw results, results
 *                    relative to a reference machine, and results normalised
 *                    for test-window size, desktop size and colour depth
 */
#include "fbench.h"
#include "pdf.h"

#define LM      40              /* left margin                     */
#define RM      555             /* right edge                      */
#define TOPY    72              /* first content line on a page    */
#define BOTY    800             /* last content line on a page     */
#define MAX_PROF 32

typedef struct {
    PDF  *p;
    const char *title;
    short y;
} REP;

/* ------------------------------------------------------------ layout */

static void rep_page(REP *r)
{
    char t[16];
    pdf_page(r->p);
    pdf_rect(r->p, 0, 0, PDF_W, 6, 32, 64, 128);
    {
        char v[24];
        sprintf(v, "Fbench %s", FB_VERSION);
        pdf_text(r->p, LM, 30, PF_HELVB, 9, v, 32, 64, 128);
    }
    pdf_text_r(r->p, RM, 30, PF_HELV, 9, r->title, 90, 90, 90);
    pdf_line(r->p, LM, 38, RM, 38, 1, 180, 180, 180);
    sprintf(t, "Page %d", pdf_pageno(r->p));
    pdf_text_c(r->p, PDF_W / 2, 825, PF_HELV, 8, t, 120, 120, 120);
    r->y = TOPY;
}

static void need(REP *r, short h)
{
    if (r->y + h > BOTY) rep_page(r);
}

static void heading(REP *r, const char *s)
{
    need(r, 130);           /* keep a heading with the start of its section */
    r->y += 8;
    pdf_text(r->p, LM, r->y, PF_HELVB, 15, s, 32, 64, 128);
    r->y += 6;
    pdf_line(r->p, LM, r->y, RM, r->y, 1, 32, 64, 128);
    r->y += 16;
}

static void subheading(REP *r, const char *s)
{
    need(r, 80);            /* heading + a few rows, never alone at the foot */
    r->y += 4;
    pdf_text(r->p, LM, r->y, PF_HELVB, 11, s, 40, 40, 40);
    r->y += 14;
}

/* simple word-wrapping paragraph */
static void para(REP *r, const char *s, short size)
{
    char line[200];
    short n = 0, maxw = RM - LM;
    const char *w = s;
    line[0] = 0;
    while (*w) {
        const char *e = w;
        char word[80];
        short wl;
        while (*e && *e != ' ') e++;
        wl = (short)(e - w);
        if (wl > 78) wl = 78;
        memcpy(word, w, wl); word[wl] = 0;
        {
            char tryl[200];
            sprintf(tryl, "%s%s%s", line, n ? " " : "", word);
            if (pdf_textw(PF_HELV, size, tryl) > maxw && n) {
                need(r, size + 4);
                pdf_text(r->p, LM, r->y, PF_HELV, size, line, 50, 50, 50);
                r->y += size + 4;
                strcpy(line, word);
                n = 1;
            } else {
                strcpy(line, tryl);
                n++;
            }
        }
        w = *e ? e + 1 : e;
    }
    if (n) {
        need(r, size + 4);
        pdf_text(r->p, LM, r->y, PF_HELV, size, line, 50, 50, 50);
        r->y += size + 4;
    }
    r->y += 4;
}

static void kv(REP *r, short x, const char *k, const char *v)
{
    pdf_text(r->p, x, r->y, PF_HELVB, 9, k, 60, 60, 60);
    pdf_text(r->p, x + 95, r->y, PF_HELV, 9, v, 20, 20, 20);
}

/* machine colours (Tableau-like, readable in print) */
static const short pal[8][3] = {
    {31,119,180}, {255,127,14}, {44,160,44}, {214,39,40},
    {148,103,189}, {140,86,75}, {227,119,194}, {127,127,127}
};
#define PC(i) pal[(i) & 7][0], pal[(i) & 7][1], pal[(i) & 7][2]

/* "87%", "4250%"; from 10000% up as a multiple ("280x") so the columns
   stay narrow when an emulator or a fast machine meets a slow baseline */
static void fmt_pct(char *o, long pct)
{
    if (pct < 0)            strcpy(o, "-");
    else if (pct >= 10000L) sprintf(o, "%ldx", pct / 100);
    else                    sprintf(o, "%ld%%", pct);
}

/* horizontal bar with value label; 'scale' = value at full width */
static void hbar(REP *r, short x, short w, long v, long scale, short h, short cr, short cg, short cb)
{
    short bw;
    if (scale <= 0) scale = 1;
    bw = (short)(v * (long)w / scale);
    if (bw > w) bw = w;
    if (v > 0 && bw < 1) bw = 1;
    pdf_rect(r->p, x, r->y - h + 2, bw, h, cr, cg, cb);
}

/* two-colour bar: the baseline's share (left) against this machine's
   (right), split where they meet - past the middle = this one is faster.
   pct = this machine as % of the baseline */
static void split_bar(REP *r, short x, short y, short w, short h, long pct,
                      short br, short bg, short bb, short tr, short tg, short tb)
{
    short bw;
    if (pct < 0) return;
    bw = (short)(100L * w / (100L + pct));
    if (bw < 1) bw = 1;
    if (bw > w - 1) bw = w - 1;
    pdf_rect(r->p, x, y, bw, h, br, bg, bb);
    pdf_rect(r->p, x + bw, y, w - bw, h, tr, tg, tb);
    pdf_line(r->p, x + bw, y, x + bw, y + h, 1, 20, 20, 20);
}

/* ================================================================== */
/*  1. Blitter off / on report                                        */
/* ================================================================== */

static void sys_block(REP *r)
{
    static const char *want[] = { "Machine", "CPU", "TOS", "MiNT", "AES ver", "Multitask",
                                  "Resolution", "Colours", "GDOS", "Fonts", NULL };
    short i, k, col = 0, y0 = r->y;
    for (i = 0; i < nsys; i++) {
        if (sysline[i][0] == '#') continue;
        for (k = 0; want[k]; k++)
            if (!strncmp(sysline[i], want[k], strlen(want[k]))) break;
        if (!want[k]) continue;
        {
            char key[20], *c = strchr(sysline[i], ':'), val[80];
            short n = c ? (short)(c - sysline[i]) : 0;
            while (n > 0 && sysline[i][n - 1] == ' ') n--;
            if (n > 19) n = 19;
            memcpy(key, sysline[i], n); key[n] = 0;
            pdf_fit(PF_HELV, 9, c ? c + 2 : sysline[i], 150, val, sizeof(val));
            kv(r, col ? 300 : LM, key, val);
            if (col) r->y += 13;
            col = !col;
        }
    }
    if (col) r->y += 13;
    if (r->y == y0) r->y += 4;
    r->y += 6;
}

short blit_report(const char *path, const RESULT *off, const RESULT *on)
{
    REP R, *r = &R;
    char t[320], a[24], b[24];
    short g, i;
    long gsum[NGROUPS], gcnt[NGROUPS], best_i[10], best_v[10];
    short nbest = 0;

    r->p = pdf_create(path);
    if (!r->p) return 0;
    r->title = "Blitter off / on comparison";
    prog_set(5, "Collecting system information");
    sysinfo_collect();
    rep_page(r);

    /* title block */
    pdf_text(r->p, LM, r->y + 10, PF_HELVB, 22, "Blitter off / on", 20, 20, 20);
    r->y += 28;
    if (machine_desc[0]) {
        pdf_text(r->p, LM, r->y, PF_HELVB, 13, machine_desc, 32, 64, 128);
        r->y += 16;
    }
    sysinfo_short(t, sizeof(t));
    pdf_text(r->p, LM, r->y, PF_HELV, 11, t, 70, 70, 70);
    r->y += 22;
    sys_block(r);
    sprintf(t, "Each test was run for %ld.%ld s with the blitter disabled, then again with it enabled "
               "(XBIOS 64). Speed-up = blitter-on operations per second as a percentage of blitter-off; "
               "above 100%% means the blitter helps.",
            dur_ticks / 200, (dur_ticks % 200) / 20);
    para(r, t, 9);

    /* group summary */
    for (g = 0; g < NGROUPS; g++) { gsum[g] = 0; gcnt[g] = 0; }
    for (i = 0; i < ntests; i++) {
        long pct;
        if (off[i].state != RS_OK || on[i].state != RS_OK || off[i].score <= 0) continue;
        pct = on[i].score * 100L / off[i].score;
        gsum[tests[i].group] += pct; gcnt[tests[i].group]++;
        /* keep the 10 biggest speed-ups, sorted */
        {
            short k;
            if (nbest < 10) k = nbest++;
            else if (pct > best_v[9]) k = 9;
            else continue;
            best_v[k] = pct; best_i[k] = i;
            while (k > 0 && best_v[k] > best_v[k - 1]) {
                long tv = best_v[k], ti = best_i[k];
                best_v[k] = best_v[k - 1]; best_i[k] = best_i[k - 1];
                best_v[k - 1] = tv; best_i[k - 1] = ti; k--;
            }
        }
    }

    subheading(r, "Average speed-up per group");
    for (g = 0; g < NGROUPS; g++) {
        long avg = gcnt[g] ? gsum[g] / gcnt[g] : -1;
        need(r, 18);
        pdf_text(r->p, LM, r->y, PF_HELV, 10, group_name[g], 30, 30, 30);
        if (avg >= 0) {
            pdf_rect(r->p, 190, r->y - 9, 300, 11, 235, 235, 235);
            if (avg >= 105)     hbar(r, 190, 300, avg, 400, 11, 44, 160, 44);     /* faster    */
            else if (avg <= 95) hbar(r, 190, 300, avg, 400, 11, 214, 39, 40);     /* slower    */
            else                hbar(r, 190, 300, avg, 400, 11, 150, 150, 150);   /* no change */
            pdf_line(r->p, 190 + 75, r->y - 11, 190 + 75, r->y + 3, 1, 60, 60, 60);   /* 100% */
            fmt_pct(a, avg);
            sprintf(t, "%s  (%ld tests)", a, gcnt[g]);
            pdf_text(r->p, 496, r->y, PF_HELV, 9, t, 30, 30, 30);
        } else
            pdf_text(r->p, 190, r->y, PF_HELV, 9, "not run", 140, 140, 140);
        r->y += 17;
    }
    pdf_text(r->p, 190, r->y, PF_HELV, 7, "0%", 120, 120, 120);
    pdf_text_c(r->p, 265, r->y, PF_HELV, 7, "100%", 120, 120, 120);
    pdf_text_c(r->p, 340, r->y, PF_HELV, 7, "200%", 120, 120, 120);
    pdf_text_r(r->p, 490, r->y, PF_HELV, 7, "400%", 120, 120, 120);
    r->y += 18;

    if (nbest) {
        subheading(r, "Biggest gains from the blitter");
        for (i = 0; i < nbest; i++) {
            short k = (short)best_i[i];
            char nm[60];
            need(r, 14);
            sprintf(nm, "%s  [%s]", tests[k].name, group_name[tests[k].group]);
            pdf_text(r->p, LM + 10, r->y, PF_HELV, 9, nm, 30, 30, 30);
            fmt_pct(a, best_v[i]);
            pdf_text_r(r->p, 330, r->y, PF_COURB, 9, a, 44, 120, 44);
            r->y += 13;
        }
        r->y += 6;
    }

    /* per-test tables */
    for (g = 0; g < NGROUPS; g++) {
        long mx = 1;
        short any = 0;
        for (i = 0; i < ntests; i++)
            if (tests[i].group == g && (off[i].state == RS_OK || on[i].state == RS_OK)) {
                any = 1;
                if (off[i].score > mx) mx = off[i].score;
                if (on[i].score > mx) mx = on[i].score;
            }
        if (!any) continue;
        prog_set((short)(15 + g * 75 / NGROUPS), group_name[g]);
        heading(r, group_name[g]);
        need(r, 30);
        pdf_text(r->p, LM, r->y, PF_HELVB, 8, "Test", 80, 80, 80);
        pdf_text_r(r->p, 255, r->y, PF_HELVB, 8, "Off (ops/s)", 80, 80, 80);
        pdf_text_r(r->p, 320, r->y, PF_HELVB, 8, "On (ops/s)", 80, 80, 80);
        pdf_text_r(r->p, 370, r->y, PF_HELVB, 8, "Speed-up", 80, 80, 80);
        pdf_rect(r->p, 385, r->y - 7, 8, 7, 160, 160, 160);
        pdf_text(r->p, 396, r->y, PF_HELV, 7, "off", 80, 80, 80);
        pdf_rect(r->p, 418, r->y - 7, 8, 7, 31, 119, 180);
        pdf_text(r->p, 429, r->y, PF_HELV, 7, "on", 80, 80, 80);
        r->y += 5;
        pdf_line(r->p, LM, r->y, RM, r->y, 1, 200, 200, 200);
        r->y += 11;
        for (i = 0; i < ntests; i++) {
            char nm[64];
            short row;
            if (tests[i].group != g) continue;
            if (off[i].state == RS_NONE && on[i].state == RS_NONE) continue;
            need(r, 14);
            row = r->y;
            if (((row - TOPY) / 13) & 1) pdf_rect(r->p, LM - 2, row - 9, RM - LM + 4, 13, 246, 248, 252);
            pdf_fit(PF_HELV, 8, tests[i].name, 140, nm, sizeof(nm));
            pdf_text(r->p, LM, row, PF_HELV, 8, nm, 20, 20, 20);
            if (off[i].state == RS_OK) fmt_score(a, off[i].score); else strcpy(a, off[i].state == RS_NA ? "n/a" : "-");
            if (on[i].state == RS_OK)  fmt_score(b, on[i].score);  else strcpy(b, on[i].state == RS_NA ? "n/a" : "-");
            pdf_text_r(r->p, 255, row, PF_COUR, 8, a, 20, 20, 20);
            pdf_text_r(r->p, 320, row, PF_COUR, 8, b, 20, 20, 20);
            if (off[i].state == RS_OK && on[i].state == RS_OK && off[i].score > 0) {
                long pct = on[i].score * 100L / off[i].score;
                fmt_pct(a, pct);
                pdf_text_r(r->p, 370, row, PF_COURB, 8, a,
                           pct >= 105 ? 34 : (pct <= 95 ? 200 : 60),
                           pct >= 105 ? 130 : (pct <= 95 ? 40 : 60),
                           pct >= 105 ? 34 : (pct <= 95 ? 40 : 60));
            }
            r->y = row - 5;                     /* two 4 pt bars inside the row */
            if (off[i].state == RS_OK) hbar(r, 385, 170, off[i].score, mx, 4, 160, 160, 160);
            r->y = row;
            if (on[i].state == RS_OK)  hbar(r, 385, 170, on[i].score, mx, 4, 31, 119, 180);
            r->y = row + 13;
        }
        r->y += 6;
    }

    heading(r, "Notes");
    para(r, "Timings use the 200 Hz system timer. Under MultiTOS other programs keep running and can "
            "lower the numbers; close them for repeatable results. NVDI and other accelerated VDIs may "
            "ignore the XBIOS blitter switch, in which case both columns are expected to match. "
            "Tests where the work is done by the CPU or the AES (events, messages, window lists) "
            "should show about 100%.", 9);
    prog_set(95, "Writing PDF");
    return pdf_close(r->p);
}

/* ================================================================== */
/*  1b. This machine: results + configuration (no baseline needed)    */
/* ================================================================== */

/* "280x faster", "35% faster", "about the same speed", "2.5x slower" */
static void speed_words(char *o, long pct)
{
    if (pct >= 1000)     sprintf(o, "%ldx faster", pct / 100);
    else if (pct >= 200) sprintf(o, "%ld.%ldx faster", pct / 100, (pct % 100) / 10);
    else if (pct >= 105) sprintf(o, "%ld%% faster", pct - 100);
    else if (pct > 95)   strcpy(o, "about the same speed");
    else if (pct <= 0)   strcpy(o, "too slow to measure against it");
    else {
        long x = 10000L / pct;                      /* slower: how many times */
        if (x >= 1000)     sprintf(o, "%ldx slower", x / 100);
        else if (x >= 200) sprintf(o, "%ld.%ldx slower", x / 100, (x % 100) / 10);
        else               sprintf(o, "%ld%% slower", 100 - pct);
    }
}

/* The same averages as the group summary (mean of the per-test %), then
   the overall figure and the tests furthest ahead and behind. */
short conclusion_lines(const BASELINE *b, char (*lines)[CONC_W], short max)
{
    short g, i, n = 0, tcnt = 0, tn = 0, best = -1, worst = -1;
    long  tsum = 0, tnsum = 0, bestp = 0, worstp = 0;
    char  w1[40], w2[40];

    if (!b || !b->used || max < 4) return 0;
    for (g = 0; g < NGROUPS && n < max - 3; g++) {
        long sum = 0, nsum = 0;
        short cnt = 0, ncnt = 0;
        for (i = 0; i < ntests; i++) {
            long bv = b->val[i], pct, np;
            if (tests[i].group != g || res[i].state != RS_OK || bv <= 0) continue;
            pct = res[i].score * 100L / bv;
            np = norm_pct(i, res[i].score, b);
            sum += pct; cnt++;
            if (np >= 0) { nsum += np; ncnt++; }
            if (best < 0 || pct > bestp)   { best = i; bestp = pct; }
            if (worst < 0 || pct < worstp) { worst = i; worstp = pct; }
        }
        if (!cnt) continue;
        tsum += sum; tcnt += cnt; tnsum += nsum; tn += ncnt;
        speed_words(w1, sum / cnt);
        if (ncnt) {
            speed_words(w2, nsum / ncnt);
            sprintf(lines[n++], "%s: %s (normalised: %s), %d tests", group_name[g], w1, w2, cnt);
        } else
            sprintf(lines[n++], "%s: %s, %d tests", group_name[g], w1, cnt);
    }
    if (!tcnt) return 0;
    speed_words(w1, tsum / tcnt);
    if (tn) {
        speed_words(w2, tnsum / tn);
        sprintf(lines[n++], "Overall: %s (normalised: %s), %d tests", w1, w2, tcnt);
    } else
        sprintf(lines[n++], "Overall: %s, %d tests", w1, tcnt);
    if (best >= 0) {
        speed_words(w1, bestp);
        sprintf(lines[n++], "%s: %s, %s", bestp >= 100 ? "Furthest ahead" : "Least behind",
                tests[best].name, w1);
    }
    if (worst >= 0 && worst != best) {
        speed_words(w1, worstp);
        sprintf(lines[n++], "%s: %s, %s", worstp >= 100 ? "Least ahead" : "Furthest behind",
                tests[worst].name, w1);
    }
    return n;
}

/* table columns with a baseline: numbers, then the split bars */
#define BX_OPS   205
#define BX_BASE  250
#define BX_RAW   285
#define BX_NORM  322
#define BX_BAR   330
#define BX_BARW  108
#define BX_SCL   443
#define BX_NOTE  468

short machine_report(const char *path)
{
    REP R, *r = &R;
    char t[320], a[24];
    short g, i, run = 0, k;
    long bv;
    BASELINE *b = base[active_base].used ? &base[active_base] : NULL;

    for (i = 0; i < ntests; i++) if (res[i].state != RS_NONE) run++;
    r->p = pdf_create(path);
    if (!r->p) return 0;
    r->title = "Benchmark results";
    prog_set(5, "Collecting system information");
    sysinfo_collect();
    rep_page(r);

    pdf_text(r->p, LM, r->y + 10, PF_HELVB, 22, "Benchmark results", 20, 20, 20);
    r->y += 28;
    if (machine_desc[0]) {
        pdf_text(r->p, LM, r->y, PF_HELVB, 13, machine_desc, 32, 64, 128);
        r->y += 16;
    }
    sysinfo_short(t, sizeof(t));
    pdf_text(r->p, LM, r->y, PF_HELV, 11, t, 70, 70, 70);
    r->y += 18;
    sprintf(t, "%d of %d tests run, %ld.%ld s each. Values are operations per second (higher is better)%s",
            run, ntests, dur_ticks / 200, (dur_ticks % 200) / 20,
            b ? "; % compares with the loaded baseline." : ". Bars compare tests within each group.");
    para(r, t, 9);
    if (b) {
        GEOM me;
        sprintf(t, "Baseline %c: %s  [%s]", 'A' + active_base, b->name, b->file);
        para(r, t, 9);
        this_geom(&me);
        /* two shorter paragraphs: the whole sentence could exceed t[] */
        para(r, "Norm % allows for the work one operation does on each machine - colour planes, "
                "test window size, and desktop size for full-screen tests - so different screen "
                "modes compare fairly.", 8);
        sprintf(t, "This machine: planes %d, test window %dx%d, desktop %dx%d%s. "
                   "Baseline: planes %d, test window %dx%d, desktop %dx%d%s.",
                me.planes, me.tw, me.th, me.dw, me.dh, me.est ? " (estimated)" : "",
                b->geo.planes, b->geo.tw, b->geo.th, b->geo.dw, b->geo.dh,
                b->geo.est ? " (estimated: older profile)" : "");
        para(r, t, 8);
        para(r, "In the tables, each test has two two-colour bars: the baseline's share in blue "
                "and this machine's in orange, raw on top and normalised below (paler). The black "
                "mark is where they meet: right of the middle, this machine is faster.", 8);
    }

    /* per-group summary */
    subheading(r, "Groups");
    for (g = 0; g < NGROUPS; g++) {
        short n = 0, nb = 0, nn = 0;
        long sum = 0, nsum = 0;
        for (i = 0; i < ntests; i++) {
            if (tests[i].group != g || res[i].state != RS_OK) continue;
            n++;
            bv = b ? b->val[i] : -1;
            if (bv > 0) {
                long np = norm_pct(i, res[i].score, b);
                sum += res[i].score * 100L / bv; nb++;
                if (np >= 0) { nsum += np; nn++; }
            }
        }
        need(r, 30);
        pdf_text(r->p, LM, r->y, PF_HELV, 10, group_name[g], 30, 30, 30);
        sprintf(t, "%d test%s run", n, n == 1 ? "" : "s");
        pdf_text(r->p, 190, r->y, PF_HELV, 9, n ? t : "not run", n ? 60 : 150, n ? 60 : 150, n ? 60 : 150);
        if (nb) {
            long avg = sum / nb;
            pdf_rect(r->p, 280, r->y - 9, 200, 11, 235, 235, 235);
            if (avg >= 105)     hbar(r, 280, 200, avg, 200, 11, 44, 160, 44);
            else if (avg <= 95) hbar(r, 280, 200, avg, 200, 11, 214, 39, 40);
            else                hbar(r, 280, 200, avg, 200, 11, 150, 150, 150);
            pdf_line(r->p, 380, r->y - 11, 380, r->y + 3, 1, 60, 60, 60);
            fmt_pct(a, avg);
            sprintf(t, "%s of %c", a, 'A' + active_base);
            pdf_text(r->p, 488, r->y, PF_HELV, 9, t, 30, 30, 30);
        }
        if (nn) {                               /* normalised, second bar */
            long avg = nsum / nn;
            r->y += 13;
            pdf_text_r(r->p, 274, r->y, PF_HELV, 8, "normalised", 90, 90, 90);
            pdf_rect(r->p, 280, r->y - 9, 200, 11, 235, 235, 235);
            if (avg >= 105)     hbar(r, 280, 200, avg, 200, 11, 44, 160, 44);
            else if (avg <= 95) hbar(r, 280, 200, avg, 200, 11, 214, 39, 40);
            else                hbar(r, 280, 200, avg, 200, 11, 150, 150, 150);
            pdf_line(r->p, 380, r->y - 11, 380, r->y + 3, 1, 60, 60, 60);
            fmt_pct(a, avg);
            sprintf(t, "%s norm", a);
            pdf_text(r->p, 488, r->y, PF_HELV, 9, t, 30, 30, 30);
        }
        r->y += 16;
    }
    r->y += 6;

    /* results tables */
    for (g = 0; g < NGROUPS; g++) {
        long mx = 1;
        short any = 0, rh = b ? 16 : 13;      /* taller rows for the bar pairs */
        for (i = 0; i < ntests; i++)
            if (tests[i].group == g && res[i].state != RS_NONE) {
                any = 1;
                if (res[i].state == RS_OK && res[i].score > mx) mx = res[i].score;
            }
        if (!any) continue;
        prog_set((short)(15 + g * 60 / NGROUPS), group_name[g]);
        heading(r, group_name[g]);
        pdf_text(r->p, LM, r->y, PF_HELVB, 8, "Test", 80, 80, 80);
        pdf_text_r(r->p, b ? BX_OPS : 255, r->y, PF_HELVB, 8, "ops/s", 80, 80, 80);
        if (b) {
            pdf_text_r(r->p, BX_BASE, r->y, PF_HELVB, 8, "Baseline", 80, 80, 80);
            pdf_text_r(r->p, BX_RAW, r->y, PF_HELVB, 8, "Raw %", 80, 80, 80);
            pdf_text_r(r->p, BX_NORM, r->y, PF_HELVB, 8, "Norm %", 80, 80, 80);
            /* legend over the bars */
            pdf_rect(r->p, BX_BAR, r->y - 6, 6, 6, 31, 119, 180);
            pdf_text(r->p, BX_BAR + 8, r->y, PF_HELV, 7, "base", 80, 80, 80);
            pdf_rect(r->p, BX_BAR + 30, r->y - 6, 6, 6, 255, 127, 14);
            pdf_text(r->p, BX_BAR + 38, r->y, PF_HELV, 7, "this (raw/norm)", 80, 80, 80);
            pdf_text(r->p, BX_SCL, r->y, PF_HELVB, 7, "scales", 80, 80, 80);
        }
        pdf_text(r->p, b ? BX_NOTE : 385, r->y, PF_HELVB, 8, "Note", 80, 80, 80);
        r->y += 5;
        pdf_line(r->p, LM, r->y, RM, r->y, 1, 200, 200, 200);
        r->y += 11;
        for (i = 0; i < ntests; i++) {
            char nm[64], note[40];
            short row;
            if (tests[i].group != g || res[i].state == RS_NONE) continue;
            need(r, rh + 1);
            row = r->y;
            if (((row - TOPY) / rh) & 1)
                pdf_rect(r->p, LM - 2, row - (b ? 11 : 9), RM - LM + 4, rh, 246, 248, 252);
            pdf_fit(PF_HELV, 8, tests[i].name, b ? BX_OPS - LM - 40 : 150, nm, sizeof(nm));
            pdf_text(r->p, LM, row, PF_HELV, 8, nm, 20, 20, 20);
            switch (res[i].state) {
            case RS_OK:   fmt_score(a, res[i].score); break;
            case RS_NA:   strcpy(a, "n/a"); break;
            default:      strcpy(a, "failed"); break;
            }
            pdf_text_r(r->p, b ? BX_OPS : 270, row, PF_COUR, 8, a, res[i].state == RS_OK ? 20 : 180, 20, 20);
            if (res[i].state == RS_OK && !b)
                hbar(r, 280, 100, res[i].score, mx, 7, 31, 119, 180);
            if (b) {
                bv = b->val[i];
                fmt_score(a, bv);
                pdf_text_r(r->p, BX_BASE, row, PF_COUR, 8, a, 90, 90, 90);
                if (res[i].state == RS_OK && bv > 0) {
                    long pct = res[i].score * 100L / bv;
                    long np = norm_pct(i, res[i].score, b);
                    fmt_pct(a, pct);
                    pdf_text_r(r->p, BX_RAW, row, PF_COUR, 8, a,
                               pct >= 100 ? 34 : 200, pct >= 100 ? 130 : 40, pct >= 100 ? 34 : 40);
                    fmt_pct(a, np);
                    pdf_text_r(r->p, BX_NORM, row, PF_COURB, 8, a,
                               np < 0 ? 150 : (np >= 100 ? 34 : 200),
                               np < 0 ? 150 : (np >= 100 ? 130 : 40),
                               np < 0 ? 150 : (np >= 100 ? 34 : 40));
                    /* raw on top, normalised below in paler colours */
                    split_bar(r, BX_BAR, row - 10, BX_BARW, 6, pct, 31, 119, 180, 255, 127, 14);
                    split_bar(r, BX_BAR, row - 3, BX_BARW, 6, np, 150, 190, 222, 253, 190, 133);
                    /* the middle: equal speed */
                    pdf_line(r->p, BX_BAR + BX_BARW / 2, row - 11, BX_BAR + BX_BARW / 2, row + 4, 1, 255, 255, 255);
                }
                pdf_text(r->p, BX_SCL, row, PF_HELV, 7, norm_label(norm_class(tests[i].key)), 110, 110, 110);
            }
            pdf_fit(PF_HELV, 7, res[i].note, RM - (b ? BX_NOTE : 385), note, sizeof(note));
            pdf_text(r->p, b ? BX_NOTE : 385, row, PF_HELV, 7, note, 110, 110, 110);
            r->y = row + rh;
        }
        r->y += 6;
    }

    /* full configuration */
    prog_set(80, "Configuration");
    heading(r, "Configuration");
    for (k = 0; k < nsys; k++) {
        if (sysline[k][0] == '#') { r->y += 6; subheading(r, sysline[k] + 1); continue; }
        need(r, 11);
        {
            char line[90];
            pdf_fit(PF_COUR, 7, sysline[k], RM - LM, line, sizeof(line));
            pdf_text(r->p, LM, r->y, PF_COUR, 7, line, 30, 30, 30);
        }
        r->y += 10;
    }
    /* conclusion, at the end */
    if (b) {
        static char cl[NGROUPS + 4][CONC_W];
        short nl = conclusion_lines(b, cl, NGROUPS + 4);
        if (nl) {
            heading(r, "Conclusion");
            sprintf(t, "Compared with baseline %c, %s, this machine is:", 'A' + active_base, b->name);
            para(r, t, 10);
            r->y += 4;
            for (k = 0; k < nl; k++) {
                const char *c = strchr(cl[k], ':');
                char lead[64];
                short lw = c ? (short)(c - cl[k] + 1) : 0;
                need(r, 16);
                if (lw > 0 && lw < (short)sizeof(lead)) {   /* "VDI primitives:" in bold */
                    memcpy(lead, cl[k], lw); lead[lw] = 0;
                    pdf_text(r->p, LM + 8, r->y, PF_HELVB, 10, lead, 20, 20, 20);
                    pdf_text(r->p, LM + 8 + pdf_textw(PF_HELVB, 10, lead) + 4, r->y,
                             PF_HELV, 10, c + 2, 20, 20, 20);
                } else
                    pdf_text(r->p, LM + 8, r->y, PF_HELV, 10, cl[k], 20, 20, 20);
                r->y += 15;
            }
            r->y += 4;
            para(r, "Averages are the mean of the per-test percentages in each group, as in the "
                    "Groups summary at the start; normalised figures allow for colour planes, "
                    "window and desktop size (see the introduction).", 8);
        }
    }

    prog_set(95, "Writing PDF");
    return pdf_close(r->p);
}

/* ================================================================== */
/*  2. Folder of profiles                                             */
/* ================================================================== */

typedef struct {
    char  name[48], file[16], date[20], blit[8], system[64];
    short blitfixed;
    short sw, sh, planes, colours, tw, th, dw, dh, est, dur;
    long  val[MAX_TESTS];
} PROF;

/* how one operation's work scales with the machine */
enum { NC_NONE, NC_FIXED, NC_LEN, NC_MIN, NC_DIM2, NC_AREA, NC_DESK };
static const char *nc_label[] = { "calls", "planes", "len", "min", "dim\xB2", "area", "desk" };

static const struct { const char *pfx; short nc; } nctab[] = {
    { "VDI.PLINE", NC_LEN }, { "VDI.WIDELN", NC_LEN }, { "VDI.DASHLN", NC_LEN },
    { "VDI.USERLN", NC_LEN }, { "VDI.XORLN", NC_LEN },
    { "VDI.ARC", NC_MIN }, { "VDI.ELLARC", NC_MIN }, { "VDI.RBOX", NC_MIN },
    { "VDI.CIRCLE", NC_DIM2 }, { "VDI.ELLIPSE", NC_DIM2 }, { "VDI.PIE", NC_DIM2 },
    { "VDI.ELLPIE", NC_DIM2 }, { "VDI.RFBOX", NC_DIM2 },
    { "VDI.RECTFUL", NC_AREA }, { "VDI.SCROLL", NC_AREA },
    { "VDI.EXTENT", NC_NONE }, { "VDI.ATTR", NC_NONE }, { "VDI.VWK", NC_NONE },
    { "VDI.MOUSE", NC_NONE }, { "VDI.GETPIX", NC_NONE },
    { "VDI.", NC_FIXED },
    { "AES.OPEN", NC_AREA }, { "AES.MOVE", NC_AREA }, { "AES.SIZE", NC_AREA },
    { "AES.TOP", NC_AREA }, { "AES.SCROLL", NC_AREA }, { "AES.SCRSL", NC_AREA },
    { "AES.OBJDRAW", NC_FIXED }, { "AES.FDIAL", NC_FIXED },
    { "AES.DLG", NC_FIXED },
    { "AES.", NC_NONE },
    { "ALN.WMOVE", NC_AREA }, { "ALN.WOPEN", NC_AREA }, { "ALN.WSCR", NC_AREA },
    { "ALN.VSC", NC_AREA }, { "ALN.HSC", NC_AREA }, { "ALN.WD.", NC_AREA },
    { "ALN.", NC_FIXED },
    { "FULL.", NC_DESK },
    { "GDOS.SCRMIX", NC_AREA }, { "GDOS.T", NC_FIXED }, { "GDOS.BOLD", NC_FIXED },
    { "GDOS.ARBPT", NC_FIXED }, { "GDOS.", NC_NONE },
    { NULL, NC_NONE }
};

const char *norm_label(short nc)
{
    return (nc >= 0 && nc <= NC_DESK) ? nc_label[nc] : "";
}

short norm_class(const char *key)
{
    short i;
    for (i = 0; nctab[i].pfx; i++)
        if (!strncmp(key, nctab[i].pfx, strlen(nctab[i].pfx))) return nctab[i].nc;
    return NC_NONE;
}

/* units of work per operation on a machine of geometry g, class nc */
static double work_units_g(const GEOM *g, short nc)
{
    double pl = g->planes > 0 ? g->planes : 1;
    double w = g->tw, h = g->th, mn = w < h ? w : h;
    switch (nc) {
    case NC_FIXED: return pl;
    case NC_LEN:   return (w + h) / 2.0 * pl;
    case NC_MIN:   return mn * pl;
    case NC_DIM2:  return mn * mn * pl;
    case NC_AREA:  return w * h * pl;
    case NC_DESK:  return (double)g->dw * g->dh * pl;
    }
    return 1.0;
}

static double work_units(const PROF *p, short nc)
{
    GEOM g;
    g.planes = p->planes; g.tw = p->tw; g.th = p->th; g.dw = p->dw; g.dh = p->dh;
    return work_units_g(&g, nc);
}

/* fill what a profile did not record, from its screen size and colours
   (fields <= 0 are missing); sets est when anything was estimated */
void geom_estimate(GEOM *g, short sw, short sh, short colours)
{
    if (sw <= 0) { sw = 640; sh = 400; g->est = 1; }
    if (g->planes <= 0) {
        g->planes = colours >= 256 ? 8 : colours >= 16 ? 4 : colours >= 4 ? 2 : 1;
        if (colours <= 0) g->est = 1;
    }
    if (g->dw <= 0) { g->dw = sw; g->dh = sh - 20; g->est = 1; }
    if (g->tw <= 0) {
        g->tw = g->dw < 400 ? g->dw - 24 : g->dw - g->dw / 3 - 12;
        g->th = g->dh - 50;
        g->est = 1;
    }
}

/* this machine, as the last run measured it */
void this_geom(GEOM *g)
{
    memset(g, 0, sizeof(*g));
    g->planes = planes;
    g->tw = run_tw_w; g->th = run_tw_h;
    g->dw = desk.g_w; g->dh = desk.g_h;
    geom_estimate(g, scr_w, scr_h, ncolors);
}

/* this machine's result for test i, normalised, as % of baseline b */
long norm_pct(short i, long score, const BASELINE *b)
{
    GEOM me;
    short nc;
    double a, c;
    if (!b || !b->used || score < 0 || b->val[i] <= 0) return -1;
    this_geom(&me);
    nc = norm_class(tests[i].key);
    a = (double)score * work_units_g(&me, nc);
    c = (double)b->val[i] * work_units_g(&b->geo, nc);
    if (c <= 0) return -1;
    return (long)(a * 100.0 / c + 0.5);
}

static void ptrim(char *s)
{
    short n = (short)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) s[--n] = 0;
    while (*s == ' ') memmove(s, s + 1, strlen(s));
}

static long parse_val(const char *s)
{
    long v = 0, f = 0;
    short got = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); got = 1; }
    if (*s == '.' && s[1] >= '0' && s[1] <= '9') f = s[1] - '0';
    return got ? v * 10 + f : -1;
}

static short load_prof(const char *path, const char *fname, PROF *p)
{
    LSRC f;
    char line[160], *eq, *c;
    short i, found = 0;
    if (!lsrc_open(&f, path)) return 0;
    memset(p, 0, sizeof(*p));
    for (i = 0; i < MAX_TESTS; i++) p->val[i] = -1;
    strncpy(p->file, fname, sizeof(p->file) - 1);
    strncpy(p->name, fname, sizeof(p->name) - 1);
    strcpy(p->blit, "?");
    p->dur = 200;
    while (lsrc_gets(line, sizeof(line), &f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        if ((c = strchr(line, ';')) != NULL) *c = 0;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        ptrim(line); ptrim(eq);
        if (!strcmp(line, "NAME"))          strncpy(p->name, eq, sizeof(p->name) - 1);
        else if (!strcmp(line, "DATE"))     strncpy(p->date, eq, sizeof(p->date) - 1);
        else if (!strcmp(line, "SYSTEM"))   strncpy(p->system, eq, sizeof(p->system) - 1);
        else if (!strcmp(line, "BLITTER"))  strncpy(p->blit, eq, sizeof(p->blit) - 1);
        else if (!strcmp(line, "BLITCTL"))  p->blitfixed = !strcmp(eq, "fixed");
        else if (!strcmp(line, "DURATION")) p->dur = (short)atoi(eq);
        else if (!strcmp(line, "SCREEN")) {
            short a = 0, b = 0, cc = 0, pl = 0;
            if (sscanf(eq, "%hdx%hd %hd colours %hd planes", &a, &b, &cc, &pl) >= 2) {
                p->sw = a; p->sh = b;
                if (cc) p->colours = cc;
                if (pl) p->planes = pl;
            }
        }
        else if (!strcmp(line, "PLANES"))   p->planes = (short)atoi(eq);
        else if (!strcmp(line, "COLOURS"))  p->colours = (short)atoi(eq);
        else if (!strcmp(line, "TESTWIN"))  sscanf(eq, "%hdx%hd", &p->tw, &p->th);
        else if (!strcmp(line, "DESK"))     sscanf(eq, "%hdx%hd", &p->dw, &p->dh);
        else if ((i = find_test(line)) >= 0) {
            p->val[i] = parse_val(eq);
            if (p->val[i] >= 0) found++;
        }
    }
    lsrc_close(&f);
    if (!found) return 0;
    /* older profiles: estimate what wasn't recorded */
    if (p->sw <= 0) { p->sw = 640; p->sh = 400; p->est = 1; }
    if (p->planes <= 0) {
        p->planes = p->colours >= 256 ? 8 : p->colours >= 16 ? 4 : p->colours >= 4 ? 2 : 1;
        p->est = 1;
    }
    if (p->dw <= 0) { p->dw = p->sw; p->dh = p->sh - 20; p->est = 1; }
    if (p->tw <= 0) {
        p->tw = p->dw < 400 ? p->dw - 24 : p->dw - p->dw / 3 - 12;
        p->th = p->dh - 50;
        p->est = 1;
    }
    return 1;
}

static short collect_profiles(const char *dir, PROF *pr, short max, short *total)
{
    char pat[200], path[220];
    _DTA dta, *old = Fgetdta();
    short n = 0, i, j;
    long rc;

    Fsetdta(&dta);
    strcpy(pat, dir);
    strcat(pat, "*.FBP");
    *total = 0;
    rc = Fsfirst(pat, 0);
    while (rc == 0) {
        strcpy(path, dir);
        strcat(path, dta.dta_name);
        {
            char m[40];
            sprintf(m, "Loading %s", dta.dta_name);
            prog_set((short)(2 + (n < 28 ? n : 28)), m);
        }
        if (!strcmp(dta.dta_name, ref_file))
            ;                            /* the built-in one is already A */
        else if (n < max) {
            if (load_prof(path, dta.dta_name, &pr[n])) { n++; (*total)++; }
        } else
            (*total)++;                  /* counted, not loaded */
        rc = Fsnext();
    }
    Fsetdta(old);
    /* sort by file name so the reference (A) is predictable */
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcmp(pr[j].file, pr[j - 1].file) < 0; j--) {
            PROF t = pr[j]; pr[j] = pr[j - 1]; pr[j - 1] = t;
        }
    return n;
}

/* % of reference machine (0) for test i; -1 if missing */
static long rel_raw(const PROF *pr, short m, short i)
{
    if (pr[0].val[i] <= 0 || pr[m].val[i] < 0) return -1;
    return pr[m].val[i] * 100L / pr[0].val[i];
}

static long rel_norm(const PROF *pr, short m, short i)
{
    short nc = norm_class(tests[i].key);
    double a, b;
    if (pr[0].val[i] <= 0 || pr[m].val[i] < 0) return -1;
    a = (double)pr[m].val[i] * work_units(&pr[m], nc);
    b = (double)pr[0].val[i] * work_units(&pr[0], nc);
    if (b <= 0) return -1;
    return (long)(a * 100.0 / b + 0.5);
}

static long group_index(const PROF *pr, short m, short g, short norm, short *cnt)
{
    long sum = 0;
    short i, n = 0;
    for (i = 0; i < ntests; i++) {
        long v;
        if (tests[i].group != g) continue;
        v = norm ? rel_norm(pr, m, i) : rel_raw(pr, m, i);
        if (v < 0) continue;
        sum += v; n++;
    }
    if (cnt) *cnt = n;
    return n ? sum / n : -1;
}

/* bar chart: one row per machine for each group */
static void index_chart(REP *r, const PROF *pr, short np, short norm)
{
    short g, m;
    long mx = 100;
    for (g = 0; g < NGROUPS; g++)
        for (m = 0; m < np; m++) {
            long v = group_index(pr, m, g, norm, NULL);
            if (v > mx) mx = v;
        }
    mx = (mx + 49) / 50 * 50;
    for (g = 0; g < NGROUPS; g++) {
        short any = 0, cnt;
        for (m = 0; m < np; m++) if (group_index(pr, m, g, norm, NULL) >= 0) any = 1;
        if (!any) continue;
        need(r, 16 + np * 12);
        pdf_text(r->p, LM, r->y, PF_HELVB, 9, group_name[g], 30, 30, 30);
        r->y += 12;
        for (m = 0; m < np; m++) {
            long v = group_index(pr, m, g, norm, &cnt);
            char lab[8], t[24], nm[40];
            sprintf(lab, "%c", 'A' + m);
            pdf_text(r->p, LM + 4, r->y, PF_HELVB, 8, lab, PC(m));
            pdf_fit(PF_HELV, 8, pr[m].name, 120, nm, sizeof(nm));
            pdf_text(r->p, LM + 16, r->y, PF_HELV, 8, nm, 60, 60, 60);
            pdf_rect(r->p, 190, r->y - 8, 300, 9, 240, 240, 240);
            if (v >= 0) {
                hbar(r, 190, 300, v, mx, 9, PC(m));
                fmt_pct(t, v);
                pdf_text(r->p, 496, r->y, PF_COUR, 8, t, 30, 30, 30);
            } else
                pdf_text(r->p, 496, r->y, PF_HELV, 8, "-", 140, 140, 140);
            r->y += 12;
        }
        { short x100 = 190 + (short)(100L * 300 / mx);
          pdf_line(r->p, x100, r->y - 12 * np - 10, x100, r->y - 9, 1, 60, 60, 60); }
        r->y += 6;
    }
    {
        char t[16];
        sprintf(t, "%ld%%", mx);
        pdf_text(r->p, 190, r->y, PF_HELV, 7, "0%", 120, 120, 120);
        pdf_text_r(r->p, 490, r->y, PF_HELV, 7, t, 120, 120, 120);
        r->y += 14;
    }
}

/* table of every test, machines across; mode 0 raw ops/s, 1 raw %, 2 normalised % */
static void results_table(REP *r, const PROF *pr, short np, short mode)
{
    short per = 6, b0, g, i, m;
    const short namew = 150, colw = 60;
    for (b0 = 0; b0 < np; b0 += per) {
        short b1 = b0 + per < np ? b0 + per : np;
        for (g = 0; g < NGROUPS; g++) {
            short any = 0;
            for (i = 0; i < ntests && !any; i++)
                if (tests[i].group == g)
                    for (m = b0; m < b1; m++) if (pr[m].val[i] >= 0) any = 1;
            if (!any) continue;
            need(r, 48);
            {
                char t[80];
                if (np > per) sprintf(t, "%s  (machines %c-%c)", group_name[g], 'A' + b0, 'A' + b1 - 1);
                else strcpy(t, group_name[g]);
                subheading(r, t);
            }
            if (mode == 2) pdf_text(r->p, LM + namew - 34, r->y, PF_HELVB, 7, "scales", 90, 90, 90);
            for (m = b0; m < b1; m++) {
                char lab[4];
                short xr = LM + namew + (m - b0 + 1) * colw;
                sprintf(lab, "%c", 'A' + m);
                pdf_rect(r->p, xr - 14, r->y - 8, 9, 9, PC(m));
                pdf_text(r->p, xr - 3, r->y, PF_HELVB, 8, lab, 40, 40, 40);
            }
            r->y += 4;
            pdf_line(r->p, LM, r->y, RM, r->y, 1, 200, 200, 200);
            r->y += 10;
            for (i = 0; i < ntests; i++) {
                long best = -1;
                short row, has = 0;
                char nm[64];
                if (tests[i].group != g) continue;
                for (m = b0; m < b1; m++) if (pr[m].val[i] >= 0) has = 1;
                if (!has) continue;
                for (m = 0; m < np; m++) {          /* best over all machines */
                    long v = mode == 2 ? rel_norm(pr, m, i) : (mode == 1 ? rel_raw(pr, m, i) : pr[m].val[i]);
                    if (v > best) best = v;
                }
                need(r, 12);
                row = r->y;
                if (((row - TOPY) / 12) & 1) pdf_rect(r->p, LM - 2, row - 8, RM - LM + 4, 12, 246, 248, 252);
                pdf_fit(PF_HELV, 8, tests[i].name, mode == 2 ? namew - 40 : namew - 4, nm, sizeof(nm));
                pdf_text(r->p, LM, row, PF_HELV, 8, nm, 20, 20, 20);
                if (mode == 2)
                    pdf_text(r->p, LM + namew - 34, row, PF_HELV, 7, nc_label[norm_class(tests[i].key)], 110, 110, 110);
                for (m = b0; m < b1; m++) {
                    long v = mode == 2 ? rel_norm(pr, m, i) : (mode == 1 ? rel_raw(pr, m, i) : pr[m].val[i]);
                    char t[20];
                    short xr = LM + namew + (m - b0 + 1) * colw - 4;
                    if (v < 0) { pdf_text_r(r->p, xr, row, PF_COUR, 8, "-", 150, 150, 150); continue; }
                    if (mode == 0) fmt_score(t, v); else fmt_pct(t, v);
                    if (v == best && np > 1)
                        pdf_text_r(r->p, xr, row, PF_COURB, 8, t, 34, 130, 34);
                    else
                        pdf_text_r(r->p, xr, row, PF_COUR, 8, t, 20, 20, 20);
                }
                r->y = row + 12;
            }
            r->y += 6;
        }
    }
}

short profiles_report(const char *dir, const char *path, short *nfound, short *ntotal)
{
    PROF *pr;
    REP R, *r = &R;
    short np, m, i;
    char t[320];

    pr = malloc(sizeof(PROF) * MAX_PROF);
    if (!pr) return 0;
    {
        short tot = 0;
        /* A is always the built-in reference, then the folder's profiles */
        np = collect_profiles(dir, pr + 1, MAX_PROF - 1, &tot);
        if (ntotal) *ntotal = tot;
    }
    if (nfound) *nfound = np;
    if (!np) { free(pr); return 0; }
    if (load_prof(REF_PATH, ref_file, &pr[0])) {
        short l = (short)strlen(pr[0].name);
        if (l > (short)(sizeof(pr[0].name) - sizeof(REF_TAG))) l = sizeof(pr[0].name) - sizeof(REF_TAG);
        strcpy(pr[0].name + l, REF_TAG);
        np++;
    } else {
        memmove(pr, pr + 1, sizeof(PROF) * np);
    }

    r->p = pdf_create(path);
    if (!r->p) { free(pr); return 0; }
    r->title = "Profile comparison";
    rep_page(r);

    pdf_text(r->p, LM, r->y + 10, PF_HELVB, 22, "Profile comparison", 20, 20, 20);
    r->y += 26;
    sprintf(t, "%d profiles from %s", np, dir);
    pdf_text(r->p, LM, r->y, PF_HELV, 10, t, 70, 70, 70);
    r->y += 20;

    /* machine list */
    subheading(r, "Machines");
    pdf_text(r->p, LM + 16, r->y, PF_HELVB, 7, "Name / file", 90, 90, 90);
    pdf_text(r->p, 240, r->y, PF_HELVB, 7, "Screen", 90, 90, 90);
    pdf_text(r->p, 318, r->y, PF_HELVB, 7, "Planes", 90, 90, 90);
    pdf_text(r->p, 352, r->y, PF_HELVB, 7, "Test window", 90, 90, 90);
    pdf_text(r->p, 412, r->y, PF_HELVB, 7, "Desktop", 90, 90, 90);
    pdf_text(r->p, 466, r->y, PF_HELVB, 7, "Blitter", 90, 90, 90);
    pdf_text(r->p, 505, r->y, PF_HELVB, 7, "Date", 90, 90, 90);
    r->y += 12;
    for (m = 0; m < np; m++) {
        char nm[60], lab[4];
        need(r, 22);
        sprintf(lab, "%c", 'A' + m);
        pdf_rect(r->p, LM, r->y - 8, 10, 10, PC(m));
        pdf_text(r->p, LM + 2, r->y, PF_HELVB, 8, lab, 255, 255, 255);
        pdf_fit(PF_HELVB, 8, pr[m].name, 180, nm, sizeof(nm));
        pdf_text(r->p, LM + 16, r->y, PF_HELVB, 8, nm, 20, 20, 20);
        sprintf(t, "%dx%d", pr[m].sw, pr[m].sh);
        pdf_text(r->p, 240, r->y, PF_HELV, 8, t, 20, 20, 20);
        sprintf(t, "%d", pr[m].planes);
        pdf_text(r->p, 318, r->y, PF_HELV, 8, t, 20, 20, 20);
        sprintf(t, "%dx%d%s", pr[m].tw, pr[m].th, pr[m].est ? "*" : "");
        pdf_text(r->p, 352, r->y, PF_HELV, 8, t, 20, 20, 20);
        sprintf(t, "%dx%d", pr[m].dw, pr[m].dh);
        pdf_text(r->p, 412, r->y, PF_HELV, 8, t, 20, 20, 20);
        sprintf(t, "%s%s", pr[m].blit, pr[m].blitfixed ? " \x86" : "");
        pdf_text(r->p, 466, r->y, PF_HELV, 8, t, 20, 20, 20);
        pdf_fit(PF_HELV, 7, pr[m].date, 52, nm, sizeof(nm));
        pdf_text(r->p, 505, r->y, PF_HELV, 7, nm, 20, 20, 20);
        r->y += 10;
        if (pr[m].system[0]) sprintf(t, "%s  -  %s", pr[m].file, pr[m].system);
        else                 strcpy(t, pr[m].file);
        pdf_fit(PF_HELV, 7, t, 410, nm, sizeof(nm));
        pdf_text(r->p, LM + 16, r->y, PF_HELV, 7, nm, 120, 120, 120);
        r->y += 12;
    }
    for (m = 0; m < np && !pr[m].blitfixed; m++) ;
    if (m < np) para(r, "\x86 fixed: this machine's TOS cannot switch the blitter off (XBIOS 64 is "
                        "ignored, e.g. TOS 4.x), so its results always include the blitter and it "
                        "has no blitter-off figures.", 8);
    for (m = 0; m < np && !pr[m].est; m++) ;
    if (m < np) para(r, "* estimated: this profile was saved by an older Fbench that did not record the "
                        "test-window and desktop sizes, so they were derived from the screen size.", 8);
    sprintf(t, "Machine A (%s) is the reference: percentages are that machine's speed as 100%%. "
               "Bold green marks the best machine for each test.", pr[0].name);
    para(r, t, 9);

    prog_set(35, "Summary charts");
    heading(r, "Summary - raw speed (% of A)");
    para(r, "Average of the per-test percentages in each group, exactly as measured. A machine "
            "running a smaller window or fewer colours does less work per operation and looks faster here.", 9);
    index_chart(r, pr, np, 0);

    heading(r, "Summary - normalised (% of A)");
    para(r, "The same averages after adjusting each test for the work one operation does on that "
            "machine: colour depth (bit planes), the size of the test window, and the desktop size for "
            "full-screen tests. This compares graphics throughput independent of the screen mode.", 9);
    index_chart(r, pr, np, 1);

    prog_set(50, "Raw results");
    heading(r, "Raw results (operations per second)");
    results_table(r, pr, np, 0);

    prog_set(65, "Relative results");
    heading(r, "Relative to A (raw, %)");
    results_table(r, pr, np, 1);

    prog_set(80, "Normalised results");
    heading(r, "Normalised results (% of A)");
    para(r, "Each test's operations per second are multiplied by the work one operation does on that "
            "machine, then shown as a percentage of machine A. The 'scales' column says what the "
            "work is proportional to:", 9);
    para(r, "planes - fixed-size drawing (64x64 blits, text, 32x32 fills): bit planes only.  "
            "len - lines across the window: (width + height) / 2 x planes.  "
            "min - arcs and rounded boxes sized from the window: min(width, height) x planes.  "
            "dim\xB2 - filled shapes whose radius follows the window: min(w, h)\xB2 x planes.  "
            "area - whole test-window work (fills, scrolls, window moves): width x height x planes.  "
            "desk - full-screen windows: desktop width x height x planes.  "
            "calls - AES/VDI calls with no pixel work (events, messages, queries): not adjusted.", 8);
    results_table(r, pr, np, 2);

    heading(r, "Notes");
    para(r, "Normalisation is a model, not a measurement: it assumes drawing cost grows with the "
            "number of bit planes touched and the pixels covered. Real hardware adds fixed per-call "
            "overheads, so small operations on high-resolution modes are usually under-rewarded and "
            "large ones fit best. GDOS text at the same point size covers different pixel counts at "
            "different screen resolutions (DPI); that is not adjusted. Profiles measured with different "
            "test durations, blitter settings or background programs are not directly comparable.", 8);
    for (m = 0; m < np; m++)
        if (pr[m].dur != pr[0].dur) {
            para(r, "Warning: these profiles were not all measured with the same test duration.", 8);
            break;
        }

    prog_set(95, "Writing PDF");
    i = pdf_close(r->p);
    free(pr);
    return i;
}
