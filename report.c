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
    for (i = 0; i < MAX_TESTS; i++)
        b->val[i] = (i < ntests && r[i].state == RS_OK) ? r[i].score : -1;
    strncpy(b->name, name, sizeof(b->name) - 1);
    strcpy(b->file, "(this run)");
    b->used = 1;
}

void baseline_clear(void)
{
    memset(base, 0, sizeof(base));
    active_base = 0;
}

short baseline_load(const char *path)
{
    FILE *f;
    char line[160], *eq, *c;
    short slot, i, found = 0;
    BASELINE *b;

    f = fopen(path, "r");
    if (!f) return -1;

    /* same file already loaded? reuse its slot, else first free, else active */
    for (slot = 0; slot < MAX_BASE; slot++)
        if (base[slot].used && !strcmp(base[slot].file, base_name(path))) break;
    if (slot == MAX_BASE)
        for (slot = 0; slot < MAX_BASE && base[slot].used; slot++)
            ;
    if (slot == MAX_BASE) slot = active_base;

    b = &base[slot];
    memset(b, 0, sizeof(*b));
    for (i = 0; i < MAX_TESTS; i++) b->val[i] = -1;
    strncpy(b->file, base_name(path), sizeof(b->file) - 1);
    strncpy(b->name, b->file, sizeof(b->name) - 1);

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        if ((c = strchr(line, ';')) != NULL) *c = 0;
        trim(line);
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        trim(line);
        if (!strcmp(line, "NAME")) {
            while (*eq == ' ') eq++;
            strncpy(b->name, eq, sizeof(b->name) - 1);
        } else if ((i = find_test(line)) >= 0) {
            b->val[i] = parse_score(eq);
            if (b->val[i] >= 0) found++;
        }
    }
    fclose(f);
    if (!found) { b->used = 0; return -1; }
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
    char name[64], date[24], v[20];
    short i, n = 0;

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
    fprintf(f, "[results]\n");
    for (i = 0; i < ntests; i++) {
        if (res[i].state != RS_OK) continue;
        fmt_score(v, res[i].score);
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
        long sum = 0;
        short cnt = 0;
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
                char bs[20], ps[12];
                if (!base[b].used) continue;
                bv = base[b].val[i];
                fmt_score(bs, bv);
                if (res[i].state == RS_OK && bv > 0) {
                    long pct = res[i].score * 100L / bv;
                    sprintf(ps, "%ld%%", pct);
                    if (b == active_base) { sum += pct; cnt++; }
                } else strcpy(ps, "-");
                if (compact) sprintf(cell, "  %6s", ps);
                else         sprintf(cell, " %10s %5s", bs, ps);
                strcat(line, cell);
            }
            if (res[i].note[0]) { strcat(line, "  ("); strcat(line, res[i].note); strcat(line, ")"); }
            fprintf(f, "%s\n", line);
        }
        if (cnt) {
            char bar[42];
            ascii_bar(bar, sum / cnt, 40);
            fprintf(f, "  => %s index vs %c: %ld%%  [%s]  (%d tests)\n",
                    group_name[g], 'A' + active_base, sum / cnt, bar, cnt);
        }
    }

    prog_set(85, "Comparison with baseline");
    if (base[active_base].used) {
        long sum = 0;
        short cnt = 0;
        fprintf(f, "\n-- Comparison with %c: %s --\n", 'A' + active_base, base[active_base].name);
        fprintf(f, "   (100%% = same speed, bar centre mark '|' = 100%%, full bar = 200%%+)\n\n");
        for (i = 0; i < ntests; i++) {
            long bv = base[active_base].val[i];
            char bar[34];
            if (res[i].state != RS_OK || bv <= 0) continue;
            ascii_bar(bar, res[i].score * 100L / bv, 32);
            fprintf(f, "%-26.26s %5ld%% %s\n", tests[i].name, res[i].score * 100L / bv, bar);
            sum += res[i].score * 100L / bv; cnt++;
        }
        if (cnt) fprintf(f, "\nOverall average: %ld%% over %d tests\n", sum / cnt, cnt);
    }

    fprintf(f, "\nNotes: timings use the 200 Hz system timer. Under MultiTOS other\n");
    fprintf(f, "applications keep running and can lower the scores; close them for\n");
    fprintf(f, "repeatable results. Fbench holds the screen (wind_update) while each\n");
    fprintf(f, "test runs. NVDI and other accelerated VDIs may ignore the blitter switch.\n");
    fclose(f);
    prog_set(100, "Done");
    graf_mouse(ARROW, NULL);
    return 1;
}
