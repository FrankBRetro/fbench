/*
 * Fbench - cpu.c : probe the real CPU type
 *
 * The _CPU cookie is written by the ROM at boot and is wrong on many
 * accelerated machines (e.g. a 68040 board reported as 68020), so the CPU
 * is identified by which control registers exist, catching the illegal
 * instruction trap for those that don't:
 *
 *   MOVEC VBR   traps on 68000                 -> 68000
 *   MOVEC CACR  traps on 68010                 -> 68010
 *   MOVEC ITT0  exists on 68040/68060 only     -> 040 / 060
 *   MOVEC PCR   exists on 68060 only           -> 060
 *   CACR bit 8  (data cache enable) sticks on 68030 only -> 030 / 020
 *
 * Nothing is left changed: caches are restored, the vector is put back,
 * and interrupts are masked while the vector is borrowed.  Instructions are
 * written as .dc.w so this builds with -m68000.
 */
#include "fbench.h"

static volatile long cpu_found;

#ifdef __mc68000__
static long cpu_probe_super(void)
{
    __asm__ volatile (
        "move.w  %%sr,-(%%sp)\n\t"
        "ori.w   #0x0700,%%sr\n\t"          /* no interrupts while vector 4 is ours */
        "move.l  0x10.w,-(%%sp)\n\t"        /* 68000 / VBR=0 illegal-instruction vector */
        "lea     8f(%%pc),%%a0\n\t"
        "move.l  %%a0,0x10.w\n\t"
        "suba.l  %%a2,%%a2\n\t"             /* a2 = VBR (0 until known)         */
        "moveq   #0,%%d1\n\t"               /* d1 = result: 0 = 68000           */

        "moveq   #0,%%d2\n\t"
        ".dc.w   0x4E7A,0x0801\n\t"         /* movec vbr,d0                      */
        "tst.w   %%d2\n\t"
        "bne     9f\n\t"
        "move.l  %%d0,%%a2\n\t"             /* vectors elsewhere? borrow there too */
        "cmpa.w  #0,%%a2\n\t"
        "beq.s   1f\n\t"
        "move.l  0x10(%%a2),-(%%sp)\n\t"
        "lea     8f(%%pc),%%a0\n\t"
        "move.l  %%a0,0x10(%%a2)\n"
        "1:\n\t"
        "moveq   #10,%%d1\n\t"              /* at least 68010                    */

        "moveq   #0,%%d2\n\t"
        ".dc.w   0x4E7A,0x0002\n\t"         /* movec cacr,d0                     */
        "tst.w   %%d2\n\t"
        "bne     7f\n\t"
        "moveq   #20,%%d1\n\t"              /* at least 68020                    */

        "moveq   #0,%%d2\n\t"
        ".dc.w   0x4E7A,0x0004\n\t"         /* movec itt0,d0  (040/060)          */
        "tst.w   %%d2\n\t"
        "bne.s   2f\n\t"
        "moveq   #40,%%d1\n\t"
        "moveq   #0,%%d2\n\t"
        ".dc.w   0x4E7A,0x0808\n\t"         /* movec pcr,d0   (060 only)         */
        "tst.w   %%d2\n\t"
        "bne     7f\n\t"
        "moveq   #60,%%d1\n\t"
        "bra     7f\n"

        "2:\n\t"                            /* 020 or 030                        */
        ".dc.w   0x4E7A,0x3002\n\t"         /* movec cacr,d3  (original)         */
        "btst    #8,%%d3\n\t"
        "bne.s   3f\n\t"                    /* data cache on: only a 030 has one */
        "move.l  %%d3,%%d0\n\t"
        "ori.w   #0x0900,%%d0\n\t"          /* clear (CD) + enable (ED) data cache */
        ".dc.w   0x4E7B,0x0002\n\t"         /* movec d0,cacr                     */
        ".dc.w   0x4E7A,0x0002\n\t"         /* movec cacr,d0                     */
        ".dc.w   0x4E7B,0x3002\n\t"         /* movec d3,cacr  (restore)          */
        "btst    #8,%%d0\n\t"
        "beq.s   7f\n"                      /* bit didn't stick: 68020           */
        "3:\n\t"
        "moveq   #30,%%d1\n"

        "7:\n\t"                            /* 010+: restore the VBR vector      */
        "cmpa.w  #0,%%a2\n\t"
        "beq.s   9f\n\t"
        "move.l  (%%sp)+,0x10(%%a2)\n"
        "9:\n\t"
        "move.l  (%%sp)+,0x10.w\n\t"
        "move.w  (%%sp)+,%%sr\n\t"
        "move.l  %%d1,%0\n\t"
        "bra.s   6f\n"

        "8:\n\t"                            /* illegal instruction: flag, skip 4 */
        "moveq   #-1,%%d2\n\t"
        "addq.l  #4,2(%%sp)\n\t"
        "rte\n"
        "6:\n"
        : "=m"(cpu_found)
        :
        : "d0", "d1", "d2", "d3", "a0", "a2", "memory", "cc");
    return 0;
}
#endif

/* 0, 10, 20, 30, 40 or 60 */
long cpu_probe(void)
{
#ifdef __mc68000__
    cpu_found = 0;
    Supexec(cpu_probe_super);
    return cpu_found;
#else
    return 0;
#endif
}
