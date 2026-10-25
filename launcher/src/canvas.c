#include "canvas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static double clampd(double v, double lo, double hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int canvas_init(Canvas *c, int w, int h)
{
    if (c == NULL || w <= 0 || h <= 0) {
        return 0;
    }
    c->w = w;
    c->h = h;
    c->stride = w;
    c->pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    if (c->pixels == NULL) {
        c->w = c->h = c->stride = 0;
        return 0;
    }
    return 1;
}

void canvas_free(Canvas *c)
{
    if (c == NULL) {
        return;
    }
    free(c->pixels);
    c->pixels = NULL;
    c->w = c->h = c->stride = 0;
}

void canvas_clear(Canvas *c, uint8_t value)
{
    if (c == NULL || c->pixels == NULL) {
        return;
    }
    memset(c->pixels, value, (size_t)c->stride * (size_t)c->h);
}

void canvas_fill_rect(Canvas *c, int x, int y, int w, int h, uint8_t value)
{
    int x0, y0, x1, y1, yy;
    if (c == NULL || c->pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > c->w) {
        x1 = c->w;
    }
    if (y1 > c->h) {
        y1 = c->h;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (yy = y0; yy < y1; yy++) {
        memset(c->pixels + (size_t)yy * (size_t)c->stride + x0, value,
               (size_t)(x1 - x0));
    }
}

void canvas_outline_rect(Canvas *c, int x, int y, int w, int h, int thickness,
                         uint8_t value)
{
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    if (thickness * 2 > w) {
        thickness = w / 2;
    }
    if (thickness * 2 > h) {
        thickness = h / 2;
    }
    canvas_fill_rect(c, x, y, w, thickness, value);
    canvas_fill_rect(c, x, y + h - thickness, w, thickness, value);
    canvas_fill_rect(c, x, y, thickness, h, value);
    canvas_fill_rect(c, x + w - thickness, y, thickness, h, value);
}

static void blend_pixel(Canvas *c, int x, int y, int alpha, uint8_t value)
{
    uint8_t *p;
    int out;
    if (alpha <= 0 || x < 0 || y < 0 || x >= c->w || y >= c->h) {
        return;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    p = c->pixels + (size_t)y * (size_t)c->stride + x;
    out = ((int)value * alpha + (int)(*p) * (255 - alpha) + 127) / 255;
    *p = (uint8_t)clampd(out, 0, 255);
}

static void paint_alpha(Canvas *c, int x, int y, double coverage, uint8_t value)
{
    double a = clampd(coverage, 0.0, 1.0) * 255.0;
    blend_pixel(c, x, y, (int)(a + 0.5), value);
}

void canvas_fill_circle(Canvas *c, double cx, double cy, double radius,
                        uint8_t value)
{
    int x0, y0, x1, y1, x, y;
    if (c == NULL || c->pixels == NULL || radius <= 0.0) {
        return;
    }
    x0 = (int)floor(cx - radius - 1.0);
    y0 = (int)floor(cy - radius - 1.0);
    x1 = (int)ceil(cx + radius + 1.0);
    y1 = (int)ceil(cy + radius + 1.0);
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            double dx = (double)x + 0.5 - cx;
            double dy = (double)y + 0.5 - cy;
            double d = sqrt(dx * dx + dy * dy);
            paint_alpha(c, x, y, radius + 0.5 - d, value);
        }
    }
}

void canvas_outline_circle(Canvas *c, double cx, double cy, double radius,
                           double thickness, uint8_t value)
{
    int x0, y0, x1, y1, x, y;
    double half;
    if (c == NULL || c->pixels == NULL || radius <= 0.0 || thickness <= 0.0) {
        return;
    }
    half = thickness * 0.5;
    x0 = (int)floor(cx - radius - 1.0);
    y0 = (int)floor(cy - radius - 1.0);
    x1 = (int)ceil(cx + radius + 1.0);
    y1 = (int)ceil(cy + radius + 1.0);
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            double dx = (double)x + 0.5 - cx;
            double dy = (double)y + 0.5 - cy;
            double d = sqrt(dx * dx + dy * dy);
            double ring = fabs(d - (radius - half)) - half;
            paint_alpha(c, x, y, 0.5 - ring, value);
        }
    }
}

