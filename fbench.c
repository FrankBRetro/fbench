/*
 * Fbench - GEM VDI / AES / GDOS benchmark
 * fbench.c : application start-up, menu, windows, event handling
 */
#include "fbench.h"

/* Under MultiTOS the AES runs on the calling program's stack, so deep AES
   work (window redraws, objc_draw, form_dial redraw handling) needs room on
   ours.  The mintlib default is too small for that; 32 KB is plenty and
   still modest on a 4 MB machine. */
long _stksize = 32768L;

short app_id, phys_h, vdi_h, bh;
short work_out[57], ext_out[57];
short scr_w, scr_h, planes, ncolors;
short char_w, char_h, box_w, box_h;
GRECT desk;
WIN   wins[NWIN];
short busy, abort_run, quit_app;
short blitter_present, blitter_on, blitter_fixed;
long  dur_ticks = 200;              /* 1 s per test (200 Hz ticks) */
short multitask;
short cur_run = -1;
short magic_present, polite;
short mouse_vis;                    /* option: pointer stays visible   */
static short cur_form = ARROW;
short ping_seen;
OBJECT *menu_tree;

static short blitter_orig;
static char  res_info[120];
static char  test_info[100];
static char  last_path[160];
char machine_desc[34];             /* Options > Machine description   */
static char  res_title[64];
static void  results_title(void);
static void  after_dialog(void);
char home_dir[160];
static const GRECT *open_at;        /* if set, win_open uses this rect */
static short res_hidden;            /* results window closed for a run */
static GRECT res_saved;        /* directory for the file selector */

/* ================================================================== */
/*  Menu                                                              */
/* ================================================================== */

enum {
    C_NONE, C_ABOUT,
    C_SAVEPROF, C_EXPORT, C_PDFREP, C_BLITREP, C_CMPPROF, C_QUIT,
    C_RUNALL, C_RUNSEL, C_SELALL, C_SELNONE, C_SELVDI, C_SELAES, C_SELALIGN, C_SELFULL, C_SELGDOS,
    C_LOADBASE, C_CLEARBASE, C_BASE0, C_BASE1, C_BASE2, C_BASE3,
    C_BLITTER, C_DSHORT, C_DNORM, C_DLONG, C_NEXTFONT, C_MOUSEVIS, C_POLITE, C_TRACE, C_DESC,
    C_WRESULTS, C_WINFO,
    C_MAX
};

typedef struct { short cmd; const char *text; } MITEM;

#define SEP  { C_NONE, "-" }
#define DESK_ACC(n) { C_NONE, "  Desk Accessory " n }

static const MITEM m_desk[] = {
    { C_ABOUT, "  About Fbench..." }, SEP,
    DESK_ACC("1"), DESK_ACC("2"), DESK_ACC("3"),
    DESK_ACC("4"), DESK_ACC("5"), DESK_ACC("6"),
    { -1, NULL }
};
static const MITEM m_file[] = {
    { C_SAVEPROF, "  Save profile...   ^S" },
    { C_EXPORT,   "  Export report...  ^E" },
    { C_PDFREP,   "  Results report (PDF)..." }, SEP,
    { C_BLITREP,  "  Blitter report (PDF)..." },
    { C_CMPPROF,  "  Compare folder (PDF)..." }, SEP,
    { C_QUIT,     "  Quit              ^Q" },
    { -1, NULL }
};
static const MITEM m_test[] = {
    { C_RUNALL,  "  Run all tests     ^A" },
    { C_RUNSEL,  "  Run selected      ^R" }, SEP,
    { C_SELALL,  "  Select all" },
    { C_SELNONE, "  Select none" },
    { C_SELVDI,  "  Select VDI only" },
    { C_SELAES,  "  Select AES only" },
    { C_SELALIGN,"  Select alignment only" },
    { C_SELFULL, "  Select full-screen only" },
    { C_SELGDOS, "  Select GDOS only" },
    { -1, NULL }
};
static const MITEM m_base[] = {
    { C_LOADBASE,  "  Load baseline...  ^O" },
    { C_CLEARBASE, "  Clear baselines" }, SEP,
    { C_BASE0, "  (empty)" }, { C_BASE1, "  (empty)" },
    { C_BASE2, "  (empty)" }, { C_BASE3, "  (empty)" },
    { -1, NULL }
};
static const MITEM m_opts[] = {
    { C_DESC,     "  Machine description..." }, SEP,
    { C_BLITTER,  "  Blitter enabled   ^B" }, SEP,
    { C_DSHORT,   "  Duration: 0.5 s" },
    { C_DNORM,    "  Duration: 1 s" },
    { C_DLONG,    "  Duration: 3 s" }, SEP,
    { C_NEXTFONT, "  Next GDOS font    ^F" }, SEP,
    { C_MOUSEVIS, "  Mouse visible in tests" },
    { C_POLITE,   "  Stay in background" },
#if FB_DEBUG
    { C_TRACE,    "  Trace test steps (debug)" },
#endif
    { -1, NULL }
};
static const MITEM m_win[] = {
    { C_WRESULTS, "  Results window" },
    { C_WINFO,    "  System info      ^I" },
    { -1, NULL }
};

static const MITEM *menus[] = { m_desk, m_file, m_test, m_base, m_opts, m_win };
#define NMENUS 6
static const char *titles_long[NMENUS]  =
    { " Fbench ", " File ", " Tests ", " Baseline ", " Options ", " Window " };
static const char *titles_short[NMENUS] =
    { " Fbench ", " File ", " Test ", " Base ", " Opts ", " Win " };

#define MOBJ 96
static OBJECT mtree[MOBJ];
static short  nmobj;
static short  objcmd[MOBJ];
static short  cmdobj[C_MAX];
static char   mpool[2600];
static short  mpool_n;

static short mk(short type, short x, short y, short w, short h, long spec, short state)
{
    OBJECT *o = &mtree[nmobj];
    o->ob_next = o->ob_head = o->ob_tail = -1;
    o->ob_type = type;
    o->ob_flags = 0;
    o->ob_state = state;
    o->ob_spec.index = spec;
    o->ob_x = x; o->ob_y = y; o->ob_width = w; o->ob_height = h;
    objcmd[nmobj] = C_NONE;
    return nmobj++;
}

static void add_child(short p, short c)
{
    if (mtree[p].ob_head < 0)
        mtree[p].ob_head = c;
    else
        mtree[mtree[p].ob_tail].ob_next = c;
    mtree[p].ob_tail = c;
    mtree[c].ob_next = p;
}

static char *pool_str(const char *s, short width, char padc)
{
    char *d = &mpool[mpool_n];
    short n = (short)strlen(s), i;
    for (i = 0; i < width; i++)
        d[i] = (i < n) ? s[i] : padc;
    d[width] = 0;
    mpool_n += width + 1;
    return d;
}

static OBJECT *build_menu(void)
{
    short cols = scr_w / char_w;
    const char **tt = (cols < 60) ? titles_short : titles_long;
    short root, bar, active, screen, t, i, tx, tl[NMENUS], tpos[NMENUS];
    const short HB = 0x0201, HT = 0x0301;   /* 1 char + 2/3 pixels */

    nmobj = 0; mpool_n = 0;
    memset(cmdobj, 0, sizeof(cmdobj));

    root   = mk(G_IBOX, 0, 0, cols, 25, 0L, 0);
    bar    = mk(G_BOX,  0, 0, cols, HB, 0x00001100L, 0);
    add_child(root, bar);

    tx = 0;
    for (t = 0; t < NMENUS; t++) { tl[t] = (short)strlen(tt[t]); tpos[t] = tx; tx += tl[t]; }
    active = mk(G_IBOX, 2, 0, tx, HT, 0L, 0);
    add_child(bar, active);
    for (t = 0; t < NMENUS; t++) {
        short o = mk(G_TITLE, tpos[t], 0, tl[t], HT, (long)tt[t], 0);
        add_child(active, o);
    }

    screen = mk(G_IBOX, 0, HT, cols, 19, 0L, 0);
    add_child(root, screen);

    for (t = 0; t < NMENUS; t++) {
        const MITEM *m = menus[t];
        short n = 0, w = 0, x, box;
        while (m[n].cmd >= 0) {
            short l = (short)strlen(m[n].text);
            if (m[n].text[0] != '-' && l > w) w = l;
            n++;
        }
        w += 1;
        if (t == 3 && w < 26) w = 26;           /* room for baseline names */
        if (w > cols - 1) w = cols - 1;
        x = 2 + tpos[t];
        if (x + w > cols) x = cols - w;
        if (x < 0) x = 0;
        box = mk(G_BOX, x, 0, w, n, 0x00FF1100L, 0);
        add_child(screen, box);
        for (i = 0; i < n; i++) {
            short o, sep = (m[i].text[0] == '-');
            char *s = sep ? pool_str("", w, '-') : pool_str(m[i].text, w, ' ');
            o = mk(G_STRING, 0, i, w, 1, (long)s, sep ? OS_DISABLED : 0);
            add_child(box, o);
            objcmd[o] = m[i].cmd;
            if (m[i].cmd > C_NONE) cmdobj[m[i].cmd] = o;
        }
    }
    mtree[nmobj - 1].ob_flags |= OF_LASTOB;
    for (i = 0; i < nmobj; i++)
        rsrc_obfix(mtree, i);
    return mtree;
}

