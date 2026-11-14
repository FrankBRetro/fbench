/*
 * Fbench - tests.c : the VDI, AES and GDOS benchmarks and the timing loop
 *
 * Every test is a small "body" function that performs ONE operation.
 * The runner calls it in growing batches until the chosen duration
 * (0.5 / 1 / 3 s, measured with the 200 Hz system timer) has passed,
 * and reports operations per second.
 */
#include "fbench.h"

RESULT  res[MAX_TESTS];
short   run_tw_w, run_tw_h;
short   ntests;
short   test_fail;
GRECT   tw_area;
FONTREC fonts[MAX_FONTS];
short   nfonts, gdos_loaded, gfont;
long    gdos_type = GDOS_NONE;

const char *group_name[NGROUPS] = { "VDI primitives", "AES windows & events",
                                     "Alignment (blitter)", "Full-screen windows",
                                     "GDOS fonts" };

static char  cur_note[28];
static short cur_param;                 /* TEST.param of the running test */
static short ax, ay, aw, ah;            /* drawing area (test window work) */
static short pts[256][2];
/* Positions for raster copies (vro_cpyfm / vrt_cpyfm): the whole RBLK x RBLK
   block, plus 16 px for alignment shifts, stays inside the test area.  The
   VDI of TOS 1.x does not clip raster copies, so a block running past the
   bottom of the screen read and wrote beyond screen memory - past the end
   of RAM on a 4 MB machine - and crashed (VDI.CPY64 on TOS 1.06). */
#define RBLK 64
static short rpts[256][2];               /* pre-computed random points      */
static short rad[256];                  /* pre-computed random radii       */
static unsigned char pi;                /* index into the tables (wraps)   */
static short sys_th;                    /* default system text height      */
static short tog;                       /* alternating state for tests     */
static MFDB  scr_mfdb;                  /* fd_addr == NULL: the screen     */

#define NX() (pi++)

/* ------------------------------------------------------------------ */
/*  Timer                                                             */
/* ------------------------------------------------------------------ */

#ifdef PCGEM
/* the PC: DJGPP's uclock() counts the timer chip at 1.19 MHz; scaled to
 * the Atari's 200 Hz ticks so durations and results mean the same */
#include <time.h>
long hz200(void)
{
    return (long)(uclock() * 200 / UCLOCKS_PER_SEC);
}
#else
static long hz_tmp;
static long rd_hz(void) { hz_tmp = *(volatile long *)0x4baL; return 0; }

long hz200(void)
{
    Supexec(rd_hz);
    return hz_tmp;
}
#endif

/* returns operations per second x 10, or -1 on failure */
static long max_iter;
static short step_on, step_test = -1, step_op;
short trace_steps;          /* Options > Trace test steps (debug, slows tests) */

/* Trace one step of the first operation of a test into FBLAST.LOG, so a
   hang inside a test names the exact AES call. */
void fb_step(const char *s)
{
#if FB_DEBUG
    char p[180];
    FILE *f;
    if (!step_on || step_test < 0) return;
    /* 1. on screen, straight through the VDI (no AES, no disk): the last
          line drawn shows where a freeze happened even if the disk can't
          be written any more */
    {
        short px[4], d;
        sprintf(p, "op %d: %-28s", step_op + 1, s);
        px[0] = tw_area.g_x; px[1] = tw_area.g_y + tw_area.g_h - char_h - 2;
        px[2] = tw_area.g_x + tw_area.g_w - 1; px[3] = tw_area.g_y + tw_area.g_h - 1;
        vs_clip(vdi_h, 1, px);
        vswr_mode(vdi_h, MD_REPLACE);
        vst_color(vdi_h, 1);
        vst_alignment(vdi_h, 0, 5, &d, &d);
        v_gtext(vdi_h, px[0] + 2, px[1] + 1, p);
        vs_clip(vdi_h, 0, px);
    }
    /* 2. into FBLAST.LOG */
    strcpy(p, home_dir);
    strcat(p, "FBLAST.LOG");
    if ((f = fopen(p, "w")) != NULL) {
        fprintf(f, "Fbench %s - hung or crashed in:\n%s  %s\noperation %d, step: %s\n",
                FB_VERSION, tests[step_test].key, tests[step_test].name, step_op + 1, s);
        fclose(f);
    }
#else
    (void)s;
#endif
}        /* >0: stop after this many operations */

/* The first operation of a test is done once, untimed: it can include
   one-off costs - a GDOS building and caching the glyphs of a new size
   (XGDOS: ~1.2 s on an STE, then ~0.08 s a line), loading, first-touch -
   that would otherwise land in, or even fill, a 1 s measurement and make
   runs differ wildly.  Its time is kept; if it was much slower than the
   operations after it, the note says so ("1st op 1.25 s"). */
static long first_ticks, first_slow;

static long time_test(void (*body)(void))
{
    long n = 0, batch = 1, i, t0, t, el;

    first_ticks = first_slow = 0;
    if (max_iter <= 0) {                /* limited tests (font load) aren't repeated */
        t0 = hz200();
        body();
        first_ticks = hz200() - t0;
        if (step_on && ++step_op >= 16) step_on = 0;
        if (test_fail) return -1;
    }
    t0 = hz200();
    while ((t = hz200()) == t0)        /* start on a tick edge */
        ;
    t0 = t;
    for (;;) {
        for (i = 0; i < batch && !test_fail; i++) {
            body();
            if (step_on && ++step_op >= 16) step_on = 0;   /* trace the first 16 ops */
        }
        n += i;
        fb_step("reading timer (Supexec)");
        el = hz200() - t0;
        fb_step("timer read OK");
        if (max_iter > 0 && n >= max_iter) break;
        if (test_fail) return -1;
        if (el >= dur_ticks) break;
        if (el < dur_ticks / 8) batch <<= 1;
    }
    if (el < 1) el = 1;
    /* first op much slower than the rest (and at least 50 ms)? */
    if (n > 0 && first_ticks >= 10 && first_ticks * n > 4 * el) first_slow = 1;
    if (n < 1000000L) return n * 2000L / el;
    return (n / el) * 2000L;
}

/* ------------------------------------------------------------------ */
/*  Shared set-up                                                     */
/* ------------------------------------------------------------------ */

static unsigned long seed = 0x46424E43UL;
static short rnd(short n)
{
    seed = seed * 1103515245UL + 12345UL;
    return n > 0 ? (short)((seed >> 16) % (unsigned short)n) : 0;
}

static short col(short i)
{
    short n = ncolors > 16 ? 16 : ncolors;
    if (n <= 2) return 1;
    return 1 + (i % (n - 1));
}

static void vdi_reset(void)
{
    short d;
    vswr_mode(bh, MD_REPLACE);
    vsl_color(bh, 1); vsl_width(bh, 1); vsl_type(bh, SOLID); vsl_ends(bh, 0, 0);
    vsf_color(bh, 1); vsf_interior(bh, FIS_SOLID); vsf_style(bh, 1); vsf_perimeter(bh, 1);
    vsm_type(bh, 1); vsm_color(bh, 1); vsm_height(bh, 1);
    vst_font(bh, 1); vst_color(bh, 1); vst_effects(bh, 0); vst_rotation(bh, 0);
    vst_alignment(bh, 0, 5, &d, &d);
    vst_height(bh, sys_th, &d, &d, &d, &d);
}

static void clear_area(void)
{
    short p[4];
    p[0] = ax; p[1] = ay; p[2] = ax + aw - 1; p[3] = ay + ah - 1;
    vswr_mode(bh, MD_REPLACE);
    vsf_interior(bh, FIS_SOLID);
    vsf_color(bh, 0);
    vr_recfl(bh, p);
    vsf_color(bh, 1);
}

static void area_setup(short test)
{
    short p[4], i, rmax;
    const char *k;

    ax = tw_area.g_x; ay = tw_area.g_y; aw = tw_area.g_w; ah = tw_area.g_h;
    /* never draw outside the screen work area */
    if (ax + aw > desk.g_x + desk.g_w) aw = desk.g_x + desk.g_w - ax;
    if (ay + ah > desk.g_y + desk.g_h) ah = desk.g_y + desk.g_h - ay;
    p[0] = ax; p[1] = ay; p[2] = ax + aw - 1; p[3] = ay + ah - 1;
    vs_clip(bh, 1, p);
    /* the points cover the whole window, as before; the sequence is
       seeded from the test's key, so it doesn't depend on which tests ran
       first and every run of a test (long run) draws the same things */
    seed = 0x46424E43UL;
    for (k = tests[test].key; *k; k++) seed = seed * 31UL + (unsigned char)*k;
    rmax = (aw < ah ? aw : ah) / 4;
    if (rmax < 8) rmax = 8;
    for (i = 0; i < 256; i++) {
        pts[i][0] = ax + rnd(aw);
        pts[i][1] = ay + rnd(ah);
        rad[i] = 4 + rnd(rmax);
        rpts[i][0] = ax + rnd(aw - RBLK - 16 > 1 ? aw - RBLK - 16 : 1);
        rpts[i][1] = ay + rnd(ah - RBLK > 1 ? ah - RBLK : 1);
    }
    pi = 0;
    tog = 0;
    vdi_reset();
}

static void area_done(void)
{
    short p[4] = { 0, 0, 0, 0 };
    vdi_reset();
    vs_clip(bh, 0, p);
}

/* ------------------------------------------------------------------ */
/*  VDI tests                                                         */
/* ------------------------------------------------------------------ */

static void s_colour(void) { vsl_color(bh, col(2)); vsf_color(bh, col(3)); vsm_color(bh, col(4)); }

static void b_pline2(void)
{
    short p[4], a = NX(), b = NX();
    p[0] = pts[a][0]; p[1] = pts[a][1]; p[2] = pts[b][0]; p[3] = pts[b][1];
    v_pline(bh, 2, p);
}

static short poly[64];
static short npoly = 32;
static void s_pline32(void)
{
    short i;
    s_colour();
    npoly = 32;
    if (ext_out[14] > 1 && ext_out[14] < npoly) npoly = ext_out[14];
    for (i = 0; i < npoly; i++) {
        poly[i * 2]     = ax + rnd(aw);
        poly[i * 2 + 1] = ay + rnd(ah);
    }
}
static void b_pline32(void) { v_pline(bh, npoly, poly); }

static void s_wide(void)   { s_colour(); vsl_width(bh, 7); vsl_ends(bh, 2, 2); }
static void s_dashed(void) { s_colour(); vsl_type(bh, 4); }
static void s_user(void)   { s_colour(); vsl_type(bh, 7); vsl_udsty(bh, (short)0xF0CC); }
static void s_xorln(void)  { vswr_mode(bh, MD_XOR); }

static void s_marker(void) { s_colour(); vsm_type(bh, 6); vsm_height(bh, 11); }
static void b_marker(void)
{
    short p[32], i;
    for (i = 0; i < 16; i++) { short k = NX(); p[i * 2] = pts[k][0]; p[i * 2 + 1] = pts[k][1]; }
    v_pmarker(bh, 16, p);
}

static void b_rect32(void)
{
    short p[4], k = NX();
    p[0] = pts[k][0]; p[1] = pts[k][1]; p[2] = p[0] + 31; p[3] = p[1] + 31;
    vr_recfl(bh, p);
}

static void b_rectfull(void)
{
    short p[4];
    vsf_color(bh, col(tog++));
    p[0] = ax; p[1] = ay; p[2] = ax + aw - 1; p[3] = ay + ah - 1;
    vr_recfl(bh, p);
}

static void s_bar(void)    { s_colour(); vsf_interior(bh, FIS_HATCH); vsf_style(bh, 3); vsf_perimeter(bh, 1); }
static void b_bar(void)
{
    short p[4], k = NX();
    p[0] = pts[k][0]; p[1] = pts[k][1]; p[2] = p[0] + 47; p[3] = p[1] + 31;
    v_bar(bh, p);
}
static void s_pattern(void) { s_colour(); vsf_interior(bh, FIS_PATTERN); vsf_style(bh, 9); vsf_perimeter(bh, 0); }

static short udpat[16] = {
    (short)0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180,
    0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, (short)0x8001
};
static void s_udpat(void) { s_colour(); vsf_udpat(bh, udpat, 1); vsf_interior(bh, FIS_USER); vsf_perimeter(bh, 0); }
static void s_xorfill(void) { vswr_mode(bh, MD_XOR); vsf_perimeter(bh, 0); }

static void b_polygon(void)
{
    short p[16], i;
    short cx = pts[pi][0], cy = pts[pi][1];
    pi++;
    for (i = 0; i < 8; i++) {
        short k = NX();
        p[i * 2]     = cx + (rad[k] & 63) - 32;
        p[i * 2 + 1] = cy + (rad[(unsigned char)(k + 1)] & 63) - 32;
    }
    v_fillarea(bh, 8, p);
}

static void s_solid(void) { s_colour(); vsf_interior(bh, FIS_SOLID); vsf_perimeter(bh, 1); }

