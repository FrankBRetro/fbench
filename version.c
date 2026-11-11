/* Fbench version string: FB_VER and FB_BUILD come from the Makefile */
#ifndef FB_VER
#define FB_VER "1.1"
#endif
#ifndef FB_BUILD
#define FB_BUILD 0
#endif
#define STR2(x) #x
#define STR(x) STR2(x)
#if defined(FB_DEBUG) && FB_DEBUG
const char fb_version[] = FB_VER "." STR(FB_BUILD) "-debug";
#else
const char fb_version[] = FB_VER "." STR(FB_BUILD);
#endif