static void m_check(short cmd, short on)  { if (cmdobj[cmd]) menu_icheck(menu_tree, cmdobj[cmd], on); }
static void m_enable(short cmd, short on) { if (cmdobj[cmd]) menu_ienable(menu_tree, cmdobj[cmd], on); }

static void update_menu(void)
{
    short i;
    char buf[40];

    m_check(C_BLITTER, blitter_on);
    m_enable(C_BLITTER, blitter_present && !blitter_fixed && !busy);
    m_enable(C_BLITREP, blitter_present && !blitter_fixed && !busy);
    m_check(C_DSHORT, dur_ticks == 100);
    m_check(C_DNORM,  dur_ticks == 200);
    m_check(C_DLONG,  dur_ticks == 600);
    m_enable(C_NEXTFONT, nfonts > 1);
    m_check(C_MOUSEVIS, mouse_vis);
    m_check(C_POLITE, polite);
    m_enable(C_POLITE, !busy);
    m_check(C_TRACE, trace_steps);
    for (i = 0; i < MAX_BASE; i++) {
        short o = cmdobj[C_BASE0 + i];
        short w = mtree[o].ob_width / char_w;
        char *s = mtree[o].ob_spec.free_string;
        if (base[i].used) sprintf(buf, "  %c: %s", 'A' + i, base[i].name);
        else              strcpy(buf, "  (empty)");
        if ((short)strlen(buf) > w) buf[w] = 0;
        memset(s, ' ', w); memcpy(s, buf, strlen(buf)); s[w] = 0;
        menu_ienable(menu_tree, o, base[i].used);
        menu_icheck(menu_tree, o, base[i].used && i == active_base);
    }
}

/* ================================================================== */
/*  Drawing helpers                                                   */
/* ================================================================== */



static void fill(short x, short y, short w, short h, short col, short interior, short style)
{
    short p[4];
    if (w <= 0 || h <= 0) return;
    p[0] = x; p[1] = y; p[2] = x + w - 1; p[3] = y + h - 1;
    vswr_mode(vdi_h, MD_REPLACE);
    vsf_interior(vdi_h, interior);
    vsf_style(vdi_h, style);
    vsf_color(vdi_h, col);
    vr_recfl(vdi_h, p);
}

/* fill with a theme pen if one was found, else a pattern in the text pen */
static void pfill(short x, short y, short w, short h, short p, short patstyle)
{
    if (p >= 0) fill(x, y, w, h, p, FIS_SOLID, 1);
    else        fill(x, y, w, h, pen.fg, FIS_PATTERN, patstyle);
}
#define PEN_OR_FG(p) ((p) >= 0 ? (p) : pen.fg)

static void txt(short x, short y, const char *s, short maxc, short col, short eff)
{
    char buf[SYS_W + 40];
    short n = (short)strlen(s);
    if (maxc <= 0) return;
    if (n > maxc) n = maxc;
    if (n > (short)sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf, s, n); buf[n] = 0;
    vswr_mode(vdi_h, MD_TRANS);
    vst_color(vdi_h, col);
    vst_effects(vdi_h, eff);
    v_gtext(vdi_h, x, y, buf);
    vst_effects(vdi_h, 0);
}

static void hline(short x1, short x2, short y, short col)
{
    short p[4];
    p[0] = x1; p[1] = y; p[2] = x2; p[3] = y;
    vswr_mode(vdi_h, MD_REPLACE);
    vsl_color(vdi_h, col);
    v_pline(vdi_h, 2, p);
}

/* kind: 0 neutral, 1 faster/equal, 2 slower */
static void bar(short x, short y, short w, short h, short kind)
{
    if (kind == 1)      fill(x, y, w, h, PEN_OR_FG(pen.good), FIS_SOLID, 1);  /* solid  */
    else if (kind == 2) pfill(x, y, w, h, pen.bad, 2);                          /* dotted */
    else                pfill(x, y, w, h, pen.info >= 0 ? pen.info : pen.accent, 4);
}

/* ================================================================== */
/*  Results window                                                    */
/* ================================================================== */

/* row codes: -1 = column header, -(10+g) = group header, >=0 test index */
static short rowmap[MAX_TESTS + NGROUPS + 2];
static short nrows;

static void build_rows(void)
{
    short g, i;
    nrows = 0;
    rowmap[nrows++] = -1;
    for (g = 0; g < NGROUPS; g++) {
        rowmap[nrows++] = -(10 + g);
        for (i = 0; i < ntests; i++)
            if (tests[i].group == g) rowmap[nrows++] = i;
    }
    wins[WN_RESULTS].rows = nrows;
}

static long group_max(short g)
{
    long m = 1;
    short i;
    for (i = 0; i < ntests; i++)
        if (tests[i].group == g && res[i].state == RS_OK && res[i].score > m)
            m = res[i].score;
    return m;
}

static short group_selected(short g, short *total)
{
    short i, n = 0, t = 0;
    for (i = 0; i < ntests; i++)
        if (tests[i].group == g) { t++; if (res[i].sel) n++; }
    if (total) *total = t;
    return n;
}

typedef struct { short cols, cname, wname, cval, cpct, cbar; } LAYOUT;

static void layout(const GRECT *work, LAYOUT *L)
{
    L->cols  = work->g_w / char_w;
    L->wname = (L->cols >= 70) ? 26 : (L->cols >= 46 ? 18 : 12);
    L->cname = 3;
    L->cval  = L->cname + L->wname + 1;
    L->cpct  = L->cval + 9 + 1;
    L->cbar  = L->cpct + (base[active_base].used ? 6 : 0);
}

static void draw_result_row(short r, short x, short y, short w, short lh, const LAYOUT *L)
{
    short code = rowmap[r];
    short ty = y + (lh - char_h) / 2;
    char buf[64];

    if (code == -1) {
        txt(x + L->cname * char_w, ty, "Test", L->wname, 1, 1);
        txt(x + L->cval * char_w, ty, "    ops/s", 9, 1, 1);
        if (base[active_base].used)
            txt(x + L->cpct * char_w, ty, " % ", 5, 1, 1);
        if (L->cols - L->cbar > 4) {
            if (base[active_base].used) sprintf(buf, "vs %c: %s", 'A' + active_base, base[active_base].name);
            else                        strcpy(buf, "relative (no baseline)");
            txt(x + L->cbar * char_w, ty, buf, L->cols - L->cbar, 1, 0);
        }
        hline(x, x + w - 1, y + lh - 1, 1);
        return;
    }
    if (code <= -10) {
        short g = -(code + 10), tot, sel = group_selected(g, &tot);
        fill(x, y + 1, w, lh - 2, pen.accent >= 0 ? pen.accent : pen.fg, FIS_SOLID, 1);
        sprintf(buf, " %s  (%d of %d selected)", group_name[g], sel, tot);
        txt(x + char_w, ty, buf, L->cols - 1, pen.accent >= 0 ? pen.on_accent : pen.bg, 1);
        return;
    }

    /* test row */
    {
        RESULT *rs = &res[code];
        short bx = x + char_w / 2, bs = (char_h < char_w * 2 ? char_h : char_w * 2) - 2;
        short by = y + (lh - bs) / 2, p[4];
        long  bv = base[active_base].used ? base[active_base].val[code] : -1;

        if (code == cur_run)
            pfill(x, y, w, lh, pen.hilite, 1);           /* running test */

        /* check box */
        p[0] = bx; p[1] = by; p[2] = bx + bs - 1; p[3] = by + bs - 1;
        vswr_mode(vdi_h, MD_REPLACE);
        vsf_interior(vdi_h, FIS_HOLLOW);
        vsf_perimeter(vdi_h, 1);
        vsf_color(vdi_h, 1);
        v_bar(vdi_h, p);
        if (rs->sel)
            fill(bx + 2, by + 2, bs - 4, bs - 4, PEN_OR_FG(pen.good), FIS_SOLID, 1);

        txt(x + L->cname * char_w, ty, tests[code].name, L->wname, 1, 0);

        if (code == cur_run)            strcpy(buf, " running");
        else if (rs->state == RS_OK)  { char v[20]; fmt_score(v, rs->score); sprintf(buf, "%9s", v); }
        else if (rs->state == RS_NA)    strcpy(buf, "      n/a");
        else if (rs->state == RS_FAIL)  strcpy(buf, "   failed");
        else                            strcpy(buf, "        -");
        txt(x + L->cval * char_w, ty, buf, 9, (rs->state == RS_NA) ? PEN_OR_FG(pen.bad) : pen.fg, 0);

        if (rs->state == RS_OK) {
            short bx0 = x + L->cbar * char_w;
            short bw  = x + w - 4 - bx0;
            short bh2 = lh - 4;
            if (bv > 0) {
                long pct = rs->score * 100L / bv;
                sprintf(buf, "%4ld%%", pct);
                txt(x + L->cpct * char_w, ty, buf, 5, PEN_OR_FG(pct >= 100 ? pen.good : pen.bad), pct >= 100 ? 1 : 0);
                if (bw > 8) {
                    long lp = pct > 200 ? 200 : pct;
                    bar(bx0, y + 2, (short)(lp * bw / 200), bh2, pct >= 100 ? 1 : 2);
                    p[0] = bx0 + bw / 2; p[1] = y + 1; p[2] = p[0]; p[3] = y + lh - 2;
                    vsl_color(vdi_h, 1);
                    v_pline(vdi_h, 2, p);   /* 100% mark */
                }
            } else if (bw > 8) {
                long m = group_max(tests[code].group);
                short bl = (short)(rs->score * (long)bw / m);
                if (rs->score > 0 && bl < 1) bl = 1;
                bar(bx0, y + 2, bl, bh2, 0);
            }
        }
    }
}

