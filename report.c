/*
 * Fbench - report.c : baseline profiles (load/save) and text report export
 *
 * Profile / baseline file (*.FBP) - plain text, editable:
 *
 *   ; Fbench 1.0 profile
 *   NAME=STE TOS 1.62 320x200 16c blit on
 *   DATE=2026-10-31 10:07
 *   BLITTER=on
 *   DURATION=200
 *   VDI.PLINE2=12345.6        ; Line, 2 points
 *   ...
 *
 * Values are operations per second.  Unknown keys are ignored, so
 * profiles from older or newer Fbench versions still load.
 */
#include "fbench.h"

BASELINE base[MAX_BASE];
short active_base;

short find_test(const char *key)
{
    short i;
    for (i = 0; i < ntests; i++)
        if (!strcmp(tests[i].key, key)) return i;
    return -1;
}

void fmt_score(char *buf, long v)
{
    if (v < 0)            strcpy(buf, "-");
    else if (v >= 100000L) sprintf(buf, "%ld", v / 10);
    else                  sprintf(buf, "%ld.%ld", v / 10, v % 10);
}

static long parse_score(const char *s)
{
    long v = 0, f = 0;
    short got = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); got = 1; }
    if (*s == '.' && s[1] >= '0' && s[1] <= '9') { f = s[1] - '0'; got = 1; }
    return got ? v * 10 + f : -1;
}

/* the next number after the current one on a line ("337.5 330.1 341.2"),
   -1 = none; older Fbench reads only the first, so profiles stay compatible */
static const char *skip_num(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    while ((*s >= '0' && *s <= '9') || *s == '.') s++;
    return s;
}

short swing_of(const RESULT *r, long *down, long *up)
{
    if (r->state != RS_OK || r->runs < 2 || r->score <= 0 || r->smin < 0) return 0;
    *down = (r->score - r->smin) * 1000L / r->score;
    *up   = (r->smax - r->score) * 1000L / r->score;
    return 1;
}
void swing_str(char *o, const RESULT *r)
{
    long d, u;
    if (!swing_of(r, &d, &u)) { *o = 0; return; }
    sprintf(o, "-%ld.%ld%% / +%ld.%ld%%", d / 10, d % 10, u / 10, u % 10);
}

static void trim(char *s)
{
    short n = (short)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '\\');
    if (!p) p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* turn a set of in-memory results into a baseline slot (used by the
   blitter comparison so the results window shows on-vs-off) */
void baseline_from_results(short slot, const char *name, const RESULT *r)
{
    BASELINE *b = &base[slot];
    short i;
    memset(b, 0, sizeof(*b));
    for (i = 0; i < MAX_TESTS; i++) {
        short ok = i < ntests && r[i].state == RS_OK;
        b->val[i] = ok ? r[i].score : -1;
        b->vmin[i] = ok && r[i].runs > 1 ? r[i].smin : -1;
        b->vmax[i] = ok && r[i].runs > 1 ? r[i].smax : -1;
        if (ok && r[i].runs > b->runs) b->runs = r[i].runs;
    }
    strncpy(b->name, name, sizeof(b->name) - 1);
    strcpy(b->file, "(this run)");
    this_geom(&b->geo);
    b->used = 1;
}

