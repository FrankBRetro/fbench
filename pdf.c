/*
 * pdf.c - a tiny PDF 1.4 writer for Fbench.
 *
 * Each page's drawing operators are collected in a growable memory buffer
 * and written out as one content stream when the next page starts (or at
 * close).  Object numbers:
 *   1 catalog, 2 page tree, 3..6 fonts, then a (page, content) pair per page.
 * Only integer arithmetic is used, so no FPU or float printf is needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf.h"

#define MAX_PAGES  200
#define MAX_OBJ    (6 + 2 * MAX_PAGES + 1)

struct PDF {
    FILE *f;
    long  off[MAX_OBJ + 1];
    short nobj;
    short npages;
    short page_obj[MAX_PAGES];
    char *buf;
    long  len, cap;
    short open_page;
    short err;
};

/* Helvetica advance widths (1/1000 em) for ASCII 32..126 (Adobe AFM) */
static const short helv_w[95] = {
    278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
    1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
    333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
    556,556,333,500,278,556,500,722,500,500,500,334,260,334,584
};
/* Helvetica-Bold advance widths for ASCII 32..126 (Adobe AFM) */
static const short helvb_w[95] = {
    278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
    975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
    333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
    611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
};

short pdf_textw(short font, short size, const char *s)
{
    long w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (font == PF_COUR || font == PF_COURB) w += 600;
        else if (c >= 32 && c <= 126) w += (font == PF_HELVB ? helvb_w : helv_w)[c - 32];
        else w += 556;
    }
    return (short)((w * size + 500) / 1000);
}

/* copy s into out, shortened with ".." so it fits maxw; returns width */
short pdf_fit(short font, short size, const char *s, short maxw, char *out, short outlen)
{
    short n = (short)strlen(s);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, s, n); out[n] = 0;
    if (pdf_textw(font, size, out) <= maxw) return pdf_textw(font, size, out);
    while (n > 0) {
        n--;
        out[n] = 0;
        if (n >= 2) { out[n - 1] = '.'; out[n - 2] = '.'; }
        if (pdf_textw(font, size, out) <= maxw) break;
        if (n >= 2) { out[n - 1] = s[n - 1]; out[n - 2] = s[n - 2]; }
    }
    return pdf_textw(font, size, out);
}

/* ---------------------------------------------------------- buffer */

static void put(PDF *p, const char *s)
{
    long n = (long)strlen(s);
    if (p->err) return;
    if (p->len + n + 1 > p->cap) {
        long nc = p->cap ? p->cap * 2 : 16384L;
        char *nb;
        while (nc < p->len + n + 1) nc *= 2;
        nb = realloc(p->buf, nc);
        if (!nb) { p->err = 1; return; }
        p->buf = nb; p->cap = nc;
    }
    memcpy(p->buf + p->len, s, n);
    p->len += n;
}

/* colour component 0..255 -> "0.xxx" */
static void col3(char *o, short v)
{
    long m;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    m = (long)v * 1000L / 255L;
    if (m >= 1000) strcpy(o, "1");
    else sprintf(o, "0.%03ld", m);
}

static void rgb(PDF *p, short r, short g, short b, int fill)
{
    char a[8], bb[8], c[8], t[40];
    col3(a, r); col3(bb, g); col3(c, b);
    sprintf(t, "%s %s %s %s\n", a, bb, c, fill ? "rg" : "RG");
    put(p, t);
}

/* ---------------------------------------------------------- objects */

static void obj_begin(PDF *p, short n)
{
    p->off[n] = ftell(p->f);
    fprintf(p->f, "%d 0 obj\n", n);
}

static void flush_page(PDF *p)
{
    short pg, ct;
    if (!p->open_page) return;
    p->open_page = 0;
    pg = p->nobj + 1; ct = p->nobj + 2;
    p->nobj += 2;
    p->page_obj[p->npages - 1] = pg;

    obj_begin(p, pg);
    fprintf(p->f, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d]\n"
                  "   /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >>\n"
                  "   /Contents %d 0 R >>\nendobj\n", PDF_W, PDF_H, ct);
    obj_begin(p, ct);
    fprintf(p->f, "<< /Length %ld >>\nstream\n", p->len);
    if (p->len) fwrite(p->buf, 1, p->len, p->f);
    fprintf(p->f, "\nendstream\nendobj\n");
    p->len = 0;
}

