/*
 * Fbench - sysinfo.c : OS, environment and display report
 *
 * Low memory and the cookie jar are read in supervisor mode (Supexec),
 * which is safe under MiNT memory protection.
 */
#include "fbench.h"
#include <stdarg.h>

char  sysline[MAX_SYS][SYS_W + 2];
long  cpu_type;
short nsys;

static unsigned short sv_tosver, sv_conf, nvdi_ver;
static unsigned long  sv_tosdate, sv_phystop, sv_ramtop, sv_ramvalid;
static long  jar_id[64], jar_val[64];
static short njar;

static long read_sys(void)
{
#ifdef PCGEM
    njar = 0; nvdi_ver = 0;     /* no ROM header or cookie jar on the PC */
    return 0;
#else
    unsigned char *sb = *(unsigned char **)0x4f2L;
    long *jar;

    sb = *(unsigned char **)(sb + 8);           /* os_beg: the ROM header */
    sv_tosver  = *(unsigned short *)(sb + 2);
    sv_tosdate = *(unsigned long *)(sb + 24);
    sv_conf    = *(unsigned short *)(sb + 28);
    sv_phystop = *(unsigned long *)0x42eL;
    sv_ramtop  = *(unsigned long *)0x5a4L;
    sv_ramvalid= *(unsigned long *)0x5a8L;

    njar = 0; nvdi_ver = 0;
    jar = *(long **)0x5a0L;
    if (jar) {
        while (jar[0] && njar < 64) {
            jar_id[njar] = jar[0];
            jar_val[njar] = jar[1];
            if (jar[0] == 0x4E564449L && jar[1])    /* 'NVDI' */
                nvdi_ver = *(unsigned short *)jar[1];
            njar++;
            jar += 2;
        }
    }
    return 0;
#endif
}

static short cookie(long id, long *val)
{
    short i;
    for (i = 0; i < njar; i++)
        if (jar_id[i] == id) { if (val) *val = jar_val[i]; return 1; }
    return 0;
}

#define CK(a,b,c,d) (((long)(a) << 24) | ((long)(b) << 16) | ((long)(c) << 8) | (long)(d))

/* for other modules: read the cookie jar (supervisor) and look one up */
short get_cookie(long id, long *val)
{
    Supexec(read_sys);
    return cookie(id, val);
}

static void add(const char *fmt, ...)
{
    va_list ap;
    if (nsys >= MAX_SYS) return;
    va_start(ap, fmt);
    vsprintf(sysline[nsys], fmt, ap);
    va_end(ap);
    sysline[nsys][SYS_W] = 0;
    nsys++;
}

#ifndef PCGEM
static const char *country_name(short c)
{
    static const char *n[] = {
        "USA", "Germany", "France", "UK", "Spain", "Italy", "Sweden",
        "Swiss (Fr)", "Swiss (De)", "Turkey", "Finland", "Norway",
        "Denmark", "Saudi Arabia", "Netherlands", "Czech", "Hungary"
    };
    return (c >= 0 && c < 17) ? n[c] : "other";
}

static const char *machine_name(void)
{
    long v;
    if (!cookie(CK('_','M','C','H'), &v)) return "ST (no _MCH cookie)";
    switch ((short)(v >> 16)) {
    case 0: return "ST / Mega ST";
    case 1:
        switch ((short)(v & 0xffff)) {
        case 0x0008: return "ST Book";
        case 0x0010: return "Mega STE";
        default:     return "STE";
        }
    case 2: return "TT030";
    case 3: return "Falcon030";
    case 4: return "Milan";
    case 5: return "ARAnyM";
    }
    return "unknown";
}
#endif

static const char *rez_name(short r)
{
    switch (r) {
    case 0: return "ST Low";
    case 1: return "ST Medium";
    case 2: return "ST High";
    case 4: return "TT Medium";
    case 6: return "TT High";
    case 7: return "TT Low";
    }
    return "extended";
}

#ifdef PCGEM
/* ---- PC GEM: DOS, the DPMI host and GEM instead of TOS and cookies ---- */
#include <dos.h>
#include <dpmi.h>

static void pc_machine(void)
{
    __dpmi_version_ret dv;
    __dpmi_free_mem_info fm;
    unsigned short dos = (unsigned short)((_osmajor << 8) | _osminor);
    add("#Machine");
    add("Description     : %s", machine_desc[0] ? machine_desc : "(not set - Options menu)");
    add("Machine type    : PC compatible");
    add("CPU             : %s (probed)", cpu_name());
    add("Blitter         : none (PC)");
    add("#Operating system");
    add("DOS             : %d.%02d (%s)", dos >> 8, dos & 0xff, _os_flavor);
    if (__dpmi_get_version(&dv) == 0)
        add("DPMI            : %d.%02d, %s mode", dv.major, dv.minor,
            (dv.flags & 1) ? "32-bit" : "16-bit");
    if (__dpmi_get_free_memory_information(&fm) == 0 && fm.largest_available_free_block_in_bytes != 0xFFFFFFFFUL)
        add("Largest free    : %lu KB (DPMI)", fm.largest_available_free_block_in_bytes / 1024UL);
}
#endif