static void b_circle(void)  { short k = NX(); v_circle(bh, pts[k][0], pts[k][1], rad[k]); }
static void b_ellipse(void) { short k = NX(); v_ellipse(bh, pts[k][0], pts[k][1], rad[k], rad[(unsigned char)(k + 7)]); }
static void b_arc(void)     { short k = NX(); v_arc(bh, pts[k][0], pts[k][1], rad[k], 0, 2700); }
static void b_pie(void)     { short k = NX(); v_pieslice(bh, pts[k][0], pts[k][1], rad[k], 300, 3300); }
static void b_ellarc(void)  { short k = NX(); v_ellarc(bh, pts[k][0], pts[k][1], rad[k], rad[(unsigned char)(k + 3)], 450, 3150); }
static void b_ellpie(void)  { short k = NX(); v_ellpie(bh, pts[k][0], pts[k][1], rad[k], rad[(unsigned char)(k + 3)], 900, 3600); }
static void b_rbox(void)
{
    short p[4], k = NX();
    p[0] = pts[k][0]; p[1] = pts[k][1]; p[2] = p[0] + 15 + rad[k]; p[3] = p[1] + 11 + rad[k] / 2;
    v_rbox(bh, p);
}
static void b_rfbox(void)
{
    short p[4], k = NX();
    p[0] = pts[k][0]; p[1] = pts[k][1]; p[2] = p[0] + 15 + rad[k]; p[3] = p[1] + 11 + rad[k] / 2;
    v_rfbox(bh, p);
}

/* contour fill: clear an outlined 64x40 box and flood it */
static short cf_x, cf_y;
static void s_contour(void)
{
    short p[10];
    cf_x = ax + aw / 2 - 32; cf_y = ay + ah / 2 - 20;
    p[0] = cf_x - 1;  p[1] = cf_y - 1;
    p[2] = cf_x + 64; p[3] = cf_y - 1;
    p[4] = cf_x + 64; p[5] = cf_y + 40;
    p[6] = cf_x - 1;  p[7] = cf_y + 40;
    p[8] = cf_x - 1;  p[9] = cf_y - 1;
    vsl_color(bh, 1);
    v_pline(bh, 5, p);
    vsf_interior(bh, FIS_SOLID);
    vsf_perimeter(bh, 0);
}
static void b_contour(void)
{
    short p[4];
    p[0] = cf_x; p[1] = cf_y; p[2] = cf_x + 63; p[3] = cf_y + 39;
    vsf_color(bh, 0);
    vr_recfl(bh, p);
    vsf_color(bh, ncolors > 2 ? col(2 + (tog++ & 3)) : 1);
    v_contourfill(bh, cf_x + 32, cf_y + 20, -1);
}

static const char *txt_short = "Fbench VDI 0123";
static const char *txt_long  = "The quick brown fox jumps over the lazy dog. 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ!!";

static void b_text(void)     { short k = NX(); v_gtext(bh, pts[k][0], pts[k][1], txt_short); }
static void b_textlong(void) { short k = NX(); v_gtext(bh, ax, pts[k][1], txt_long); }
static void s_texteff(void)  { vst_color(bh, col(4)); vst_effects(bh, TXT_THICKENED | TXT_SKEWED | TXT_UNDERLINED | TXT_OUTLINED); }
static short a_rotate(void)  { return ext_out[8] > 0; }
static void s_rotate(void)   { vst_color(bh, col(5)); vst_rotation(bh, 900); }
static void b_justified(void)
{
    short k = NX();
    short len = aw - 8 > 260 ? 260 : aw - 8;
    v_justified(bh, ax + 4, pts[k][1], "Justified text in Fbench", len, 1, 1);
}
static void b_extent(void) { short e[8]; vqt_extent(bh, txt_long, e); }
static void b_attrs(void)  { vsl_color(bh, 1); vsf_color(bh, 1); vst_color(bh, 1); }

/* raster operations */
static void b_cpy64(void)
{
    short p[8], a = NX(), b = NX();
    p[0] = rpts[a][0]; p[1] = rpts[a][1]; p[2] = p[0] + 63; p[3] = p[1] + 63;
    p[4] = rpts[b][0]; p[5] = rpts[b][1]; p[6] = p[4] + 63; p[7] = p[5] + 63;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
}

static void s_scroll(void)
{
    short i;
    for (i = 0; i < ah; i += 16) {                     /* stripes to move */
        short p[4];
        vsf_color(bh, col(i / 16));
        p[0] = ax; p[1] = ay + i; p[2] = ax + aw - 1; p[3] = ay + i + 7;
        if (p[3] > ay + ah - 1) p[3] = ay + ah - 1;   /* stay in the area */
        vr_recfl(bh, p);
    }
}
static void b_scroll(void)
{
    short p[8];
    p[0] = ax; p[1] = ay + 8; p[2] = ax + aw - 1; p[3] = ay + ah - 1;
    p[4] = ax; p[5] = ay;     p[6] = ax + aw - 1; p[7] = ay + ah - 9;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
}

static MFDB  off_mfdb, std_mfdb, mono_mfdb;
static void *off_buf, *std_buf;
static short mono_buf[64 * 4];               /* 64x64, 1 plane */

static void s_offscreen(void)
{
    long sz = 64L * 64L / 8L * planes;
    short p[8];
    off_buf = malloc(sz);
    if (!off_buf) { test_fail = 1; strcpy(cur_note, "out of memory"); return; }
    memset(off_buf, 0, sz);
    off_mfdb.fd_addr = off_buf;
    off_mfdb.fd_w = 64; off_mfdb.fd_h = 64; off_mfdb.fd_wdwidth = 4;
    off_mfdb.fd_stand = 0; off_mfdb.fd_nplanes = planes;
    off_mfdb.fd_r1 = off_mfdb.fd_r2 = off_mfdb.fd_r3 = 0;
    /* paint something into it from the screen */
    s_solid();
    b_circle(); b_rfbox(); b_circle();
    p[0] = ax; p[1] = ay; p[2] = ax + 63; p[3] = ay + 63;
    p[4] = 0;  p[5] = 0;  p[6] = 63;      p[7] = 63;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &off_mfdb);
}
static void c_offscreen(void) { if (off_buf) free(off_buf); off_buf = NULL; }
static void b_mem2scr(void)
{
    short p[8], k = NX();
    p[0] = 0; p[1] = 0; p[2] = 63; p[3] = 63;
    p[4] = rpts[k][0]; p[5] = rpts[k][1]; p[6] = p[4] + 63; p[7] = p[5] + 63;
    vro_cpyfm(bh, S_ONLY, p, &off_mfdb, &scr_mfdb);
}
static void b_scr2mem(void)
{
    short p[8], k = NX();
    p[0] = rpts[k][0]; p[1] = rpts[k][1]; p[2] = p[0] + 63; p[3] = p[1] + 63;
    p[4] = 0; p[5] = 0; p[6] = 63; p[7] = 63;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &off_mfdb);
}

static void s_mono(void)
{
    short i;
    for (i = 0; i < 64 * 4; i++)
        mono_buf[i] = (i & 4) ? (short)0xF0F0 : 0x0F0F;
    mono_mfdb.fd_addr = mono_buf;
    mono_mfdb.fd_w = 64; mono_mfdb.fd_h = 64; mono_mfdb.fd_wdwidth = 4;
    mono_mfdb.fd_stand = 0; mono_mfdb.fd_nplanes = 1;
    mono_mfdb.fd_r1 = mono_mfdb.fd_r2 = mono_mfdb.fd_r3 = 0;
}
static void b_vrt(void)
{
    short p[8], c[2], k = NX();
    c[0] = col(k); c[1] = 0;
    p[0] = 0; p[1] = 0; p[2] = 63; p[3] = 63;
    p[4] = rpts[k][0]; p[5] = rpts[k][1]; p[6] = p[4] + 63; p[7] = p[5] + 63;
    vrt_cpyfm(bh, MD_TRANS, p, &mono_mfdb, &scr_mfdb, c);
}

static short a_trnfm(void) { return planes <= 8; }
static void s_trnfm(void)
{
    long sz = 64L * 64L / 8L * planes;
    s_offscreen();
    if (test_fail) return;
    std_buf = malloc(sz);
    if (!std_buf) { test_fail = 1; strcpy(cur_note, "out of memory"); return; }
    memset(std_buf, 0x5A, sz);
    std_mfdb = off_mfdb;
    std_mfdb.fd_addr = std_buf;
    std_mfdb.fd_stand = 1;
}
static void c_trnfm(void) { c_offscreen(); if (std_buf) free(std_buf); std_buf = NULL; }
static void b_trnfm(void) { vr_trnfm(bh, &std_mfdb, &off_mfdb); }

static void b_getpix(void) { short pel, idx, k = NX(); v_get_pixel(bh, pts[k][0], pts[k][1], &pel, &idx); }
static void b_mouse(void)  { v_hide_c(bh); v_show_c(bh, 0); }

static void b_vwk(void)
{
    short out[57], h = open_vwk(out);
    if (h <= 0) { test_fail = 1; strcpy(cur_note, "v_opnvwk failed"); return; }
    v_clsvwk(h);
}

/* ------------------------------------------------------------------ */
/*  AES tests                                                         */
/* ------------------------------------------------------------------ */

static short aux_h;
static short aux_isopen;    /* TOS 1.x/2.x corrupt their window list if a
                               window that isn't open is closed: track it */
static GRECT aux_r;

static void aux_close(void)
{
    if (aux_h > 0 && aux_isopen) {
        wind_close(aux_h);
        aux_isopen = 0;
        drain_events();
    }
}
static short aux_align;     /* 0 = anywhere, 1 = work x on 16 px, 2 = work x on 16 px + 5,
                               3 = full screen (whole desktop) */
static short move_dx = 16;  /* horizontal step of the move test */

#define AUX_KIND (NAME | MOVER | SIZER | INFO | CLOSER | UPARROW | DNARROW | VSLIDE)
#define AUX_BARS (AUX_KIND | LFARROW | RTARROW | HSLIDE)
#define AUX_NOBARS (NAME | MOVER | INFO | CLOSER)
static short aux_kind = AUX_KIND;  /* kind of the next aux window */

static void aux_create(short open)
{
    GRECT r;
    r.g_x = ax + aw / 8; r.g_y = ay + ah / 8;
    r.g_w = aw / 2;      r.g_h = ah / 2;
    if (r.g_w < 10 * box_w) r.g_w = 10 * box_w;
    if (r.g_h < 6 * box_h)  r.g_h = 6 * box_h;
    if (aux_align == 3)
        r = desk;
    else {
        /* place the WORK area: left edge on a 16 pixel (word) boundary
           (also the default, so every machine gets the same alignment),
           or deliberately 5 pixels past one; width a multiple of 16 */
        GRECT w = r;
        w.g_x = ((w.g_x + 15) & ~15) + (aux_align == 2 ? 5 : 0);
        w.g_w &= ~15;
        wind_calc_grect(WC_BORDER, aux_kind, &w, &r);
    }
    aux_r = r;
    fb_step("aux: wind_create");
    aux_h = wind_create_grect(aux_kind, &desk);
    if (aux_h <= 0) { aux_h = 0; test_fail = 1; strcpy(cur_note, "no window handles"); return; }
    win_register(WN_AUX, aux_h, " Fbench AES ");
    wins[WN_AUX].kind = aux_kind;
    wind_set_str(aux_h, WF_NAME, " Fbench AES ");
    wind_set_str(aux_h, WF_INFO, " window test ");
    aux_isopen = 0;
    if (open) {
        fb_step("aux: wind_open");
        aux_isopen = wind_open_grect(aux_h, &aux_r) != 0;
        drain_events();
        if (aux_align == 1 || aux_align == 2) {   /* what the AES actually gave us */
            GRECT w;
            wind_get_grect(aux_h, WF_WORKXYWH, &w);
            sprintf(cur_note, "work x mod 16 = %d", w.g_x & 15);
        }
    }
}
static void aux_open(void)   { aux_create(1); }
static void aux_closed(void) { aux_create(0); }
static void aux_destroy(void)
{
    if (aux_h > 0) {
        aux_close();             /* only if open: see aux_isopen */
        wind_delete(aux_h);
    }
    aux_h = 0;
    wins[WN_AUX].h = 0;
    aux_align = 0;
    aux_kind = AUX_KIND;
    move_dx = 16;
    drain_events();
}