static void results_draw(WIN *wn, const GRECT *work, const GRECT *clip)
{
    LAYOUT L;
    short r, first, last, y;

    fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, 0, FIS_SOLID, 1);
    layout(work, &L);
    first = wn->top + (clip->g_y - work->g_y) / wn->lh;
    last  = wn->top + (clip->g_y + clip->g_h - 1 - work->g_y) / wn->lh;
    if (last >= nrows) last = nrows - 1;
    for (r = first; r <= last; r++) {
        y = work->g_y + (r - wn->top) * wn->lh;
        draw_result_row(r, work->g_x, y, work->g_w, wn->lh, &L);
    }
}

/* ================================================================== */
/*  System info window                                                */
/* ================================================================== */

static void info_draw(WIN *wn, const GRECT *work, const GRECT *clip)
{
    short r, first, last, y, cols = work->g_w / char_w;

    fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, 0, FIS_SOLID, 1);
    first = wn->top + (clip->g_y - work->g_y) / wn->lh;
    last  = wn->top + (clip->g_y + clip->g_h - 1 - work->g_y) / wn->lh;
    if (last >= nsys) last = nsys - 1;
    for (r = first; r <= last; r++) {
        y = work->g_y + (r - wn->top) * wn->lh + 1;
        if (sysline[r][0] == '#') {
            if (pen.accent >= 0) fill(work->g_x, y - 1, work->g_w, wn->lh, pen.accent, FIS_SOLID, 1);
            txt(work->g_x + char_w / 2, y, sysline[r] + 1, cols - 1, pen.accent >= 0 ? pen.on_accent : pen.fg, 1);
            if (pen.accent < 0) hline(work->g_x, work->g_x + work->g_w - 1, y + char_h, pen.fg);
        } else
            txt(work->g_x + char_w / 2, y, sysline[r], cols - 1, 1, 0);
    }
}

/* test and auxiliary (AES test) windows: content is disposable */
static void test_draw(WIN *wn, const GRECT *work, const GRECT *clip)
{
    (void)wn; (void)work;
    fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, 0, FIS_SOLID, 1);
}

static void aux_draw(WIN *wn, const GRECT *work, const GRECT *clip)
{
    (void)wn;
    if (pen.info >= 0) fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, pen.info, FIS_SOLID, 1);
    else       fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, 1, FIS_PATTERN, 2);
    txt(work->g_x + 4, work->g_y + 4, "AES window test", work->g_w / char_w, 1, 1);
}

/* ================================================================== */
/*  Window management                                                 */
/* ================================================================== */

/*
 * Screen lock with our own nesting count.  TOS 1.x/2.x AES does NOT allow
 * an application to call wind_update(BEG_UPDATE) again while it already
 * holds it - it waits for itself forever (MultiTOS/N.AES/XaAES allow it).
 * So only the outermost begin/end reaches the AES.
 */
static short upd_depth;
void fb_update(short beg)
{
    if (beg) {
        if (upd_depth++ == 0) wind_update(BEG_UPDATE);
    } else if (upd_depth > 0 && --upd_depth == 0)
        wind_update(END_UPDATE);
}

const char *blitter_desc(void)
{
    if (!blitter_present) return "none";
    if (blitter_fixed) return blitter_on ? "on (fixed)" : "off (fixed)";
    return blitter_on ? "on" : "off";
}

/* ================================================================== */
/*  Blitter control: who really decides whether drawing uses it?      */
/* ================================================================== */
/*
 * Under XGEM the VDI is XGEM's: it decides blitter use itself (also on the
 * Falcon, where TOS 4 never uses it) and XBIOS 64 does not reach it.  XGEM
 * offers AES opcode 184 (XG_BLITTER) for this.  Elsewhere the TOS VDI
 * follows XBIOS 64 - but TOS 4.x accepts the call and ignores it, so the
 * result is checked by switching and reading back.
 */
short blit_src;                 /* BS_NONE / BS_XBIOS / BS_XGEM */
short xgem_present;

static short xg_blitter(short mode, short *present)
{
    static short ctrl[5], in[2], out[4];
    static long  ai[1], ao[1];
    AESPB pb;
    ctrl[0] = 184; ctrl[1] = 1; ctrl[2] = 2; ctrl[3] = 0; ctrl[4] = 0;
    in[0] = mode; out[0] = out[1] = 0;
    pb.control = ctrl; pb.global = aes_global;
    pb.intin = in; pb.intout = out; pb.addrin = ai; pb.addrout = ao;
    aes(&pb);
    if (present) *present = out[1];
    return out[0];
}

static short blit_get(void)
{
    if (blit_src == BS_XGEM)  return xg_blitter(-1, NULL) != 0;
    if (blit_src == BS_XBIOS) return (short)(Blitter(-1) & 1);
    return 0;
}

void blit_set(short on)
{
    if (blit_src == BS_XGEM)       xg_blitter(on ? 1 : 0, NULL);
    else if (blit_src == BS_XBIOS) Blitter(on ? 1 : 0);
    blitter_on = blit_get();
}

static void blit_detect(void)
{
    short a1, a2, a3, a4, pres = 0;
    long b;

    blitter_present = blitter_on = blitter_fixed = 0;
    blit_src = BS_NONE;
    xgem_present = appl_find("XGEM    ") >= 0;
    if (!xgem_present && (_AESversion >= 0x0400 || appl_find("?AGI") >= 0))
        xgem_present = appl_getinfo(0x5847, &a1, &a2, &a3, &a4) && a1 == 1;   /* XGI_INFO */

    if (xgem_present) {
        short on = xg_blitter(-1, &pres);
        if (pres) {
            blit_src = BS_XGEM;
            blitter_present = 1;
            blitter_on = blitter_orig = on != 0;
            /* XGEM can always switch its own blitter use; still verify */
            xg_blitter(on ? 0 : 1, NULL);
            if ((xg_blitter(-1, NULL) != 0) == (on != 0)) blitter_fixed = 1;
            xg_blitter(on, NULL);
            blitter_on = blit_get();
            return;
        }
        /* XGEM says no blitter (or is too old to know): XBIOS below */
    }

    b = Blitter(-1);
    if (!(b & 2)) return;
    blit_src = BS_XBIOS;
    blitter_present = 1;
    blitter_on = blitter_orig = (short)(b & 1);
    Blitter(blitter_on ? 0 : 1);
    if ((short)(Blitter(-1) & 1) == blitter_on) blitter_fixed = 1;
    Blitter(blitter_orig);
    blitter_on = (short)(Blitter(-1) & 1);
}

const char *blit_src_name(void)
{
    switch (blit_src) {
    case BS_XGEM:  return "XGEM (AES XG_BLITTER)";
    case BS_XBIOS: return blitter_fixed ? "XBIOS 64 (ignored by this TOS)" : "XBIOS 64";
    }
    return "none";
}

/* ================================================================== */
/*  Theme-aware pens                                                  */
/* ================================================================== */
/*
 * Fbench never writes the palette.  Pen 0 is the window background and
 * pen 1 the text (the GEM convention XGEM's themes keep); every other
 * colour is the nearest pen actually in the palette, read with vq_color,
 * provided it is close enough to the wanted colour and visible on pen 0.
 * Otherwise a pattern in pen 1 is used.  Re-read on XGEM theme changes.
 */
PENS pen;