PDF *pdf_create(const char *path)
{
    static const char *fn[4] = { "Helvetica", "Helvetica-Bold", "Courier", "Courier-Bold" };
    PDF *p = calloc(1, sizeof(PDF));
    short i;
    if (!p) return NULL;
    p->f = fopen(path, "wb");
    if (!p->f) { free(p); return NULL; }
    fprintf(p->f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    obj_begin(p, 1);
    fprintf(p->f, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    for (i = 0; i < 4; i++) {
        obj_begin(p, 3 + i);
        fprintf(p->f, "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n", fn[i]);
    }
    p->nobj = 6;
    return p;
}

void pdf_page(PDF *p)
{
    flush_page(p);
    if (p->npages >= MAX_PAGES) { p->err = 1; return; }
    p->npages++;
    p->open_page = 1;
    p->len = 0;
}

short pdf_pageno(PDF *p) { return p->npages; }

static void esc_text(PDF *p, const char *s)
{
    char t[4];
    put(p, "(");
    for (; *s; s++) {
        if (*s == '(' || *s == ')' || *s == '\\') { t[0] = '\\'; t[1] = *s; t[2] = 0; }
        else if ((unsigned char)*s < 32) { t[0] = ' '; t[1] = 0; }
        else { t[0] = *s; t[1] = 0; }
        put(p, t);
    }
    put(p, ")");
}

void pdf_text(PDF *p, short x, short y, short font, short size, const char *s,
              short r, short g, short b)
{
    char t[64];
    if (!p->open_page) pdf_page(p);
    rgb(p, r, g, b, 1);
    sprintf(t, "BT /F%d %d Tf %d %d Td ", font + 1, size, x, PDF_H - y);
    put(p, t);
    esc_text(p, s);
    put(p, " Tj ET\n");
}

void pdf_text_r(PDF *p, short xr, short y, short font, short size, const char *s,
                short r, short g, short b)
{
    pdf_text(p, xr - pdf_textw(font, size, s), y, font, size, s, r, g, b);
}

void pdf_text_c(PDF *p, short xc, short y, short font, short size, const char *s,
                short r, short g, short b)
{
    pdf_text(p, xc - pdf_textw(font, size, s) / 2, y, font, size, s, r, g, b);
}

void pdf_rect(PDF *p, short x, short y, short w, short h, short r, short g, short b)
{
    char t[64];
    if (w <= 0 || h <= 0) return;
    if (!p->open_page) pdf_page(p);
    rgb(p, r, g, b, 1);
    sprintf(t, "%d %d %d %d re f\n", x, PDF_H - y - h, w, h);
    put(p, t);
}

void pdf_line(PDF *p, short x1, short y1, short x2, short y2, short width,
              short r, short g, short b)
{
    char t[80];
    if (!p->open_page) pdf_page(p);
    rgb(p, r, g, b, 0);
    sprintf(t, "%d w %d %d m %d %d l S\n", width, x1, PDF_H - y1, x2, PDF_H - y2);
    put(p, t);
}

short pdf_close(PDF *p)
{
    long xref;
    short i, ok;
    if (!p) return 0;
    if (!p->npages) pdf_page(p);
    flush_page(p);

    obj_begin(p, 2);
    fprintf(p->f, "<< /Type /Pages /Count %d /Kids [", p->npages);
    for (i = 0; i < p->npages; i++)
        fprintf(p->f, "%s%d 0 R", i ? " " : "", p->page_obj[i]);
    fprintf(p->f, "] >>\nendobj\n");

    xref = ftell(p->f);
    fprintf(p->f, "xref\n0 %d\n0000000000 65535 f \n", p->nobj + 1);
    for (i = 1; i <= p->nobj; i++)
        fprintf(p->f, "%010ld 00000 n \n", p->off[i]);
    fprintf(p->f, "trailer\n<< /Size %d /Root 1 0 R /Info << /Producer (Fbench) >> >>\n"
                  "startxref\n%ld\n%%%%EOF\n", p->nobj + 1, xref);
    ok = !p->err && !ferror(p->f);
    if (fclose(p->f)) ok = 0;
    free(p->buf);
    free(p);
    return ok;
}