static void b_wcreate(void)
{
    short h = wind_create_grect(NAME | MOVER | CLOSER, &desk);
    if (h <= 0) { test_fail = 1; strcpy(cur_note, "no window handles"); return; }
    wind_delete(h);
}
static void b_wopen(void)
{
    fb_step("wind_open");
    if (!wind_open_grect(aux_h, &aux_r)) { test_fail = 1; strcpy(cur_note, "wind_open failed"); return; }
    aux_isopen = 1;
    drain_events();
    fb_step("wind_close");
    aux_close();
    fb_step("open+close done");
}
static void b_wmove(void)
{
    GRECT r = aux_r;
    if (tog++ & 1) { r.g_x += move_dx; r.g_y += 8; }
    wind_set_grect(aux_h, WF_CURRXYWH, &r);
    drain_events();
}
static void b_wsize(void)
{
    GRECT r = aux_r;
    if (tog++ & 1) { r.g_w += 32; r.g_h += 16; }
    wind_set_grect(aux_h, WF_CURRXYWH, &r);
    drain_events();
}
static void b_wname(void)  { wind_set_str(aux_h, WF_NAME, (tog++ & 1) ? " Fbench AES " : " Window title "); }
static void b_winfo(void)  { wind_set_str(aux_h, WF_INFO, (tog++ & 1) ? " info line A " : " info line B "); }
static void b_wslide(void)
{
    tog = (tog + 125) % 1000;
    wind_set(aux_h, WF_VSLIDE, tog, 0, 0, 0);
    wind_set(aux_h, WF_VSLSIZE, 100 + tog / 2, 0, 0, 0);
}
static void b_wtop(void)
{
    wind_set((tog++ & 1) ? aux_h : wins[WN_TEST].h, WF_TOP, 0, 0, 0, 0);
    drain_events();
}
static void b_wget(void)  { GRECT r; wind_get_grect(wins[WN_TEST].h, WF_WORKXYWH, &r); }
static void b_wrect(void)
{
    GRECT r;
    short h = wins[WN_RESULTS].h > 0 ? wins[WN_RESULTS].h : wins[WN_TEST].h, n = 0;
    wind_get_grect(h, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h && ++n < 100)
        wind_get_grect(h, WF_NEXTXYWH, &r);
}
static void b_wcalc(void)  { GRECT o; wind_calc_grect(WC_WORK, NAME | MOVER | SIZER | INFO | VSLIDE | HSLIDE, &tw_area, &o); }
static void b_wupd(void)   { wind_update(BEG_UPDATE); wind_update(END_UPDATE); }

/* scroll the content of the test window by one text line, AES style:
   walk the rectangle list, blit, then draw the new line */
static void s_wscroll(void) { fb_update(1); if (!mouse_vis) graf_mouse(M_OFF, NULL); s_scroll(); }
static void c_wscroll(void) { if (!mouse_vis) graf_mouse(M_ON, NULL); fb_update(0); }
static void scroll_win(short wh)
{
    GRECT r, work;
    short p[8], line = char_h, n = 0;
    char buf[40];

    wind_get_grect(wh, WF_WORKXYWH, &work);
    wind_get_grect(wh, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h && ++n < 100) {      /* cap: a broken list can't hang us */
        if (rc_intersect(&work, &r) && r.g_h > line) {
            p[0] = r.g_x; p[1] = r.g_y + line; p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
            p[4] = r.g_x; p[5] = r.g_y;        p[6] = r.g_x + r.g_w - 1; p[7] = r.g_y + r.g_h - 1 - line;
            vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
            p[0] = r.g_x; p[1] = r.g_y + r.g_h - line; p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
            vsf_color(bh, 0);
            vr_recfl(bh, p);
        }
        wind_get_grect(wh, WF_NEXTXYWH, &r);
    }
    sprintf(buf, "Scrolled line %d", tog++);
    v_gtext(bh, work.g_x + 4, work.g_y + work.g_h - line, buf);
    /* move the slider too, as a real program would - but only on a window
       that has one: the test window (NAME|INFO|CLOSER) doesn't, and some
       AESes hang setting a slider the window doesn't have */
    {
        WIN *w = win_find(wh);
        if (w && (w->kind & VSLIDE))
            wind_set(wh, WF_VSLIDE, (tog * 37) % 1000, 0, 0, 0);
    }
}
static void b_wscroll(void)   { scroll_win(wins[WN_TEST].h); }

/* ---- alignment tests: same operation on / off the 16 pixel grid ---- */

/* mode 1: x on 16 px; 2: x on 16 px + 3 / + 11 (alternating, so source and
   destination of a copy need different shifts); 3: x on 8 px; 4: 8 px + 3 */
static short align_x(short x, short i, short mode)
{
    switch (mode) {
    case 1: x &= ~15; break;
    case 2: x = (x & ~15) + ((i & 1) ? 11 : 3); break;
    case 3: x &= ~7; break;
    case 4: x = (x & ~7) + 3; break;
    }
    if (x < ax) x += 16;
    return x;
}
/* shifts both the drawing points and the raster-copy points; rpts keeps
   16 px spare at the right, which covers the largest shift (+11) */
static void align_pts(short mode)
{
    short i;
    for (i = 0; i < 256; i++) {
        pts[i][0]  = align_x(pts[i][0], i, mode);
        rpts[i][0] = align_x(rpts[i][0], i, mode);
    }
}
static void s_rect_a(void) { s_colour(); align_pts(1); }
static void s_rect_u(void) { s_colour(); align_pts(2); }
static void b_rect64(void)
{
    short p[4], k = NX();
    p[0] = pts[k][0]; p[1] = pts[k][1]; p[2] = p[0] + 63; p[3] = p[1] + 63;
    vr_recfl(bh, p);
}
static void s_cpy_a(void)  { align_pts(1); }
static void s_cpy_u(void)  { align_pts(2); }
static void s_m2s_a(void)  { s_offscreen(); align_pts(1); }
static void s_m2s_u(void)  { s_offscreen(); align_pts(2); }
static void s_vrt_a(void)  { s_mono(); align_pts(1); }
static void s_vrt_u(void)  { s_mono(); align_pts(2); }
static void s_txt_a(void)  { align_pts(3); }
static void s_txt_u(void)  { align_pts(4); }

/* vertical scroll of a region whose left/right edges are aligned or not */
static short sc_x0, sc_w;
static void s_vsc_a(void) { s_scroll(); sc_x0 = (ax + 15) & ~15;       sc_w = (aw - 32) & ~15; }
static void s_vsc_u(void) { s_scroll(); sc_x0 = ((ax + 15) & ~15) + 5; sc_w = (aw - 32) & ~15; }
static void b_vsc(void)
{
    short p[8];
    p[0] = sc_x0; p[1] = ay + 8; p[2] = sc_x0 + sc_w - 1; p[3] = ay + ah - 1;
    p[4] = sc_x0; p[5] = ay;     p[6] = sc_x0 + sc_w - 1; p[7] = ay + ah - 9;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
}
/* horizontal scroll: by 16 px (no bit shift) or by 3 px (every word shifted) */
static short hsc_dx;
static void s_hsc_a(void) { s_vsc_a(); hsc_dx = 16; }
static void s_hsc_u(void) { s_vsc_a(); hsc_dx = 3; }
static void b_hsc(void)
{
    short p[8];
    p[0] = sc_x0 + hsc_dx; p[1] = ay; p[2] = sc_x0 + sc_w - 1;          p[3] = ay + ah - 1;
    p[4] = sc_x0;          p[5] = ay; p[6] = sc_x0 + sc_w - 1 - hsc_dx; p[7] = ay + ah - 1;
    vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
}

/* AES windows whose work area is / isn't on the 16 pixel grid */
static void s_amove_a(void) { aux_align = 1; move_dx = 16; aux_open(); }
static void s_amove_u(void) { aux_align = 2; move_dx = 19; aux_open(); }   /* +5 and +24: both off-grid */
static void s_aopen_a(void) { aux_align = 1; aux_create(1); aux_close(); }
static void s_aopen_u(void) { aux_align = 2; aux_create(1); aux_close(); }
static void s_ascr_a(void)  { aux_align = 1; aux_open(); if (!mouse_vis) graf_mouse(M_OFF, NULL); }
static void s_ascr_u(void)  { aux_align = 2; aux_open(); if (!mouse_vis) graf_mouse(M_OFF, NULL); }
static void c_ascr(void)    { if (!mouse_vis) graf_mouse(M_ON, NULL); aux_destroy(); }
static void b_ascroll(void) { scroll_win(aux_h); }
/* AES.SCRSL: a window with arrows and a vertical slider, scrolled a line
   at a time with the slider following, like a text viewer */
/* ---- scrolling a window right, left and diagonally ----
   param: direction 1 right, 2 left, 3 down-right, 4 up-left (the way the
   view moves, as with the window's arrows: "right" moves the content
   left); + 8 = window with both sliders (moved every step), else none;
   + 16 = work x on the 16 px grid, step 16 px; + 32 = work x on the grid
   + 5, step 3 px. Vertical steps are one text line. */
static short wd_dx, wd_dy;
static void wd_fill(void)                /* stripes and bars to move about */
{
    GRECT r, work;
    short p[4], i;
    wind_get_grect(aux_h, WF_WORKXYWH, &work);
    wind_get_grect(aux_h, WF_FIRSTXYWH, &r);
    vsf_interior(bh, FIS_SOLID);
    for (i = 0; r.g_w && r.g_h && i < 100; i++) {
        if (rc_intersect(&work, &r) && rc_intersect(&desk, &r)) {
            short k;
            p[0] = r.g_x; p[1] = r.g_y; p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
            vs_clip(bh, 1, p);
            vsf_color(bh, 0);
            vr_recfl(bh, p);
            for (k = 0; k < work.g_h; k += 16) {
                vsf_color(bh, col(k / 16));
                p[0] = work.g_x; p[1] = work.g_y + k; p[2] = work.g_x + work.g_w - 1; p[3] = p[1] + 5;
                vr_recfl(bh, p);
            }
            for (k = 0; k < work.g_w; k += 24) {
                vsf_color(bh, col(k / 24 + 3));
                p[0] = work.g_x + k; p[1] = work.g_y; p[2] = p[0] + 3; p[3] = work.g_y + work.g_h - 1;
                vr_recfl(bh, p);
            }
        }
        wind_get_grect(aux_h, WF_NEXTXYWH, &r);
    }
    p[0] = tw_area.g_x; p[1] = tw_area.g_y;
    p[2] = tw_area.g_x + tw_area.g_w - 1; p[3] = tw_area.g_y + tw_area.g_h - 1;
    vs_clip(bh, 1, p);
}
static void s_wdir(void)
{
    short dir = cur_param & 7, al = cur_param >> 4, step = al == 2 ? 3 : 16;
    aux_kind = (cur_param & 8) ? AUX_BARS : AUX_NOBARS;
    aux_align = al;
    aux_open();
    if (!aux_h || test_fail) return;
    wd_dx = dir == 1 || dir == 3 ? step : -step;
    wd_dy = dir == 3 ? char_h : dir == 4 ? -char_h : 0;
    sprintf(cur_note + strlen(cur_note), ", step %d", step);
    wd_fill();
    if (!mouse_vis) graf_mouse(M_OFF, NULL);
}
/* scroll the visible parts of the window by dx, dy (either sign) */
static void scroll_dir(short wh, short dx, short dy)
{
    GRECT r, work;
    short p[8], adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, n = 0;
    char  c[2];

    wind_get_grect(wh, WF_WORKXYWH, &work);
    wind_get_grect(wh, WF_FIRSTXYWH, &r);
    vsf_interior(bh, FIS_SOLID);
    while (r.g_w && r.g_h && ++n < 100) {
        if (rc_intersect(&work, &r) && rc_intersect(&desk, &r)) {
            short x0 = r.g_x, y0 = r.g_y, x1 = r.g_x + r.g_w - 1, y1 = r.g_y + r.g_h - 1;
            p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
            vs_clip(bh, 1, p);
            if (r.g_w > adx && r.g_h > ady) {           /* move what stays visible */
                p[0] = dx > 0 ? x0 + adx : x0; p[2] = dx > 0 ? x1 : x1 - adx;
                p[4] = dx > 0 ? x0 : x0 + adx; p[6] = dx > 0 ? x1 - adx : x1;
                p[1] = dy > 0 ? y0 + ady : y0; p[3] = dy > 0 ? y1 : y1 - ady;
                p[5] = dy > 0 ? y0 : y0 + ady; p[7] = dy > 0 ? y1 - ady : y1;
                vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
            }
            if (adx) {                                  /* new column */
                p[0] = dx > 0 ? x1 - adx + 1 : x0; p[2] = dx > 0 ? x1 : x0 + adx - 1;
                if (p[0] < x0) p[0] = x0;
                if (p[2] > x1) p[2] = x1;
                p[1] = y0; p[3] = y1;
                vsf_color(bh, 0);
                vr_recfl(bh, p);
                p[1] = work.g_y + (tog & 7) * 4; p[3] = work.g_y + work.g_h - 1 - (tog & 3) * 8;
                vsf_color(bh, col(tog));
                vr_recfl(bh, p);
            }
            if (ady) {                                  /* new line */
                p[1] = dy > 0 ? y1 - ady + 1 : y0; p[3] = dy > 0 ? y1 : y0 + ady - 1;
                if (p[1] < y0) p[1] = y0;
                if (p[3] > y1) p[3] = y1;
                p[0] = x0; p[2] = x1;
                vsf_color(bh, 0);
                vr_recfl(bh, p);
                c[0] = '0' + tog % 10; c[1] = 0;
                v_gtext(bh, work.g_x + 4, dy > 0 ? work.g_y + work.g_h - ady : work.g_y, c);
            }
        }
        wind_get_grect(wh, WF_NEXTXYWH, &r);
    }
    p[0] = tw_area.g_x; p[1] = tw_area.g_y;
    p[2] = tw_area.g_x + tw_area.g_w - 1; p[3] = tw_area.g_y + tw_area.g_h - 1;
    vs_clip(bh, 1, p);
    tog++;
    if (wins[WN_AUX].kind & HSLIDE) wind_set(wh, WF_HSLIDE, (tog * 37) % 1000, 0, 0, 0);
    if (dy && (wins[WN_AUX].kind & VSLIDE)) wind_set(wh, WF_VSLIDE, (tog * 53) % 1000, 0, 0, 0);
}
static void b_wdir(void) { scroll_dir(aux_h, wd_dx, wd_dy); }