static long rgb_d2(const short *a, short r, short g, short b)
{
    long dr = a[0] - r, dg = a[1] - g, db = a[2] - b;
    return dr * dr + dg * dg + db * db;
}
static long luma(const short *c) { return (299L * c[0] + 587L * c[1] + 114L * c[2]) / 1000; }

void pens_update(void)
{
    static short rgb[256][3];
    short n = ncolors > 256 ? 256 : ncolors, i;
    static const struct { short r, g, b; long lim; } want[5] = {
        { 0, 750, 0, 450L * 450L },      /* good  : green  */
        { 850, 0, 0, 450L * 450L },      /* bad   : red    */
        { 0, 300, 850, 500L * 500L },    /* accent: blue   */
        { 0, 750, 850, 450L * 450L },    /* info  : cyan   */
        { 950, 950, 0, 450L * 450L },    /* hilite: yellow */
    };
    short *slot[5];

    if (n < 2) n = 2;
    for (i = 0; i < n; i++) {
        if (vq_color(vdi_h, i, 1, rgb[i]) < 0) { rgb[i][0] = rgb[i][1] = rgb[i][2] = i ? 0 : 1000; }
    }
    pen.bg = 0; pen.fg = 1;
    slot[0] = &pen.good; slot[1] = &pen.bad; slot[2] = &pen.accent;
    slot[3] = &pen.info; slot[4] = &pen.hilite;
    for (i = 0; i < 5; i++) {
        short k, best = -1;
        long bd = 0x7FFFFFFFL;
        *slot[i] = -1;
        if (n < 4) continue;                         /* mono: patterns only */
        for (k = 2; k < n; k++) {                    /* 0/1 are bg/text */
            long d = rgb_d2(rgb[k], want[i].r, want[i].g, want[i].b);
            if (d < bd && rgb_d2(rgb[k], rgb[0][0], rgb[0][1], rgb[0][2]) > 250L * 250L) { bd = d; best = k; }
        }
        if (best >= 0 && bd <= want[i].lim) *slot[i] = best;
    }
    /* text drawn on the accent colour: pen 0 or 1, whichever stands out */
    pen.on_accent = pen.fg;
    if (pen.accent >= 0) {
        long la = luma(rgb[pen.accent]);
        long d0 = la - luma(rgb[0]), d1 = la - luma(rgb[1]);
        if (d0 < 0) d0 = -d0;
        if (d1 < 0) d1 = -d1;
        pen.on_accent = d0 > d1 ? 0 : 1;
    }
}

WIN *win_find(short h)
{
    short i;
    if (h <= 0) return NULL;
    for (i = 0; i < NWIN; i++)
        if (wins[i].h == h) return &wins[i];
    return NULL;
}

static void win_redraw(WIN *w, const GRECT *area)
{
    GRECT work, r, a;
    short pxy[4];

    if (!w || w->h <= 0 || !w->draw) return;
    a = *area;
    if (!rc_intersect(&desk, &a)) return;
    fb_update(1);
    graf_mouse(M_OFF, NULL);
    wind_get_grect(w->h, WF_WORKXYWH, &work);
    wind_get_grect(w->h, WF_FIRSTXYWH, &r);
    while (r.g_w > 0 && r.g_h > 0) {
        if (rc_intersect(&a, &r)) {
            pxy[0] = r.g_x; pxy[1] = r.g_y;
            pxy[2] = r.g_x + r.g_w - 1; pxy[3] = r.g_y + r.g_h - 1;
            vs_clip(vdi_h, 1, pxy);
            w->draw(w, &work, &r);
        }
        wind_get_grect(w->h, WF_NEXTXYWH, &r);
    }
    vs_clip(vdi_h, 0, pxy);
    graf_mouse(M_ON, NULL);
    fb_update(0);
}

void win_redraw_all(WIN *w)
{
    GRECT work;
    if (!w || w->h <= 0) return;
    wind_get_grect(w->h, WF_WORKXYWH, &work);
    win_redraw(w, &work);
}

static short win_visible_rows(WIN *w)
{
    GRECT work;
    short v;
    wind_get_grect(w->h, WF_WORKXYWH, &work);
    v = work.g_h / w->lh;
    return v < 1 ? 1 : v;
}

static void win_sliders(WIN *w)
{
    short vis, maxtop;
    if (w->h <= 0 || !(w->kind & VSLIDE)) return;
    vis = win_visible_rows(w);
    maxtop = w->rows - vis;
    if (maxtop < 0) maxtop = 0;
    if (w->top > maxtop) w->top = maxtop;
    if (w->top < 0) w->top = 0;
    wind_set(w->h, WF_VSLSIZE, w->rows <= vis ? 1000 : (short)(vis * 1000L / w->rows), 0, 0, 0);
    wind_set(w->h, WF_VSLIDE, maxtop ? (short)(w->top * 1000L / maxtop) : 0, 0, 0, 0);
}

static void win_scroll_to(WIN *w, short top)
{
    short old = w->top;
    w->top = top;
    win_sliders(w);
    if (w->top != old) win_redraw_all(w);
}

void win_register(short slot, short handle, const char *title)
{
    WIN *w = &wins[slot];
    w->h = handle;
    w->title = title;
    w->top = 0;
    w->lh = char_h;
    w->rows = 1;
    w->draw = (slot == WN_TEST) ? test_draw : aux_draw;
}

static void win_open(short slot)
{
    WIN *w = &wins[slot];
    GRECT r;
    short cols;

    if (w->h > 0) { wind_set(w->h, WF_TOP, 0, 0, 0, 0); return; }

    if (slot == WN_RESULTS) {
        w->kind = NAME | CLOSER | FULLER | MOVER | SIZER | INFO | UPARROW | DNARROW | VSLIDE;
        results_title();
        w->title = res_title;
        w->draw = results_draw;
        w->lh = char_h + 4;
        cols = scr_w / char_w >= 80 ? 74 : scr_w / char_w;
        r.g_x = desk.g_x; r.g_y = desk.g_y;
        r.g_w = cols * char_w; r.g_h = desk.g_h;
        wind_calc_grect(WC_BORDER, (short)w->kind, &r, &r);
        r.g_x = desk.g_x; r.g_y = desk.g_y;
        if (r.g_w > desk.g_w) r.g_w = desk.g_w;
        if (r.g_h > desk.g_h) r.g_h = desk.g_h;
    } else {
        w->kind = NAME | CLOSER | FULLER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE;
        w->title = " Fbench system info ";
        w->draw = info_draw;
        w->lh = char_h + 1;
        w->rows = nsys;
        r.g_w = desk.g_w > 82 * char_w ? 82 * char_w : desk.g_w - box_w;
        r.g_h = desk.g_h * 3 / 4;
        r.g_x = desk.g_x + desk.g_w - r.g_w;
        r.g_y = desk.g_y + desk.g_h - r.g_h;
    }
    if (open_at) r = *open_at;
    w->h = wind_create_grect((short)w->kind, &desk);
    if (w->h <= 0) {
        w->h = 0;
        form_alert(1, "[3][Fbench:|No more windows available.][ OK ]");
        return;
    }
    wind_set_str(w->h, WF_NAME, w->title);
    if (w->kind & INFO) wind_set_str(w->h, WF_INFO, res_info);
    w->cur = r;
    wind_open_grect(w->h, &r);
    win_sliders(w);
}

static void win_close(WIN *w)
{
    if (!w || w->h <= 0) return;
    wind_close(w->h);
    wind_delete(w->h);
    w->h = 0;
}

/* close the results window for the duration of a run, remembering where it was */
void results_hide(void)
{
    WIN *w = &wins[WN_RESULTS];
    res_hidden = 0;
    if (w->h <= 0) return;
    wind_get_grect(w->h, WF_CURRXYWH, &res_saved);
    win_close(w);
    res_hidden = 1;
    drain_events();
}

/* reopen it at the same place and scroll position after the run */
void results_show(void)
{
    if (!res_hidden) return;
    res_hidden = 0;
    open_at = &res_saved;
    win_open(WN_RESULTS);
    open_at = NULL;
}

/* After a run in the background: put the reopened results window behind
   the program the user is working in, where the AES knows WF_BOTTOM
   (AES 4.1, MagiC 3+: appl_getinfo(AES_WINDOW) bit 6). */
void results_to_back(void)
{
    short a1 = 0, a2, a3, a4;
    WIN *w = &wins[WN_RESULTS];
    if (w->h <= 0) return;
    if ((_AESversion >= 0x0400 || appl_find("?AGI") >= 0) &&
        appl_getinfo(11, &a1, &a2, &a3, &a4) && (a1 & 0x40))
        wind_set(w->h, 25 /* WF_BOTTOM */, 0, 0, 0, 0);
}

