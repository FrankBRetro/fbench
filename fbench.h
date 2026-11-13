/*
 * Fbench - GEM VDI / AES / GDOS benchmark for Atari ST, STE, TT, Falcon
 *          MultiTOS / MiNT friendly.
 *
 * Build: m68k-atari-mint-gcc + gemlib (see Makefile)
 */
#ifndef FBENCH_H
#define FBENCH_H

#include <gem.h>
#include <gemx.h>
#include <osbind.h>
#include <mintbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* XBIOS 64 (blitter on/off/inquire): mintlib calls it Blitmode() */
#ifndef Blitter
# ifdef Blitmode
#  define Blitter(m) Blitmode(m)
# else
static inline long fb_xbios64(short mode)
{
    register long ret __asm__("d0");
    __asm__ volatile ("move.w %1,-(%%sp)\n\t"
                      "move.w #64,-(%%sp)\n\t"
                      "trap #14\n\t"
                      "addq.l #4,%%sp"
                      : "=r"(ret) : "d"(mode)
                      : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return ret;
}
#  define Blitter(m) fb_xbios64(m)
# endif
#endif

#define FB_NAME     "Fbench"
/* version: VERSION file (bumped per release) + BUILD counter (bumped by
   make at every link) -> "1.1.7"; see version.c and the Makefile */
extern const char fb_version[];
#define FB_VERSION  fb_version

/* FB_DEBUG=1 (make debug -> FBENCHD.PRG): crash breadcrumbs in FBLAST.LOG
   and the "Trace test steps" option.  Release builds (make) have no
   logging at all: no file is written while tests run. */
#ifndef FB_DEBUG
#define FB_DEBUG 0
#endif

#define MAX_TESTS   128
#define MAX_BASE    4
#define MAX_FONTS   64
#define MAX_SYS     96          /* lines in the system report */
#define SYS_W       80

/* test groups */
enum { GRP_VDI, GRP_AES, GRP_ALIGN, GRP_FULL, GRP_GDOS, NGROUPS };

/* result states */
enum { RS_NONE, RS_OK, RS_NA, RS_FAIL };

typedef struct {
    short group;
    const char *key;            /* stable id used in profile files      */
    const char *name;           /* display name (keep <= 26 chars)      */
    short (*avail)(void);       /* NULL = always available              */
    void  (*setup)(void);       /* NULL = nothing                       */
    void  (*body)(void);        /* ONE operation, timed in a loop       */
    void  (*cleanup)(void);     /* NULL = nothing                       */
    short param;                /* per-test parameter (e.g. point size) */
} TEST;

typedef struct {
    short sel;                  /* selected for "Run selected"          */
    short state;                /* RS_*                                 */
    long  score;                /* operations per second x 10           */
    short blit;                 /* blitter state when measured          */
    char  note[28];
} RESULT;

typedef struct {
    short used;
    char  name[48];             /* NAME= line, or file name             */
    char  file[16];
    long  val[MAX_TESTS];       /* x10, -1 = not in profile             */
} BASELINE;

typedef struct {
    short id;
    short outline;              /* vector font (FSM/Speedo/NVDI)        */
    char  name[36];
} FONTREC;

typedef struct WIN {
    short h;                    /* AES handle, 0 = closed               */
    long  kind;
    GRECT cur, prev;
    short top;                  /* first visible row                    */
    short rows;                 /* total rows                           */
    short lh;                   /* row height in pixels                 */
    const char *title;
    void (*draw)(struct WIN *, const GRECT *work, const GRECT *clip);
} WIN;

enum { WN_RESULTS, WN_INFO, WN_TEST, WN_AUX, WN_PROG, NWIN };

/* ---- globals (fbench.c) ---- */
extern short app_id, phys_h, vdi_h, bh;
extern short work_out[57], ext_out[57];
extern short scr_w, scr_h, planes, ncolors;
extern short char_w, char_h, box_w, box_h;
extern GRECT desk;
extern WIN   wins[NWIN];
extern short busy, abort_run, quit_app;
extern short blitter_present, blitter_on;
extern short blitter_fixed;
enum { BS_NONE, BS_XBIOS, BS_XGEM };
extern short blit_src, xgem_present;
void  blit_set(short on);
const char *blit_src_name(void);

/* theme-aware pens: -1 = no suitable pen, use a pattern in pen.fg */
typedef struct { short bg, fg, good, bad, accent, info, hilite, on_accent; } PENS;
extern PENS pen;
void  pens_update(void);        /* present but XBIOS 64 can't switch it (TOS 4.x) */
const char *blitter_desc(void);    /* "none" / "on" / "off" / "on (fixed)" */
extern long  dur_ticks;
extern short multitask;
extern short magic_present;     /* MagiC (MagX cookie): Atari, Mac, PC, Linux */
extern short polite;            /* Options > Stay in background            */
short get_cookie(long id, long *val);
short fb_in_front(void);        /* is the top window one of Fbench's?      */
short win_fully_visible(short h);
void  results_to_back(void);
extern char  machine_desc[34];   /* user's description, "" = not set   */

/* non-modal progress window (fbench.c) */
void  prog_open(const char *title);
void  prog_set(short pct, const char *msg);
void  prog_close(void);
extern char  home_dir[160];      /* directory Fbench was started from */

/* ---- tests (tests.c) ---- */
extern TEST   tests[];
extern short  ntests;
extern RESULT res[MAX_TESTS];
extern short  test_fail;
extern short  run_tw_w, run_tw_h;   /* test window work size of the last run */
extern GRECT  tw_area;          /* work area of the test window         */
extern FONTREC fonts[MAX_FONTS];
extern short  nfonts, gdos_loaded, gfont;
extern long   gdos_type;
extern const char *group_name[NGROUPS];

void  tests_init(void);
void  fonts_init(void);
void  fonts_exit(void);
short bench_open(void);
void  crumb_text(const char *s);
void  crumb_clear(void);
void  bench_close(void);
short run_tests(short all);
long  hz200(void);

/* ---- baselines / reports (report.c) ---- */
extern BASELINE base[MAX_BASE];
extern short active_base;

short baseline_load(const char *path);
void  baseline_from_results(short slot, const char *name, const RESULT *r);
void  baseline_clear(void);
short profile_save(const char *path);
short report_export(const char *path);
short find_test(const char *key);
void  fmt_score(char *buf, long v);

/* ---- system info (sysinfo.c) ---- */
extern char  sysline[MAX_SYS][SYS_W + 2];
extern short nsys;
void  sysinfo_collect(void);
void  sysinfo_short(char *buf, short len);
extern long cpu_type;            /* probed: 0, 10, 20, 30, 40, 60 */
long  cpu_probe(void);

/* ---- PDF reports (pdfrep.c) ---- */
short blit_report(const char *path, const RESULT *off, const RESULT *on);
short machine_report(const char *path);
short profiles_report(const char *dir, const char *path, short *nfound, short *ntotal);

/* ---- UI helpers (fbench.c) ---- */
extern OBJECT *menu_tree;
extern short mouse_vis;         /* keep pointer visible during tests  */
void  busy_mouse(short on);
extern short cur_run;           /* test currently running, -1 = none   */
extern short ping_seen;
#define FB_PING 0x7A46          /* private message used by AES test     */

short open_vwk(short *out);
extern short trace_steps;
void  fb_step(const char *s);  /* trace a step into FBLAST.LOG */
void  fb_update(short beg);     /* nest-safe wind_update BEG/END */
short testwin_open(void);
void  testwin_close(void);
void  drain_events(void);
void  win_redraw_all(WIN *w);
void  win_register(short slot, short handle, const char *title);
void  results_changed(void);
void  results_hide(void);
void  results_show(void);
void  set_test_info(const char *s);
WIN  *win_find(short h);

#endif