static void s_wscrsl(void)  { aux_align = 0; aux_open(); if (!mouse_vis) graf_mouse(M_OFF, NULL); }

/* ---- generic rectangle-list scroller ----
   Scrolls the visible parts of a window's work area left by dx or up by dy,
   clears the exposed strip and lets 'fresh' draw new content into it,
   clipped to each visible rectangle (correct even when partly covered). */
typedef void (*FRESHFN)(const GRECT *work, const GRECT *strip);

static void scroll_rects(short wh, short dx, short dy, FRESHFN fresh)
{
    GRECT r, work, strip;
    short p[8];

    wind_get_grect(wh, WF_WORKXYWH, &work);
    if (dx >= work.g_w || dy >= work.g_h) return;
    strip = work;
    if (dy) { strip.g_y = work.g_y + work.g_h - dy; strip.g_h = dy; }
    else    { strip.g_x = work.g_x + work.g_w - dx; strip.g_w = dx; }

    wind_get_grect(wh, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h) {
        if (rc_intersect(&work, &r)) {
            GRECT s = strip;
            p[0] = r.g_x; p[1] = r.g_y; p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
            vs_clip(bh, 1, p);
            if (r.g_w > dx && r.g_h > dy) {
                p[0] = r.g_x + dx; p[1] = r.g_y + dy;
                p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
                p[4] = r.g_x; p[5] = r.g_y;
                p[6] = r.g_x + r.g_w - 1 - dx; p[7] = r.g_y + r.g_h - 1 - dy;
                vro_cpyfm(bh, S_ONLY, p, &scr_mfdb, &scr_mfdb);
            }
            /* the part of this rectangle that is now stale */
            if (dy) { s.g_y = r.g_y + r.g_h - dy; s.g_h = dy; s.g_x = r.g_x; s.g_w = r.g_w; }
            else    { s.g_x = r.g_x + r.g_w - dx; s.g_w = dx; s.g_y = r.g_y; s.g_h = r.g_h; }
            p[0] = s.g_x; p[1] = s.g_y; p[2] = s.g_x + s.g_w - 1; p[3] = s.g_y + s.g_h - 1;
            vsf_interior(bh, FIS_SOLID);
            vsf_color(bh, 0);
            vr_recfl(bh, p);
            if (fresh) fresh(&work, &strip);
        }
        wind_get_grect(wh, WF_NEXTXYWH, &r);
    }
}

/* new content for a horizontal scroll: a coloured column with a digit */
static void fresh_column(const GRECT *work, const GRECT *strip)
{
    short p[4];
    char c[2];
    p[0] = strip->g_x; p[1] = work->g_y + (tog & 7) * 4;
    p[2] = strip->g_x + strip->g_w - 1; p[3] = work->g_y + work->g_h - 1 - (tog & 3) * 8;
    vsf_color(bh, col(tog));
    vr_recfl(bh, p);
    c[0] = '0' + (tog % 10); c[1] = 0;
    v_gtext(bh, strip->g_x, work->g_y + 2, c);
}

/* new content for a vertical scroll in the current font/size */
static void fresh_line(const GRECT *work, const GRECT *strip)
{
    char buf[48];
    sprintf(buf, "Fbench line %d - The quick brown fox", tog);
    v_gtext(bh, work->g_x + 4, strip->g_y, buf);
}

/* ---- full-screen windows ---- */
static void s_full(void)
{
    aux_align = 3;
    aux_open();
    if (aux_h) {
        GRECT w;
        wind_get_grect(aux_h, WF_WORKXYWH, &w);
        sprintf(cur_note, "work %dx%d", w.g_w, w.g_h);
    }
}
static void s_full_closed(void) { s_full(); aux_close(); }
static void s_full_toggle(void) { aux_open(); }               /* normal size first */
static void b_full_toggle(void)
{
    wind_set_grect(aux_h, WF_CURRXYWH, (tog++ & 1) ? &aux_r : &desk);
    drain_events();
}
static void b_full_fill(void)
{
    GRECT r, work;
    short p[4];
    wind_get_grect(aux_h, WF_WORKXYWH, &work);
    vsf_interior(bh, FIS_SOLID);
    vsf_color(bh, col(tog++));
    wind_get_grect(aux_h, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h) {
        if (rc_intersect(&work, &r)) {
            p[0] = r.g_x; p[1] = r.g_y; p[2] = r.g_x + r.g_w - 1; p[3] = r.g_y + r.g_h - 1;
            vs_clip(bh, 1, p);
            vr_recfl(bh, p);
        }
        wind_get_grect(aux_h, WF_NEXTXYWH, &r);
    }
}
static void b_full_vscr(void)  { tog++; scroll_rects(aux_h, 0, char_h, fresh_line); }
static void b_full_hscr8(void) { tog++; scroll_rects(aux_h, 8, 0, fresh_column); }
static void b_full_hscr16(void){ tog++; scroll_rects(aux_h, 16, 0, fresh_column); }
static void b_full_hscr3(void) { tog++; scroll_rects(aux_h, 3, 0, fresh_column); }

/* ---- scrolling text where each new line uses a different GDOS size ---- */
static const short mix_sizes[] = { 10, 18, 12, 36, 8, 24, 14, 48 };
static void b_gdos_mixscroll_win(short wh)
{
    short cw, chh, cellw, cellh;
    vst_point(bh, mix_sizes[tog & 7], &cw, &chh, &cellw, &cellh);
    tog++;
    scroll_rects(wh, 0, cellh > 0 ? cellh : char_h, fresh_line);
}
static void s_mixscroll(void)
{
    vst_font(bh, nfonts ? fonts[gfont].id : 1);
    vst_color(bh, col(4));
    if (!gdos_loaded) strcpy(cur_note, "system font");
}
static void b_mixscroll(void)    { b_gdos_mixscroll_win(wins[WN_TEST].h); }
static void s_full_mix(void)     { s_full(); s_mixscroll(); }

/* full-screen scroll with one fixed GDOS size: each step scrolls by that
   size's cell height and draws a new line in it */
static short fsz_cellh;
static void s_full_size(void)
{
    short cw, chh, cellw, got;
    s_full();
    s_mixscroll();
    got = vst_point(bh, cur_param, &cw, &chh, &cellw, &fsz_cellh);
    if (fsz_cellh <= 0) fsz_cellh = char_h;
    if (got != cur_param) sprintf(cur_note, "got %dpt%s", got, gdos_loaded ? "" : ", system font");
}
static void b_full_size(void) { tog++; scroll_rects(aux_h, 0, fsz_cellh, fresh_line); }
static void b_full_mix(void)     { b_gdos_mixscroll_win(aux_h); }

/* object tree for objc_draw / objc_find */
static OBJECT otree[6];
static TEDINFO oted;
static char ot_txt[] = "Fbench objc";
static void s_objc(void)
{
    short cw = char_w, chh = char_h, i;
    memset(otree, 0, sizeof(otree));
    for (i = 0; i < 6; i++) otree[i].ob_next = otree[i].ob_head = otree[i].ob_tail = -1;
    otree[0].ob_type = G_BOX;
    otree[0].ob_spec.index = ncolors >= 16 ? 0x00FF1173L : 0x00FF1121L;
    otree[0].ob_x = ax + 8; otree[0].ob_y = ay + 8;
    otree[0].ob_width = 24 * cw; otree[0].ob_height = 8 * chh;
    otree[0].ob_head = 1; otree[0].ob_tail = 5;

    memset(&oted, 0, sizeof(oted));
    oted.te_ptext = ot_txt; oted.te_ptmplt = ""; oted.te_pvalid = "";
    oted.te_font = 3; oted.te_just = 2; oted.te_color = 0x1180; oted.te_thickness = 1;
    oted.te_txtlen = sizeof(ot_txt); oted.te_tmplen = 1;
    otree[1].ob_type = G_BOXTEXT; otree[1].ob_spec.tedinfo = &oted;
    otree[1].ob_x = cw; otree[1].ob_y = chh / 2; otree[1].ob_width = 22 * cw; otree[1].ob_height = chh + 4;
    otree[1].ob_state = OS_OUTLINED; otree[1].ob_next = 2;

    otree[2].ob_type = G_STRING; otree[2].ob_spec.free_string = "objc_draw benchmark";
    otree[2].ob_x = cw; otree[2].ob_y = 2 * chh + 4; otree[2].ob_width = 20 * cw; otree[2].ob_height = chh;
    otree[2].ob_next = 3;

    otree[3].ob_type = G_BUTTON; otree[3].ob_spec.free_string = "OK";
    otree[3].ob_flags = OF_SELECTABLE | OF_DEFAULT | OF_EXIT;
    otree[3].ob_x = 2 * cw; otree[3].ob_y = 5 * chh; otree[3].ob_width = 8 * cw; otree[3].ob_height = chh + 2;
    otree[3].ob_next = 4;

    otree[4].ob_type = G_BUTTON; otree[4].ob_spec.free_string = "Cancel";
    otree[4].ob_flags = OF_SELECTABLE | OF_EXIT;
    otree[4].ob_x = 13 * cw; otree[4].ob_y = 5 * chh; otree[4].ob_width = 8 * cw; otree[4].ob_height = chh + 2;
    otree[4].ob_next = 5;

    otree[5].ob_type = G_BOXCHAR; otree[5].ob_spec.index = 0x46FF1100L;  /* 'F' */
    otree[5].ob_x = 21 * cw; otree[5].ob_y = 2 * chh + 4; otree[5].ob_width = 2 * cw; otree[5].ob_height = chh;
    otree[5].ob_next = 0; otree[5].ob_flags = OF_LASTOB;
    fb_update(1);
    if (!mouse_vis) graf_mouse(M_OFF, NULL);
}
static void c_objc(void) { if (!mouse_vis) graf_mouse(M_ON, NULL); fb_update(0); }
static void b_objc(void)
{
    otree[3].ob_state = (tog++ & 1) ? OS_SELECTED : 0;
    objc_draw_grect(otree, 0, 8, &tw_area);
}
static void b_ofind(void)
{
    short k = NX();
    objc_find(otree, 0, 8, pts[k][0], pts[k][1]);
}

/* ---- bigger dialogs: medium (~20 objects), complex (~45), very complex
   (~80, nested 4 deep). Built at run time in character units, so the same
   dialog is drawn on every machine; the row pitch shrinks to fit the test
   window (ST low / medium). Objects that still don't fit are left out and
   the note gives the real count. param: level 1..3, + 16 = x on the 16 px
   grid, + 32 = x on the grid + 3 */
#ifndef OS_CROSSED                /* older / PC GEM headers */
# define OS_CROSSED  0x0002
#endif
#ifndef OS_CHECKED
# define OS_CHECKED  0x0004
#endif
#ifndef OF_RBUTTON
# define OF_RBUTTON  0x0010
#endif
#define DLG_MAX  256
#define DLG_COLS 34
static OBJECT  dtree[DLG_MAX];
static TEDINFO dted[80];
static BITBLK  dbit;
static short   dabs_x[DLG_MAX], dabs_y[DLG_MAX];
static short   dn, dtn, d_w, d_h, d_rp, d_oh, d_ok;
static short   dimg[16] = {                          /* 16x16 disk icon */
    0x7FFE, 0x4012, 0x4012, 0x4012, 0x4012, 0x401E, 0x4002, 0x4002,
    0x5FFA, 0x500A, 0x500A, 0x57CA, 0x500A, 0x57CA, 0x500A, 0x7FFE };

/* add an object at x, y (pixels from the dialog's top left); -1 = left out */
static short d_add(short par, short type, short x, short y, short w, short h)
{
    OBJECT *o;
    if (dn >= DLG_MAX || (dn && par < 0)) return -1;
    if (dn && (x < 0 || y < 0 || x + w > d_w || y + h > d_h)) return -1;
    o = &dtree[dn];
    memset(o, 0, sizeof(*o));
    o->ob_next = o->ob_head = o->ob_tail = -1;
    o->ob_type = type;
    o->ob_x = dn ? x - dabs_x[par] : x;
    o->ob_y = dn ? y - dabs_y[par] : y;
    o->ob_width = w; o->ob_height = h;
    dabs_x[dn] = dn ? x : 0; dabs_y[dn] = dn ? y : 0;
    if (dn) {
        if (dtree[par].ob_head < 0) dtree[par].ob_head = dn;
        else dtree[dtree[par].ob_tail].ob_next = dn;
        dtree[par].ob_tail = dn;
        o->ob_next = par;
    }
    return dn++;
}
/* column / row to pixels */
#define DC(c)  ((c) * char_w)
#define DR(r)  ((r) * d_rp + 2)