/* Is the top window ours, i.e. is Fbench the program in front? */
short fb_in_front(void)
{
    short top = 0, d;
    wind_get(0, WF_TOP, &top, &d, &d, &d);
    return win_find(top) != NULL;
}

/* Is the whole work area of window h visible (one rectangle, unclipped
   except by the screen)?  Fbench's VDI tests draw straight into the work
   area, so they may only run when nothing covers it. */
short win_fully_visible(short h)
{
    GRECT work, r;
    if (h <= 0) return 0;
    wind_get_grect(h, WF_WORKXYWH, &work);
    if (!rc_intersect(&desk, &work)) return 0;
    wind_get_grect(h, WF_FIRSTXYWH, &r);
    if (r.g_x != work.g_x || r.g_y != work.g_y || r.g_w != work.g_w || r.g_h != work.g_h)
        return 0;
    wind_get_grect(h, WF_NEXTXYWH, &r);
    return r.g_w == 0 || r.g_h == 0;
}

void results_changed(void)
{
    WIN *w = &wins[WN_RESULTS];
    if (w->h <= 0) return;
    win_sliders(w);
    win_redraw_all(w);
}

static void update_res_info(void)
{
    char fn[40];
    short n = 0, i;
    for (i = 0; i < MAX_BASE; i++) if (base[i].used) n++;
    if (nfonts > 0) { strncpy(fn, fonts[gfont].name, 18); fn[18] = 0; }
    else strcpy(fn, "system");
    sprintf(res_info, " Blitter:%s  Time:%s  Font:%s  Baselines:%d",
            blitter_desc(),
            dur_ticks == 100 ? "0.5s" : (dur_ticks == 200 ? "1s" : "3s"),
            fn, n);
    if (wins[WN_RESULTS].h > 0 && !busy)
        wind_set_str(wins[WN_RESULTS].h, WF_INFO, res_info);
    update_menu();
}

void set_test_info(const char *s)
{
    strncpy(test_info, s, sizeof(test_info) - 1);
    if (wins[WN_TEST].h > 0)
        wind_set_str(wins[WN_TEST].h, WF_INFO, test_info);
    if (wins[WN_RESULTS].h > 0)
        wind_set_str(wins[WN_RESULTS].h, WF_INFO, test_info);
}

/* ---- the window the VDI tests draw into ---- */
short testwin_open(void)
{
    WIN *w = &wins[WN_TEST];
    GRECT r;
    short h;

    if (w->h > 0) return 1;
    r.g_x = desk.g_x + desk.g_w / 3;
    r.g_y = desk.g_y + box_h;
    r.g_w = desk.g_w - desk.g_w / 3 - 2;
    r.g_h = desk.g_h - box_h - 2;
    if (desk.g_w < 400) {                /* ST low: use most of the screen */
        r.g_x = desk.g_x + box_w; r.g_w = desk.g_w - box_w - 2;
    }
    h = wind_create_grect(NAME | INFO | CLOSER, &r);
    if (h <= 0) {
        form_alert(1, "[3][Fbench:|Cannot create the test|window (no handles).][ OK ]");
        return 0;
    }
    win_register(WN_TEST, h, " Fbench running ");
    w->kind = NAME | INFO | CLOSER;
    wind_set_str(h, WF_NAME, w->title);
    wind_set_str(h, WF_INFO, test_info);
    wind_open_grect(h, &r);
    drain_events();
    return 1;
}

void testwin_close(void)
{
    win_close(&wins[WN_TEST]);
    win_close(&wins[WN_AUX]);
}

/* ================================================================== */
/*  Progress window (non-modal)                                       */
/* ================================================================== */

static char  prog_title[48], prog_msg[64];
static short prog_pct, prog_prevbusy;

static void prog_draw(WIN *wn, const GRECT *work, const GRECT *clip)
{
    short bx = work->g_x + char_w, by = work->g_y + char_h + 8;
    short bw = work->g_w - 2 * char_w, bh = char_h + 2, p[4], cols = work->g_w / char_w;
    char pc[8];
    (void)wn;
    fill(clip->g_x, clip->g_y, clip->g_w, clip->g_h, 0, FIS_SOLID, 1);
    txt(work->g_x + char_w, work->g_y + 4, prog_msg, cols - 2, 1, 0);
    p[0] = bx; p[1] = by; p[2] = bx + bw - 1; p[3] = by + bh - 1;
    vswr_mode(vdi_h, MD_REPLACE);
    vsf_interior(vdi_h, FIS_HOLLOW);
    vsf_perimeter(vdi_h, 1);
    vsf_color(vdi_h, 1);
    v_bar(vdi_h, p);
    fill(bx + 2, by + 2, (short)((long)(bw - 4) * prog_pct / 100), bh - 4,
         pen.accent >= 0 ? pen.accent : pen.fg, pen.accent >= 0 ? FIS_SOLID : FIS_PATTERN, pen.accent >= 0 ? 1 : 4);
    sprintf(pc, "%d%%", prog_pct);
    txt(work->g_x + (work->g_w - (short)strlen(pc) * char_w) / 2, by + bh + 4, pc, 5, 1, 1);
}

void prog_open(const char *title)
{
    WIN *w = &wins[WN_PROG];
    GRECT r;
    short h;

    prog_prevbusy = busy;
    busy = 1;                        /* menu commands wait until we finish */
    if (w->h > 0) return;
    strncpy(prog_title, title, sizeof(prog_title) - 1);
    prog_msg[0] = 0;
    prog_pct = 0;
    r.g_w = 34 * char_w;
    if (r.g_w > desk.g_w - 2 * box_w) r.g_w = desk.g_w - 2 * box_w;
    r.g_h = 3 * char_h + 20;
    r.g_x = r.g_y = 0;
    wind_calc_grect(WC_BORDER, NAME | MOVER, &r, &r);
    r.g_x = desk.g_x + (desk.g_w - r.g_w) / 2;
    r.g_y = desk.g_y + (desk.g_h - r.g_h) / 2;
    h = wind_create_grect(NAME | MOVER, &r);
    if (h <= 0) return;              /* progress is optional: carry on without */
    w->h = h; w->kind = NAME | MOVER; w->title = prog_title;
    w->draw = prog_draw; w->lh = char_h; w->rows = 1; w->top = 0;
    wind_set_str(h, WF_NAME, prog_title);
    wind_open_grect(h, &r);
    drain_events();
}

void prog_set(short pct, const char *msg)
{
    WIN *w = &wins[WN_PROG];
    short changed = 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (msg && strncmp(msg, prog_msg, sizeof(prog_msg) - 1)) {
        strncpy(prog_msg, msg, sizeof(prog_msg) - 1);
        changed = 1;
    }
    if (pct != prog_pct) { prog_pct = pct; changed = 1; }
    if (w->h > 0 && changed) win_redraw_all(w);
    drain_events();                  /* keep other windows redrawn meanwhile */
}

void prog_close(void)
{
    win_close(&wins[WN_PROG]);
    busy = prog_prevbusy;
    drain_events();
}

/* After a file selector or dialog closes, the area it covered needs
   redrawing before any long job starts: handle those redraw messages now,
   and give the AES a moment to deliver late ones. */
static void after_dialog(void)
{
    drain_events();
    evnt_timer(40L);
    drain_events();
}

/* ================================================================== */
/*  Machine description                                               */
/* ================================================================== */

static void inf_path(char *p)
{
    strcpy(p, home_dir);
    strcat(p, "FBENCH.INF");
}

static void load_inf(void)
{
    char p[180], line[80];
    FILE *f;
    inf_path(p);
    if ((f = fopen(p, "r")) == NULL) return;
    while (fgets(line, sizeof(line), f)) {
        short n = (short)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "DESC=", 5)) {
            strncpy(machine_desc, line + 5, sizeof(machine_desc) - 1);
            machine_desc[sizeof(machine_desc) - 1] = 0;
        }
    }
    fclose(f);
}

static void save_inf(void)
{
    char p[180];
    FILE *f;
    inf_path(p);
    if ((f = fopen(p, "w")) == NULL) return;
    fprintf(f, "; Fbench settings\nDESC=%s\n", machine_desc);
    fclose(f);
}

static void results_title(void)
{
    /* the version is in the title, so every screenshot shows it */
    if (machine_desc[0]) sprintf(res_title, " Fbench %s - %.32s ", FB_VERSION, machine_desc);
    else                 sprintf(res_title, " Fbench %s results ", FB_VERSION);
}