void canvas_arc(Canvas *c, double cx, double cy, double radius,
                double thickness, double start_rad, double end_rad,
                uint8_t value)
{
    int x0, y0, x1, y1, x, y;
    double half;
    if (c == NULL || c->pixels == NULL || radius <= 0.0 || thickness <= 0.0) {
        return;
    }
    half = thickness * 0.5;
    x0 = (int)floor(cx - radius - 1.0);
    y0 = (int)floor(cy - radius - 1.0);
    x1 = (int)ceil(cx + radius + 1.0);
    y1 = (int)ceil(cy + radius + 1.0);
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            double dx = (double)x + 0.5 - cx;
            double dy = (double)y + 0.5 - cy;
            double d = sqrt(dx * dx + dy * dy);
            double ring = fabs(d - (radius - half)) - half;
            double angle = atan2(dy, dx);
            int inside;
            if (end_rad < start_rad) {
                inside = (angle >= start_rad || angle <= end_rad);
            } else {
                inside = (angle >= start_rad && angle <= end_rad);
            }
            if (inside) {
                paint_alpha(c, x, y, 0.5 - ring, value);
            }
        }
    }
}

void canvas_line(Canvas *c, double x0, double y0, double x1, double y1,
                 double thickness, uint8_t value)
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    double half = thickness * 0.5;
    double ux, uy, ex0, ey0, ex1, ey1;
    int bx0, by0, bx1, by1, x, y;
    if (c == NULL || c->pixels == NULL || thickness <= 0.0) {
        return;
    }
    if (len < 1e-6) {
        canvas_fill_circle(c, x0, y0, half, value);
        return;
    }
    ux = dx / len;
    uy = dy / len;
    /* SVG square line caps extend half a stroke width past each end. */
    ex0 = x0 - ux * half;
    ey0 = y0 - uy * half;
    ex1 = x1 + ux * half;
    ey1 = y1 + uy * half;
    bx0 = (int)floor((ex0 < ex1 ? ex0 : ex1) - 1.0);
    by0 = (int)floor((ey0 < ey1 ? ey0 : ey1) - 1.0);
    bx1 = (int)ceil((ex0 > ex1 ? ex0 : ex1) + 1.0);
    by1 = (int)ceil((ey0 > ey1 ? ey0 : ey1) + 1.0);
    for (y = by0; y <= by1; y++) {
        for (x = bx0; x <= bx1; x++) {
            double px = (double)x + 0.5;
            double py = (double)y + 0.5;
            double vx = ex1 - ex0;
            double vy = ey1 - ey0;
            double vv = vx * vx + vy * vy;
            double t = ((px - ex0) * vx + (py - ey0) * vy) / vv;
            double qx, qy, d;
            if (t < 0.0) {
                t = 0.0;
            } else if (t > 1.0) {
                t = 1.0;
            }
            qx = ex0 + t * vx;
            qy = ey0 + t * vy;
            d = sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy));
            paint_alpha(c, x, y, half + 0.5 - d, value);
        }
    }
}

static double round_rect_sdf(double px, double py, double cx, double cy,
                             double hw, double hh, double r)
{
    double qx = fabs(px - cx) - (hw - r);
    double qy = fabs(py - cy) - (hh - r);
    double ax = qx > 0.0 ? qx : 0.0;
    double ay = qy > 0.0 ? qy : 0.0;
    double m = qx > qy ? qx : qy;
    if (m > 0.0) {
        m = 0.0;
    }
    return sqrt(ax * ax + ay * ay) + m - r;
}