static short d_box(short par, short c, short r, short w, short rows, long spec)
{
    short i = d_add(par, G_BOX, DC(c), DR(r), DC(w), rows * d_rp - (d_rp - d_oh));
    if (i >= 0) dtree[i].ob_spec.index = spec;
    return i;
}
static short d_str(short par, short c, short r, const char *s)
{
    short i = d_add(par, G_STRING, DC(c), DR(r), DC((short)strlen(s)), d_oh);
    if (i >= 0) dtree[i].ob_spec.free_string = (char *)s;
    return i;
}
static short d_btn(short par, short c, short r, short w, const char *s, short fl, short st)
{
    short i = d_add(par, G_BUTTON, DC(c), DR(r), DC(w), d_oh);
    if (i >= 0) {
        dtree[i].ob_spec.free_string = (char *)s;
        dtree[i].ob_flags = fl; dtree[i].ob_state = st;
    }
    return i;
}
static short d_ted_px(short par, short type, short x, short y, short w, short h,
                      const char *txt, const char *tmpl, const char *valid,
                      short font, short just, short st)
{
    short i;
    TEDINFO *t;
    if (dtn >= (short)(sizeof(dted) / sizeof(dted[0]))) return -1;
    i = d_add(par, type, x, y, w, h);
    if (i < 0) return -1;
    t = &dted[dtn++];
    memset(t, 0, sizeof(*t));
    t->te_ptext = (char *)txt; t->te_ptmplt = (char *)tmpl; t->te_pvalid = (char *)valid;
    t->te_font = font; t->te_just = just; t->te_color = 0x1180; t->te_thickness = -1;
    t->te_txtlen = (short)strlen(txt) + 1; t->te_tmplen = (short)strlen(tmpl) + 1;
    dtree[i].ob_spec.tedinfo = t;
    dtree[i].ob_state = st;
    return i;
}
static short d_ted(short par, short type, short c, short r, short w,
                   const char *txt, const char *tmpl, const char *valid, short st)
{
    return d_ted_px(par, type, DC(c), DR(r), DC(w), d_oh, txt, tmpl, valid, 3,
                    type == G_FTEXT || type == G_FBOXTEXT ? 0 : 2, st);
}

/* level 4, "pathological": ~140 objects. Three full-size layers drawn
   over each other, towers of boxes nested 6 levels below the root, every
   object state at once, 3 pixel borders, images at
   odd x offsets, overlapping objects, transparent fills and small text */
/* NOT deeper than 6: TOS's objc_draw (GEM's everyobj) walks the tree with
   x[8]/y[8] arrays on its stack indexed by level + 1, and visits children
   down to the depth asked for - level 7 or more writes past them and
   crashes TOS 1.x (STE, TOS 1.62) */
#define DLG_MAXLEVEL 6
static void d_patho(void)
{
    static const char *const pt[8] = { "Wi", "Fx", "Qz", "Ok", "Ab", "Dz", "Mu", "Xe" };
    short lh = DR(13) - 1, l1, l2, l3, c, r, n = 0, i, v, j;
    long  pat = ncolors >= 4 ? 0x00FF1122L : 0x00FF1121L;

    if (lh > d_h) lh = d_h;

    l1 = d_add(0, G_BOX, 0, 0, d_w, lh);
    if (l1 < 0) return;
    dtree[l1].ob_spec.index = pat;
    l2 = d_add(l1, G_BOX, 2, 2, d_w - 4, lh - 4);
    if (l2 < 0) return;
    dtree[l2].ob_spec.index = 0x00021131L;              /* 2 px, pattern 3 */
    l3 = d_add(l2, G_IBOX, 4, 4, d_w - 8, lh - 8);
    if (l3 < 0) return;
    dtree[l3].ob_spec.index = 0x00FD1100L;              /* 3 px outside */

    for (r = 0; r < 13; r++)
        for (c = 0; c < 8; c++, n++) {
            short x = DC(1 + 4 * c), y = DR(r);
            switch (n % 8) {
            case 0:                                     /* every state at once */
                i = d_ted_px(l3, G_BOXTEXT, x, y, DC(4), d_oh, pt[r & 7], "", "", 5, 2,
                             OS_SELECTED | OS_CROSSED | OS_CHECKED | OS_DISABLED |
                             OS_OUTLINED | OS_SHADOWED);
                if (i >= 0) dtree[i].ob_spec.tedinfo->te_thickness = -3;
                break;
            case 1:                                     /* image off the grid */
                i = d_add(l3, G_IMAGE, x + (n % 7), y, 16, 16);
                if (i >= 0) { dtree[i].ob_spec.bitblk = &dbit; dtree[i].ob_state = OS_SELECTED; }
                break;
            case 2:
                d_ted_px(l3, G_FTEXT, x, y, DC(4), d_oh, "12", "#___", "999", 5, 0, OS_CROSSED);
                break;
            case 3:
                d_btn(l3, 1 + 4 * c, r, 4, "Go", OF_SELECTABLE | OF_DEFAULT,
                      OS_SHADOWED | OS_OUTLINED | OS_CHECKED | OS_CROSSED);
                break;
            case 4:
                i = d_add(l3, G_BOXCHAR, x, y, DC(4), d_oh);
                if (i >= 0) {
                    dtree[i].ob_spec.index = ((long)('A' + r) << 24) | 0x00FD1171L;
                    dtree[i].ob_state = OS_SELECTED | OS_CROSSED;
                }
                break;
            case 5:                                     /* tower: down to level 6 */
                v = d_add(l3, G_BOX, x, y, DC(4), d_oh);
                for (j = 1; j < DLG_MAXLEVEL - 4 && v >= 0; j++) {   /* cell is level 4 */
                    dtree[v].ob_spec.index = 0x00FF1100L | ((j & 1) ? 0x41 : 0);
                    v = d_add(v, G_BOX, x + j, y + (j > 1), DC(4) - 2 * j, d_oh - 2 * (j > 1));
                }
                if (v >= 0) {
                    dtree[v].ob_spec.index = 0x00FF1100L;
                    d_ted_px(v, G_BOXTEXT, x + 4, y + 2, DC(4) - 8, d_oh - 4 > 1 ? d_oh - 4 : 1,
                             "6", "", "", 5, 2, OS_SELECTED);
                }
                break;
            case 6:                                     /* transparent, overlaps the next */
                i = d_add(l3, G_BOX, x, y, DC(6), d_oh);
                if (i >= 0) { dtree[i].ob_spec.index = 0x00FE1150L; dtree[i].ob_state = OS_SHADOWED; }
                break;
            case 7:
                i = d_str(l3, 1 + 4 * c, r, pt[c]);
                if (i >= 0) dtree[i].ob_state = OS_DISABLED | OS_SELECTED;
                break;
            }
        }
}

static void s_dialog(void)
{
    static const char *const tb = "\x01\x02\x03\x04+-*/";   /* toolbar chars */
    short lvl = cur_param & 15, mode = cur_param >> 4;
    short rows = lvl == 1 ? 7 : lvl == 2 ? 11 : 14, br = rows - 1;
    short x0 = ax + 8, y0 = ay + 8, cols, avail_h, i, j, p, k;
    long  boxsp = ncolors >= 16 ? 0x00FF1173L : 0x00FF1100L;

    if (mode == 1) x0 = (x0 + 15) & ~15;
    else if (mode == 2) x0 = ((x0 + 15) & ~15) + 3;
    cols = (ax + aw - 6 - x0) / char_w;               /* 3 px outline + spare */
    if (cols > DLG_COLS) cols = DLG_COLS;
    avail_h = ah - 16 - 4;
    d_rp = char_h + char_h / 2;
    if (d_rp * rows + 4 > avail_h) d_rp = (avail_h - 4) / rows;
    if (d_rp < char_h + 1) d_rp = char_h + 1;
    d_oh = d_rp - 1 > char_h + 2 ? char_h + 2 : d_rp - 1;
    d_w = DC(cols); d_h = rows * d_rp + 4;
    if (d_h > avail_h) d_h = avail_h;             /* tiny window: drop what won't fit */
    dn = dtn = 0;

    /* level 1: title, three fields, radio group, check buttons, buttons */
    p = d_add(-1, G_BOX, x0, y0, d_w, d_h);
    dtree[p].ob_spec.index = boxsp;
    dtree[p].ob_state = lvl == 4 ? OS_OUTLINED | OS_SHADOWED : OS_OUTLINED;
    dbit.bi_pdata = dimg; dbit.bi_wb = 2; dbit.bi_hl = 16;
    dbit.bi_x = dbit.bi_y = 0; dbit.bi_color = 1;
    if (lvl == 4) d_patho();
    else {
    d_ted(0, G_BOXTEXT, 1, 0, 32, "Fbench dialog", "", "", OS_SHADOWED);
    d_ted(0, G_FTEXT, 1, 1, 15, "Atari", "Name: ________", "XXXXXXXX", 0);
    d_ted(0, G_FTEXT, 1, 2, 15, "0123", "Phone: _______", "9999999", 0);
    d_ted(0, G_FTEXT, 1, 3, 15, "Glasgow", "City: ________", "XXXXXXXX", 0);
    d_str(0, 18, 1, "Mode:");
    k = d_add(0, G_IBOX, DC(18), DR(2), DC(15), 3 * d_rp - (d_rp - d_oh));
    d_btn(k, 18, 2, 15, "Fast", OF_SELECTABLE | OF_RBUTTON, OS_SELECTED);
    d_btn(k, 18, 3, 15, "Normal", OF_SELECTABLE | OF_RBUTTON, 0);
    d_btn(k, 18, 4, 15, "Careful", OF_SELECTABLE | OF_RBUTTON, 0);
    d_str(0, 1, 4, "Flags:");
    d_btn(0, 1, 5, 5, "Bold", OF_SELECTABLE, OS_SELECTED);
    d_btn(0, 6, 5, 5, "Wide", OF_SELECTABLE, lvl > 1 ? OS_CHECKED : 0);
    d_btn(0, 11, 5, 5, "Fast", OF_SELECTABLE, 0);
    d_ted_px(0, G_TEXT, DC(18), DR(5), DC(15), d_oh, "Ready.", "", "", 5, 0, 0);

    if (lvl >= 2) {                                 /* rows 6..9 */
        /* list with a slider */
        k = d_box(0, 1, 6, 13, 4, 0x00FF1100L);
        d_str(k, 1, 6, "AUTO");
        i = d_str(k, 1, 7, "DESKTOP.INF");
        if (i >= 0) dtree[i].ob_state = OS_SELECTED;
        d_str(k, 1, 8, "FBENCH.PRG");
        d_str(k, 1, 9, "NEWDESK.INF");
        k = d_box(0, 14, 6, 2, 4, 0x00FF1111L);
        if (k >= 0) {
            i = d_add(k, G_BOX, DC(14), DR(7), DC(2), d_rp + d_oh / 2);
            if (i >= 0) dtree[i].ob_spec.index = 0x00FF1101L;
        }
        /* toolbar */
        k = d_box(0, 18, 6, 16, 1, 0x00FF1100L);
        for (j = 0; j < 8; j++) {
            i = d_add(k, G_BOXCHAR, DC(18 + 2 * j), DR(6), DC(2), d_oh);
            if (i >= 0) dtree[i].ob_spec.index = ((long)(unsigned char)tb[j] << 24) | 0x00FF1100L;
        }
        /* image, caption, progress bar, field, buttons in odd states */
        i = d_add(0, G_IMAGE, DC(18), DR(7), 16, 16);
        if (i >= 0) dtree[i].ob_spec.bitblk = &dbit;
        d_str(0, 21, 7, "Disk");
        k = d_box(0, 26, 7, 8, 1, 0x00FF1100L);
        if (k >= 0) {
            i = d_add(k, G_BOX, DC(26), DR(7), DC(5), d_oh);
            if (i >= 0) dtree[i].ob_spec.index = ncolors >= 4 ? 0x00001172L : 0x00001171L;
        }
        d_ted(0, G_FBOXTEXT, 21, 8, 13, "512", "Size: ____K", "9999", 0);
        d_btn(0, 18, 9, 5, "Copy", OF_SELECTABLE, OS_CROSSED);
        d_btn(0, 23, 9, 5, "Move", OF_SELECTABLE, OS_DISABLED);
        d_btn(0, 28, 9, 5, "Del", OF_SELECTABLE, OS_SHADOWED);
    }
    if (lvl >= 3) {                                 /* rows 10..12 */
        /* colour swatches */
        k = d_add(0, G_IBOX, DC(1), DR(10), DC(16), d_rp + d_oh);
        for (j = 0; j < 16; j++) {
            i = d_add(k, G_BOX, DC(1 + 2 * (j & 7)), DR(10 + (j >> 3)), DC(2), d_oh);
            if (i >= 0) dtree[i].ob_spec.index = 0x00011170L | (j % (ncolors > 16 ? 16 : ncolors));
        }
        /* boxes nested four deep */
        p = 0;
        for (j = 0; j < 3 && p >= 0; j++) {
            short v = d_add(p, G_BOX, DC(1) + 3 * j, DR(12) + j, DC(16) - 6 * j, d_oh - 2 * j);
            if (v >= 0) dtree[v].ob_spec.index = 0x00FF1100L | (j == 1 ? 0x10 : 0);
            p = v;
        }
        if (p >= 0) d_ted_px(p, G_BOXTEXT, DC(1) + 9, DR(12) + 3, DC(16) - 18, d_oh - 6 > 1 ? d_oh - 6 : 1,
                             "deep", "", "", 5, 2, 0);
        /* tabs and a tab panel */
        for (j = 0; j < 4; j++) {
            static const char *const tn[4] = { "Gen", "Scr", "Fnt", "Net" };
            d_ted(0, G_BOXTEXT, 18 + 4 * j, 10, 4, tn[j], "", "", j == 1 ? OS_SELECTED : 0);
        }
        k = d_box(0, 18, 11, 16, 2, 0x00FF1100L);
        d_ted(k, G_FTEXT, 19, 11, 7, "640", "W: ____", "9999", 0);
        d_ted(k, G_FTEXT, 19, 12, 7, "400", "H: ____", "9999", 0);
        for (j = 0; j < 6; j++) {
            i = d_add(k, G_BOX, DC(27 + 2 * (j % 3)), DR(11 + j / 3), DC(1), d_oh);
            if (i >= 0) {
                dtree[i].ob_spec.index = 0x00FF1100L;
                dtree[i].ob_flags = OF_SELECTABLE;
                dtree[i].ob_state = (j & 1) ? OS_CROSSED : OS_SELECTED;
            }
        }
    }
    }
    /* exit buttons on the last row */
    d_ok = d_btn(0, 4, br, 8, "OK", OF_SELECTABLE | OF_DEFAULT | OF_EXIT, 0);
    d_btn(0, 13, br, 8, "Cancel", OF_SELECTABLE | OF_EXIT, 0);
    d_btn(0, 22, br, 8, "Help", OF_SELECTABLE | OF_EXIT, 0);
    dtree[dn - 1].ob_flags |= OF_LASTOB;

    if (mode) sprintf(cur_note, "%d obj, x mod 16 = %d", dn & 255, x0 & 15);
    else      sprintf(cur_note, "%d objects", dn);
    fb_update(1);
    if (!mouse_vis) graf_mouse(M_OFF, NULL);
}
static void b_dialog(void)
{
    if (d_ok >= 0) dtree[d_ok].ob_state = (tog++ & 1) ? OS_SELECTED : 0;
    objc_draw_grect(dtree, 0, 8, &tw_area);
}