/* Dialog built in code: one editable 32-character field, OK / Cancel */
static void desc_dialog(void)
{
    static OBJECT  dt[6];
    static TEDINFO ted;
    static char    buf[34], tmpl[34], valid[34];
    short i, x, y, w, h, r, n;

    memset(tmpl, '_', 32);  tmpl[32] = 0;
    memset(valid, 'X', 32); valid[32] = 0;
    strcpy(buf, machine_desc);

    memset(dt, 0, sizeof(dt));
    for (i = 0; i < 6; i++) dt[i].ob_next = dt[i].ob_head = dt[i].ob_tail = -1;
    dt[0].ob_type = G_BOX;    dt[0].ob_state = OS_OUTLINED;
    dt[0].ob_spec.index = 0x00021100L;
    dt[0].ob_x = 0; dt[0].ob_y = 0; dt[0].ob_width = 38; dt[0].ob_height = 9;
    dt[0].ob_head = 1; dt[0].ob_tail = 5;

    dt[1].ob_type = G_STRING; dt[1].ob_spec.free_string = "Machine description";
    dt[1].ob_x = 2; dt[1].ob_y = 1; dt[1].ob_width = 19; dt[1].ob_height = 1; dt[1].ob_next = 2;

    dt[2].ob_type = G_STRING; dt[2].ob_spec.free_string = "e.g. FireBee, MultiTOS, XGEM";
    dt[2].ob_x = 2; dt[2].ob_y = 2; dt[2].ob_width = 28; dt[2].ob_height = 1; dt[2].ob_next = 3;

    memset(&ted, 0, sizeof(ted));
    ted.te_ptext = buf; ted.te_ptmplt = tmpl; ted.te_pvalid = valid;
    ted.te_font = 3; ted.te_just = 0; ted.te_color = 0x1180;
    ted.te_txtlen = 33; ted.te_tmplen = 33;
    dt[3].ob_type = G_FTEXT;  dt[3].ob_spec.tedinfo = &ted;
    dt[3].ob_flags = OF_EDITABLE;
    dt[3].ob_x = 2; dt[3].ob_y = 4; dt[3].ob_width = 32; dt[3].ob_height = 1; dt[3].ob_next = 4;

    dt[4].ob_type = G_BUTTON; dt[4].ob_spec.free_string = "OK";
    dt[4].ob_flags = OF_SELECTABLE | OF_DEFAULT | OF_EXIT;
    dt[4].ob_x = 16; dt[4].ob_y = 7; dt[4].ob_width = 8; dt[4].ob_height = 1; dt[4].ob_next = 5;

    dt[5].ob_type = G_BUTTON; dt[5].ob_spec.free_string = "Cancel";
    dt[5].ob_flags = OF_SELECTABLE | OF_EXIT | OF_LASTOB;
    dt[5].ob_x = 27; dt[5].ob_y = 7; dt[5].ob_width = 9; dt[5].ob_height = 1; dt[5].ob_next = 0;

    for (i = 0; i < 6; i++) rsrc_obfix(dt, i);
    form_center(dt, &x, &y, &w, &h);
    fb_update(1);
    form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
    objc_draw(dt, 0, 8, x, y, w, h);
    r = form_do(dt, 3) & 0x7FFF;
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
    fb_update(0);
    dt[r].ob_state &= ~OS_SELECTED;
    after_dialog();
    if (r != 4) return;

    n = (short)strlen(buf);
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
    strcpy(machine_desc, buf);
    save_inf();
    results_title();
    if (wins[WN_RESULTS].h > 0) wind_set_str(wins[WN_RESULTS].h, WF_NAME, res_title);
    sysinfo_collect();
    if (wins[WN_INFO].h > 0) { wins[WN_INFO].rows = nsys; win_redraw_all(&wins[WN_INFO]); }
}

/* ================================================================== */
/*  File selector                                                     */
/* ================================================================== */

static void init_path(void)
{
    short drv = Dgetdrv();
    last_path[0] = 'A' + drv; last_path[1] = ':';
    if (Dgetpath(last_path + 2, drv + 1) < 0) last_path[2] = 0;
    if (!last_path[2]) strcpy(last_path + 2, "\\");
    if (last_path[strlen(last_path) - 1] != '\\') strcat(last_path, "\\");
}

/* returns 1 and full path in out on OK */
static short file_select(const char *title, const char *pattern, const char *defname, char *out)
{
    char path[200], file[20];
    short ok = 0, rc;
    char *p;

    strcpy(path, last_path);
    strcat(path, pattern);
    strcpy(file, defname);
    fb_update(1);
    if (_AESversion >= 0x0140) rc = fsel_exinput(path, file, &ok, title);
    else                       rc = fsel_input(path, file, &ok);
    fb_update(0);
    after_dialog();
    if (!rc || !ok || !file[0]) return 0;
    p = strrchr(path, '\\');
    if (p) p[1] = 0;
    strcpy(last_path, path);
    strcpy(out, path);
    strcat(out, file);
    return 1;
}

/* ================================================================== */
/*  Commands                                                          */
/* ================================================================== */

static void select_group(short g)   /* g<0: all, g==-2: none */
{
    short i;
    for (i = 0; i < ntests; i++)
        res[i].sel = (g == -1) ? 1 : (g == -2 ? 0 : tests[i].group == g);
    results_changed();
}

/* Run the selected tests with the blitter off, then on, and write a PDF.
   The blitter is put back as it was; the results window then shows the
   blitter-on run against the blitter-off run as baseline. */
static void blitter_report(void)
{
    char path[220];
    RESULT *off, *on;
    short orig = blitter_on, ok, slot;

    if (!blitter_present) {
        form_alert(1, "[1][Fbench:|This machine has no blitter,|so there is nothing to|compare.][ OK ]");
        return;
    }
    if (blitter_fixed) {
        form_alert(1, "[1][Fbench:|This TOS cannot switch the|blitter off (XBIOS 64 is|ignored, e.g. TOS 4.x), so|an off/on run isn't possible.][ OK ]");
        return;
    }
    if (form_alert(1, "[2][Blitter report:|Runs the selected tests twice,|blitter off then on, and|writes a PDF comparison.][ Start |Cancel]") != 1)
        return;
    if (!file_select("Save blitter report (PDF)", "*.PDF", "BLITTER.PDF", path)) return;
    off = malloc(sizeof(RESULT) * MAX_TESTS);
    on  = malloc(sizeof(RESULT) * MAX_TESTS);
    if (!off || !on) {
        free(off); free(on);
        form_alert(1, "[3][Fbench:|Not enough memory.][ OK ]");
        return;
    }

    blit_set(0); update_res_info();
    ok = run_tests(0) && !abort_run && !quit_app;
    memcpy(off, res, sizeof(RESULT) * MAX_TESTS);
    if (ok) {
        blit_set(1); update_res_info();
        ok = run_tests(0) && !abort_run && !quit_app;
        memcpy(on, res, sizeof(RESULT) * MAX_TESTS);
    }
    blit_set(orig);

    if (ok) {
        prog_open(" Fbench: blitter report ");
        ok = blit_report(path, off, on);
        prog_close();
        if (!ok) form_alert(1, "[3][Fbench:|Could not write the PDF.][ OK ]");
        /* show blitter-on results against blitter-off in the window */
        for (slot = 0; slot < MAX_BASE && base[slot].used; slot++) ;
        if (slot == MAX_BASE) slot = MAX_BASE - 1;
        baseline_from_results(slot, "Blitter off (this run)", off);
        memcpy(res, on, sizeof(RESULT) * MAX_TESTS);
        active_base = slot;
    } else if (!quit_app)
        form_alert(1, "[1][Fbench:|Blitter report cancelled.][ OK ]");
    free(off); free(on);
    update_res_info();
    results_changed();
}

/* Pick a folder (by choosing any profile in it) and compare all its *.FBP */
/* Folder picker built on the GEM file selector: open the folder and press
   OK - the file name may be left empty.  Returns the folder with a
   trailing backslash. */
static short dir_select(const char *title, char *dir)
{
    char path[200], file[20], *p;
    short ok = 0, rc;

    strcpy(path, last_path);
    strcat(path, "*.FBP");
    file[0] = 0;
    fb_update(1);
    if (_AESversion >= 0x0140) rc = fsel_exinput(path, file, &ok, title);
    else                       rc = fsel_input(path, file, &ok);
    fb_update(0);
    after_dialog();
    if (!rc || !ok) return 0;
    p = strrchr(path, '\\');
    if (!p) return 0;
    p[1] = 0;                        /* drop the *.FBP mask */
    strcpy(last_path, path);
    strcpy(dir, path);
    return 1;
}