void sysinfo_short(char *buf, short len)
{
    char t[120];
#ifdef PCGEM
    sprintf(t, "PC %s DOS %d.%02d GEM %x.%02x %dx%d %dc",
            cpu_name(), _osmajor, _osminor, _AESversion >> 8, _AESversion & 0xff,
            scr_w, scr_h, ncolors);
#else
    Supexec(read_sys);          /* low memory: supervisor only */
    sprintf(t, "%s 680%02ld TOS %x.%02x %dx%d %dc blit %s",
            machine_name(), cpu_type, sv_tosver >> 8, sv_tosver & 0xff,
            scr_w, scr_h, ncolors,
            blitter_desc());
#endif
    strncpy(buf, t, len - 1);
    buf[len - 1] = 0;
}

void sysinfo_collect(void)
{
    short i, has_agi, rez = Getrez();
    char ids[SYS_W + 2], *e;
#ifndef PCGEM
    long v, st_free, alt_free;
    unsigned short gd = (unsigned short)Sversion();
#endif

    Supexec(read_sys);
    nsys = 0;

#ifdef PCGEM
    pc_machine();
#else
    add("#Machine");
    add("Description     : %s", machine_desc[0] ? machine_desc : "(not set - Options menu)");
    add("Machine type    : %s", machine_name());
    {
        long ck = -1;
        if (cookie(CK('_','C','P','U'), &v)) ck = v;
        if (cpu_type == ck || ck < 0)
            add("CPU             : 680%02ld (probed)%s", cpu_type, ck < 0 ? ", no _CPU cookie" : "");
        else
            add("CPU             : 680%02ld (probed; _CPU cookie says 680%02ld)", cpu_type, ck);
    }
    if (cookie(CK('_','F','P','U'), &v)) {
        if ((v >> 16) == 0) add("FPU             : none");
        else                add("FPU             : present ($%08lx)", v);
    } else add("FPU             : none reported");
    if (cookie(CK('_','V','D','O'), &v)) {
        static const char *vn[] = { "ST shifter", "STE shifter", "TT shifter", "Falcon VIDEL" };
        short k = (short)(v >> 16);
        add("Video hardware  : %s", k >= 0 && k < 4 ? vn[k] : "other");
    }
    if (cookie(CK('_','S','N','D'), &v))
        add("Sound           : %s%s%s%s",
            (v & 1) ? "YM2149 " : "", (v & 2) ? "DMA-8bit " : "",
            (v & 4) ? "CODEC-16bit " : "", (v & 8) ? "DSP" : "");
    add("Blitter         : %s%s", !blitter_present ? "not present" :
                                 (blitter_on ? "present, ENABLED" : "present, disabled"),
        blitter_fixed ? " - cannot be switched" : "");
    if (blitter_present) add("Blitter control : %s", blit_src_name());
    add("ST RAM          : %lu KB total", sv_phystop / 1024UL);
    if (sv_ramvalid == 0x1357BD13UL && sv_ramtop > 0x01000000UL)
        add("TT/Fast RAM     : %lu KB total", (sv_ramtop - 0x01000000UL) / 1024UL);

    add("#Operating system");
    add("TOS             : %x.%02x  (%02lx/%02lx/%04lx m/d/y)",
        sv_tosver >> 8, sv_tosver & 0xff,
        (sv_tosdate >> 24) & 0xff, (sv_tosdate >> 16) & 0xff, sv_tosdate & 0xffff);
    add("Country / video : %s, %s", country_name(sv_conf >> 1), (sv_conf & 1) ? "PAL" : "NTSC");
    add("GEMDOS          : %d.%02d", gd & 0xff, gd >> 8);
    if (cookie(CK('M','i','N','T'), &v))
        add("MiNT            : %ld.%ld  (pid %ld)", (v >> 8) & 0xff, v & 0xff, (long)Pgetpid());
    else
        add("MiNT            : not running");
    if (cookie(CK('M','a','g','X'), &v)) {
        const char *var = cookie(CK('M','g','M','c'), NULL) ? " (MagiC Mac)" :
                          cookie(CK('M','g','P','C'), NULL) ? " (MagiC PC)" : "";
        add("MagiC           : present%s", var);
    }
    if (nvdi_ver) add("NVDI            : %x.%02x", nvdi_ver >> 8, nvdi_ver & 0xff);

    st_free = (long)Malloc(-1L);
    alt_free = (long)Mxalloc(-1L, 1);
    v = (long)Mxalloc(-1L, 0);
    if (v > 0) st_free = v;
    add("Largest free    : ST %ld KB", st_free / 1024L);
    if (alt_free > 0 && alt_free != st_free)
        add("                  TT/Fast %ld KB", alt_free / 1024L);

#endif
    add("#AES / environment");
    add("AES version     : %x.%02x", _AESversion >> 8, _AESversion & 0xff);
    add("Multitasking    : %s (max apps %d)%s", multitask ? "yes" : "no", _AESnumapps,
        magic_present ? ", MagiC" : "");
    add("Stay in backgr. : %s", polite ? "yes - windows are never topped" : "no");
    if (xgem_present) {
        static const char *th[] = { "TOS 4", "TOS 2", "MagiC", "AmigaOS 1.3", "AmigaOS 2.0" };
        short a1 = -1, a2, a3, a4;
        if (!appl_getinfo(0x5849, &a1, &a2, &a3, &a4)) a1 = -1;    /* XGI_THEME */
        add("XGEM            : yes, theme %s", a1 >= 0 && a1 < 5 ? th[a1] : "unknown");
    }
    add("Theme pens      : green %d, red %d, blue %d, cyan %d, highlight %d",
        pen.good, pen.bad, pen.accent, pen.info, pen.hilite);
    add("Application id  : %d", app_id);
    has_agi = (_AESversion >= 0x0400) || (appl_find("?AGI") >= 0);
    if (has_agi) {
        short a1, a2, a3, a4;
        if (appl_getinfo(AES_LARGEFONT, &a1, &a2, &a3, &a4))
            add("AES large font  : %d pt, id %d, %s", a1, a2, a3 ? "outline" : "bitmap");
        if (appl_getinfo(AES_SMALLFONT, &a1, &a2, &a3, &a4))
            add("AES small font  : %d pt, id %d", a1, a2);
        if (appl_getinfo(AES_LANGUAGE, &a1, &a2, &a3, &a4))
            add("AES language    : %d", a1);
    } else
        add("appl_getinfo    : not available");
    add("Desktop area    : %d,%d %dx%d", desk.g_x, desk.g_y, desk.g_w, desk.g_h);
    add("AES char / box  : %dx%d / %dx%d", char_w, char_h, box_w, box_h);
    {
        char p[128];
        short d = Dgetdrv();
        p[0] = 0;
        Dgetpath(p, d + 1);
        add("Current dir     : %c:%s", 'A' + d, p[0] ? p : "\\");
    }
    if ((e = getenv("PATH")) != NULL)  add("PATH            : %.60s", e);
    if ((e = getenv("HOME")) != NULL)  add("HOME            : %.60s", e);
    if ((e = getenv("SHELL")) != NULL) add("SHELL           : %.60s", e);
    if ((e = getenv("TEMP")) != NULL)  add("TEMP            : %.60s", e);

    add("#Display");
    add("Resolution      : %dx%d, Getrez %d (%s)", scr_w, scr_h, rez, rez_name(rez));
    add("Colours         : %d on screen, %d planes", ncolors, planes);
    add("Palette         : %d %s", work_out[39],
        work_out[39] == 0 ? "(direct colour)" : "available colours");
    add("Lookup table    : %s", ext_out[5] ? "yes" : "no (true colour)");
    add("Pixel size      : %d x %d microns", work_out[3], work_out[4]);
#ifdef PCGEM
    add("Screen memory   : %ld bytes", (long)scr_w * scr_h * planes / 8L);
#else
    add("Screen memory   : %ld bytes at $%08lx", (long)scr_w * scr_h * planes / 8L, (long)Physbase());
#endif
    add("VDI handles     : physical %d, ui %d (bench ws opened only for runs)", phys_h, vdi_h);
    add("VDI max vertices: %d", ext_out[14]);
    add("Text rotation   : %s", ext_out[8] == 0 ? "none" : (ext_out[8] == 1 ? "90 degree steps" : "arbitrary"));
    add("Writing modes   : %d", ext_out[9]);

    add("#GDOS / fonts");
    if (!vq_gdos())
        add("GDOS            : none (system font only)");
    else if (gdos_type == GDOS_FSM)
        add("GDOS            : FSM / vector GDOS (_FSM)");
    else if (gdos_type == GDOS_FNT)
        add("GDOS            : FONTGDOS (_FNT)");
    else if (gdos_type == 0x5F535044L)
        add("GDOS            : SpeedoGDOS (_SPD)");
    else
        add("GDOS            : bitmap GDOS (id $%08lx)", gdos_type);
    add("Fonts           : %d (%d loaded by GDOS)", nfonts, gdos_loaded);
    for (i = 0; i < nfonts && nsys < MAX_SYS - 4; i++)
        add("  %c id %5d  %-32s %s", i == gfont ? '>' : ' ',
            fonts[i].id, fonts[i].name, fonts[i].outline ? "vector" : "");

#ifdef PCGEM
    (void)ids;
    return;
#endif
    add("#Cookie jar");
    ids[0] = 0;
    for (i = 0; i < njar; i++) {
        char c[6];
        short k;
        for (k = 0; k < 4; k++) {
            char ch = (char)(jar_id[i] >> (24 - 8 * k));
            c[k] = (ch >= 32 && ch < 127) ? ch : '.';
        }
        c[4] = ' '; c[5] = 0;
        if (strlen(ids) + 5 > 60) { add("%s", ids); ids[0] = 0; }
        strcat(ids, c);
    }
    if (ids[0]) add("%s", ids);
    if (!njar) add("(no cookie jar)");
}