void canvas_round_rect_fill(Canvas *c, double x, double y, double w, double h,
                            double radius, uint8_t value)
{
    int x0, y0, x1, y1, px, py;
    double cx, cy, hw, hh, r;
    if (c == NULL || c->pixels == NULL || w <= 0.0 || h <= 0.0) {
        return;
    }
    hw = w * 0.5;
    hh = h * 0.5;
    r = clampd(radius, 0.0, hw < hh ? hw : hh);
    cx = x + hw;
    cy = y + hh;
    x0 = (int)floor(x - 1.0);
    y0 = (int)floor(y - 1.0);
    x1 = (int)ceil(x + w + 1.0);
    y1 = (int)ceil(y + h + 1.0);
    for (py = y0; py <= y1; py++) {
        for (px = x0; px <= x1; px++) {
            double d = round_rect_sdf((double)px + 0.5, (double)py + 0.5,
                                      cx, cy, hw, hh, r);
            paint_alpha(c, px, py, 0.5 - d, value);
        }
    }
}

void canvas_round_rect_outline(Canvas *c, double x, double y, double w,
                               double h, double radius, double thickness,
                               uint8_t value)
{
    int x0, y0, x1, y1, px, py;
    double cx, cy, hw, hh, r, half;
    if (c == NULL || c->pixels == NULL || w <= 0.0 || h <= 0.0 ||
        thickness <= 0.0) {
        return;
    }
    hw = w * 0.5;
    hh = h * 0.5;
    r = clampd(radius, 0.0, hw < hh ? hw : hh);
    cx = x + hw;
    cy = y + hh;
    half = thickness * 0.5;
    x0 = (int)floor(x - 1.0);
    y0 = (int)floor(y - 1.0);
    x1 = (int)ceil(x + w + 1.0);
    y1 = (int)ceil(y + h + 1.0);
    for (py = y0; py <= y1; py++) {
        for (px = x0; px <= x1; px++) {
            double d = round_rect_sdf((double)px + 0.5, (double)py + 0.5,
                                      cx, cy, hw, hh, r);
            double ring = fabs(d + half) - half;
            paint_alpha(c, px, py, 0.5 - ring, value);
        }
    }
}

void canvas_blit(Canvas *c, int x, int y, const Bitmap *src)
{
    int row;
    int sx0, sy0, sx1, sy1, w;
    if (c == NULL || c->pixels == NULL || src == NULL || src->pixels == NULL) {
        return;
    }
    sx0 = 0;
    sy0 = 0;
    sx1 = src->w;
    sy1 = src->h;
    if (x < 0) {
        sx0 = -x;
    }
    if (y < 0) {
        sy0 = -y;
    }
    if (x + src->w > c->w) {
        sx1 = c->w - x;
    }
    if (y + src->h > c->h) {
        sy1 = c->h - y;
    }
    if (sx0 >= sx1 || sy0 >= sy1) {
        return;
    }
    w = sx1 - sx0;
    for (row = sy0; row < sy1; row++) {
        memcpy(c->pixels + (size_t)(y + row) * (size_t)c->stride + (x + sx0),
               src->pixels + (size_t)row * (size_t)src->w + sx0, (size_t)w);
    }
}