/* Compare every *.FBP profile in a folder; COMPARE.PDF goes in the folder */
static void compare_profiles(void)
{
    char dir[200], out[220], msg[200], shown[40];
    short n = 0, total = 0, ok, l;

    if (!dir_select("Open the profile folder, press OK", dir)) return;
    strcpy(out, dir);
    strcat(out, "COMPARE.PDF");
    prog_open(" Fbench: compare folder ");
    ok = profiles_report(dir, out, &n, &total);
    prog_close();

    l = (short)strlen(dir);              /* folder name for the alert */
    if (l > 26) { strcpy(shown, ".."); strcat(shown, dir + l - 24); }
    else strcpy(shown, dir);
    if (!n)
        sprintf(msg, "[1][Fbench:|No Fbench profiles (*.FBP)|found in|%s][ OK ]", shown);
    else if (!ok)
        sprintf(msg, "[3][Fbench:|Could not write COMPARE.PDF|in %s][ OK ]", shown);
    else if (total > n)
        sprintf(msg, "[1][Fbench: compared the first|%d of %d profiles (by name).|COMPARE.PDF written to|%s][ OK ]", n, total, shown);
    else
        sprintf(msg, "[1][Fbench: compared %d profiles.|COMPARE.PDF written to|%s][ OK ]", n, shown);
    form_alert(1, msg);
}

static void do_cmd(short cmd)
{
    char path[220];
    short i;

    switch (cmd) {
    case C_ABOUT:
        {
            char al[200];
            sprintf(al, "[1][Fbench %s|GEM VDI / AES / GDOS benchmark|for Atari ST, STE, TT, Falcon|MultiTOS and XGEM.][ OK ]", FB_VERSION);
            form_alert(1, al);
        }
        break;
    case C_SAVEPROF:
        if (file_select("Save profile (baseline)", "*.FBP", "MACHINE.FBP", path)) {
            if (!profile_save(path))
                form_alert(1, "[3][Fbench:|Could not write the profile.][ OK ]");
        }
        break;
    case C_PDFREP:
        if (file_select("Save results report (PDF)", "*.PDF", "RESULTS.PDF", path)) {
            short ok, k, any = 0;
            for (k = 0; k < ntests; k++) if (res[k].state != RS_NONE) any = 1;
            if (!any && form_alert(1, "[2][Fbench:|No tests have been run yet.|Write a configuration-only|report anyway?][ Write |Cancel]") != 1)
                break;
            prog_open(" Fbench: results report ");
            ok = machine_report(path);
            prog_close();
            if (!ok) form_alert(1, "[3][Fbench:|Could not write the PDF.][ OK ]");
        }
        break;
    case C_BLITREP:
        blitter_report();
        break;
    case C_CMPPROF:
        compare_profiles();
        break;
    case C_EXPORT:
        if (file_select("Export report", "*.TXT", "FBENCH.TXT", path)) {
            short ok;
            prog_open(" Fbench: export report ");
            ok = report_export(path);
            prog_close();
            if (!ok) form_alert(1, "[3][Fbench:|Could not write the report.][ OK ]");
        }
        break;
    case C_QUIT:
        quit_app = 1;
        break;
    case C_RUNALL:
    case C_RUNSEL:
        run_tests(cmd == C_RUNALL);
        update_res_info();
        if (wins[WN_RESULTS].h > 0) wind_set_str(wins[WN_RESULTS].h, WF_INFO, res_info);
        results_changed();
        break;
    case C_SELALL:  select_group(-1); break;
    case C_SELNONE: select_group(-2); break;
    case C_SELVDI:  select_group(GRP_VDI); break;
    case C_SELAES:  select_group(GRP_AES); break;
    case C_SELALIGN: select_group(GRP_ALIGN); break;
    case C_SELFULL: select_group(GRP_FULL); break;
    case C_SELGDOS: select_group(GRP_GDOS); break;
    case C_LOADBASE:
        if (file_select("Load baseline profile", "*.FBP", "", path)) {
            graf_mouse(BUSY_BEE, NULL);
            i = baseline_load(path);
            graf_mouse(ARROW, NULL);
            if (i < 0)
                form_alert(1, "[3][Fbench:|No Fbench results found|in that file.][ OK ]");
            else
                active_base = i;
            update_res_info();
            results_changed();
        }
        break;
    case C_CLEARBASE:
        baseline_clear();
        update_res_info();
        results_changed();
        break;
    case C_BASE0: case C_BASE1: case C_BASE2: case C_BASE3:
        if (base[cmd - C_BASE0].used) active_base = cmd - C_BASE0;
        update_res_info();
        results_changed();
        break;
    case C_BLITTER:
        if (blitter_fixed) {
            form_alert(1, "[1][Fbench:|This TOS cannot switch the|blitter off (XBIOS 64 is|ignored, e.g. TOS 4.x).][ OK ]");
            break;
        }
        if (blitter_present) {
            blitter_on = !blitter_on;
            blit_set(blitter_on);
            sysinfo_collect();
            if (wins[WN_INFO].h > 0) { wins[WN_INFO].rows = nsys; win_redraw_all(&wins[WN_INFO]); }
            update_res_info();
        }
        break;
    case C_DSHORT: dur_ticks = 100; update_res_info(); break;
    case C_DNORM:  dur_ticks = 200; update_res_info(); break;
    case C_DLONG:  dur_ticks = 600; update_res_info(); break;
    case C_NEXTFONT:
        if (nfonts > 1) {
            gfont = (gfont + 1) % nfonts;
            update_res_info();
        }
        break;
    case C_DESC:
        desc_dialog();
        break;
    case C_TRACE:
        trace_steps = !trace_steps;
        if (trace_steps)
            form_alert(1, "[1][Trace test steps:|Each step of a test's first|16 operations is written to|FBLAST.LOG and the screen.|Scores will be lower.][ OK ]");
        update_menu();
        break;
    case C_POLITE:
        polite = !polite;
        update_menu();
        break;
    case C_MOUSEVIS:
        mouse_vis = !mouse_vis;
        update_menu();
        break;
    case C_WRESULTS: win_open(WN_RESULTS); break;
    case C_WINFO:
        sysinfo_collect();
        wins[WN_INFO].rows = nsys;
        win_open(WN_INFO);
        break;
    }
}

static void do_key(short key, short kstate)
{
    short a = key & 0xff;
    (void)kstate;
    switch (a) {
    case 0x01: do_cmd(C_RUNALL);   break;   /* ^A */
    case 0x12: do_cmd(C_RUNSEL);   break;   /* ^R */
    case 0x11: do_cmd(C_QUIT);     break;   /* ^Q */
    case 0x05: do_cmd(C_EXPORT);   break;   /* ^E */
    case 0x0f: do_cmd(C_LOADBASE); break;   /* ^O */
    case 0x13: do_cmd(C_SAVEPROF); break;   /* ^S */
    case 0x02: do_cmd(C_BLITTER);  break;   /* ^B */
    case 0x06: do_cmd(C_NEXTFONT); break;   /* ^F */
    case 0x09: do_cmd(C_WINFO);    break;   /* ^I / Tab */
    case 0x15: {                             /* ^U close top window */
        short top, d;
        wind_get(0, WF_TOP, &top, &d, &d, &d);
        win_close(win_find(top));
        break; }
    }
}

/* ================================================================== */
/*  Event handling                                                    */
/* ================================================================== */

static void handle_message(short *msg)
{
    WIN *w = win_find(msg[3]);
    GRECT r;
    short vis;

    switch (msg[0]) {
    case MN_SELECTED:
        /* un-highlight the title first: a command may run for minutes and
           single-tasking TOS keeps menu state until the app releases it */
        menu_tnormal(menu_tree, msg[3], 1);
        if (!busy) do_cmd(objcmd[msg[4]]);
        break;
    case WM_REDRAW:
        r.g_x = msg[4]; r.g_y = msg[5]; r.g_w = msg[6]; r.g_h = msg[7];
        win_redraw(w, &r);
        break;
    case WM_TOPPED:
    case WM_NEWTOP:
        /* during a run the user may bring the test window forward (that is
           how a paused run continues) but nothing else of ours moves */
        if (w && (!busy || w == &wins[WN_PROG] || w == &wins[WN_TEST]))
            wind_set(w->h, WF_TOP, 0, 0, 0, 0);
        break;
    case WM_CLOSED:
        if (!w) break;
        if (busy) { if (w == &wins[WN_TEST]) abort_run = 1; break; }
        if (w == &wins[WN_RESULTS] || w == &wins[WN_INFO]) win_close(w);
        break;
    case WM_MOVED:
    case WM_SIZED:
        if (!w || (busy && (w == &wins[WN_TEST] || w == &wins[WN_AUX]))) break;
        r.g_x = msg[4]; r.g_y = msg[5];
        r.g_w = msg[6] < 12 * box_w ? 12 * box_w : msg[6];
        r.g_h = msg[7] < 6 * box_h ? 6 * box_h : msg[7];
        wind_set_grect(w->h, WF_CURRXYWH, &r);
        w->cur = r;
        win_sliders(w);
        if (msg[0] == WM_SIZED) win_redraw_all(w);
        break;
    case WM_FULLED:
        if (!w || busy) break;
        {
            GRECT c, f;
            wind_get_grect(w->h, WF_CURRXYWH, &c);
            wind_get_grect(w->h, WF_FULLXYWH, &f);
            if (c.g_x == f.g_x && c.g_y == f.g_y && c.g_w == f.g_w && c.g_h == f.g_h)
                f = w->prev;
            else
                w->prev = c;
            if (f.g_w <= 0) break;
            wind_set_grect(w->h, WF_CURRXYWH, &f);
            win_sliders(w);
            win_redraw_all(w);
        }
        break;
    case WM_ARROWED:
        if (!w) break;
        vis = win_visible_rows(w);
        switch (msg[4]) {
        case WA_UPPAGE: win_scroll_to(w, w->top - vis); break;
        case WA_DNPAGE: win_scroll_to(w, w->top + vis); break;
        case WA_UPLINE: win_scroll_to(w, w->top - 1);   break;
        case WA_DNLINE: win_scroll_to(w, w->top + 1);   break;
        }
        break;
    case WM_VSLID:
        if (!w) break;
        vis = win_visible_rows(w);
        win_scroll_to(w, (short)((long)msg[4] * (w->rows - vis > 0 ? w->rows - vis : 0) / 1000));
        break;
    case AP_TERM:
        quit_app = 1;
        abort_run = 1;
        break;
    case FB_PING:
        ping_seen = 1;
        break;
    case 0x5854:                         /* XGEM XM_THEME: theme (pens) changed */
        pens_update();
        { short k; for (k = 0; k < NWIN; k++) win_redraw_all(&wins[k]); }
        break;
    }
}

