/*
 * pdf.h - a tiny PDF 1.4 writer for Fbench (no libraries, no FPU needed)
 *
 * Coordinates are in points on an A4 page (595 x 842) with the origin at
 * the TOP-left and y growing downwards (converted when written).  Fonts
 * are the PDF base-14 Helvetica / Courier families, so nothing is embedded.
 * Colours are 0..255 RGB.
 */
#ifndef FB_PDF_H
#define FB_PDF_H

#define PDF_W   595
#define PDF_H   842

enum { PF_HELV, PF_HELVB, PF_COUR, PF_COURB };

typedef struct PDF PDF;

PDF  *pdf_create(const char *path);
void  pdf_page(PDF *p);                         /* start a new page */
short pdf_pageno(PDF *p);
void  pdf_text(PDF *p, short x, short y, short font, short size, const char *s,
               short r, short g, short b);
void  pdf_text_r(PDF *p, short xr, short y, short font, short size, const char *s,
                 short r, short g, short b);   /* right-aligned at xr */
void  pdf_text_c(PDF *p, short xc, short y, short font, short size, const char *s,
                 short r, short g, short b);   /* centred on xc */
void  pdf_rect(PDF *p, short x, short y, short w, short h, short r, short g, short b);
void  pdf_line(PDF *p, short x1, short y1, short x2, short y2, short width,
               short r, short g, short b);
short pdf_textw(short font, short size, const char *s);   /* width in points */
short pdf_fit(short font, short size, const char *s, short maxw, char *out, short outlen);
short pdf_close(PDF *p);                        /* 1 = written OK */

#endif