short lsrc_open(LSRC *l, const char *path)
{
    l->f = NULL; l->s = NULL;
    if (!strcmp(path, REF_PATH)) { l->s = ref_fbp; return 1; }
    l->f = fopen(path, "r");
    return l->f != NULL;
}
char *lsrc_gets(char *buf, short n, LSRC *l)
{
    short i = 0;
    if (l->f) return fgets(buf, n, l->f);
    if (!l->s || !*l->s) return NULL;
    while (*l->s && i < n - 1) {
        char c = *l->s++;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    return buf;
}
void lsrc_close(LSRC *l)
{
    if (l->f) fclose(l->f);
    l->f = NULL; l->s = NULL;
}

/* the built-in reference profile (STE, TOS 1.6, blitter, medium res, run
   in cycle-exact Hatari) is always baseline A and the active one at start */
void baseline_builtin(void)
{
    short slot = baseline_load(REF_PATH);
    if (slot >= 0) {
        BASELINE *b = &base[slot];
        b->name[sizeof(b->name) - 1 - (sizeof(REF_TAG) - 1)] = 0;
        strcat(b->name, REF_TAG);
        active_base = slot;
    }
}

/* Clear baselines keeps the built-in one */
void baseline_clear(void)
{
    memset(base, 0, sizeof(base));
    active_base = 0;
    baseline_builtin();
}

short baseline_load(const char *path)
{
    LSRC f;
    char line[160], *eq, *c;
    const char *fname = strcmp(path, REF_PATH) ? base_name(path) : ref_file;
    short slot, i, found = 0, sw = 0, sh = 0, colours = 0;
    BASELINE *b;

    if (!lsrc_open(&f, path)) return -1;

    /* same file already loaded? reuse its slot, else first free, else active */
    for (slot = 0; slot < MAX_BASE; slot++)
        if (base[slot].used && !strcmp(base[slot].file, fname)) break;
    if (slot == MAX_BASE)
        for (slot = 0; slot < MAX_BASE && base[slot].used; slot++)
            ;
    if (slot == MAX_BASE) slot = active_base;

    b = &base[slot];
    memset(b, 0, sizeof(*b));
    for (i = 0; i < MAX_TESTS; i++) b->val[i] = -1;
    strncpy(b->file, fname, sizeof(b->file) - 1);
    strncpy(b->name, b->file, sizeof(b->name) - 1);

    while (lsrc_gets(line, sizeof(line), &f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        if ((c = strchr(line, ';')) != NULL) *c = 0;
        trim(line);
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        trim(line);
        while (*eq == ' ') eq++;
        if (!strcmp(line, "NAME")) {
            strncpy(b->name, eq, sizeof(b->name) - 1);
        } else if (!strcmp(line, "SCREEN")) {
            short pl = 0, cc = 0;
            if (sscanf(eq, "%hdx%hd %hd colours %hd planes", &sw, &sh, &cc, &pl) >= 2) {
                if (cc) colours = cc;
                if (pl) b->geo.planes = pl;
            }
        } else if (!strcmp(line, "PLANES"))  { b->geo.planes = (short)atoi(eq);
        } else if (!strcmp(line, "COLOURS")) { colours = (short)atoi(eq);
        } else if (!strcmp(line, "TESTWIN")) { sscanf(eq, "%hdx%hd", &b->geo.tw, &b->geo.th);
        } else if (!strcmp(line, "RUNS"))    { b->runs = (short)atoi(eq);
        } else if (!strcmp(line, "DESK"))    { sscanf(eq, "%hdx%hd", &b->geo.dw, &b->geo.dh);
        } else if ((i = find_test(line)) >= 0) {
            const char *q = skip_num(eq);
            b->val[i] = parse_score(eq);
            b->vmin[i] = parse_score(q);                 /* long-run profiles: */
            b->vmax[i] = b->vmin[i] >= 0 ? parse_score(skip_num(q)) : -1;   /* avg min max */
            if (b->vmax[i] < 0) b->vmin[i] = -1;
            if (b->val[i] >= 0) found++;
        }
    }
    lsrc_close(&f);
    if (!found) { b->used = 0; return -1; }
    geom_estimate(&b->geo, sw, sh, colours);   /* older profiles */
    b->used = 1;
    return slot;
}

static void date_str(char *buf)
{
    unsigned short d = Tgetdate(), t = Tgettime();
    sprintf(buf, "%04d-%02d-%02d %02d:%02d",
            1980 + (d >> 9), (d >> 5) & 15, d & 31, t >> 11, (t >> 5) & 63);
}

short profile_save(const char *path)
{
    FILE *f;
    char name[64], date[24], v[20], vmn[20], vmx[20];
    short i, n = 0, runs = 0;

    for (i = 0; i < ntests; i++)
        if (res[i].state == RS_OK && res[i].runs > runs) runs = res[i].runs;
    /* gather everything first, so a failure never leaves an empty file */
    sysinfo_short(name, sizeof(name));          /* automatic system summary */
    date_str(date);
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "; Fbench %s profile - load it with Baseline > Load baseline\n", FB_VERSION);
    fprintf(f, "; Values are operations per second. Edit NAME to describe the machine.\n");
    fprintf(f, "NAME=%s\n", machine_desc[0] ? machine_desc : name);
    fprintf(f, "SYSTEM=%s\n", name);
    fprintf(f, "DATE=%s\n", date);
    fprintf(f, "BLITTER=%s\n", !blitter_present ? "none" : (blitter_on ? "on" : "off"));
    fprintf(f, "BLITCTL=%s\n", !blitter_present ? "none" : (blitter_fixed ? "fixed" : "switchable"));
    fprintf(f, "BLITSRC=%s\n", blit_src == BS_XGEM ? "xgem" : (blit_src == BS_XBIOS ? "xbios" : "none"));
    fprintf(f, "CPU=%s\n", cpu_name());
    fprintf(f, "DURATION=%ld\n", dur_ticks);
    fprintf(f, "SCREEN=%dx%d %d colours %d planes\n", scr_w, scr_h, ncolors, planes);
    fprintf(f, "PLANES=%d\nCOLOURS=%d\n", planes, ncolors);
    fprintf(f, "DESK=%dx%d\n", desk.g_w, desk.g_h);
    if (run_tw_w > 0) fprintf(f, "TESTWIN=%dx%d\n", run_tw_w, run_tw_h);
    if (runs > 1) {
        fprintf(f, "; Long run: each value is the average of the runs, then the\n");
        fprintf(f, "; slowest and fastest run (older Fbench reads the average only).\n");
        fprintf(f, "RUNS=%d\n", runs);
    }
    fprintf(f, "[results]\n");
    for (i = 0; i < ntests; i++) {
        if (res[i].state != RS_OK) continue;
        fmt_score(v, res[i].score);
        if (res[i].runs > 1) {
            char sv[64];
            fmt_score(vmn, res[i].smin); fmt_score(vmx, res[i].smax);
            sprintf(sv, "%s %s %s", v, vmn, vmx);
            fprintf(f, "%-12s=%-26s ; %s (%d runs)\n", tests[i].key, sv, tests[i].name, res[i].runs);
        } else
            fprintf(f, "%-12s=%-10s ; %s\n", tests[i].key, v, tests[i].name);
        n++;
    }
    fclose(f);
    if (!n)
        form_alert(1, "[1][Fbench:|Profile saved, but no tests|have been run yet.][ OK ]");
    return 1;
}

static void ascii_bar(char *out, long pct, short width)
{
    short n, i;
    if (pct < 0) pct = 0;
    if (pct > 200) pct = 200;
    n = (short)(pct * width / 200);
    for (i = 0; i < width; i++)
        out[i] = (i < n) ? '#' : (i == width / 2 ? '|' : '.');
    out[width] = 0;
}

short report_export(const char *path)
{
    FILE *f;
    char date[24], v[20], line[200], cell[40], prof[64];
    short i, g, b, nb = 0, compact;

    graf_mouse(BUSY_BEE, NULL);
    prog_set(5, "Collecting system information");
    sysinfo_collect();
    date_str(date);
    sysinfo_short(prof, sizeof(prof));
    f = fopen(path, "w");
    if (!f) { graf_mouse(ARROW, NULL); return 0; }
    for (b = 0; b < MAX_BASE; b++) if (base[b].used) nb++;
    compact = nb > 2;

    fprintf(f, "Fbench %s - GEM VDI / AES / GDOS benchmark report\n", FB_VERSION);
    fprintf(f, "==========================================================\n");
    fprintf(f, "Date      : %s\n", date);
    if (machine_desc[0]) fprintf(f, "Machine   : %s\n", machine_desc);
    fprintf(f, "Profile   : %s\n", prof);
    fprintf(f, "Blitter   : %s%s\n", !blitter_present ? "not present" : (blitter_on ? "enabled" : "disabled"),
            blitter_fixed ? " (this TOS cannot switch it off - XBIOS 64 is ignored)" : "");
    fprintf(f, "Duration  : %ld.%ld s per test\n", dur_ticks / 200, (dur_ticks % 200) / 20);
    if (nfonts) fprintf(f, "GDOS font : %s (id %d)\n", fonts[gfont].name, fonts[gfont].id);
    {
        short runs = 0;
        for (i = 0; i < ntests; i++) if (res[i].state == RS_OK && res[i].runs > runs) runs = res[i].runs;
        if (runs > 1)
            fprintf(f, "Long run  : %d runs per test - ops/s are averages; [swing] = slowest\n"
                       "            and fastest run against the average\n", runs);
    }
    fprintf(f, "\n");

    for (i = 0; i < nsys; i++) {
        if (sysline[i][0] == '#') fprintf(f, "\n-- %s --\n", sysline[i] + 1);
        else                      fprintf(f, "  %s\n", sysline[i]);
    }

    fprintf(f, "\n-- Baselines --\n");
    if (!nb) fprintf(f, "  (none loaded)\n");
    for (b = 0; b < MAX_BASE; b++)
        if (base[b].used)
            fprintf(f, "  %c: %s  [%s]%s\n", 'A' + b, base[b].name, base[b].file,
                    b == active_base ? "  (active)" : "");

    fprintf(f, "\n-- Results (operations per second, higher is better) --\n\n");
    sprintf(line, "%-26s %10s", "Test", "This");
    for (b = 0; b < MAX_BASE; b++) {
        if (!base[b].used) continue;
        if (compact) sprintf(cell, "  %5c%%", 'A' + b);
        else         sprintf(cell, " %10c %5s", 'A' + b, "%");
        strcat(line, cell);
    }
    fprintf(f, "%s\n", line);

    for (g = 0; g < NGROUPS; g++) {
        long sum = 0, nsum = 0;
        short cnt = 0, ncnt = 0;
        fprintf(f, "\n[ %s ]\n", group_name[g]);
        prog_set((short)(20 + g * 60 / NGROUPS), group_name[g]);
        for (i = 0; i < ntests; i++) {
            if (tests[i].group != g) continue;
            switch (res[i].state) {
            case RS_OK:   fmt_score(v, res[i].score); break;
            case RS_NA:   strcpy(v, "n/a"); break;
            case RS_FAIL: strcpy(v, "failed"); break;
            default:      strcpy(v, "-"); break;
            }
            sprintf(line, "%-26.26s %10s", tests[i].name, v);
            for (b = 0; b < MAX_BASE; b++) {
                long bv;
                char bs[20], ps[16];
                if (!base[b].used) continue;
                bv = base[b].val[i];
                fmt_score(bs, bv);
                if (res[i].state == RS_OK && bv > 0) {
                    long pct = res[i].score * 100L / bv;
                    if (pct >= 10000L) sprintf(ps, "%ldx", pct / 100); else sprintf(ps, "%ld%%", pct);
                    if (b == active_base) {
                        long np = norm_pct(i, res[i].score, &base[b]);
                        sum += pct; cnt++;
                        if (np >= 0) { nsum += np; ncnt++; }
                    }
                } else strcpy(ps, "-");
                if (compact) sprintf(cell, "  %6s", ps);
                else         sprintf(cell, " %10s %5s", bs, ps);
                strcat(line, cell);
            }
            if (res[i].note[0]) { strcat(line, "  ("); strcat(line, res[i].note); strcat(line, ")"); }
            {
                char sw[40];
                swing_str(sw, &res[i]);
                if (sw[0]) { strcat(line, "  [swing "); strcat(line, sw); strcat(line, "]"); }
            }
            fprintf(f, "%s\n", line);
        }
        if (cnt) {
            char bar[42];
            ascii_bar(bar, sum / cnt, 40);
            fprintf(f, "  => %s index vs %c: %ld%%  [%s]  (%d tests)\n",
                    group_name[g], 'A' + active_base, sum / cnt, bar, cnt);
            if (ncnt) {
                ascii_bar(bar, nsum / ncnt, 40);
                fprintf(f, "     normalised:     %5ld%%  [%s]\n", nsum / ncnt, bar);
            }
        }
    }

    prog_set(85, "Comparison with baseline");
    if (base[active_base].used) {
        BASELINE *bb = &base[active_base];
        GEOM me;
        long sum = 0, nsum = 0;
        short cnt = 0, ncnt = 0;
        this_geom(&me);
        fprintf(f, "\n-- Comparison with %c: %s --\n", 'A' + active_base, bb->name);
        fprintf(f, "   Raw %% = this machine's ops/s as %% of %c (100%% = same speed).\n", 'A' + active_base);
        fprintf(f, "   Norm %% = the same after allowing for the work one operation does on\n");
        fprintf(f, "   each machine: colour planes, test window size, desktop size for\n");
        fprintf(f, "   full-screen tests (see 'Scales'). Bar = normalised; '|' = 100%%,\n");
        fprintf(f, "   full bar = 200%%+.\n");
        fprintf(f, "   This machine: planes %d, test window %dx%d, desktop %dx%d%s\n",
                me.planes, me.tw, me.th, me.dw, me.dh, me.est ? " (estimated)" : "");
        fprintf(f, "   Baseline %c:   planes %d, test window %dx%d, desktop %dx%d%s\n\n",
                'A' + active_base, bb->geo.planes, bb->geo.tw, bb->geo.th, bb->geo.dw, bb->geo.dh,
                bb->geo.est ? " (estimated - older profile)" : "");
        if (me.planes != bb->geo.planes || me.tw != bb->geo.tw || me.th != bb->geo.th)
            fprintf(f, "   NOT like for like: different screen mode or test window, so each test\n"
                       "   does a different amount of work - use Norm %%, or compare profiles\n"
                       "   made in the same screen mode.\n\n");
        else
            fprintf(f, "   Like for like: same planes and test window as the baseline.\n\n");
        fprintf(f, "%-26s %6s %6s %-6s %s\n", "Test", "Raw %", "Norm %", "Scales", "");
        for (i = 0; i < ntests; i++) {
            long bv = bb->val[i], pct, np;
            char bar[34], ns[16];
            if (res[i].state != RS_OK || bv <= 0) continue;
            pct = res[i].score * 100L / bv;
            np = norm_pct(i, res[i].score, bb);
            if (np >= 10000L) sprintf(ns, "%ldx", np / 100); else if (np >= 0) sprintf(ns, "%ld%%", np); else strcpy(ns, "-");
            ascii_bar(bar, np >= 0 ? np : pct, 32);
            {
                const char *lb = norm_label(norm_class(tests[i].key));
                if (!strcmp(lb, "dim\xB2")) lb = "dim2";        /* plain ASCII file */
                fprintf(f, "%-26.26s %5ld%% %6s %-6s %s\n", tests[i].name, pct, ns, lb, bar);
            }
            sum += pct; cnt++;
            if (np >= 0) { nsum += np; ncnt++; }
        }
        if (cnt) fprintf(f, "\nOverall average: %ld%% raw", sum / cnt);
        if (ncnt) fprintf(f, ", %ld%% normalised", nsum / ncnt);
        if (cnt) fprintf(f, " over %d tests\n", cnt);
        fprintf(f, "\nScales: planes = fixed-size drawing; len = lines across the window;\n");
        fprintf(f, "min / dim2 = shapes sized from the window (size, size squared);\n");
        fprintf(f, "area = whole test window; desk = full-screen window; calls = AES and\n");
        fprintf(f, "query calls with no pixel work (not adjusted). Normalisation is a model:\n");
        fprintf(f, "fixed per-call costs mean small operations at high resolutions are\n");
        fprintf(f, "under-rewarded.\n");
    }

    fprintf(f, "\nNotes: timings use the 200 Hz system timer. Under MultiTOS other\n");
    fprintf(f, "applications keep running and can lower the scores; close them for\n");
    fprintf(f, "repeatable results. Fbench holds the screen (wind_update) while each\n");
    fprintf(f, "test runs. NVDI and other accelerated VDIs may ignore the blitter switch.\n");

    /* long run: how repeatable the results are */
    {
        static char cl[12][CONC_W];
        short nl = consistency_lines(cl, 12);
        if (nl) {
            fprintf(f, "\n-- Consistency (long run) --\n");
            for (i = 0; i < nl; i++) fprintf(f, "  %s\n", cl[i]);
        }
    }

    /* conclusion, at the end */
    if (base[active_base].used) {
        static char cl[NGROUPS + 4][CONC_W];
        short nl = conclusion_lines(&base[active_base], cl, NGROUPS + 4);
        if (nl) {
            fprintf(f, "\n-- Conclusion --\n");
            fprintf(f, "Compared with baseline %c, %s, this machine is:\n\n",
                    'A' + active_base, base[active_base].name);
            for (i = 0; i < nl; i++) fprintf(f, "  %s\n", cl[i]);
            fprintf(f, "\n(mean of the per-test %% in each group; normalised allows for colour\n"
                       "planes, window and desktop size)\n");
        }
    }
    fclose(f);
    prog_set(100, "Done");
    graf_mouse(ARROW, NULL);
    return 1;
}