static void handle_click(short mx, short my)
{
    WIN *w = &wins[WN_RESULTS];
    GRECT work;
    short row, code, i;

    if (busy || w->h <= 0 || wind_find(mx, my) != w->h) return;
    wind_get_grect(w->h, WF_WORKXYWH, &work);
    if (mx < work.g_x || my < work.g_y || mx >= work.g_x + work.g_w || my >= work.g_y + work.g_h)
        return;
    row = w->top + (my - work.g_y) / w->lh;
    if (row < 0 || row >= nrows) return;
    code = rowmap[row];
    if (code == -1) return;
    if (code <= -10) {
        short g = -(code + 10), tot, sel = group_selected(g, &tot);
        for (i = 0; i < ntests; i++)
            if (tests[i].group == g) res[i].sel = (sel < tot);
    } else
        res[code].sel = !res[code].sel;
    win_redraw_all(w);
    /* wait for button release so one click toggles once */
    evnt_button(1, 1, 0, &mx, &my, &i, &i);
}

/*
 * While a run is in progress and "Mouse visible in tests" is on, show the
 * busy bee only when the pointer is over Fbench's menu bar or one of its
 * windows; elsewhere keep the arrow so other applications stay usable.
 * busy_mouse(0) restores the arrow.
 */
void busy_mouse(short on)
{
    short mx, my, mb, ks, f = ARROW;

    if (on && mouse_vis) {
        graf_mkstate(&mx, &my, &mb, &ks);
        if (my < desk.g_y || win_find(wind_find(mx, my)) != NULL)
            f = BUSY_BEE;
    }
    if (f != cur_form) {
        graf_mouse(f, NULL);
        cur_form = f;
    }
}

/* Poll and handle all pending events without blocking (used by tests) */
void drain_events(void)
{
    short msg[8], mx, my, mb, ks, key, clicks, ev, guard = 0;
    /* Single-tasking TOS (AES < 4.0): a zero timer returns at once without
       ever running the AES screen manager, so window work and redraw
       messages pile up until the AES blocks writing to our own full message
       pipe.  The dispatcher call (trap #2, d0=201) lets it run first. */
    if (!multitask && _AESversion < 0x0400) {
        fb_step("drain: dispatch (appl_yield)");
        _appl_yield();
    }
    fb_step("drain: evnt_multi");
    do {
        ev = evnt_multi(MU_MESAG | MU_TIMER | (busy ? MU_KEYBD : 0),
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks, &key, &clicks);
        if (ev & MU_MESAG) {
            if (msg[0] == WM_REDRAW) fb_step("drain: WM_REDRAW");
            handle_message(msg);
        }
        if ((ev & MU_KEYBD) && (key & 0xff) == 0x1b) abort_run = 1;
    } while ((ev & (MU_MESAG | MU_KEYBD)) && ++guard < 64);
}

static void event_loop(void)
{
    short msg[8], mx, my, mb, ks, key, clicks, ev;

    while (!quit_app) {
        ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON,
                        2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        msg, 0L, &mx, &my, &mb, &ks, &key, &clicks);
        if (ev & MU_MESAG)  handle_message(msg);
        if (ev & MU_KEYBD)  do_key(key, ks);
        if (ev & MU_BUTTON) handle_click(mx, my);
    }
}

/* ================================================================== */
/*  Start-up / shut-down                                              */
/* ================================================================== */

/*
 * Open a virtual screen workstation.  With GDOS loaded the device id
 * Getrez()+2 selects the screen driver from ASSIGN.SYS; some VDIs and
 * set-ups reject that (no GDOS driver for the mode, extended TT/Falcon
 * modes, fVDI/NVDI configurations), so fall back to device 1, which every
 * VDI accepts for a virtual screen workstation.  Returns 0 on failure.
 */
short open_vwk(short *out)
{
    short in[11], ids[2], n = 0, t, h, i;

    if (vq_gdos()) ids[n++] = Getrez() + 2;
    ids[n++] = 1;
    for (t = 0; t < n; t++) {
        for (i = 0; i < 10; i++) in[i] = 1;
        in[0] = ids[t];
        in[10] = 2;                 /* raster coordinates */
        h = phys_h;
        v_opnvwk(in, &h, out);
        if (h > 0) return h;
    }
    return 0;
}

int main(void)
{
    short dummy[57], i;

    app_id = appl_init();
    if (app_id < 0) return 1;
    /* MagiC (any flavour: Atari, MagiC Mac, MagiC PC, MagiCOnLinux) sets the
       MagX cookie; it multitasks whatever global[1] says */
    magic_present = get_cookie(0x4D616758L, NULL);              /* 'MagX' */
    multitask = (_AESnumapps != 1) || magic_present;
    mouse_vis = multitask;              /* default on when multitasking */
    polite = multitask;                 /* never top windows when others run */

    phys_h = graf_handle(&char_w, &char_h, &box_w, &box_h);
    vdi_h = open_vwk(work_out);
    if (vdi_h <= 0) {
        char al[160];
        sprintf(al, "[3][Fbench:|Cannot open a VDI workstation.|physical handle %d, Getrez %d,|GDOS %s][ Quit ]",
                phys_h, Getrez(), vq_gdos() ? "loaded" : "not loaded");
        form_alert(1, al);
        appl_exit();
        return 1;
    }
    bh = 0;                             /* opened only while tests run */
    (void)dummy;
    vq_extnd(vdi_h, 1, ext_out);
    scr_w   = work_out[0] + 1;
    scr_h   = work_out[1] + 1;
    ncolors = work_out[13];
    planes  = ext_out[4];
    vst_alignment(vdi_h, 0, 5, &i, &i);     /* left / top */
    vsf_perimeter(vdi_h, 0);

    wind_get_grect(0, WF_WORKXYWH, &desk);

    cpu_type = cpu_probe();
    blit_detect();
    pens_update();                      /* read the palette; never write it */

    if (_AESversion >= 0x0400 || multitask)
        shel_write(SWM_NEWMSG, NM_APTERM, 0, NULL, NULL);

    menu_tree = build_menu();
    menu_bar(menu_tree, 1);
    if (multitask || _AESversion >= 0x0400)
        menu_register(app_id, "  Fbench");

    graf_mouse(BUSY_BEE, NULL);
    init_path();
    strcpy(home_dir, last_path);
    load_inf();
    fonts_init();
    tests_init();
    sysinfo_collect();
    build_rows();
    graf_mouse(ARROW, NULL);
    cur_form = ARROW;

    update_res_info();
    win_open(WN_RESULTS);

    event_loop();

    /* each quit step leaves a breadcrumb in FBLAST.LOG, so a crash while
       quitting names the call that crashed; the file is removed at the end */
    crumb_text("quit 1/6: closing windows");
    for (i = 0; i < NWIN; i++) win_close(&wins[i]);
    crumb_text("quit 2/6: menu_bar(tree, 0)");
    menu_bar(menu_tree, 0);
    crumb_text("quit 3/6: restoring blitter (XBIOS 64)");
    if (blitter_present && !blitter_fixed) blit_set(blitter_orig);
    crumb_text("quit 4/6: closing bench workstation");
    fonts_exit();
    crumb_text("quit 5/6: v_clsvwk(ui workstation)");
    v_clsvwk(vdi_h);
    crumb_text("quit 6/6: appl_exit");
    appl_exit();
    crumb_clear();
    return 0;
}