static void b_mkstate(void) { short x, y, b, k; graf_mkstate(&x, &y, &b, &k); }
static void b_gmouse(void)  { graf_mouse(M_OFF, NULL); graf_mouse(M_ON, NULL); }
static void b_evpoll(void)  { drain_events(); }
static void b_msg(void)
{
    short msg[8], n = 0;
    msg[0] = FB_PING; msg[1] = app_id; msg[2] = 0;
    msg[3] = msg[4] = msg[5] = msg[6] = msg[7] = 0;
    ping_seen = 0;
    appl_write(app_id, 16, msg);
    while (!ping_seen && ++n < 200) drain_events();
    if (!ping_seen) { test_fail = 1; strcpy(cur_note, "message lost"); }
}
static void b_menubar(void) { menu_bar(menu_tree, 1); }
/* The redraw area comes from rpts, so all of it is inside the test area:
   the TOS 1.x AES does not clip a form_dial redraw to the screen, and an
   area hanging off the bottom made it fill the desktop pattern past the end
   of screen memory (crash after test 30 on TOS 1.x). */
static void b_fdial(void)
{
    short k = NX(), x = rpts[k][0] & ~15;
    if (x < ax) x += 16;                 /* rpts keeps 16 px spare for this */
    form_dial(FMD_FINISH, 0, 0, 0, 0, x, rpts[k][1], 64, 48);
    drain_events();
}

/* ------------------------------------------------------------------ */
/*  GDOS tests                                                        */
/* ------------------------------------------------------------------ */


static short a_gdos(void)   { return vq_gdos() != 0; }
static short a_twofont(void){ return nfonts >= 2; }
static short a_vector(void)
{
    return gdos_type == GDOS_FSM || gdos_type == 0x5F535044L /* _SPD */ ||
           (nfonts > 0 && fonts[gfont].outline);
}

static void s_gsize(void)
{
    short d, got;
    vst_font(bh, nfonts ? fonts[gfont].id : 1);
    vst_color(bh, col(4));
    got = vst_point(bh, cur_param, &d, &d, &d, &d);
    if (got != cur_param) sprintf(cur_note, "got %dpt", got);
    if (!gdos_loaded) {
        if (cur_note[0]) strcat(cur_note, ", system font");
        else             strcpy(cur_note, "system font");
    }
}
static void b_gtext(void)
{
    short k = NX();
    v_gtext(bh, ax + (pts[k][0] - ax) / 3, pts[k][1], "Fbench GDOS Ag 0123");
}
/*
 * Text effects build each character in the VDI's special-effects scratch
 * buffer, which TOS sizes for the 8x16 system font.  A GDOS font with skew
 * plus outline overruns it and corrupts memory (crashed under MultiTOS).
 * Enlarging it needs v_set_app_buff, which changes a pointer shared by all
 * programs - not acceptable here.  So this test uses bold only (one extra
 * pixel per character), at 10pt, and is off by default.
 */
static void s_geff(void)
{
    cur_param = 10;
    s_gsize();
    vst_effects(bh, TXT_THICKENED);
    if (!cur_note[0]) strcpy(cur_note, "bold only");
}
static void b_gswitch(void)
{
    short d;
    vst_font(bh, (tog++ & 1) ? fonts[gfont].id : fonts[gfont == 0 ? 1 : 0].id);
    vst_point(bh, 12, &d, &d, &d, &d);
}
static const short sizes[] = { 8, 10, 12, 14, 18, 24, 36, 48 };
static void s_gfont(void) { vst_font(bh, nfonts ? fonts[gfont].id : 1); }
static void b_gpoint(void) { short d; vst_point(bh, sizes[tog++ & 7], &d, &d, &d, &d); }
static void s_gext(void)  { short d; s_gfont(); vst_point(bh, 12, &d, &d, &d, &d); }
static void b_garb(void)
{
    static const short arb[] = { 7, 11, 17, 23, 31, 47, 63, 13 };
    short d;
    vst_arbpt(bh, arb[tog++ & 7], &d, &d, &d, &d);
    b_gtext();
}
static void b_gname(void)
{
    char name[40];
    short i;
    for (i = 1; i <= nfonts; i++) vqt_name(bh, i, name);
}
/* Loading every font again and again exhausts / fragments memory on some
   GDOS versions (which don't check Malloc), so: only 3 cycles, and off by
   default (param -1). */
static void s_gload(void) { max_iter = 3; }
static void b_gload(void)
{
    short out[57], h = open_vwk(out);
    if (h <= 0) { test_fail = 1; strcpy(cur_note, "v_opnvwk failed"); return; }
    vst_load_fonts(h, 0);
    vst_unload_fonts(h, 0);
    v_clsvwk(h);
}

/* ------------------------------------------------------------------ */
/*  The test table                                                    */
/* ------------------------------------------------------------------ */

#define V GRP_VDI
#define A GRP_AES
#define G GRP_GDOS
#define L GRP_ALIGN
#define F GRP_FULL

