/* SPDX-License-Identifier: GPL-3.0-only */
/* Baseline JPEG encoder (4:4:4, quality 1–100) for the 181×181 Active Loop jog image.
 * Standard Annex K quant/Huffman tables. No heap. */
#ifndef JOG_JPEG_H
#define JOG_JPEG_H
#include <stdint.h>
#include <string.h>

static const uint8_t jog_zz[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};
static const uint8_t jog_qY0[64] = {
    16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99
};
static const uint8_t jog_qC0[64] = {
    17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
};
/* bits then vals: DC luma, AC luma, DC chroma, AC chroma (Annex K) */
static const uint8_t jog_dcL_n[16] = { 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };
static const uint8_t jog_dcL_v[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
static const uint8_t jog_acL_n[16] = { 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125 };
static const uint8_t jog_acL_v[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};
static const uint8_t jog_dcC_n[16] = { 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
static const uint8_t jog_dcC_v[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
static const uint8_t jog_acC_n[16] = { 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119 };
static const uint8_t jog_acC_v[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

typedef struct { uint16_t code[256]; uint8_t nbits[256]; } jog_huff;
typedef struct {
    uint8_t *p, *end;
    uint32_t bitbuf; int bitn;
    int qY[64], qC[64];
    jog_huff dcY, acY, dcC, acC;
    int prevY, prevCb, prevCr;
} jog_jenc;

static void jog_huff_build(jog_huff *h, const uint8_t *nb, const uint8_t *vals, int nval)
{
    memset(h, 0, sizeof *h);
    uint16_t code = 0; int k = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < nb[i]; j++, k++) {
            int v = vals[k];
            h->code[v] = code;
            h->nbits[v] = (uint8_t)(i + 1);
            code++;
        }
        code <<= 1;
    }
    (void)nval;
}
static int jog_jput(jog_jenc *e, uint8_t b)
{
    if (e->p >= e->end) return -1;
    *e->p++ = b;
    return 0;
}
static int jog_jput16(jog_jenc *e, unsigned v)
{
    return jog_jput(e, (uint8_t)(v >> 8)) || jog_jput(e, (uint8_t)v);
}
static int jog_jbits(jog_jenc *e, unsigned bits, int n)
{
    if (n <= 0) return 0;
    e->bitbuf = (e->bitbuf << n) | (bits & (n == 32 ? 0xffffffffu : ((1u << n) - 1)));
    e->bitn += n;
    while (e->bitn >= 8) {
        uint8_t b = (uint8_t)(e->bitbuf >> (e->bitn - 8));
        e->bitn -= 8;
        if (jog_jput(e, b)) return -1;
        if (b == 0xff && jog_jput(e, 0x00)) return -1;
    }
    return 0;
}
static int jog_jflush(jog_jenc *e)
{
    if (e->bitn > 0) {
        if (jog_jbits(e, (1u << (8 - e->bitn)) - 1, 8 - e->bitn)) return -1;
    }
    e->bitn = 0;
    return 0;
}
static int jog_jhuff(jog_jenc *e, const jog_huff *h, int sym)
{
    int n = h->nbits[sym];
    if (n <= 0) return -1;
    return jog_jbits(e, h->code[sym], n);
}
static int jog_cat(int v)
{
    if (v < 0) v = -v;
    int n = 0; while (v) { n++; v >>= 1; }
    return n;
}
static int jog_block(jog_jenc *e, const jog_huff *dc, const jog_huff *ac, int *prev, const int du[64])
{
    int diff = du[0] - *prev;
    *prev = du[0];
    int cat = jog_cat(diff);
    if (jog_jhuff(e, dc, cat)) return -1;
    if (cat) {
        unsigned bits = (unsigned)(diff < 0 ? diff + (1 << cat) - 1 : diff);
        if (jog_jbits(e, bits, cat)) return -1;
    }
    int run = 0;
    for (int k = 1; k < 64; k++) {
        int v = du[k];
        if (!v) { run++; continue; }
        while (run >= 16) {
            if (jog_jhuff(e, ac, 0xf0)) return -1;
            run -= 16;
        }
        cat = jog_cat(v);
        if (jog_jhuff(e, ac, (run << 4) | cat)) return -1;
        unsigned bits = (unsigned)(v < 0 ? v + (1 << cat) - 1 : v);
        if (jog_jbits(e, bits, cat)) return -1;
        run = 0;
    }
    if (run && jog_jhuff(e, ac, 0x00)) return -1;
    return 0;
}

/* Textbook 8×8 float FDCT, then zigzag + quantise. */
static const float jog_costab[8][8] = {
    { 1.00000000f, 0.98078528f, 0.92387953f, 0.83146961f, 0.70710678f, 0.55557023f, 0.38268343f, 0.19509032f },
    { 1.00000000f, 0.83146961f, 0.38268343f, -0.19509032f, -0.70710678f, -0.98078528f, -0.92387953f, -0.55557023f },
    { 1.00000000f, 0.55557023f, -0.38268343f, -0.98078528f, -0.70710678f, 0.19509032f, 0.92387953f, 0.83146961f },
    { 1.00000000f, 0.19509032f, -0.92387953f, -0.55557023f, 0.70710678f, 0.83146961f, -0.38268343f, -0.98078528f },
    { 1.00000000f, -0.19509032f, -0.92387953f, 0.55557023f, 0.70710678f, -0.83146961f, -0.38268343f, 0.98078528f },
    { 1.00000000f, -0.55557023f, -0.38268343f, 0.98078528f, -0.70710678f, -0.19509032f, 0.92387953f, -0.83146961f },
    { 1.00000000f, -0.83146961f, 0.38268343f, 0.19509032f, -0.70710678f, 0.98078528f, -0.92387953f, 0.55557023f },
    { 1.00000000f, -0.98078528f, 0.92387953f, -0.83146961f, 0.70710678f, -0.55557023f, 0.38268343f, -0.19509032f }
};
static void jog_fdct_q(const float *blk, int *zz, const int *q)
{
    float F[64];
    for (int v = 0; v < 8; v++) {
        for (int u = 0; u < 8; u++) {
            float sum = 0.f;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    sum += blk[y * 8 + x] * jog_costab[x][u] * jog_costab[y][v];
            float cu = (u == 0) ? 0.70710678f : 1.f;
            float cv = (v == 0) ? 0.70710678f : 1.f;
            F[v * 8 + u] = 0.25f * cu * cv * sum;
        }
    }
    for (int k = 0; k < 64; k++) {
        int n = jog_zz[k];
        int qi = q[n];
        if (qi < 1) qi = 1;
        float v = F[n] / (float)qi;
        zz[k] = (int)(v >= 0 ? v + 0.5f : v - 0.5f);
    }
}
static int jog_marker_dqt(jog_jenc *e, int id, const int *q)
{
    if (jog_jput16(e, 0xffdb) || jog_jput16(e, 67) || jog_jput(e, (uint8_t)id)) return -1;
    for (int i = 0; i < 64; i++) {
        int v = q[jog_zz[i]];
        if (v < 1) v = 1;
        if (v > 255) v = 255;
        if (jog_jput(e, (uint8_t)v)) return -1;
    }
    return 0;
}
static int jog_marker_dht(jog_jenc *e, int cls_id, const uint8_t *nb, const uint8_t *vals, int nval)
{
    int n = 0; for (int i = 0; i < 16; i++) n += nb[i];
    if (n != nval) return -1;
    if (jog_jput16(e, 0xffc4) || jog_jput16(e, (unsigned)(19 + n)) || jog_jput(e, (uint8_t)cls_id)) return -1;
    for (int i = 0; i < 16; i++) if (jog_jput(e, nb[i])) return -1;
    for (int i = 0; i < n; i++) if (jog_jput(e, vals[i])) return -1;
    return 0;
}

static int jog_samp(const uint8_t *rgb, int w, int h, int x, int y, int c)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    return rgb[(y * w + x) * 3 + c];
}
/* Encode packed RGB888 (w*h*3) into out. Returns byte length, or -1. */
static int jog_jpeg_encode(const uint8_t *rgb, int w, int h, int quality, uint8_t *out, int cap)
{
    if (!rgb || !out || w <= 0 || h <= 0 || cap < 128) return -1;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    jog_jenc e;
    memset(&e, 0, sizeof e);
    e.p = out; e.end = out + cap;
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (int i = 0; i < 64; i++) {
        int y = (jog_qY0[i] * scale + 50) / 100; if (y < 1) y = 1; if (y > 255) y = 255;
        int c = (jog_qC0[i] * scale + 50) / 100; if (c < 1) c = 1; if (c > 255) c = 255;
        e.qY[i] = y; e.qC[i] = c;
    }
    jog_huff_build(&e.dcY, jog_dcL_n, jog_dcL_v, 12);
    jog_huff_build(&e.acY, jog_acL_n, jog_acL_v, 162);
    jog_huff_build(&e.dcC, jog_dcC_n, jog_dcC_v, 12);
    jog_huff_build(&e.acC, jog_acC_n, jog_acC_v, 162);

    if (jog_jput16(&e, 0xffd8)) return -1;                                 /* SOI */
    /* APP0 JFIF */
    if (jog_jput16(&e, 0xffe0) || jog_jput16(&e, 16) ||
        jog_jput(&e,'J')||jog_jput(&e,'F')||jog_jput(&e,'I')||jog_jput(&e,'F')||jog_jput(&e,0) ||
        jog_jput(&e,1)||jog_jput(&e,1)||jog_jput(&e,0) ||
        jog_jput16(&e,1)||jog_jput16(&e,1)||jog_jput(&e,0)||jog_jput(&e,0)) return -1;
    if (jog_marker_dqt(&e, 0, e.qY) || jog_marker_dqt(&e, 1, e.qC)) return -1;
    /* SOF0, 4:4:4 (sharp red caption; no chroma subsample) */
    if (jog_jput16(&e, 0xffc0) || jog_jput16(&e, 17) || jog_jput(&e, 8) ||
        jog_jput16(&e, (unsigned)h) || jog_jput16(&e, (unsigned)w) || jog_jput(&e, 3) ||
        jog_jput(&e,1)||jog_jput(&e,0x11)||jog_jput(&e,0) ||
        jog_jput(&e,2)||jog_jput(&e,0x11)||jog_jput(&e,1) ||
        jog_jput(&e,3)||jog_jput(&e,0x11)||jog_jput(&e,1)) return -1;
    if (jog_marker_dht(&e, 0x00, jog_dcL_n, jog_dcL_v, 12) ||
        jog_marker_dht(&e, 0x10, jog_acL_n, jog_acL_v, 162) ||
        jog_marker_dht(&e, 0x01, jog_dcC_n, jog_dcC_v, 12) ||
        jog_marker_dht(&e, 0x11, jog_acC_n, jog_acC_v, 162)) return -1;
    if (jog_jput16(&e, 0xffda) || jog_jput16(&e, 12) || jog_jput(&e, 3) ||
        jog_jput(&e,1)||jog_jput(&e,0x00) ||
        jog_jput(&e,2)||jog_jput(&e,0x11) ||
        jog_jput(&e,3)||jog_jput(&e,0x11) ||
        jog_jput(&e,0)||jog_jput(&e,0x3f)||jog_jput(&e,0)) return -1;


    int mbx = (w + 7) / 8, mby = (h + 7) / 8;
    for (int my = 0; my < mby; my++) {
        for (int mx = 0; mx < mbx; mx++) {
            float yb[64], cb[64], cr[64];
            for (int iy = 0; iy < 8; iy++) {
                for (int ix = 0; ix < 8; ix++) {
                    int x = mx * 8 + ix, y = my * 8 + iy;
                    int r = jog_samp(rgb, w, h, x, y, 0);
                    int g = jog_samp(rgb, w, h, x, y, 1);
                    int b = jog_samp(rgb, w, h, x, y, 2);
                    yb[iy * 8 + ix] =  0.299f * r + 0.587f * g + 0.114f * b - 128.f;
                    cb[iy * 8 + ix] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                    cr[iy * 8 + ix] =  0.5f * r - 0.418688f * g - 0.081312f * b;
                }
            }
            int du[64];
            jog_fdct_q(yb, du, e.qY);
            if (jog_block(&e, &e.dcY, &e.acY, &e.prevY, du)) return -1;
            jog_fdct_q(cb, du, e.qC);
            if (jog_block(&e, &e.dcC, &e.acC, &e.prevCb, du)) return -1;
            jog_fdct_q(cr, du, e.qC);
            if (jog_block(&e, &e.dcC, &e.acC, &e.prevCr, du)) return -1;
        }
    }
    if (jog_jflush(&e) || jog_jput16(&e, 0xffd9)) return -1;
    return (int)(e.p - out);
}

#endif
