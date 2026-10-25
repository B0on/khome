#include "png_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* stb_image_write does not emit a pHYs chunk; the design system's PNGs are
 * tagged 300 dpi, so add the same tag after encoding. */
unsigned char *stbi_write_png_to_mem(const unsigned char *pixels,
                                     int stride_bytes, int x, int y, int n,
                                     int *out_len);

static uint32_t crc32_bytes(const unsigned char *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

int png_write_gray(const char *path, Canvas *canvas)
{
    unsigned char *png;
    unsigned char *out;
    unsigned char phys[21];
    int len = 0;
    size_t out_len;
    unsigned char *rest;
    size_t rest_len;
    FILE *fp;
    int ok;

    if (path == NULL || canvas == NULL || canvas->pixels == NULL) {
        return 0;
    }
    canvas_quantize16(canvas);
    png = stbi_write_png_to_mem(canvas->pixels, canvas->stride, canvas->w,
                                canvas->h, 1, &len);
    if (png == NULL || len < 33) {
        free(png);
        return 0;
    }
    /* Signature (8) + IHDR chunk (25). */
    rest = png + 33;
    rest_len = (size_t)len - 33;
    out_len = (size_t)len + sizeof(phys);
    out = (unsigned char *)malloc(out_len);
    if (out == NULL) {
        free(png);
        return 0;
    }
    memcpy(out, png, 33);
    put_u32(phys, 9);
    memcpy(phys + 4, "pHYs", 4);
    /* 300 dpi = 11811 pixels per metre. */
    put_u32(phys + 8, 11811);
    put_u32(phys + 12, 11811);
    phys[16] = 1; /* unit: metre */
    put_u32(phys + 17, crc32_bytes(phys + 4, 13));
    memcpy(out + 33, phys, sizeof(phys));
    memcpy(out + 33 + sizeof(phys), rest, rest_len);
    free(png);

    fp = fopen(path, "wb");
    if (fp == NULL) {
        free(out);
        return 0;
    }
    ok = fwrite(out, 1, out_len, fp) == out_len;
    if (fclose(fp) != 0) {
        ok = 0;
    }
    free(out);
    return ok;
}
