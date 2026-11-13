/* mintbind.h for PC GEM: DOS has no MiNT, and a PC has no blitter */
#ifndef PC_MINTBIND_H
#define PC_MINTBIND_H
#include <osbind.h>
static __inline__ long Blitter(short m) { (void)m; return 0; }   /* XBIOS 64: no blitter */
#define Blitter Blitter
#endif
