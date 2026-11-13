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

static long hz_tmp;
static long rd_hz(void) { hz_tmp = *(volatile long *)0x4baL; return 0; }

long hz200(void)
{
    Supexec(rd_hz);
    return hz_tmp;
}

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

static long time_test(void (*body)(void))
{
    long n = 0, batch = 1, i, t0, t, el;

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

static void area_setup(void)
{
    short p[4], i, rmax;

    ax = tw_area.g_x; ay = tw_area.g_y; aw = tw_area.g_w; ah = tw_area.g_h;
    /* never draw outside the screen work area */
    if (ax + aw > desk.g_x + desk.g_w) aw = desk.g_x + desk.g_w - ax;
    if (ay + ah > desk.g_y + desk.g_h) ah = desk.g_y + desk.g_h - ay;
    p[0] = ax; p[1] = ay; p[2] = ax + aw - 1; p[3] = ay + ah - 1;
    vs_clip(bh, 1, p);
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

static void aux_create(short open)
{
    GRECT r;
    r.g_x = ax + aw / 8; r.g_y = ay + ah / 8;
    r.g_w = aw / 2;      r.g_h = ah / 2;
    if (r.g_w < 10 * box_w) r.g_w = 10 * box_w;
    if (r.g_h < 6 * box_h)  r.g_h = 6 * box_h;
    if (aux_align == 3)
        r = desk;
    else if (aux_align) {
        /* place the WORK area: left edge on a 16 pixel (word) boundary,
           or deliberately 5 pixels past one; width a multiple of 16 */
        GRECT w = r;
        w.g_x = ((w.g_x + 15) & ~15) + (aux_align == 2 ? 5 : 0);
        w.g_w &= ~15;
        wind_calc_grect(WC_BORDER, AUX_KIND, &w, &r);
    }
    aux_r = r;
    fb_step("aux: wind_create");
    aux_h = wind_create_grect(AUX_KIND, &desk);
    if (aux_h <= 0) { aux_h = 0; test_fail = 1; strcpy(cur_note, "no window handles"); return; }
    win_register(WN_AUX, aux_h, " Fbench AES ");
    wins[WN_AUX].kind = NAME | MOVER | SIZER | INFO | CLOSER | UPARROW | DNARROW | VSLIDE;
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
    short p[8], line = char_h;
    char buf[40];

    wind_get_grect(wh, WF_WORKXYWH, &work);
    wind_get_grect(wh, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h) {
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
    wind_set(wh, WF_VSLIDE, (tog * 37) % 1000, 0, 0, 0);
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
static void b_fdial(void)
{
    short k = NX();
    form_dial(FMD_FINISH, 0, 0, 0, 0, pts[k][0] & ~15, pts[k][1], 64, 48);
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
 { A, "AES.WGET",    "wind_get work area",      NULL, NULL,       b_wget,    NULL, 0 },
 { A, "AES.RECTS",   "Rectangle list walk",     NULL, NULL,       b_wrect,   NULL, 0 },
 { A, "AES.CALC",    "wind_calc",               NULL, NULL,       b_wcalc,   NULL, 0 },
 { A, "AES.UPDATE",  "wind_update pair",        NULL, NULL,       b_wupd,    NULL, 0 },
 { A, "AES.OBJDRAW", "objc_draw dialog",        NULL, s_objc,     b_objc,    c_objc, 0 },
 { A, "AES.OBJFIND", "objc_find",               NULL, s_objc,     b_ofind,   c_objc, 0 },
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

short run_tests(short all)
{
    short i, n = 0, done = 0, hide, aes, front, covered;
    char info[100];
    long sc;

    for (i = 0; i < ntests; i++) if (all || res[i].sel) n++;
    if (!n) {
        form_alert(1, "[1][Fbench:|No tests are selected.|Click the boxes in the|results window.][ OK ]");
        return 0;
    }
    if (!bench_open()) return 0;
    pens_update();                      /* palette may have changed since */
    busy = 1; abort_run = 0;
    busy_mouse(1);
    results_hide();
    if (!testwin_open()) { busy = 0; busy_mouse(0); results_show(); bench_close(); return 0; }

    for (i = 0; i < ntests && !abort_run && !quit_app; i++) {
        if (!all && !res[i].sel) continue;
        done++;
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
                strcpy(res[i].note, "skipped: Fbench in background");
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
        area_setup();
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
        strcpy(res[i].note, cur_note);
        if (sc < 0) { res[i].state = RS_FAIL; res[i].score = -1; }
        else        { res[i].state = RS_OK;   res[i].score = sc; }
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