void canvas_blit_scaled(Canvas *c, int x, int y, int w, int h,
                        const Bitmap *src)
{
    int py;
    int px;

    if (c == NULL || c->pixels == NULL || src == NULL || src->pixels == NULL ||
        w <= 0 || h <= 0) {
        return;
    }
    for (py = y; py < y + h; py++) {
        int sy0;
        int sy1;
        int sy;

        if (py < 0 || py >= c->h) {
            continue;
        }
        sy0 = (int)(((long long)(py - y) * src->h) / h);
        sy1 = (int)((((long long)(py - y + 1) * src->h) + h - 1) / h);
        if (sy0 < 0) {
            sy0 = 0;
        }
        if (sy1 > src->h) {
            sy1 = src->h;
        }
        if (sy0 >= sy1) {
            sy0 = sy1 > 0 ? sy1 - 1 : 0;
            sy1 = sy0 + 1;
        }
        for (px = x; px < x + w; px++) {
            int sx0;
            int sx1;
            int sx;
            uint32_t sum = 0;
            int count = 0;

            if (px < 0 || px >= c->w) {
                continue;
            }
            sx0 = (int)(((long long)(px - x) * src->w) / w);
            sx1 = (int)((((long long)(px - x + 1) * src->w) + w - 1) / w);
            if (sx0 < 0) {
                sx0 = 0;
            }
            if (sx1 > src->w) {
                sx1 = src->w;
            }
            if (sx0 >= sx1) {
                sx0 = sx1 > 0 ? sx1 - 1 : 0;
                sx1 = sx0 + 1;
            }
            for (sy = sy0; sy < sy1; sy++) {
                for (sx = sx0; sx < sx1; sx++) {
                    sum += src->pixels[(size_t)sy * (size_t)src->w + sx];
                    count++;
                }
            }
            c->pixels[(size_t)py * (size_t)c->stride + px] =
                (uint8_t)((sum + (uint32_t)count / 2) / (uint32_t)count);
        }
    }
}

void canvas_quantize16(Canvas *c)
{
    size_t i, n;
    if (c == NULL || c->pixels == NULL) {
        return;
    }
    n = (size_t)c->stride * (size_t)c->h;
    for (i = 0; i < n; i++) {
        int v = ((int)c->pixels[i] + 8) / 17 * 17;
        if (v > 255) {
            v = 255;
        }
        c->pixels[i] = (uint8_t)v;
    }
}

int bitmap_load_gray(Bitmap *b, const char *path)
{
    int w = 0, h = 0, n = 0;
    unsigned char *pixels;
    if (b == NULL || path == NULL) {
        return 0;
    }
    pixels = stbi_load(path, &w, &h, &n, 1);
    if (pixels == NULL || w <= 0 || h <= 0) {
        if (pixels != NULL) {
            stbi_image_free(pixels);
        }
        return 0;
    }
    b->w = w;
    b->h = h;
    b->pixels = pixels;
    return 1;
}

void bitmap_free(Bitmap *b)
{
    if (b == NULL) {
        return;
    }
    if (b->pixels != NULL) {
        stbi_image_free(b->pixels);
    }
    b->pixels = NULL;
    b->w = b->h = 0;
}

static const char *const icon_names[ICON_COUNT] = {
    "newspaper", "manga", "browser", "settings", "wifi", "battery"
};

static int load_icon_dir(Icons *icons, const char *dir, int device)
{
    int i;
    for (i = 0; i < ICON_COUNT; i++) {
        char path[1024];
        Bitmap *dst = device ? &icons->device[i] : &icons->design[i];
        snprintf(path, sizeof(path), "%s/%s.png", dir, icon_names[i]);
        if (!bitmap_load_gray(dst, path)) {
            fprintf(stderr, "khome: cannot read icon %s\n", path);
            return 0;
        }
    }
    return 1;
}

int icons_load(Icons *icons, const char *dir)
{
    char sub[1024];
    if (icons == NULL || dir == NULL) {
        return 0;
    }
    memset(icons, 0, sizeof(*icons));
    snprintf(sub, sizeof(sub), "%s/design", dir);
    if (!load_icon_dir(icons, sub, 0)) {
        icons_free(icons);
        return 0;
    }
    snprintf(sub, sizeof(sub), "%s/device", dir);
    if (!load_icon_dir(icons, sub, 1)) {
        icons_free(icons);
        return 0;
    }
    return 1;
}

void icons_free(Icons *icons)
{
    int i;
    if (icons == NULL) {
        return;
    }
    for (i = 0; i < ICON_COUNT; i++) {
        bitmap_free(&icons->design[i]);
        bitmap_free(&icons->device[i]);
    }
}

const Bitmap *icons_get(const Icons *icons, int id, float scale)
{
    if (icons == NULL || id < 0 || id >= ICON_COUNT) {
        return NULL;
    }
    if (scale > 0.9f) {
        return &icons->design[id];
    }
    return &icons->device[id];
}