TEST tests[] = {
 /* VDI ------------------------------------------------------------- */
 { V, "VDI.PLINE2",  "Line, 2 points",          NULL, s_colour,  b_pline2,  NULL, 0 },
 { V, "VDI.PLINE32", "Polyline, 32 points",     NULL, s_pline32, b_pline32, NULL, 0 },
 { V, "VDI.WIDELN",  "Wide line (7px, round)",  NULL, s_wide,    b_pline2,  NULL, 0 },
 { V, "VDI.DASHLN",  "Dashed line",             NULL, s_dashed,  b_pline2,  NULL, 0 },
 { V, "VDI.USERLN",  "User line style",         NULL, s_user,    b_pline2,  NULL, 0 },
 { V, "VDI.XORLN",   "XOR line",                NULL, s_xorln,   b_pline2,  NULL, 0 },
 { V, "VDI.MARKER",  "Markers x16",             NULL, s_marker,  b_marker,  NULL, 0 },
 { V, "VDI.RECT32",  "Filled rect 32x32",       NULL, s_colour,  b_rect32,  NULL, 0 },
 { V, "VDI.RECTFUL", "Filled rect, full window",NULL, NULL,      b_rectfull,NULL, 0 },
 { V, "VDI.BAR",     "Bar, hatched + border",   NULL, s_bar,     b_bar,     NULL, 0 },
 { V, "VDI.PATTERN", "Pattern fill rect",       NULL, s_pattern, b_rect32,  NULL, 0 },
 { V, "VDI.UDPAT",   "User pattern fill",       NULL, s_udpat,   b_rect32,  NULL, 0 },
 { V, "VDI.XORFILL", "XOR filled rect",         NULL, s_xorfill, b_rect32,  NULL, 0 },
 { V, "VDI.POLYGON", "Polygon, 8 points",       NULL, s_solid,   b_polygon, NULL, 0 },
 { V, "VDI.CIRCLE",  "Filled circle",           NULL, s_solid,   b_circle,  NULL, 0 },
 { V, "VDI.ELLIPSE", "Filled ellipse",          NULL, s_solid,   b_ellipse, NULL, 0 },
 { V, "VDI.ARC",     "Arc",                     NULL, s_colour,  b_arc,     NULL, 0 },
 { V, "VDI.PIE",     "Pie slice",               NULL, s_solid,   b_pie,     NULL, 0 },
 { V, "VDI.ELLARC",  "Elliptical arc",          NULL, s_colour,  b_ellarc,  NULL, 0 },
 { V, "VDI.ELLPIE",  "Elliptical pie",          NULL, s_solid,   b_ellpie,  NULL, 0 },
 { V, "VDI.RBOX",    "Rounded box",             NULL, s_colour,  b_rbox,    NULL, 0 },
 { V, "VDI.RFBOX",   "Filled rounded box",      NULL, s_solid,   b_rfbox,   NULL, 0 },
 { V, "VDI.CONTOUR", "Contour fill 64x40",      NULL, s_contour, b_contour, NULL, 0 },
 { V, "VDI.TEXT",    "Text, 15 chars",          NULL, NULL,      b_text,    NULL, 0 },
 { V, "VDI.TEXT80",  "Text, 84 chars",          NULL, NULL,      b_textlong,NULL, 0 },
 { V, "VDI.TEXTEFF", "Text with effects",       NULL, s_texteff, b_text,    NULL, 0 },
 { V, "VDI.TEXTROT", "Text rotated 90",         a_rotate, s_rotate, b_text, NULL, 0 },
 { V, "VDI.JUSTIFY", "Justified text",          NULL, NULL,      b_justified,NULL,0 },
 { V, "VDI.EXTENT",  "Text extent query",       NULL, NULL,      b_extent,  NULL, 0 },
 { V, "VDI.CPY64",   "Blit scr->scr 64x64",     NULL, NULL,      b_cpy64,   NULL, 0 },
 { V, "VDI.SCROLL",  "Blit scroll window",      NULL, s_scroll,  b_scroll,  NULL, 0 },
 { V, "VDI.M2S",     "Blit mem->scr 64x64",     NULL, s_offscreen, b_mem2scr, c_offscreen, 0 },
 { V, "VDI.S2M",     "Blit scr->mem 64x64",     NULL, s_offscreen, b_scr2mem, c_offscreen, 0 },
 { V, "VDI.VRTCPY",  "Mono blit (vrt_cpyfm)",   NULL, s_mono,    b_vrt,     NULL, 0 },
 { V, "VDI.TRNFM",   "Transform form 64x64",    a_trnfm, s_trnfm, b_trnfm,  c_trnfm, 0 },
 { V, "VDI.GETPIX",  "Get pixel",               NULL, NULL,      b_getpix,  NULL, 0 },
 { V, "VDI.MOUSE",   "Hide/show mouse",         NULL, NULL,      b_mouse,   NULL, 0 },
 { V, "VDI.ATTR",    "Attribute calls x3",      NULL, NULL,      b_attrs,   NULL, 0 },
 { V, "VDI.VWK",     "Open/close workstation",  NULL, NULL,      b_vwk,     NULL, 0 },

 /* AES ------------------------------------------------------------- */
 { A, "AES.CREATE",  "Window create+delete",    NULL, NULL,       b_wcreate, NULL, 0 },
 { A, "AES.OPEN",    "Window open+close",       NULL, aux_closed, b_wopen,   aux_destroy, 0 },
 { A, "AES.MOVE",    "Window move",             NULL, aux_open,   b_wmove,   aux_destroy, 0 },
 { A, "AES.SIZE",    "Window resize",           NULL, aux_open,   b_wsize,   aux_destroy, 0 },
 { A, "AES.TOP",     "Window top (2 windows)",  NULL, aux_open,   b_wtop,    aux_destroy, 0 },
 { A, "AES.NAME",    "Set window title",        NULL, aux_open,   b_wname,   aux_destroy, 0 },
 { A, "AES.INFO",    "Set info line",           NULL, aux_open,   b_winfo,   aux_destroy, 0 },
 { A, "AES.SLIDER",  "Set slider pos+size",     NULL, aux_open,   b_wslide,  aux_destroy, 0 },
 { A, "AES.SCROLL",  "Scroll window content",   NULL, s_wscroll,  b_wscroll, c_wscroll, 0 },
 { A, "AES.SCRSL",   "Scroll window + slider",  NULL, s_wscrsl,   b_ascroll, c_ascr, 0 },
 { A, "AES.WGET",    "wind_get work area",      NULL, NULL,       b_wget,    NULL, 0 },
 { A, "AES.RECTS",   "Rectangle list walk",     NULL, NULL,       b_wrect,   NULL, 0 },
 { A, "AES.CALC",    "wind_calc",               NULL, NULL,       b_wcalc,   NULL, 0 },
 { A, "AES.UPDATE",  "wind_update pair",        NULL, NULL,       b_wupd,    NULL, 0 },
 { A, "AES.OBJDRAW", "objc_draw dialog",        NULL, s_objc,     b_objc,    c_objc, 0 },
 { A, "AES.OBJFIND", "objc_find",               NULL, s_objc,     b_ofind,   c_objc, 0 },
 { A, "AES.DLG1",    "Dialog draw, medium",     NULL, s_dialog,   b_dialog,  c_objc, 1 },
 { A, "AES.DLG2",    "Dialog draw, complex",    NULL, s_dialog,   b_dialog,  c_objc, 2 },
 { A, "AES.DLG3",    "Dialog draw, very complex",NULL, s_dialog,  b_dialog,  c_objc, 3 },
 { A, "AES.DLG4",    "Dialog draw, pathological",NULL, s_dialog,  b_dialog,  c_objc, 4 },
 { A, "AES.FDIAL",   "form_dial redraw 64x48",  NULL, NULL,       b_fdial,   NULL, 0 },
 { A, "AES.MENU",    "Menu bar redraw",         NULL, NULL,       b_menubar, NULL, 0 },
 { A, "AES.MKSTATE", "graf_mkstate",            NULL, NULL,       b_mkstate, NULL, 0 },
 { A, "AES.GMOUSE",  "graf_mouse off+on",       NULL, NULL,       b_gmouse,  NULL, 0 },
 { A, "AES.EVPOLL",  "Event poll (evnt_multi)", NULL, NULL,       b_evpoll,  NULL, 0 },
 { A, "AES.MSG",     "Message round trip",      NULL, NULL,       b_msg,     NULL, 0 },

 /* Alignment: run with the blitter on, then off, and compare -------- */
 { L, "ALN.RECT.A",  "Fill 64x64, aligned",     NULL, s_rect_a, b_rect64,  NULL, 0 },
 { L, "ALN.RECT.U",  "Fill 64x64, x+3",         NULL, s_rect_u, b_rect64,  NULL, 0 },
 { L, "ALN.CPY.A",   "Blit 64x64, aligned",     NULL, s_cpy_a,  b_cpy64,   NULL, 0 },
 { L, "ALN.CPY.U",   "Blit 64x64, src+3 dst+11",NULL, s_cpy_u,  b_cpy64,   NULL, 0 },
 { L, "ALN.M2S.A",   "Mem->scr 64x64, aligned", NULL, s_m2s_a,  b_mem2scr, c_offscreen, 0 },
 { L, "ALN.M2S.U",   "Mem->scr 64x64, x+3",     NULL, s_m2s_u,  b_mem2scr, c_offscreen, 0 },
 { L, "ALN.VRT.A",   "Mono blit, aligned",      NULL, s_vrt_a,  b_vrt,     NULL, 0 },
 { L, "ALN.VRT.U",   "Mono blit, x+3",          NULL, s_vrt_u,  b_vrt,     NULL, 0 },
 { L, "ALN.TEXT.A",  "Text, x on 8 px",         NULL, s_txt_a,  b_text,    NULL, 0 },
 { L, "ALN.TEXT.U",  "Text, x+3",               NULL, s_txt_u,  b_text,    NULL, 0 },
 { L, "ALN.VSC.A",   "Scroll up, aligned edges",NULL, s_vsc_a,  b_vsc,     NULL, 0 },
 { L, "ALN.VSC.U",   "Scroll up, edges x+5",    NULL, s_vsc_u,  b_vsc,     NULL, 0 },
 { L, "ALN.HSC.A",   "Scroll left 16 px",       NULL, s_hsc_a,  b_hsc,     NULL, 0 },
 { L, "ALN.HSC.U",   "Scroll left 3 px",        NULL, s_hsc_u,  b_hsc,     NULL, 0 },
 { L, "ALN.WMOVE.A", "Move window, aligned",    NULL, s_amove_a, b_wmove,  aux_destroy, 0 },
 { L, "ALN.WMOVE.U", "Move window, off-grid",   NULL, s_amove_u, b_wmove,  aux_destroy, 0 },
 { L, "ALN.WOPEN.A", "Open/close win, aligned", NULL, s_aopen_a, b_wopen,  aux_destroy, 0 },
 { L, "ALN.WOPEN.U", "Open/close win, off-grid",NULL, s_aopen_u, b_wopen,  aux_destroy, 0 },
 { L, "ALN.WSCR.A",  "Scroll window, aligned",  NULL, s_ascr_a, b_ascroll, c_ascr, 0 },
 { L, "ALN.WSCR.U",  "Scroll window, off-grid", NULL, s_ascr_u, b_ascroll, c_ascr, 0 },
 { L, "ALN.WD.R0.A", "Scroll right, grid",       NULL, s_wdir,   b_wdir,    c_ascr, 17 },
 { L, "ALN.WD.R0.U", "Scroll right, off-grid",   NULL, s_wdir,   b_wdir,    c_ascr, 33 },
 { L, "ALN.WD.R1.A", "Scroll right+bar, grid",   NULL, s_wdir,   b_wdir,    c_ascr, 25 },
 { L, "ALN.WD.R1.U", "Scroll right+bar, off-grid", NULL, s_wdir,   b_wdir,    c_ascr, 41 },
 { L, "ALN.WD.L0.A", "Scroll left, grid",        NULL, s_wdir,   b_wdir,    c_ascr, 18 },
 { L, "ALN.WD.L0.U", "Scroll left, off-grid",    NULL, s_wdir,   b_wdir,    c_ascr, 34 },
 { L, "ALN.WD.L1.A", "Scroll left+bar, grid",    NULL, s_wdir,   b_wdir,    c_ascr, 26 },
 { L, "ALN.WD.L1.U", "Scroll left+bar, off-grid", NULL, s_wdir,   b_wdir,    c_ascr, 42 },
 { L, "ALN.WD.D0.A", "Diag dn-rt, grid",         NULL, s_wdir,   b_wdir,    c_ascr, 19 },
 { L, "ALN.WD.D0.U", "Diag dn-rt, off-grid",     NULL, s_wdir,   b_wdir,    c_ascr, 35 },
 { L, "ALN.WD.D1.A", "Diag dn-rt+bar, grid",     NULL, s_wdir,   b_wdir,    c_ascr, 27 },
 { L, "ALN.WD.D1.U", "Diag dn-rt+bar, off-grid", NULL, s_wdir,   b_wdir,    c_ascr, 43 },
 { L, "ALN.WD.U0.A", "Diag up-lt, grid",         NULL, s_wdir,   b_wdir,    c_ascr, 20 },
 { L, "ALN.WD.U0.U", "Diag up-lt, off-grid",     NULL, s_wdir,   b_wdir,    c_ascr, 36 },
 { L, "ALN.WD.U1.A", "Diag up-lt+bar, grid",     NULL, s_wdir,   b_wdir,    c_ascr, 28 },
 { L, "ALN.WD.U1.U", "Diag up-lt+bar, off-grid", NULL, s_wdir,   b_wdir,    c_ascr, 44 },
 { L, "ALN.DLG1.A",  "Medium dialog, aligned",  NULL, s_dialog, b_dialog,  c_objc, 17 },
 { L, "ALN.DLG1.U",  "Medium dialog, x+3",      NULL, s_dialog, b_dialog,  c_objc, 33 },
 { L, "ALN.DLG2.A",  "Complex dialog, aligned", NULL, s_dialog, b_dialog,  c_objc, 18 },
 { L, "ALN.DLG2.U",  "Complex dialog, x+3",     NULL, s_dialog, b_dialog,  c_objc, 34 },
 { L, "ALN.DLG3.A",  "V.complex dlg, aligned",  NULL, s_dialog, b_dialog,  c_objc, 19 },
 { L, "ALN.DLG3.U",  "V.complex dlg, x+3",      NULL, s_dialog, b_dialog,  c_objc, 35 },
 { L, "ALN.DLG4.A",  "Patholog. dlg, aligned",  NULL, s_dialog, b_dialog,  c_objc, 20 },
 { L, "ALN.DLG4.U",  "Patholog. dlg, x+3",      NULL, s_dialog, b_dialog,  c_objc, 36 },

 /* Full-screen windows ---------------------------------------------- */
 { F, "FULL.OPEN",   "Open/close full window",  NULL, s_full_closed, b_wopen,      aux_destroy, 0 },
 { F, "FULL.TOGGLE", "Full <-> normal size",    NULL, s_full_toggle, b_full_toggle,aux_destroy, 0 },
 { F, "FULL.TOP",    "Top full / test window",  NULL, s_full,        b_wtop,       aux_destroy, 0 },
 { F, "FULL.FILL",   "Fill full window",        NULL, s_full,        b_full_fill,  aux_destroy, 0 },
 { F, "FULL.VSCR",   "Scroll up 1 line",        NULL, s_full,        b_full_vscr,  aux_destroy, 0 },
 { F, "FULL.HSCR16", "Scroll left 16 px",       NULL, s_full,        b_full_hscr16,aux_destroy, 0 },
 { F, "FULL.HSCR8",  "Scroll left 8 px",        NULL, s_full,        b_full_hscr8, aux_destroy, 0 },
 { F, "FULL.HSCR3",  "Scroll left 3 px",        NULL, s_full,        b_full_hscr3, aux_destroy, 0 },
 { F, "FULL.SCR08",  "Scroll text 8pt",         NULL, s_full_size,   b_full_size,  aux_destroy, 8 },
 { F, "FULL.SCR10",  "Scroll text 10pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 10 },
 { F, "FULL.SCR12",  "Scroll text 12pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 12 },
 { F, "FULL.SCR14",  "Scroll text 14pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 14 },
 { F, "FULL.SCR18",  "Scroll text 18pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 18 },
 { F, "FULL.SCR24",  "Scroll text 24pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 24 },
 { F, "FULL.SCR36",  "Scroll text 36pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 36 },
 { F, "FULL.SCR48",  "Scroll text 48pt",        NULL, s_full_size,   b_full_size,  aux_destroy, 48 },
 { F, "FULL.GDSCR",  "Scroll, size per line",   NULL, s_full_mix,    b_full_mix,   aux_destroy, 0 },

 /* GDOS ------------------------------------------------------------ */
 { G, "GDOS.T06",    "Text 6pt",                NULL, s_gsize, b_gtext, NULL, 6 },
 { G, "GDOS.T08",    "Text 8pt",                NULL, s_gsize, b_gtext, NULL, 8 },
 { G, "GDOS.T10",    "Text 10pt",               NULL, s_gsize, b_gtext, NULL, 10 },
 { G, "GDOS.T12",    "Text 12pt",               NULL, s_gsize, b_gtext, NULL, 12 },
 { G, "GDOS.T14",    "Text 14pt",               NULL, s_gsize, b_gtext, NULL, 14 },
 { G, "GDOS.T18",    "Text 18pt",               NULL, s_gsize, b_gtext, NULL, 18 },
 { G, "GDOS.T24",    "Text 24pt",               NULL, s_gsize, b_gtext, NULL, 24 },
 { G, "GDOS.T36",    "Text 36pt",               NULL, s_gsize, b_gtext, NULL, 36 },
 { G, "GDOS.T48",    "Text 48pt",               NULL, s_gsize, b_gtext, NULL, 48 },
 { G, "GDOS.T72",    "Text 72pt",               NULL, s_gsize, b_gtext, NULL, 72 },
 { G, "GDOS.BOLD10", "Text 10pt bold",          NULL, s_geff,  b_gtext, NULL, -1 },
 { G, "GDOS.POINT",  "vst_point, 8 sizes",      NULL, s_gfont, b_gpoint, NULL, 0 },
 { G, "GDOS.SWITCH", "Font switch + vst_point", a_twofont, NULL, b_gswitch, NULL, 0 },
 { G, "GDOS.ARBPT",  "Arbitrary size + text",   a_vector, s_gfont, b_garb, NULL, 0 },
 { G, "GDOS.SCRMIX",  "Scroll, mixed sizes",     NULL, s_mixscroll, b_mixscroll, NULL, 0 },
 { G, "GDOS.EXTENT", "Text extent at 12pt",     NULL, s_gext,  b_extent, NULL, 0 },
 { G, "GDOS.NAMES",  "Enumerate font names",    a_gdos, NULL,   b_gname,  NULL, 0 },
 { G, "GDOS.LOAD",   "Load+unload fonts (x3)",  a_gdos, s_gload, b_gload, NULL, -1 },
 { -1, NULL, NULL, NULL, NULL, NULL, NULL, 0 }
};

#undef V
#undef A
#undef G
#undef L
#undef F

/* ------------------------------------------------------------------ */

void tests_init(void)
{
    short i, a[10];
    for (ntests = 0; tests[ntests].key && ntests < MAX_TESTS; ntests++)
        ;
    for (i = 0; i < ntests; i++) {
        res[i].sel = tests[i].param >= 0;   /* param -1: off by default */
        res[i].state = RS_NONE;
        res[i].score = -1;
        res[i].note[0] = 0;
    }
    (void)a;
    sys_th = char_h;
    memset(&scr_mfdb, 0, sizeof(scr_mfdb));
}

void fonts_init(void)
{
    short i, total;
    char name[40];

    short h, out[57];

    /* enumerate the fonts on a temporary workstation, then release it and
       the fonts again: Fbench holds no fonts while it sits idle */
    nfonts = 0; gdos_loaded = 0; gfont = 0;
    gdos_type = vq_vgdos();
    h = open_vwk(out);
    if (h <= 0) h = vdi_h;
    if (vq_gdos())
        gdos_loaded = vst_load_fonts(h, 0);
    total = work_out[10] + gdos_loaded;
    if (total < 1) total = 1;
    for (i = 1; i <= total && nfonts < MAX_FONTS; i++) {
        short n;
        memset(name, 0, sizeof(name));
        fonts[nfonts].id = vqt_name(h, i, name);
        fonts[nfonts].outline = name[32] != 0;
        name[32] = 0;
        for (n = 31; n >= 0 && (name[n] == ' ' || name[n] == 0); n--)
            name[n] = 0;
        strcpy(fonts[nfonts].name, name[0] ? name : "(unnamed)");
        nfonts++;
    }
    /* default benchmark font: first GDOS font, else the system font */
    gfont = (nfonts > 1) ? 1 : 0;
    if (gdos_loaded) vst_unload_fonts(h, 0);
    if (h != vdi_h) v_clsvwk(h);
}

void fonts_exit(void)
{
    bench_close();      /* in case we quit in the middle of a run */
}

/*
 * The benchmark workstation (and its GDOS fonts) exists only while a run is
 * in progress, so other applications - and other workspaces - aren't short
 * of VDI handles or font memory while Fbench sits idle.
 */
static short bench_fonts;
short bench_open(void)
{
    short out[57], a[10];
    if (bh > 0) return 1;
    bh = open_vwk(out);
    if (bh <= 0) {
        bh = 0;
        form_alert(1, "[3][Fbench:|Cannot open the benchmark|VDI workstation. Close an|application and try again.][ OK ]");
        return 0;
    }
    bench_fonts = vq_gdos() ? vst_load_fonts(bh, 0) : 0;
    vqt_attributes(bh, a);
    sys_th = a[7] > 0 ? a[7] : char_h;
    return 1;
}
void bench_close(void)
{
    if (bh <= 0) return;
    if (bench_fonts) vst_unload_fonts(bh, 0);
    bench_fonts = 0;
    v_clsvwk(bh);
    bh = 0;
}

/*
 * Crash breadcrumb: FBLAST.LOG in Fbench's start directory names the test
 * that is starting. It is deleted when a run ends normally, so if it is
 * still there after a crash it names the test that crashed.
 */
#if FB_DEBUG
static void crumb_path(char *p)
{
    strcpy(p, home_dir);
    strcat(p, "FBLAST.LOG");
}
void crumb_text(const char *s)
{
    char p[180];
    FILE *f;
    crumb_path(p);
    if ((f = fopen(p, "w")) != NULL) {
        fprintf(f, "Fbench %s - last step started:\n%s\n", FB_VERSION, s);
        fclose(f);
    }
}
static void crumb(short i)
{
    char p[180];
    FILE *f;
    crumb_path(p);
    if ((f = fopen(p, "w")) != NULL) {
        fprintf(f, "Fbench %s - last test started:\n%s  %s\nblitter %s, duration %ld\n",
                FB_VERSION, tests[i].key, tests[i].name,
                blitter_on ? "on" : "off", dur_ticks);
        fclose(f);
    }
}
void crumb_clear(void)
{
    char p[180];
    crumb_path(p);
    remove(p);
}
#else
/* release build: no crash log */
void crumb_text(const char *s) { (void)s; }
static void crumb(short i)     { (void)i; }
void crumb_clear(void)         { }
#endif

/* Tests that open, top, move or resize windows, or redraw the menu bar.
   Under MagiC (and MultiTOS) the owner of the top window gets the menu bar
   and the keyboard, so these pull Fbench to the front: in "Stay in
   background" mode they run only while Fbench is in front anyway. */
static short needs_front(short i)
{
    static const char *pfx[] = {
        "AES.OPEN", "AES.MOVE", "AES.SIZE", "AES.TOP", "AES.NAME", "AES.INFO",
        "AES.SLIDER", "AES.MENU", "ALN.WMOVE", "ALN.WOPEN", "ALN.WSCR", "FULL.", NULL
    };
    short k;
    for (k = 0; pfx[k]; k++)
        if (!strncmp(tests[i].key, pfx[k], strlen(pfx[k]))) return 1;
    return 0;
}

/* Background mode: a test always draws its whole area, covered or not, so
   the timings stay comparable.  If another program's window covered part
   of it, ask the AES to repaint that area afterwards: every window there
   gets a redraw message, as after a dialog. */
static void repair_area(const GRECT *a)
{
    form_dial(FMD_FINISH, 0, 0, 0, 0, a->g_x, a->g_y, a->g_w, a->g_h);
}

static long lr_sum[MAX_TESTS];          /* long run: sum of the runs */

short run_tests(short all)
{
    short i, n = 0, done = 0, hide, aes, front, covered, pass, npass = long_run ? LONG_RUNS : 1;
    char info[100];
    long sc;

    for (i = 0; i < ntests; i++) if (all || res[i].sel) n++;
    if (!n) {
        form_alert(1, "[1][Fbench:|No tests are selected.|Click the boxes in the|results window.][ OK ]");
        return 0;
    }
    if (npass > 1) {
        char a[160];
        long mins = (long)n * dur_ticks * npass / (200L * 60) + 1;
        sprintf(a, "[2][Fbench long run:|%d tests, %d times each -|at least %ld minutes.|Esc stops (keeps the runs|done so far).][ Start |Cancel]",
                n, npass, mins);
        if (form_alert(1, a) != 1) return 0;
    }
    if (!bench_open()) return 0;
    pens_update();                      /* palette may have changed since */
    busy = 1; abort_run = 0;
    busy_mouse(1);
    results_hide();
    if (!testwin_open()) { busy = 0; busy_mouse(0); results_show(); bench_close(); return 0; }

    for (i = 0; i < ntests; i++)
        if (all || res[i].sel) { res[i].runs = 0; res[i].smin = res[i].smax = -1; lr_sum[i] = 0; }

    /* a long run repeats the whole selection, so slow drifts (other
       programs, caches, temperature) show up as a swing between runs */
    for (pass = 0; pass < npass && !abort_run && !quit_app; pass++)
    for (i = 0, done = 0; i < ntests && !abort_run && !quit_app; i++) {
        if (!all && !res[i].sel) continue;
        /* Every pass starts from the same GDOS state: a fresh benchmark
           workstation with its fonts loaded again, as at the start of a
           single run.  Some GDOS versions change state after fonts are
           loaded and unloaded (XGDOS drew text ~10x faster afterwards). */
        if (pass > 0 && done == 0) {
            bench_close();
            if (!bench_open()) { abort_run = 1; break; }
        }
        /* Load+unload fonts changes the GDOS state for the tests after it,
           so in a long run it runs once, in the last pass (it is the last
           test, so nothing is measured after it) */
        if (npass > 1 && pass < npass - 1 && !strcmp(tests[i].key, "GDOS.LOAD")) { done++; continue; }
        done++;
        if (npass > 1)
            sprintf(info, " Run %d/%d  %d/%d  %s   (Esc stops)", pass + 1, npass, done, n, tests[i].name);
        else
            sprintf(info, " %d/%d  %s   (Esc stops)", done, n, tests[i].name);
        set_test_info(info);
        cur_run = i;
        results_changed();
        busy_mouse(1);

        if (tests[i].avail && !tests[i].avail()) {
            res[i].state = RS_NA;
            strcpy(res[i].note, "not supported here");
            continue;
        }

        if (!polite) {
            wind_set(wins[WN_TEST].h, WF_TOP, 0, 0, 0, 0);
            drain_events();
        } else {
            drain_events();
            if (needs_front(i) && !fb_in_front()) {
                res[i].state = RS_NA;
                strcpy(res[i].note, "skipped: in background");   /* note[] is 28 */
                continue;
            }
        }
        covered = polite && !win_fully_visible(wins[WN_TEST].h);
        wind_get_grect(wins[WN_TEST].h, WF_WORKXYWH, &tw_area);
        run_tw_w = tw_area.g_w; run_tw_h = tw_area.g_h;

        /* Hold the screen while a test draws.  AES-group tests run without
           the outer hold: they call the AES (wind_update itself is one of
           them) and take the lock themselves where they draw directly. */
        aes = !strncmp(tests[i].key, "AES.", 4);
        fb_update(1);
        /* with "Mouse visible in tests" the pointer is never hidden: the
           test window content is disposable, so cursor trails don't matter */
        hide = !mouse_vis && tests[i].group != GRP_AES;
        if (hide) graf_mouse(M_OFF, NULL);
        area_setup(i);
        clear_area();
        if (aes) fb_update(0);
        cur_note[0] = 0;
        cur_param = tests[i].param;
        max_iter = 0;
        crumb(i);
        test_fail = 0;
        step_on = trace_steps; step_test = i; step_op = 0;   /* trace setup + first 16 ops */
        if (tests[i].setup) tests[i].setup();
        sc = test_fail ? -1 : time_test(tests[i].body);
        if (tests[i].cleanup) tests[i].cleanup();
        area_done();
        if (hide) graf_mouse(M_ON, NULL);
        if (!aes) fb_update(0);
        /* also catches a window that came over the test window during an
           AES-group test (those don't hold the screen lock throughout) */
        if (covered || (polite && !win_fully_visible(wins[WN_TEST].h)))
            repair_area(&tw_area);

        res[i].blit = blitter_on;
        if (first_slow) {                       /* one-off cost of the first op */
            char fs[48];
            sprintf(fs, "1st op %ld.%02ld s", first_ticks / 200, (first_ticks % 200) / 2);
            if (!cur_note[0]) strcpy(cur_note, fs);
            else if (strlen(cur_note) + 2 + strlen(fs) < sizeof(cur_note)) {
                strcat(cur_note, ", "); strcat(cur_note, fs);
            }
        }
        strcpy(res[i].note, cur_note);
        if (sc < 0) { res[i].state = RS_FAIL; res[i].score = -1; res[i].runs = 0; }
        else if (pass == 0 || res[i].runs == 0) {
            res[i].state = RS_OK; res[i].score = sc;
            lr_sum[i] = sc; res[i].smin = res[i].smax = sc; res[i].runs = 1;
        } else if (res[i].state == RS_OK) {         /* average of the runs so far */
            lr_sum[i] += sc; res[i].runs++;
            if (sc < res[i].smin) res[i].smin = sc;
            if (sc > res[i].smax) res[i].smax = sc;
            res[i].score = lr_sum[i] / res[i].runs;
        }
        drain_events();
    }
    cur_run = -1;
    busy_mouse(0);
    front = fb_in_front();
    testwin_close();
    bench_close();
    crumb_clear();
    results_show();
    if (polite && !front) results_to_back();   /* don't jump in front of the user */
    busy = 0;
    drain_events();
    results_changed();
    return 1;
}
